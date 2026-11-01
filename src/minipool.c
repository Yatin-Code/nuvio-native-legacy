// Paired, TLS-pinned client for the shared OpenSeek thumbnail pool.
#include "miniplat.h"

#ifdef NV_MINIATURAS
#include "minipool.h"
#include "admin.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MP_URL_FILE "openseek-server.txt"
#define MP_PIN_FILE "openseek-pin.txt"
#define MP_TOKEN_FILE "openseek-device-token.txt"
#define MP_MAX_PACKAGE (16u * 1024u * 1024u)
#define MP_DEFAULT_URL "https://20.244.104.107"

static pthread_mutex_t mp_mu = PTHREAD_MUTEX_INITIALIZER;
static char mp_url[256] = MP_DEFAULT_URL, mp_pin[64], mp_token[96], mp_erro[128];
static MpoolEstado mp_estado = MPOOL_PARADO;
static int mp_busy;

typedef struct { int mode; char code[32], url[256], pin[64], token[96]; } MpoolWork;
static void wipe(void *p, size_t n) { volatile unsigned char *q = p; while (n--) *q++ = 0; }

static int pin_valido(const char *pin) {
  size_t i;
  if (!pin || strlen(pin) != 52 || strncmp(pin, "sha256//", 8) || pin[51] != '=') return 0;
  for (i = 8; i < 51; i++)
    if (!isalnum((unsigned char)pin[i]) && pin[i] != '+' && pin[i] != '/') return 0;
  return 1;
}
static int url_valida(const char *url, char out[256]) {
  size_t n;
  if (!url || strncmp(url, "https://", 8)) return 0;
  n = strlen(url); while (n && url[n - 1] == '/') n--;
  if (n < 10 || n >= 256) return 0;
  for (size_t i = 0; i < n; i++)
    if ((unsigned char)url[i] <= 32 || url[i] == '?' || url[i] == '#' || url[i] == '@') return 0;
  if (!url[8] || memchr(url + 8, '/', n - 8) || memchr(url + 8, '\\', n - 8)) return 0;
  memcpy(out, url, n); out[n] = 0; return 1;
}
static void limpar_token_memoria_locked(void) { wipe(mp_token, sizeof mp_token); mp_token[0] = 0; }

void minipool_carregar(void) {
  char *s = dados_ler(MP_URL_FILE);
  if (s) { char clean[256]; size_t n = strcspn(s, "\r\n"); if (n < sizeof clean) { memcpy(clean, s, n); clean[n] = 0; if (url_valida(clean, clean)) snprintf(mp_url, sizeof mp_url, "%s", clean); } free(s); }
  s = dados_ler(MP_PIN_FILE);
  if (s) { char *e = strpbrk(s, "\r\n"); if (e) *e = 0; if (pin_valido(s)) snprintf(mp_pin, sizeof mp_pin, "%s", s); free(s); }
  s = dados_ler(MP_TOKEN_FILE);
  if (s) {
    char *e = strpbrk(s, "\r\n"); if (e) *e = 0;
    size_t n = strlen(s); int ok = n == 43;
    for (size_t i = 0; i < n && ok; i++) ok = isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_';
    if (ok) snprintf(mp_token, sizeof mp_token, "%s", s);
    wipe(s, strlen(s)); free(s);
  }
  pthread_mutex_lock(&mp_mu); mp_estado = mp_token[0] ? MPOOL_LIGADO : MPOOL_PARADO; pthread_mutex_unlock(&mp_mu);
  printf("[miniatura] OpenSeek pool %s; TLS pin %s\n", mp_token[0] ? "paired" : "not paired", mp_pin[0] ? "configured" : "required"); fflush(stdout);
}

const char *minipool_servidor(void) { return mp_url; }
int minipool_pin_configurado(void) { int ok; pthread_mutex_lock(&mp_mu); ok = mp_pin[0] != 0; pthread_mutex_unlock(&mp_mu); return ok; }
int minipool_pareado(void) { int ok; pthread_mutex_lock(&mp_mu); ok = mp_token[0] != 0; pthread_mutex_unlock(&mp_mu); return ok; }
MpoolEstado minipool_estado(void) { MpoolEstado e; pthread_mutex_lock(&mp_mu); e = mp_estado; pthread_mutex_unlock(&mp_mu); return e; }
const char *minipool_erro(void) { static char copy[128]; pthread_mutex_lock(&mp_mu); snprintf(copy, sizeof copy, "%s", mp_erro); pthread_mutex_unlock(&mp_mu); return copy[0] ? copy : NULL; }
const char *minipool_estado_texto(void) {
  static char copy[128]; pthread_mutex_lock(&mp_mu);
  if (mp_estado == MPOOL_ERRO && mp_erro[0]) snprintf(copy, sizeof copy, "%s", mp_erro);
  else if (mp_token[0]) snprintf(copy, sizeof copy, "%s", "Conectado");
  else if (mp_estado == MPOOL_VINCULANDO) snprintf(copy, sizeof copy, "%s", "Vinculando…");
  else if (mp_estado == MPOOL_DESVINCULANDO) snprintf(copy, sizeof copy, "%s", "Desconectando…");
  else if (mp_estado == MPOOL_ERRO && mp_erro[0]) snprintf(copy, sizeof copy, "%s", mp_erro);
  else if (!mp_pin[0]) snprintf(copy, sizeof copy, "%s", "Configure o pin TLS");
  else snprintf(copy, sizeof copy, "%s", "Vincular com código");
  pthread_mutex_unlock(&mp_mu); return copy;
}

int minipool_servidor_definir(const char *url) {
  char clean[256], line[260]; int remove_token = 0;
  if (!url_valida(url, clean)) return 0;
  pthread_mutex_lock(&mp_mu);
  if (mp_busy) { pthread_mutex_unlock(&mp_mu); return 0; }
  if (strcmp(mp_url, clean)) { snprintf(mp_url, sizeof mp_url, "%s", clean); limpar_token_memoria_locked(); mp_estado = MPOOL_PARADO; remove_token = 1; }
  pthread_mutex_unlock(&mp_mu);
  if (remove_token) dados_apagar(MP_TOKEN_FILE);
  snprintf(line, sizeof line, "%s\n", clean); return dados_gravar(MP_URL_FILE, line);
}
int minipool_pin_definir(const char *pin) {
  char line[64]; int remove_token = 0;
  if (!pin_valido(pin)) return 0;
  pthread_mutex_lock(&mp_mu);
  if (mp_busy) { pthread_mutex_unlock(&mp_mu); return 0; }
  if (strcmp(mp_pin, pin)) { snprintf(mp_pin, sizeof mp_pin, "%s", pin); limpar_token_memoria_locked(); mp_estado = MPOOL_PARADO; remove_token = 1; }
  pthread_mutex_unlock(&mp_mu);
  if (remove_token) dados_apagar(MP_TOKEN_FILE);
  snprintf(line, sizeof line, "%s\n", pin); return dados_gravar(MP_PIN_FILE, line);
}

static void sha256_bin(const void *data, size_t n, unsigned char out[32]) {
  char hex[65]; admin_sha256_hex(data, (unsigned long)n, hex);
  for (int i = 0; i < 32; i++) { unsigned v = 0; sscanf(hex + i * 2, "%2x", &v); out[i] = (unsigned char)v; }
  wipe(hex, sizeof hex);
}
static void bin_hex(const unsigned char *in, size_t n, char *out) {
  static const char h[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[i * 2] = h[in[i] >> 4]; out[i * 2 + 1] = h[in[i] & 15]; }
  out[n * 2] = 0;
}
static int hmac256(const unsigned char *key, size_t nk, const unsigned char *data, size_t nd, unsigned char out[32]) {
  unsigned char k[64] = {0}, block[640], digest[32];
  if (nd > sizeof block - 64) return 0;
  if (nk > 64) { sha256_bin(key, nk, k); nk = 32; } else if (nk) memcpy(k, key, nk);
  for (int i = 0; i < 64; i++) block[i] = k[i] ^ 0x36;
  if (nd) memcpy(block + 64, data, nd);
  sha256_bin(block, 64 + nd, digest);
  for (int i = 0; i < 64; i++) block[i] = k[i] ^ 0x5c;
  memcpy(block + 64, digest, 32); sha256_bin(block, 96, out);
  wipe(k, sizeof k); wipe(block, sizeof block); wipe(digest, sizeof digest); return 1;
}
static int pbkdf2(const char *password, const char *salt, unsigned iterations, unsigned char out[32]) {
  unsigned char u[32], acc[32], msg[260]; size_t ns = strlen(salt), np = strlen(password);
  if (!iterations || ns + 4 > sizeof msg || np > 32) return 0;
  memcpy(msg, salt, ns); msg[ns] = 0; msg[ns + 1] = 0; msg[ns + 2] = 0; msg[ns + 3] = 1;
  if (!hmac256((const unsigned char *)password, np, msg, ns + 4, u)) return 0;
  memcpy(acc, u, 32);
  for (unsigned i = 1; i < iterations; i++) {
    if (!hmac256((const unsigned char *)password, np, u, sizeof u, u)) { wipe(u, sizeof u); return 0; }
    for (int k = 0; k < 32; k++) acc[k] ^= u[k];
  }
  memcpy(out, acc, 32); wipe(u, sizeof u); wipe(acc, sizeof acc); wipe(msg, sizeof msg); return 1;
}
static int hex_string(const char *s, size_t n) {
  if (!s || strlen(s) != n) return 0;
  for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)s[i])) return 0;
  return 1;
}
static int token_valid(const char *s) {
  size_t n = s ? strlen(s) : 0; if (n != 43) return 0;
  for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)s[i]) && s[i] != '-' && s[i] != '_') return 0;
  return 1;
}
static int proof_equal(const char *a, const char *b) {
  unsigned diff = 0; if (!a || !b || strlen(a) != 64 || strlen(b) != 64) return 0;
  for (int i = 0; i < 64; i++) diff |= (unsigned char)tolower((unsigned char)a[i]) ^ (unsigned char)tolower((unsigned char)b[i]);
  return diff == 0;
}

static int request(const char *method, const char *url, const char *pin, const char *token,
                   const char *content_type, const void *body, size_t body_size,
                   size_t max_response, int prazo_ms, RedeResposta *r) {
  char auth[128], ct[100]; const char *headers[3] = { NULL, NULL, NULL }; unsigned n = 0; RedePedido p;
  if (token && token[0]) { snprintf(auth, sizeof auth, "Authorization: Bearer %s", token); headers[n++] = auth; }
  if (content_type) { snprintf(ct, sizeof ct, "Content-Type: %s", content_type); headers[n++] = ct; }
  headers[n] = NULL; memset(&p, 0, sizeof p);
  p.metodo = method; p.url = url; p.cabecalhos = n ? headers : NULL;
  p.corpo = body; p.n_corpo = body_size; p.prazo_ms = prazo_ms;
  p.max_bytes = max_response; p.max_cabecalhos = 16 * 1024;
  p.seguir = 0; p.tls_pessoal = 1; p.tls_pin = pin;
  if (!rede_pedir(&p, r) && !r->status) return 0;
  return r->status;
}

static int normalize_code(const char *src, char code[17], char invite_tag[5]) {
  static const char alphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"; size_t n = 0;
  invite_tag[0] = 0;
  if (!src) return 0;
  for (; *src; src++) {
    unsigned char c = (unsigned char)*src;
    if (c == '-' || isspace(c)) continue;
    c = (unsigned char)toupper(c);
    if (!strchr(alphabet, c) || n == 16) return 0;
    code[n++] = (char)c;
  }
  code[n] = 0;
  if (n == 8) return 1;                 // short-lived owner pairing window
  if (n == 16) {                        // shared invite: four-char tag + twelve-char secret
    memcpy(invite_tag, code, 4); invite_tag[4] = 0;
    memmove(code, code + 4, 13);
    return 1;
  }
  return 0;
}

static int start_pair(const char *url, const char *pin, const char *code, char token_out[96]) {
  RedeResposta start = {0}, claim = {0};
  char endpoint[384], request_body[512], pair_id[40] = "", salt_hex[40] = "", server_pin[64] = "";
  char device[] = "Nuvio TV", client_proof[65], server_proof[80] = "", token_hex[65], expected[65];
  char code_norm[17] = "", invite_tag[5] = "", salt[200] = "", returned_token[96] = "", device_id[24] = "";
  unsigned char key[32] = {0}, mac[32] = {0}, token_hash[32] = {0}; int status, ok = 0;
  if (!normalize_code(code, code_norm, invite_tag)) goto done;
  snprintf(endpoint, sizeof endpoint, "%s/v1/pair/start", url);
  if (invite_tag[0]) snprintf(request_body, sizeof request_body, "{\"invite\":\"%s\"}", invite_tag);
  else snprintf(request_body, sizeof request_body, "{\"invite\":null}");
  status = request("POST", endpoint, pin, NULL, "application/json", request_body, strlen(request_body), 16 * 1024, 45000, &start);
  if (status != 200 || !start.corpo || !js_texto(start.corpo, NULL, "pair_id", pair_id, sizeof pair_id) ||
      !js_texto(start.corpo, NULL, "salt", salt_hex, sizeof salt_hex) ||
      !js_texto(start.corpo, NULL, "spki_pin", server_pin, sizeof server_pin)) goto done;
  unsigned iterations = (unsigned)js_num(start.corpo, NULL, "iterations", 0);
  if (!hex_string(pair_id, 32) || !hex_string(salt_hex, 32) || !pin_valido(server_pin) || strcmp(server_pin, pin) ||
      iterations < 200000 || iterations > 1000000) goto done;
  snprintf(salt, sizeof salt, "OSP1%s%s%s", pair_id, salt_hex, server_pin);
  if (!pbkdf2(code_norm, salt, iterations, key)) goto done;
  snprintf(request_body, sizeof request_body, "client%s%s", pair_id, device);
  hmac256(key, sizeof key, (const unsigned char *)request_body, strlen(request_body), mac);
  bin_hex(mac, sizeof mac, client_proof);
  snprintf(endpoint, sizeof endpoint, "%s/v1/pair/claim", url);
  snprintf(request_body, sizeof request_body,
           "{\"pair_id\":\"%s\",\"device_name\":\"%s\",\"client_proof\":\"%s\"}", pair_id, device, client_proof);
  status = request("POST", endpoint, pin, NULL, "application/json", request_body, strlen(request_body), 16 * 1024, 45000, &claim);
  if (status != 200 || !claim.corpo || !js_texto(claim.corpo, NULL, "token", returned_token, sizeof returned_token) ||
      !js_texto(claim.corpo, NULL, "device_id", device_id, sizeof device_id) ||
      !js_texto(claim.corpo, NULL, "server_proof", server_proof, sizeof server_proof) ||
      !token_valid(returned_token) || !hex_string(device_id, 16)) goto done;
  sha256_bin(returned_token, strlen(returned_token), token_hash); bin_hex(token_hash, sizeof token_hash, token_hex);
  snprintf(request_body, sizeof request_body, "server%s%s", pair_id, token_hex);
  hmac256(key, sizeof key, (const unsigned char *)request_body, strlen(request_body), mac); bin_hex(mac, sizeof mac, expected);
  if (!proof_equal(server_proof, expected)) goto done;
  snprintf(token_out, 96, "%s", returned_token); ok = 1;
done:
  wipe(code_norm, sizeof code_norm); wipe(invite_tag, sizeof invite_tag); wipe(key, sizeof key); wipe(mac, sizeof mac); wipe(token_hash, sizeof token_hash);
  wipe(returned_token, sizeof returned_token); wipe(token_hex, sizeof token_hex); wipe(expected, sizeof expected);
  wipe(request_body, sizeof request_body); wipe(salt, sizeof salt);
  rede_resposta_limpar(&start); rede_resposta_limpar(&claim); return ok;
}

static void *mp_worker(void *arg) {
  MpoolWork *w = arg; char new_token[96] = "", url[384]; int ok = 0;
  if (w->mode == 1) {
    ok = start_pair(w->url, w->pin, w->code, new_token);
    if (ok) { char line[100]; snprintf(line, sizeof line, "%s\n", new_token); if (!dados_gravar(MP_TOKEN_FILE, line)) ok = 0; }
    pthread_mutex_lock(&mp_mu);
    if (ok) { snprintf(mp_token, sizeof mp_token, "%s", new_token); mp_estado = MPOOL_LIGADO; mp_erro[0] = 0; }
    else { mp_estado = MPOOL_ERRO; snprintf(mp_erro, sizeof mp_erro, "%s", "Vinculação falhou; confira código e pin TLS"); }
    pthread_mutex_unlock(&mp_mu);
  } else {
    snprintf(url, sizeof url, "%s/v1/pair", w->url); RedeResposta r = {0};
    int status = request("DELETE", url, w->pin, w->token, NULL, NULL, 0, 1024, 45000, &r);
    rede_resposta_limpar(&r);
    pthread_mutex_lock(&mp_mu);
    if (status == 204) { limpar_token_memoria_locked(); mp_estado = MPOOL_PARADO; mp_erro[0] = 0; }
    else { mp_estado = MPOOL_ERRO; snprintf(mp_erro, sizeof mp_erro, "%s", "Não foi possível revogar; tente novamente"); }
    pthread_mutex_unlock(&mp_mu);
    if (status == 204) dados_apagar(MP_TOKEN_FILE);
  }
  pthread_mutex_lock(&mp_mu); mp_busy = 0; pthread_mutex_unlock(&mp_mu);
  wipe(new_token, sizeof new_token); wipe(w, sizeof *w); free(w); return NULL;
}
static int start_work(MpoolWork *w) {
  pthread_t thread;
  pthread_mutex_lock(&mp_mu);
  if (mp_busy || !mp_pin[0]) {
    mp_estado = MPOOL_ERRO; snprintf(mp_erro, sizeof mp_erro, "%s", mp_busy ? "Aguarde a operação atual" : "Configure o pin TLS");
    pthread_mutex_unlock(&mp_mu); wipe(w, sizeof *w); free(w); return 0;
  }
  mp_busy = 1; pthread_mutex_unlock(&mp_mu);
  if (pthread_create(&thread, NULL, mp_worker, w)) {
    pthread_mutex_lock(&mp_mu); mp_busy = 0; mp_estado = MPOOL_ERRO;
    snprintf(mp_erro, sizeof mp_erro, "%s", "Não foi possível iniciar a operação"); pthread_mutex_unlock(&mp_mu);
    wipe(w, sizeof *w); free(w); return 0;
  }
  pthread_detach(thread); return 1;
}
void minipool_vincular(const char *code) {
  MpoolWork *w = calloc(1, sizeof *w); if (!w) return;
  if (!code || strlen(code) >= sizeof w->code) { free(w); return; }
  w->mode = 1; snprintf(w->code, sizeof w->code, "%s", code);
  pthread_mutex_lock(&mp_mu); snprintf(w->url, sizeof w->url, "%s", mp_url); snprintf(w->pin, sizeof w->pin, "%s", mp_pin);
  mp_estado = MPOOL_VINCULANDO; mp_erro[0] = 0; pthread_mutex_unlock(&mp_mu); (void)start_work(w);
}
void minipool_desvincular(void) {
  MpoolWork *w = calloc(1, sizeof *w); if (!w) return;
  pthread_mutex_lock(&mp_mu);
  if (!mp_token[0]) { pthread_mutex_unlock(&mp_mu); free(w); return; }
  w->mode = 2; snprintf(w->url, sizeof w->url, "%s", mp_url); snprintf(w->pin, sizeof w->pin, "%s", mp_pin);
  snprintf(w->token, sizeof w->token, "%s", mp_token); mp_estado = MPOOL_DESVINCULANDO;
  pthread_mutex_unlock(&mp_mu); (void)start_work(w);
}

static uint32_t read32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int nvmt_ok(const unsigned char *data, size_t n, long duration, int grid) {
  uint32_t slots, count; size_t pos = 20; unsigned char *seen;
  if (!data || n < 20 || n > MP_MAX_PACKAGE || memcmp(data, "NVMT", 4) || read32(data + 4) != 1 || read32(data + 8) != (uint32_t)grid) return 0;
  slots = read32(data + 12); count = read32(data + 16);
  if (duration <= 0 || slots != ((uint64_t)duration + grid - 1) / grid || !count || count > slots || slots > 8640) return 0;
  seen = calloc(slots, 1); if (!seen) return 0;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t slot, pts, len; int32_t pts_signed;
    if (pos + 12 > n) { free(seen); return 0; }
    slot = read32(data + pos); pts = read32(data + pos + 4); len = read32(data + pos + 8); pos += 12;
    pts_signed = (int32_t)pts;
    if (slot >= slots || seen[slot] || len < 125 || len > 200000 || pos + len > n ||
        pts_signed < 0 || pts_signed > duration || llabs((long long)pts_signed - (long long)slot * grid) > 10000 ||
        data[pos] != 0xff || data[pos + 1] != 0xd8 || data[pos + len - 2] != 0xff || data[pos + len - 1] != 0xd9) {
      free(seen); return 0;
    }
    seen[slot] = 1; pos += len;
  }
  free(seen); return pos == n;
}
static int response_header(const char *all, const char *name, char *out, size_t cap) {
  size_t nn = strlen(name); const char *p = all;
  if (!all || !out || cap < 2) return 0;
  while (*p) {
    const char *end = strchr(p, '\n'); size_t n = end ? (size_t)(end - p) : strlen(p);
    if (n > nn && !strncasecmp(p, name, nn) && p[nn] == ':') {
      const char *v = p + nn + 1; size_t nv;
      while (v < p + n && (*v == ' ' || *v == '\t')) v++;
      nv = (size_t)(p + n - v); while (nv && (v[nv - 1] == '\r' || v[nv - 1] == ' ')) nv--;
      if (nv >= cap) return 0; memcpy(out, v, nv); out[nv] = 0; return 1;
    }
    if (!end) break; p = end + 1;
  }
  return 0;
}

int minipool_buscar(int enabled, const MiniMeta *meta, unsigned char **data, size_t *size, char profile[16]) {
  static const struct { const char *name; int grid; } profiles[] = { {"nvmt-v1-g5", 5000}, {"nvmt-v1-g10", 10000} };
  char url[512], base[256], pin[64], token[96]; int found = 0;
  if (data) *data = NULL; if (size) *size = 0; if (profile) profile[0] = 0;
  if (!enabled || !meta || !data || !size || !profile || !hex_string(meta->fp, 64)) return 0;
  pthread_mutex_lock(&mp_mu); snprintf(base, sizeof base, "%s", mp_url); snprintf(pin, sizeof pin, "%s", mp_pin); snprintf(token, sizeof token, "%s", mp_token); pthread_mutex_unlock(&mp_mu);
  if (!pin[0]) return 0;
  for (size_t i = 0; i < sizeof profiles / sizeof *profiles; i++) {
    RedeResposta r = {0}; char duration[32] = "", hash[80] = "", digest[65];
    snprintf(url, sizeof url, "%s/v1/pool/%s/nvmt?profile=%s", base, meta->fp, profiles[i].name);
    // The first package fetch may have to reach Telegram (4 MB took ~10 s in a
    // live check); allow that cold read, but cap each optional lookup well below
    // the generic API timeout so a dead pool cannot stall local generation.
    int status = request("GET", url, pin, token, NULL, NULL, 0, MP_MAX_PACKAGE, 15000, &r);
    if (status == 200 && r.corpo && response_header(r.cabecalhos, "X-OpenSeek-Duration-Ms", duration, sizeof duration) &&
        response_header(r.cabecalhos, "X-OpenSeek-Sha256", hash, sizeof hash) && atol(duration) == meta->durMs &&
        hex_string(hash, 64) && r.n_corpo <= MP_MAX_PACKAGE && nvmt_ok((unsigned char *)r.corpo, r.n_corpo, meta->durMs, profiles[i].grid)) {
      admin_sha256_hex((const unsigned char *)r.corpo, (unsigned long)r.n_corpo, digest);
      if (!strcasecmp(digest, hash)) { *data = (unsigned char *)r.corpo; r.corpo = NULL; *size = r.n_corpo; snprintf(profile, 16, "%s", profiles[i].name); found = 1; }
    }
    rede_resposta_limpar(&r); if (found) break;
  }
  wipe(token, sizeof token); return found;
}

int minipool_enviar(int enabled, const MiniMeta *meta, const unsigned char *data, size_t size) {
  char url[512], base[256], pin[64], token[96], profile[24]; RedeResposta r = {0}; int status, grid;
  if (!enabled || !meta || !hex_string(meta->fp, 64) || !data || size > MP_MAX_PACKAGE || !minipool_pareado()) return 0;
  if (size < 20) return 0;
  grid = (int)read32(data + 8); if (grid != 5000 && grid != 10000) return 0;
  snprintf(profile, sizeof profile, "nvmt-v1-g%d", grid / 1000);
  if (!nvmt_ok(data, size, meta->durMs, grid)) return 0;
  pthread_mutex_lock(&mp_mu); snprintf(base, sizeof base, "%s", mp_url); snprintf(pin, sizeof pin, "%s", mp_pin); snprintf(token, sizeof token, "%s", mp_token); pthread_mutex_unlock(&mp_mu);
  if (!pin[0] || !token[0]) { wipe(token, sizeof token); return 0; }
  snprintf(url, sizeof url, "%s/v1/pool/%s/nvmt?profile=%s&duration_ms=%ld", base, meta->fp, profile, meta->durMs);
  status = request("PUT", url, pin, token, "application/vnd.openseek.nvmt", data, size, 16 * 1024, 45000, &r);
  rede_resposta_limpar(&r); wipe(token, sizeof token); return status == 200 || status == 201;
}

#endif


