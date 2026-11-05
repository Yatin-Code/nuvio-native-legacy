// Paired client for the participant's OpenSeek node. Public HTTPS endpoints use
// the platform CA store; self-signed endpoints can still use an explicit SPKI pin.
#include "miniplat.h"

#ifdef NV_MINIATURAS
#include "minipool.h"
#include "admin.h"
#include "dados.h"
#include "idbase.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MP_URL_FILE "openseek-server.txt"
#define MP_PIN_FILE "openseek-pin.txt"
#define MP_TOKEN_FILE "openseek-device-token.txt"
#define MP_MAX_PACKAGE (16u * 1024u * 1024u)
#define MP_MAX_POOL_RESPONSE (4u * 1024u * 1024u)
#define MP_CONTRIBUTE_URL_MAX 512u
#define MP_DEFAULT_URL ""

static pthread_mutex_t mp_mu = PTHREAD_MUTEX_INITIALIZER;
static char mp_url[256] = MP_DEFAULT_URL, mp_pin[64], mp_token[96], mp_erro[128];
static char mp_contribute_url[MP_CONTRIBUTE_URL_MAX + 1];
static MpoolEstado mp_estado = MPOOL_PARADO;
static int mp_busy, mp_contribution_denied;

typedef struct { int mode; char code[512], url[256], pin[64], token[96]; } MpoolWork;
static void wipe(void *p, size_t n) { volatile unsigned char *q = p; while (n--) *q++ = 0; }
static void mp_log_failure(const char *operation, int status, const RedeResposta *r);
static void mp_log_rejected(const char *operation, const char *reason);
static void mp_log_note(const char *message);

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
  printf("[miniatura] OpenSeek pool %s; TLS %s\n", mp_token[0] ? "paired" : "not paired", mp_pin[0] ? "SPKI pin" : "system CA verification"); fflush(stdout);
}

const char *minipool_servidor(void) { return mp_url; }
int minipool_pin_configurado(void) { int ok; pthread_mutex_lock(&mp_mu); ok = mp_pin[0] != 0; pthread_mutex_unlock(&mp_mu); return ok; }
int minipool_endpoint_configurado(void) { int ok; pthread_mutex_lock(&mp_mu); ok = mp_url[0] != 0; pthread_mutex_unlock(&mp_mu); return ok; }
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
  else if (!mp_url[0]) snprintf(copy, sizeof copy, "%s", "Configure o servidor OpenSeek");
  else snprintf(copy, sizeof copy, "%s", "Vincular com código");
  pthread_mutex_unlock(&mp_mu); return copy;
}
int minipool_contribuicao_recusada(void) {
  int denied;
  pthread_mutex_lock(&mp_mu); denied = mp_contribution_denied; pthread_mutex_unlock(&mp_mu);
  return denied;
}

const char *minipool_contribuicao_url(void) {
  static char copy[MP_CONTRIBUTE_URL_MAX + 1];
  pthread_mutex_lock(&mp_mu);
  snprintf(copy, sizeof copy, "%s", mp_contribute_url);
  pthread_mutex_unlock(&mp_mu);
  return copy[0] ? copy : NULL;
}

static void mp_contribution_result_locked(int denied, const char *url) {
  mp_contribution_denied = denied;
  if (denied && url) snprintf(mp_contribute_url, sizeof mp_contribute_url, "%s", url);
  else mp_contribute_url[0] = 0;
}

static void mp_contribution_result(int denied, const char *url) {
  pthread_mutex_lock(&mp_mu);
  mp_contribution_result_locked(denied, url);
  pthread_mutex_unlock(&mp_mu);
}

static void mp_contribution_clear_locked(void) {
  mp_contribution_result_locked(0, NULL);
}

int minipool_servidor_definir(const char *url) {
  char clean[256], line[260]; int remove_token = 0;
  if (!url_valida(url, clean)) return 0;
  pthread_mutex_lock(&mp_mu);
  if (mp_busy) { pthread_mutex_unlock(&mp_mu); return 0; }
  if (mp_token[0] && strcmp(mp_url, clean)) { pthread_mutex_unlock(&mp_mu); return 0; }
  if (strcmp(mp_url, clean)) {
    snprintf(mp_url, sizeof mp_url, "%s", clean); limpar_token_memoria_locked();
    mp_contribution_clear_locked();
    mp_estado = MPOOL_PARADO; remove_token = 1;
  }
  pthread_mutex_unlock(&mp_mu);
  if (remove_token) dados_apagar(MP_TOKEN_FILE);
  snprintf(line, sizeof line, "%s\n", clean); return dados_gravar(MP_URL_FILE, line);
}
int minipool_pin_definir(const char *pin) {
  char line[64]; int remove_token = 0, clear = !pin || !pin[0];
  if (!clear && !pin_valido(pin)) return 0;
  pthread_mutex_lock(&mp_mu);
  if (mp_busy) { pthread_mutex_unlock(&mp_mu); return 0; }
  if (mp_token[0] && strcmp(mp_pin, clear ? "" : pin)) { pthread_mutex_unlock(&mp_mu); return 0; }
  if (strcmp(mp_pin, clear ? "" : pin)) {
    snprintf(mp_pin, sizeof mp_pin, "%s", clear ? "" : pin); limpar_token_memoria_locked();
    mp_contribution_clear_locked();
    mp_estado = MPOOL_PARADO; remove_token = 1;
  }
  pthread_mutex_unlock(&mp_mu);
  if (remove_token) dados_apagar(MP_TOKEN_FILE);
  if (clear) { dados_apagar(MP_PIN_FILE); return 1; }
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

static int candidate_int32(const char *object, const char *end, const char *key,
                           int64_t fallback, int64_t *out) {
  double value = js_num(object, end, key, (double)fallback);
  if (value != value || value < -2147483648.0 || value > 2147483647.0 ||
      value != (double)(int32_t)value) return 0;
  *out = (int64_t)(int32_t)value;
  return 1;
}

int minipool_map_pts(int64_t pts, int64_t a_num, int64_t a_den, int64_t b_ms,
                     int64_t dur_ms, long *mapped_ms) {
  int64_t product, mapped;
  if (!mapped_ms || a_den <= 0 || dur_ms < 0) return 0;
  if (a_num > 0 && pts > 0 && pts > INT64_MAX / a_num) return 0;
  if (a_num > 0 && pts < 0 && pts < INT64_MIN / a_num) return 0;
  if (a_num < 0 && pts > 0 && a_num < INT64_MIN / pts) return 0;
  if (a_num < 0 && pts < 0 && a_num < INT64_MAX / pts) return 0;
  product = pts * a_num;
  mapped = product / a_den;
  if ((b_ms > 0 && mapped > INT64_MAX - b_ms) ||
      (b_ms < 0 && mapped < INT64_MIN - b_ms)) return 0;
  mapped += b_ms;
  if (mapped < 0) mapped = 0;
  if (mapped > dur_ms) mapped = dur_ms;
  if (mapped > LONG_MAX) return 0;
  *mapped_ms = (long)mapped;
  return 1;
}
static int proof_equal(const char *a, const char *b) {
  unsigned diff = 0; if (!a || !b || strlen(a) != 64 || strlen(b) != 64) return 0;
  for (int i = 0; i < 64; i++) diff |= (unsigned char)tolower((unsigned char)a[i]) ^ (unsigned char)tolower((unsigned char)b[i]);
  return diff == 0;
}

typedef struct { volatile int *flag; } MpCancel;
static int mp_cancelar(void *u) { MpCancel *c = u; return c && c->flag && *c->flag; }

static int request_cancelable(const char *method, const char *url, const char *pin, const char *token,
                              const char *content_type, const void *body, size_t body_size,
                              size_t max_response, int prazo_ms, volatile int *cancelar, RedeResposta *r) {
  char auth[128], ct[100]; const char *headers[3] = { NULL, NULL, NULL }; unsigned n = 0; RedePedido p;
  MpCancel cancel_ctx = { cancelar };
  if (token && token[0]) { snprintf(auth, sizeof auth, "Authorization: Bearer %s", token); headers[n++] = auth; }
  if (content_type) { snprintf(ct, sizeof ct, "Content-Type: %s", content_type); headers[n++] = ct; }
  headers[n] = NULL; memset(&p, 0, sizeof p);
  p.metodo = method; p.url = url; p.cabecalhos = n ? headers : NULL;
  p.corpo = body; p.n_corpo = body_size; p.prazo_ms = prazo_ms;
  p.max_bytes = max_response; p.max_cabecalhos = 16 * 1024;
  p.seguir = 0; p.tls_pessoal = pin && pin_valido(pin); p.tls_pin = p.tls_pessoal ? pin : NULL;
  p.parar = cancelar ? mp_cancelar : NULL;
  p.parar_usuario = cancelar ? &cancel_ctx : NULL;
  if (!rede_pedir(&p, r) && !r->status) return 0;
  return r->status;
}

static int request(const char *method, const char *url, const char *pin, const char *token,
                   const char *content_type, const void *body, size_t body_size,
                   size_t max_response, int prazo_ms, RedeResposta *r) {
  return request_cancelable(method, url, pin, token, content_type, body, body_size,
                            max_response, prazo_ms, NULL, r);
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
  if (!normalize_code(code, code_norm, invite_tag)) { mp_log_rejected("pairing", "invalid_code_format"); goto done; }
  snprintf(endpoint, sizeof endpoint, "%s/v1/pair/start", url);
  if (invite_tag[0]) snprintf(request_body, sizeof request_body, "{\"invite\":\"%s\"}", invite_tag);
  else snprintf(request_body, sizeof request_body, "{\"invite\":null}");
  status = request("POST", endpoint, pin, NULL, "application/json", request_body, strlen(request_body), 16 * 1024, 45000, &start);
  if (status != 200) { mp_log_failure("pair start", status, &start); goto done; }
  if (!start.corpo || !js_texto(start.corpo, NULL, "pair_id", pair_id, sizeof pair_id) ||
      !js_texto(start.corpo, NULL, "salt", salt_hex, sizeof salt_hex)) {
    mp_log_rejected("pair start", "invalid_challenge_response"); goto done;
  }
  // Participant-owned public HTTPS nodes use the platform CA store instead of
  // an SPKI pin. js_texto returns false for an empty JSON string, so absence of
  // a pin is treated like the already-supported system-CA path below.
  if (!js_texto(start.corpo, NULL, "spki_pin", server_pin, sizeof server_pin))
    server_pin[0] = 0;
  unsigned iterations = (unsigned)js_num(start.corpo, NULL, "iterations", 0);
  int pin_matches = pin_valido(server_pin) ? (pin && pin_valido(pin) && !strcmp(server_pin, pin))
                                           : (!server_pin[0] && (!pin || !pin[0]));
  if (!hex_string(pair_id, 32) || !hex_string(salt_hex, 32) || !pin_matches ||
      iterations < 200000 || iterations > 1000000) { mp_log_rejected("pair start", "challenge_validation_failed"); goto done; }
  snprintf(salt, sizeof salt, "OSP1%s%s%s", pair_id, salt_hex, server_pin);
  if (!pbkdf2(code_norm, salt, iterations, key)) { mp_log_rejected("pair claim", "proof_generation_failed"); goto done; }
  snprintf(request_body, sizeof request_body, "client%s%s", pair_id, device);
  hmac256(key, sizeof key, (const unsigned char *)request_body, strlen(request_body), mac);
  bin_hex(mac, sizeof mac, client_proof);
  snprintf(endpoint, sizeof endpoint, "%s/v1/pair/claim", url);
  snprintf(request_body, sizeof request_body,
           "{\"pair_id\":\"%s\",\"device_name\":\"%s\",\"client_proof\":\"%s\"}", pair_id, device, client_proof);
  status = request("POST", endpoint, pin, NULL, "application/json", request_body, strlen(request_body), 16 * 1024, 45000, &claim);
  if (status != 200) { mp_log_failure("pair claim", status, &claim); goto done; }
  if (!claim.corpo || !js_texto(claim.corpo, NULL, "token", returned_token, sizeof returned_token) ||
      !js_texto(claim.corpo, NULL, "device_id", device_id, sizeof device_id) ||
      !js_texto(claim.corpo, NULL, "server_proof", server_proof, sizeof server_proof) ||
      !token_valid(returned_token) || !hex_string(device_id, 16)) {
    mp_log_rejected("pair claim", "invalid_claim_response"); goto done;
  }
  sha256_bin(returned_token, strlen(returned_token), token_hash); bin_hex(token_hash, sizeof token_hash, token_hex);
  snprintf(request_body, sizeof request_body, "server%s%s", pair_id, token_hex);
  hmac256(key, sizeof key, (const unsigned char *)request_body, strlen(request_body), mac); bin_hex(mac, sizeof mac, expected);
  if (!proof_equal(server_proof, expected)) { mp_log_rejected("pair claim", "server_proof_mismatch"); goto done; }
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
    if (ok) {
      char line[100];
      snprintf(line, sizeof line, "%s\n", new_token);
      if (!dados_gravar(MP_TOKEN_FILE, line)) { mp_log_rejected("pairing", "device_token_save_failed"); ok = 0; }
    }
    if (ok) mp_log_note("pairing completed");
    pthread_mutex_lock(&mp_mu);
    if (ok) {
      snprintf(mp_token, sizeof mp_token, "%s", new_token);
      mp_contribution_clear_locked();
      mp_estado = MPOOL_LIGADO; mp_erro[0] = 0;
    }
    else { mp_estado = MPOOL_ERRO; snprintf(mp_erro, sizeof mp_erro, "%s", "Vinculação falhou; confira o endereço do pool e o código"); }
    pthread_mutex_unlock(&mp_mu);
  } else {
    snprintf(url, sizeof url, "%s/v1/pair", w->url); RedeResposta r = {0};
    int status = request("DELETE", url, w->pin, w->token, NULL, NULL, 0, 1024, 45000, &r);
    if (status != 204) mp_log_failure("pair revoke", status, &r);
    else mp_log_note("pairing revoked");
    rede_resposta_limpar(&r);
    pthread_mutex_lock(&mp_mu);
    if (status == 204) {
      limpar_token_memoria_locked(); mp_contribution_clear_locked();
      mp_estado = MPOOL_PARADO; mp_erro[0] = 0;
    }
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
  if (mp_busy || !mp_url[0]) {
    const char *reason = mp_busy ? "operation_busy" : "endpoint_not_configured";
    mp_estado = MPOOL_ERRO; snprintf(mp_erro, sizeof mp_erro, "%s", mp_busy ? "Aguarde a operação atual" : "Configure o servidor OpenSeek");
    pthread_mutex_unlock(&mp_mu); mp_log_rejected("pairing", reason); wipe(w, sizeof *w); free(w); return 0;
  }
  mp_busy = 1; pthread_mutex_unlock(&mp_mu);
  if (pthread_create(&thread, NULL, mp_worker, w)) {
    pthread_mutex_lock(&mp_mu); mp_busy = 0; mp_estado = MPOOL_ERRO;
    snprintf(mp_erro, sizeof mp_erro, "%s", "Não foi possível iniciar a operação"); pthread_mutex_unlock(&mp_mu);
    mp_log_rejected("pairing", "worker_start_failed");
    wipe(w, sizeof *w); free(w); return 0;
  }
  pthread_detach(thread); return 1;
}
void minipool_vincular(const char *code) {
  MpoolWork *w = calloc(1, sizeof *w);
  char code_copy[512] = "", bundle_url[256] = "", clean_url[256] = "";
  const char *separator;
  if (!w) return;
  if (!code || strlen(code) >= sizeof code_copy) {
    mp_log_rejected("pairing", "invalid_code_format"); free(w); return;
  }
  snprintf(code_copy, sizeof code_copy, "%s", code);
  separator = strchr(code_copy, '|');
  if (separator) {
    size_t url_len = (size_t)(separator - code_copy);
    if (!url_len || url_len >= sizeof bundle_url || strchr(separator + 1, '|')) {
      mp_log_rejected("pairing", "invalid_bundle_format"); wipe(code_copy, sizeof code_copy); free(w); return;
    }
    memcpy(bundle_url, code_copy, url_len); bundle_url[url_len] = 0;
    snprintf(w->code, sizeof w->code, "%s", separator + 1);
    if (!url_valida(bundle_url, clean_url) || !w->code[0]) {
      mp_log_rejected("pairing", "invalid_bundle_format"); wipe(code_copy, sizeof code_copy); wipe(w, sizeof *w); free(w); return;
    }
    pthread_mutex_lock(&mp_mu);
    if (mp_busy || (mp_token[0] && strcmp(mp_url, clean_url))) {
      pthread_mutex_unlock(&mp_mu);
      mp_log_rejected("pairing", "unpair_before_changing_endpoint");
      wipe(code_copy, sizeof code_copy); wipe(w, sizeof *w); free(w); return;
    }
    snprintf(mp_url, sizeof mp_url, "%s", clean_url);
    mp_pin[0] = 0;                       // Vercel's public HTTPS certificate is CA-verified.
    snprintf(w->url, sizeof w->url, "%s", clean_url);
    w->pin[0] = 0;
    pthread_mutex_unlock(&mp_mu);
    char line[260]; snprintf(line, sizeof line, "%s\n", clean_url);
    dados_gravar(MP_URL_FILE, line);
    dados_apagar(MP_PIN_FILE);
  } else {
    snprintf(w->code, sizeof w->code, "%s", code_copy);
    pthread_mutex_lock(&mp_mu);
    snprintf(w->url, sizeof w->url, "%s", mp_url);
    snprintf(w->pin, sizeof w->pin, "%s", mp_pin);
    pthread_mutex_unlock(&mp_mu);
  }
  wipe(code_copy, sizeof code_copy);
  w->mode = 1;
  pthread_mutex_lock(&mp_mu); mp_estado = MPOOL_VINCULANDO; mp_erro[0] = 0; pthread_mutex_unlock(&mp_mu);
  (void)start_work(w);
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

static int response_error(const RedeResposta *r, const char *wanted) {
  char code[64] = "";
  return r && r->corpo && wanted && js_texto(r->corpo, NULL, "error", code, sizeof code) &&
         !strcmp(code, wanted);
}

static const char *mp_rede_erro_nome(const RedeResposta *r) {
  if (!r) return "unknown";
  switch (r->erro) {
    case REDE_OK: return r->status ? "none" : "no_response";
    case REDE_ENTRADA: return "invalid_request";
    case REDE_INDISPONIVEL: return "unavailable";
    case REDE_MEMORIA: return "memory";
    case REDE_TRANSPORTE: return "transport";
    case REDE_PRAZO: return "timeout";
    case REDE_CANCELADO: return "cancelled";
    case REDE_GERACAO: return "stale_request";
    case REDE_LIMITE_CORPO: return "body_limit";
    case REDE_LIMITE_CABECALHOS: return "header_limit";
    case REDE_REDIRECT: return "redirect";
  }
  return "unknown";
}

// API error values are server-controlled; accept only short machine codes so
// log records can never include arbitrary response text or credentials.
static void mp_api_error(const RedeResposta *r, char out[64]) {
  char raw[96] = "";
  size_t i, n = 0;
  out[0] = 0;
  if (!r || !r->corpo || !js_texto(r->corpo, NULL, "error", raw, sizeof raw)) return;
  for (i = 0; raw[i] && n < 63; i++) {
    unsigned char c = (unsigned char)raw[i];
    if (isalnum(c) || c == '_' || c == '-') out[n++] = (char)tolower(c);
    else { snprintf(out, 64, "invalid_code"); return; }
  }
  out[n] = 0;
}

static void mp_log_failure(const char *operation, int status, const RedeResposta *r) {
  char api[64] = "none";
  if (r && r->erro == REDE_CANCELADO) return;
  mp_api_error(r, api);
  printf("[miniatura] OpenSeek %s failed: HTTP %d transport=%s curl=%d api=%s\n",
         operation ? operation : "request", status, mp_rede_erro_nome(r), r ? r->curl_erro : 0, api);
  fflush(stdout);
}

static void mp_log_rejected(const char *operation, const char *reason) {
  printf("[miniatura] OpenSeek %s rejected: %s\n",
         operation ? operation : "response", reason ? reason : "invalid_response");
  fflush(stdout);
}

static void mp_log_note(const char *message) {
  printf("[miniatura] OpenSeek %s\n", message ? message : "request note");
  fflush(stdout);
}

static int mp_json_array_empty(const char *json, const char *key) {
  char needle[48];
  const char *p;
  int n = snprintf(needle, sizeof needle, "\"%s\"", key ? key : "");
  if (!json || n <= 0 || (size_t)n >= sizeof needle || !(p = strstr(json, needle))) return 0;
  p = strchr(p + n, ':');
  if (!p) return 0;
  do { p++; } while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n');
  if (*p++ != '[') return 0;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return *p == ']';
}

static int catalog_hints(const MiniMeta *meta, char imdb[24], int *temporada, int *episodio) {
  if (!meta || !imdb || !temporada || !episodio) return 0;
  imdb[0] = 0;
  // Catalog IDs can arrive with an episode suffix (tt1234567:2:4). Pool keys
  // and hint requests use the base IMDb ID plus explicit season/episode fields.
  idbase_copiar(meta->imdb, imdb, 24);
  *temporada = meta->temporada;
  *episodio = meta->episodio;
  if (*temporada < 0 || *episodio <= 0) {
    int idTemporada = 0, idEpisodio = 0;
    idbase_episodio(meta->imdb, &idTemporada, &idEpisodio);
    if (idEpisodio > 0) {
      if (*temporada < 0 || (*episodio <= 0 && *temporada == 0 && idTemporada > 0))
        *temporada = idTemporada;
      if (*episodio <= 0) *episodio = idEpisodio;
    }
  }
  if (*temporada < 0) *temporada = 0;
  if (*episodio < 0) *episodio = 0;
  size_t imdb_len = strlen(imdb);
  if (*temporada > 9999 || *episodio > 9999 || strncmp(imdb, "tt", 2) ||
      imdb_len < 3 || imdb_len > 12) return 0;
  for (const char *p = imdb + 2; *p; p++) if (!isdigit((unsigned char)*p)) return 0;
  return 1;
}

int minipool_work_key(const MiniMeta *meta, char key[65]) {
  char identity[128], imdb[24];
  int temporada, episodio;
  if (key) key[0] = 0;
  if (!meta || !key || !catalog_hints(meta, imdb, &temporada, &episodio)) return 0;
  int n;
  if (temporada == 0 && episodio == 0)
    n = snprintf(identity, sizeof identity, "openseek-work-v1\nimdb:%s", imdb);
  else {
    if (episodio <= 0) return 0;
    n = snprintf(identity, sizeof identity, "openseek-episode-v1\nimdb:%s\nseason:%d\nepisode:%d",
                 imdb, temporada, episodio);
  }
  if (n <= 0 || (size_t)n >= sizeof identity) return 0;
  admin_sha256_hex((const unsigned char *)identity, (unsigned long)n, key);
  wipe(identity, sizeof identity);
  return hex_string(key, 64);
}

static int candidate_grid(const char *profile) {
  if (!strcmp(profile, "nvmt-v1-g5")) return 5000;
  if (!strcmp(profile, "nvmt-v1-g10")) return 10000;
  return 0;
}

static int url_append(char *url, size_t cap, size_t *used, const char *text) {
  size_t n = strlen(text);
  if (*used >= cap || n >= cap - *used) return 0;
  memcpy(url + *used, text, n + 1);
  *used += n;
  return 1;
}

static int url_append_param(char *url, size_t cap, size_t *used, const char *name,
                            const char *value, size_t maxBytes) {
  static const char hex[] = "0123456789ABCDEF";
  char encoded[601]; size_t n = 0;
  if (!value) return 1;
  size_t inputBytes = 0;
  while (inputBytes < maxBytes && value[inputBytes]) inputBytes++;
  if (value[inputBytes]) {
    if (inputBytes > 0) {
      size_t lead = inputBytes - 1;
      while (lead > 0 && ((unsigned char)value[lead] & 0xc0) == 0x80) lead--;
      unsigned char first = (unsigned char)value[lead];
      size_t codepointBytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : first < 0xf8 ? 4 : 1;
      if (codepointBytes > inputBytes - lead) inputBytes = lead;
    }
  }
  for (size_t i = 0; i < inputBytes; i++) {
    unsigned char c = (unsigned char)value[i];
    int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
    if (safe) {
      if (n + 1 >= sizeof encoded) return 0;
      encoded[n++] = (char)c;
    } else {
      if (n + 3 >= sizeof encoded) return 0;
      encoded[n++] = '%'; encoded[n++] = hex[c >> 4]; encoded[n++] = hex[c & 15];
    }
  }
  encoded[n] = 0;
  char prefix[48];
  int pn = snprintf(prefix, sizeof prefix, "%s%s=", *used && strchr(url, '?') ? "&" : "?", name);
  if (pn <= 0 || (size_t)pn >= sizeof prefix || !url_append(url, cap, used, prefix) ||
      !url_append(url, cap, used, encoded)) return 0;
  return 1;
}

static int url_append_number(char *url, size_t cap, size_t *used,
                             const char *name, unsigned long long value) {
  char number[32];
  snprintf(number, sizeof number, "%llu", value);
  return url_append_param(url, cap, used, name, number, sizeof number - 1);
}

static int candidate_url(char url[2048], const char *base, const MiniMeta *meta,
                         const char *filename, uint64_t sizeBytes) {
  size_t used = 0; char key[65] = "", imdb[24] = "";
  int temporada = 0, episodio = 0;
  int have_imdb = catalog_hints(meta, imdb, &temporada, &episodio);
  int n = snprintf(url, 2048, "%s/v1/pool/candidates", base);
  if (n <= 0 || n >= 2048) return 0;
  used = (size_t)n;
  if ((have_imdb && !url_append_param(url, 2048, &used, "imdb", imdb, sizeof imdb - 1)) ||
      !url_append_number(url, 2048, &used, "season", (unsigned long long)temporada) ||
      !url_append_number(url, 2048, &used, "episode", (unsigned long long)episodio) ||
      !url_append_number(url, 2048, &used, "duration_ms", (unsigned long long)(meta->durMs > 0 ? meta->durMs : 0)) ||
      (hex_string(meta->fp, 64) && !url_append_param(url, 2048, &used, "exclude_fp", meta->fp, 64)) ||
      (sizeBytes && !url_append_number(url, 2048, &used, "size_bytes", sizeBytes)) ||
      (filename && filename[0] && !url_append_param(url, 2048, &used, "filename", filename, 200))) return 0;
  // Authenticated reads use this stable work identity so C3 applies only this
  // device's saved offsets and rejections. Public/read-key queries still work.
  if (minipool_work_key(meta, key) && !url_append_param(url, 2048, &used, "work_key", key, 64)) return 0;
  return 1;
}

static int legacy_episode_url(char url[512], const char *base, const MiniMeta *meta) {
  char key[65] = "";
  if (!minipool_work_key(meta, key) || meta->durMs <= 0) return 0;
  int n = snprintf(url, 512, "%s/v1/pool/episodes/%s?duration_ms=%ld", base, key, meta->durMs);
  return n > 0 && n < 512;
}

static int upload_url(char url[2048], const char *base, const MiniMeta *meta,
                      const char *profile, long durationMs, const char *filename,
                      uint64_t sizeBytes, int withHints) {
  char key[65] = "", imdb[24] = "";
  int temporada = 0, episodio = 0;
  int have_imdb = catalog_hints(meta, imdb, &temporada, &episodio);
  int n = snprintf(url, 2048, "%s/v1/pool/%s/nvmt?profile=%s&duration_ms=%ld",
                   base, meta->fp, profile, durationMs);
  if (n <= 0 || n >= 2048) return 0;
  size_t used = (size_t)n;
  if (minipool_work_key(meta, key) &&
      !url_append_param(url, 2048, &used, "episode_key", key, sizeof key - 1)) return 0;
  if (!withHints) return 1;
  if ((have_imdb && !url_append_param(url, 2048, &used, "imdb", imdb, sizeof imdb - 1)) ||
      !url_append_number(url, 2048, &used, "season", (unsigned long long)temporada) ||
      !url_append_number(url, 2048, &used, "episode", (unsigned long long)episodio) ||
      (sizeBytes && !url_append_number(url, 2048, &used, "size_bytes", sizeBytes)) ||
      (filename && filename[0] && !url_append_param(url, 2048, &used, "filename", filename, 200))) return 0;
  return 1;
}

static int upload_package_direct(const char *base, const MiniMeta *meta,
                                 const char *profile, long durationMs, const char *filename,
                                 uint64_t sizeBytes, const unsigned char *data, size_t size,
                                 const char *pin, const char *token, RedeResposta *response,
                                 char request_url[2048]) {
  char url[2048], legacyUrl[2048];
  if (!upload_url(url, base, meta, profile, durationMs, filename, sizeBytes, 1)) return 0;
  snprintf(request_url, 2048, "%s", url);
  int status = request("PUT", url, pin, token, "application/vnd.openseek.nvmt", data, size,
                       16 * 1024, 45000, response);
  if (status == 400 || status == 404) {
    mp_log_note("upload retrying legacy route after compatibility response");
    rede_resposta_limpar(response);
    if (!upload_url(legacyUrl, base, meta, profile, durationMs, filename, sizeBytes, 0)) return status;
    snprintf(request_url, 2048, "%s", legacyUrl);
    status = request("PUT", legacyUrl, pin, token, "application/vnd.openseek.nvmt", data, size,
                     16 * 1024, 45000, response);
  }
  return status;
}

static int json_append(char *out, size_t cap, size_t *used, const char *value) {
  static const char hex[] = "0123456789abcdef";
  if (!out || !used || !value || *used >= cap) return 0;
  if (*used + 1 >= cap) return 0;
  out[(*used)++] = '"';
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    if (*p == '"' || *p == '\\') {
      if (*used + 2 >= cap) return 0;
      out[(*used)++] = '\\'; out[(*used)++] = (char)*p;
    } else if (*p < 0x20) {
      if (*used + 6 >= cap) return 0;
      out[(*used)++] = '\\'; out[(*used)++] = 'u';
      out[(*used)++] = '0'; out[(*used)++] = '0';
      out[(*used)++] = hex[*p >> 4]; out[(*used)++] = hex[*p & 15];
    } else {
      if (*used + 1 >= cap) return 0;
      out[(*used)++] = (char)*p;
    }
  }
  if (*used + 1 >= cap) return 0;
  out[(*used)++] = '"'; out[*used] = 0;
  return 1;
}

static int upload_id_valid(const char *value) {
  size_t n = value ? strlen(value) : 0;
  if (n < 8 || n > 64 || strncmp(value, "u_", 2)) return 0;
  for (size_t i = 0; i < n; i++)
    if (!isalnum((unsigned char)value[i]) && value[i] != '_' && value[i] != '-') return 0;
  return 1;
}

static int signed_storage_url_valid(const char *url) {
  if (!url || strncmp(url, "https://", 8) || strlen(url) > 1900 ||
      !strstr(url, "/storage/v1/") || strchr(url, '#')) return 0;
  const char *authority = url + 8;
  const char *path = strchr(authority, '/');
  if (!path || path == authority || strchr(authority, '@')) return 0;
  for (const unsigned char *p = (const unsigned char *)url; *p; p++)
    if (*p <= 32 || *p >= 127 || *p == '\\') return 0;
  return 1;
}

static int upload_package_staged(const char *base, const MiniMeta *meta,
                                 const char *profile, long durationMs, const char *filename,
                                 uint64_t sizeBytes, const unsigned char *data, size_t size,
                                 const char *pin, const char *token, RedeResposta *response,
                                 int *staging_unavailable) {
  char url[2048], body[1024], upload_id[96] = "", put_url[2048] = "";
  char imdb[24] = "", state[32] = "";
  int temporada = 0, episodio = 0, have_imdb;
  size_t used = 0;
  if (staging_unavailable) *staging_unavailable = 0;
  have_imdb = catalog_hints(meta, imdb, &temporada, &episodio);
  int n = snprintf(body, sizeof body,
                   "{\"fp\":\"%s\",\"profile\":\"%s\",\"duration_ms\":%ld,\"size_bytes\":%llu,\"season\":%d,\"episode\":%d",
                   meta->fp, profile, durationMs, (unsigned long long)sizeBytes,
                   temporada, episodio);
  if (n <= 0 || (size_t)n >= sizeof body) return 0;
  used = (size_t)n;
  if (have_imdb) {
    n = snprintf(body + used, sizeof body - used, ",\"imdb\":");
    if (n <= 0 || (size_t)n >= sizeof body - used) return 0;
    used += (size_t)n;
    if (!json_append(body, sizeof body, &used, imdb)) return 0;
  }
  if (filename && filename[0]) {
    n = snprintf(body + used, sizeof body - used, ",\"filename\":");
    if (n <= 0 || (size_t)n >= sizeof body - used) return 0;
    used += (size_t)n;
    if (!json_append(body, sizeof body, &used, filename)) return 0;
  }
  if (used + 2 >= sizeof body) return 0;
  body[used++] = '}'; body[used] = 0;

  n = snprintf(url, sizeof url, "%s/v1/pool/uploads", base);
  if (n <= 0 || (size_t)n >= sizeof url) return 0;
  int status = request("POST", url, pin, token, "application/json", body, used,
                       16 * 1024, 15000, response);
  if (status == 400 && response_error(response, "staging_unavailable")) {
    if (staging_unavailable) *staging_unavailable = 1;
    return 0;
  }
  if (status != 201 || !response->corpo ||
      !js_texto(response->corpo, NULL, "upload_id", upload_id, sizeof upload_id) ||
      !js_texto(response->corpo, NULL, "put_url", put_url, sizeof put_url) ||
      !upload_id_valid(upload_id) || !signed_storage_url_valid(put_url)) {
    mp_log_failure("staged upload create", status, response);
    return 0;
  }
  rede_resposta_limpar(response);
  // The short-lived signed Storage URL is the only credential used here.
  // Never forward the participant's node token or follow a redirect.
  status = request("PUT", put_url, NULL, NULL, "application/vnd.openseek.nvmt",
                   data, size, 16 * 1024, 45000, response);
  if (status != 200 && status != 201) {
    mp_log_failure("staged package transfer", status, response);
    return 0;
  }
  rede_resposta_limpar(response);
  n = snprintf(url, sizeof url, "%s/v1/pool/uploads/%s/finalize", base, upload_id);
  if (n <= 0 || (size_t)n >= sizeof url) return 0;
  status = request("POST", url, pin, token, "application/json", "{}", 2,
                   16 * 1024, 15000, response);
  if (status != 202) {
    mp_log_failure("staged upload finalize", status, response);
    return 0;
  }
  rede_resposta_limpar(response);
  n = snprintf(url, sizeof url, "%s/v1/pool/uploads/%s", base, upload_id);
  if (n <= 0 || (size_t)n >= sizeof url) return 0;
  status = request("GET", url, pin, token, NULL, NULL, 0, 16 * 1024, 15000, response);
  if (status != 200 || !response->corpo ||
      !js_texto(response->corpo, NULL, "state", state, sizeof state)) {
    mp_log_failure("staged upload status", status, response);
    return 0;
  }
  if (!strcmp(state, "accepted")) return 1;
  if (!strcmp(state, "pending") || !strcmp(state, "retrying") || !strcmp(state, "paused")) {
    mp_log_note(!strcmp(state, "paused") ? "contribution paused by free-tier quota" :
                "contribution queued for retry");
    return 2;
  }
  if (!strcmp(state, "failed")) {
    mp_log_rejected("contribution", "publication_failed");
    return -1;                            // HTTP 200 carries a terminal, failed upload state.
  }
  return 0;
}

static int contribution_url_valid(const char *url, char out[MP_CONTRIBUTE_URL_MAX + 1],
                                  int *telegram_link) {
  size_t n, authority_n, host_n;
  const char *authority, *path, *host_end;
  char host[256];
  if (telegram_link) *telegram_link = 0;
  if (!url || strncmp(url, "https://", 8)) return 0;
  n = strlen(url);
  if (n < 10 || n > MP_CONTRIBUTE_URL_MAX || strchr(url, '?') || strchr(url, '#')) return 0;
  authority = url + 8;
  path = strchr(authority, '/');
  authority_n = path ? (size_t)(path - authority) : strlen(authority);
  if (!authority_n || authority_n >= sizeof host) return 0;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)url[i];
    if (c <= 32 || c >= 127 || c == '@' || c == '\\') return 0;
  }
  for (size_t i = 0; i < authority_n; i++) {
    unsigned char c = (unsigned char)authority[i];
    if (!isalnum(c) && c != '.' && c != '-' && c != ':' && c != '[' && c != ']') return 0;
  }
  if (authority[0] == '[') {
    const char *close = memchr(authority, ']', authority_n);
    if (!close || (size_t)(close - authority) < 3 ||
        ((size_t)(close - authority + 1) < authority_n && close[1] != ':')) return 0;
    host_end = close + 1;
  } else {
    host_end = memchr(authority, ':', authority_n);
    if (!host_end) host_end = authority + authority_n;
  }
  host_n = (size_t)(host_end - authority);
  if (!host_n || host_n >= sizeof host) return 0;
  if (host_end < authority + authority_n) {
    unsigned long port = 0;
    if (*host_end != ':' || !host_end[1]) return 0;
    for (const unsigned char *p = (const unsigned char *)host_end + 1;
         p < (const unsigned char *)authority + authority_n; p++) {
      if (!isdigit(*p)) return 0;
      if (port > 6553 || (port == 6553 && *p > '5')) return 0;
      port = port * 10 + (unsigned)(*p - '0');
    }
    if (!port) return 0;
  }
  memcpy(host, authority, host_n); host[host_n] = 0;
  for (size_t i = 0; i < host_n; i++) host[i] = (char)tolower((unsigned char)host[i]);
  if (path) {
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
      if (!(isalnum(*p) || strchr("/-._~%", *p))) return 0;
    }
  }
  int telegram_host = !strcmp(host, "t.me") || !strcmp(host, "telegram.me") ||
                      !strcmp(host, "www.telegram.me");
  int telegram_port = host_end == authority + authority_n ||
                      ((size_t)((authority + authority_n) - host_end) == 4 &&
                       !strncmp(host_end, ":443", 4));
  if (telegram_link && telegram_host && telegram_port)
    *telegram_link = 1;
  memcpy(out, url, n + 1);
  return 1;
}

static int contribution_retry_url(const char *contribute_url, const char *original_url,
                                  char out[4096]) {
  const char *target_path, *source_path, *source_query;
  size_t target_origin_n;
  int n;
  if (!contribute_url || !original_url || strncmp(original_url, "https://", 8)) return 0;
  target_path = strchr(contribute_url + 8, '/');
  source_path = strchr(original_url + 8, '/');
  if (!source_path) return 0;
  source_query = strchr(source_path, '?');
  if (!target_path || !target_path[1]) {
    target_origin_n = target_path ? (size_t)(target_path - contribute_url) : strlen(contribute_url);
    n = snprintf(out, 4096, "%.*s%s", (int)target_origin_n, contribute_url, source_path);
  } else {
    n = snprintf(out, 4096, "%s%s", contribute_url, source_query ? source_query : "");
  }
  return n > 0 && n < 4096;
}

static int request_contribution_host(const char *url, const unsigned char *data, size_t size,
                                     RedeResposta *response) {
  const char *headers[] = { "Content-Type: application/vnd.openseek.nvmt", NULL };
  RedePedido p;
  memset(&p, 0, sizeof p);
  p.metodo = "PUT"; p.url = url; p.cabecalhos = headers;
  p.corpo = data; p.n_corpo = size; p.prazo_ms = 45000;
  p.max_bytes = 16 * 1024; p.max_cabecalhos = 16 * 1024;
  p.seguir = 0; p.tls_pessoal = 0; p.tls_pin = NULL;
  if (!rede_pedir(&p, response) && !response->status) return 0;
  return response->status;
}

static int retry_read_only_upload(const char *original_url, const unsigned char *data, size_t size,
                                  RedeResposta *response, int *retried) {
  // One byte of slack so a truncated value cannot pass the length check.
  char raw[MP_CONTRIBUTE_URL_MAX + 2] = "", clean[MP_CONTRIBUTE_URL_MAX + 1] = "";
  char target[4096];
  int telegram_link = 0, status;
  if (retried) *retried = 0;
  if (!response_error(response, "read_only_channel")) return response ? response->status : 0;
  mp_contribution_result(1, NULL);
  if (!response->corpo || !js_texto(response->corpo, NULL, "contribute_url", raw, sizeof raw)) {
    mp_log_rejected("package contribution", "missing_or_invalid_contribute_url");
    return response->status;
  }
  if (strlen(raw) > MP_CONTRIBUTE_URL_MAX ||
      !contribution_url_valid(raw, clean, &telegram_link)) {
    mp_log_rejected("package contribution", "invalid_contribute_url");
    return response->status;
  }
  mp_contribution_result(1, clean);
  if (telegram_link) {
    mp_log_note("read-only channel; contribution link is available in Settings");
    return response->status;
  }
  if (!contribution_retry_url(clean, original_url, target)) {
    mp_log_rejected("package contribution", "invalid_contribution_target");
    return response->status;
  }
  if (retried) *retried = 1;
  RedeResposta retry = {0};
  status = request_contribution_host(target, data, size, &retry);
  if (status >= 200 && status < 300) {
    rede_resposta_limpar(response);
    *response = retry;
    mp_contribution_result(0, NULL);
    mp_log_note("read-only contribution accepted by the contribution host");
  } else {
    rede_resposta_limpar(response);
    *response = retry;
    mp_contribution_result(1, clean);
    mp_log_note("read-only contribution failed; destination is available in Settings");
  }
  return status;
}

int minipool_candidates(int enabled, const MiniMeta *meta, const char *filename,
                        uint64_t sizeBytes, MpoolCandidate out[8], int *count,
                        volatile int *cancelar) {
  char url[2048], base[256], legacyUrl[512], pin[64], token[96];
  RedeResposta r = {0}; int status, found = 0;
  if (count) *count = 0;
  if (out) memset(out, 0, sizeof(MpoolCandidate) * 8);
  if (!enabled || !meta || !out || !count || meta->durMs <= 0) return 0;
  pthread_mutex_lock(&mp_mu);
  snprintf(base, sizeof base, "%s", mp_url); snprintf(pin, sizeof pin, "%s", mp_pin);
  snprintf(token, sizeof token, "%s", mp_token); pthread_mutex_unlock(&mp_mu);
  if (!base[0]) { mp_log_rejected("candidate index", "endpoint_not_configured"); wipe(token, sizeof token); return 0; }
  if (!candidate_url(url, base, meta, filename, sizeBytes)) { wipe(token, sizeof token); return 0; }
  status = request_cancelable("GET", url, pin, token, NULL, NULL, 0, 16 * 1024, 2500, cancelar, &r);
  if (status == 400 && legacy_episode_url(legacyUrl, base, meta)) {
    mp_log_note("candidate index using legacy route after HTTP 400");
    rede_resposta_limpar(&r);
    status = request_cancelable("GET", legacyUrl, pin, token, NULL, NULL, 0,
                                16 * 1024, 2500, cancelar, &r);
  }
  if (status == 200 && r.corpo) {
    const char *p = js_array(r.corpo, NULL, "candidates");
    if (!p && !mp_json_array_empty(r.corpo, "candidates"))
      mp_log_rejected("candidate index", "missing_or_invalid_candidates_array");
    while (p && found < 8) {
      const char *end = js_fim(p); char fp[65] = "", profile[16] = "", relation[32] = "";
      long dur; int slots, tiles, grid, score;
      if (!end || end <= p) break;
      score = (int)js_num(p, end, "score", -1);
      if (!js_texto(p, end, "relation", relation, sizeof relation) || score < 60 ||
          !strcmp(relation, "length_differs")) {
        // The response is ranked. An ineligible best candidate ends this lookup.
        break;
      }
      if (js_texto(p, end, "fp", fp, sizeof fp) && hex_string(fp, 64) &&
          js_texto(p, end, "profile", profile, sizeof profile) && (grid = candidate_grid(profile)) != 0) {
        int64_t a_num, a_den, b_ms;
        dur = (long)js_num(p, end, "duration_ms", 0);
        slots = (int)js_num(p, end, "n_slots", 0); tiles = (int)js_num(p, end, "n_tiles", 0);
        if (candidate_int32(p, end, "a_num", 1, &a_num) && a_num > 0 &&
            candidate_int32(p, end, "a_den", 1, &a_den) && a_den > 0 &&
            candidate_int32(p, end, "b_ms", 0, &b_ms) &&
            dur > 0 && slots > 0 && slots <= 8640 && tiles > 0 && tiles <= slots &&
            slots == (int)(((long long)dur + grid - 1) / grid)) {
          snprintf(out[found].fp, sizeof out[found].fp, "%s", fp);
          snprintf(out[found].profile, sizeof out[found].profile, "%s", profile);
          out[found].durationMs = dur; out[found].nSlots = slots; out[found].nTiles = tiles;
          out[found].score = score;
          out[found].a_num = a_num; out[found].a_den = a_den; out[found].b_ms = b_ms;
          snprintf(out[found].relation, sizeof out[found].relation, "%s", relation);
          found++;
        }
      }
      p = js_prox(end);
    }
  }
  if (status == 200 && !r.corpo) mp_log_rejected("candidate index", "empty_response_body");
  if (status != 200) mp_log_failure("candidate index", status, &r);
  printf("[miniatura] OpenSeek candidate index: HTTP %d, %d candidate(s)\n", status, found);
  fflush(stdout);
  rede_resposta_limpar(&r); wipe(token, sizeof token); *count = found; return found > 0;
}

static int minipool_package_get(int enabled, const char *fp, const char *profile,
                                long durationMs, const int *slots, int nSlots,
                                unsigned char **data, size_t *size, volatile int *cancelar,
                                MpoolSampleInfo *sampleInfo) {
  char url[1024], base[256], pin[64], token[96], duration[32] = "", hash[80] = "", digest[65];
  int status = 0, grid = candidate_grid(profile); RedeResposta r = {0};
  if (sampleInfo) {
    memset(sampleInfo, 0, sizeof *sampleInfo);
    sampleInfo->status = MPOOL_SAMPLE_NOT_ATTEMPTED;
    sampleInfo->requestedSlots = slots ? nSlots : 0;
  }
  if (data) *data = NULL; if (size) *size = 0;
  if (!enabled || !data || !size || !hex_string(fp, 64) || !grid || durationMs <= 0 ||
      (slots && (nSlots < 1 || nSlots > 12)) || (!slots && nSlots != 0)) return 0;
  pthread_mutex_lock(&mp_mu);
  snprintf(base, sizeof base, "%s", mp_url); snprintf(pin, sizeof pin, "%s", mp_pin);
  snprintf(token, sizeof token, "%s", mp_token); pthread_mutex_unlock(&mp_mu);
  if (!base[0]) { mp_log_rejected("package download", "endpoint_not_configured"); wipe(token, sizeof token); return 0; }
  if (slots) {
    size_t used;
    int n = snprintf(url, sizeof url, "%s/v1/pool/%s/nvmt?profile=%s&slots=", base, fp, profile);
    if (n <= 0 || (size_t)n >= sizeof url) goto done;
    used = (size_t)n;
    for (int i = 0; i < nSlots; i++) {
      n = snprintf(url + used, sizeof url - used, "%s%d", i ? "," : "", slots[i]);
      if (n <= 0 || (size_t)n >= sizeof url - used) goto done;
      used += (size_t)n;
    }
  } else snprintf(url, sizeof url, "%s/v1/pool/%s/nvmt?profile=%s", base, fp, profile);
  status = request_cancelable("GET", url, pin, token, NULL, NULL, 0, MP_MAX_POOL_RESPONSE, 15000, cancelar, &r);
  if (sampleInfo) {
    sampleInfo->httpStatus = status;
    sampleInfo->responseBytes = r.n_corpo;
    if (r.corpo && r.n_corpo >= 20 && !memcmp(r.corpo, "NVMT", 4))
      sampleInfo->returnedTiles = (int)read32((const unsigned char *)r.corpo + 16);
  }
  if (status != 200) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_HTTP_ERROR;
    mp_log_failure("package download", status, &r);
    goto done;
  }
  if (!r.corpo) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_INVALID_RESPONSE;
    mp_log_rejected("package download", "empty_response_body");
    goto done;
  }
  if (!response_header(r.cabecalhos, "X-OpenSeek-Duration-Ms", duration, sizeof duration) ||
      !response_header(r.cabecalhos, "X-OpenSeek-Sha256", hash, sizeof hash)) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_INVALID_RESPONSE;
    mp_log_rejected("package download", "missing_integrity_headers");
    goto done;
  }
  if (atol(duration) != durationMs) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_INVALID_RESPONSE;
    mp_log_rejected("package download", "duration_mismatch");
    goto done;
  }
  if (!hex_string(hash, 64)) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_INVALID_RESPONSE;
    mp_log_rejected("package download", "invalid_sha256_header");
    goto done;
  }
  if (r.n_corpo > MP_MAX_PACKAGE) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_INVALID_RESPONSE;
    mp_log_rejected("package download", "package_too_large");
    goto done;
  }
  if (!nvmt_ok((unsigned char *)r.corpo, r.n_corpo, durationMs, grid)) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_INVALID_RESPONSE;
    mp_log_rejected("package download", "invalid_nvmt_package");
    goto done;
  }
  admin_sha256_hex((const unsigned char *)r.corpo, (unsigned long)r.n_corpo, digest);
  if (strcasecmp(digest, hash)) {
    if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_CHECKSUM_MISMATCH;
    mp_log_rejected("package download", "sha256_mismatch");
    goto done;
  }
  *data = (unsigned char *)r.corpo; r.corpo = NULL; *size = r.n_corpo;
  if (sampleInfo) sampleInfo->status = MPOOL_SAMPLE_OK;
done:
  rede_resposta_limpar(&r); wipe(token, sizeof token); return *data != NULL;
}

int minipool_buscar_slots(int enabled, const MpoolCandidate *candidate,
                          const int *slots, int nSlots, unsigned char **data, size_t *size,
                          volatile int *cancelar, MpoolSampleInfo *info) {
  return candidate ? minipool_package_get(enabled, candidate->fp, candidate->profile,
                                           candidate->durationMs, slots, nSlots, data, size, cancelar,
                                           info) : 0;
}

static void write32(unsigned char *p, uint32_t value) {
  p[0] = (unsigned char)value;
  p[1] = (unsigned char)(value >> 8);
  p[2] = (unsigned char)(value >> 16);
  p[3] = (unsigned char)(value >> 24);
}

static int nvmt_merge_chunk(unsigned char **combined, size_t *used, size_t *capacity,
                            unsigned char *seen, uint32_t n_slots, uint32_t *n_tiles,
                            const unsigned char *chunk, size_t chunk_size,
                            long duration_ms, int grid) {
  size_t pos = 20;
  if (!combined || !*combined || !used || !capacity || !seen || !n_tiles ||
      !nvmt_ok(chunk, chunk_size, duration_ms, grid) ||
      read32(chunk + 12) != n_slots) return 0;
  uint32_t chunk_tiles = read32(chunk + 16);
  for (uint32_t i = 0; i < chunk_tiles; i++) {
    uint32_t slot = read32(chunk + pos);
    uint32_t length = read32(chunk + pos + 8);
    size_t record_size = 12u + (size_t)length;
    if (slot >= n_slots || record_size > chunk_size - pos) return 0;
    if (!seen[slot]) {
      size_t needed = *used + record_size;
      if (needed > MP_MAX_PACKAGE) return 0;
      if (needed > *capacity) {
        size_t next = *capacity;
        while (next < needed && next < MP_MAX_PACKAGE / 2) next *= 2;
        if (next < needed) next = needed;
        unsigned char *grown = realloc(*combined, next);
        if (!grown) return 0;
        *combined = grown; *capacity = next;
      }
      memcpy(*combined + *used, chunk + pos, record_size);
      *used = needed; seen[slot] = 1; (*n_tiles)++;
    }
    pos += record_size;
  }
  write32(*combined + 16, *n_tiles);
  return 1;
}

static int minipool_manifest_get(int enabled, const char *fp, const char *profile,
                                 long expected_duration, const char *episode_key,
                                 int **tile_slots, int *n_tile_slots, long *duration_out) {
  char base[256], pin[64], token[96], url[512];
  RedeResposta r = {0};
  int status = 0, ok = 0;
  int64_t duration = 0, declared_count = 0;
  int *slots = NULL;
  if (tile_slots) *tile_slots = NULL;
  if (n_tile_slots) *n_tile_slots = 0;
  if (duration_out) *duration_out = 0;
  if (!enabled || !tile_slots || !n_tile_slots || !duration_out ||
      !hex_string(fp, 64) || !candidate_grid(profile) || expected_duration < 0 ||
      (episode_key && !hex_string(episode_key, 64))) return 0;
  pthread_mutex_lock(&mp_mu);
  snprintf(base, sizeof base, "%s", mp_url);
  snprintf(pin, sizeof pin, "%s", mp_pin);
  snprintf(token, sizeof token, "%s", mp_token);
  pthread_mutex_unlock(&mp_mu);
  if (!base[0] || !token[0]) { wipe(token, sizeof token); return 0; }
  int n = snprintf(url, sizeof url,
                   "%s/v1/pool/%s/nvmt/manifest?profile=%s", base, fp, profile);
  if (n <= 0 || (size_t)n >= sizeof url) goto done;
  size_t used = (size_t)n;
  if ((expected_duration > 0 && !url_append_number(url, sizeof url, &used,
                                                   "duration_ms", (unsigned long long)expected_duration)) ||
      (episode_key && !url_append_param(url, sizeof url, &used, "episode_key", episode_key, 64))) goto done;
  status = request("GET", url, pin, token, NULL, NULL, 0, 64 * 1024, 15000, &r);
  if (status != 200 || !r.corpo ||
      !candidate_int32(r.corpo, NULL, "duration_ms", 0, &duration) || duration <= 0 ||
      !candidate_int32(r.corpo, NULL, "n_tiles", 0, &declared_count) ||
      declared_count < 1 || declared_count > (duration + 9999) / 10000 ||
      (expected_duration > 0 && duration != expected_duration)) {
    if (status != 404 && status != 200) mp_log_failure("package manifest", status, &r);
    goto done;
  }
  size_t capacity = (size_t)declared_count;
  slots = calloc(capacity, sizeof *slots);
  if (!slots) goto done;
  const char *p = strstr(r.corpo, "\"tile_slots\"");
  if (!p || !(p = strchr(p, '['))) goto done;
  p++;
  int count = 0, previous = -1;
  while (*p && *p != ']') {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
    if (*p == ']') break;
    char *end = NULL;
    long value = strtol(p, &end, 10);
    if (end == p || value < 0 || value > INT_MAX || value <= previous ||
        value >= (duration + 9999) / 10000 || count >= (int)capacity) goto done;
    slots[count++] = (int)value;
    previous = (int)value;
    p = end;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ',' && *p != ']') goto done;
  }
  if (*p != ']' || count != declared_count) goto done;
  *tile_slots = slots; slots = NULL;
  *n_tile_slots = count;
  *duration_out = (long)duration;
  ok = 1;
done:
  free(slots); rede_resposta_limpar(&r); wipe(token, sizeof token); return ok;
}

static int minipool_collect_nvmt(int enabled, const char *fp, const char *profile,
                                 long duration_ms, int n_slots,
                                 const int *tile_slots, int n_tile_slots,
                                 unsigned char **data, size_t *size,
                                 volatile int *cancelar) {
  int grid = candidate_grid(profile);
  int tile_step, batch[12];
  uint32_t count = 0;
  size_t used = 20, capacity = 20;
  unsigned char *combined = NULL, *seen = NULL;
  if (data) *data = NULL;
  if (size) *size = 0;
  if (!enabled || !data || !size || !hex_string(fp, 64) || !grid || duration_ms <= 0 ||
      n_slots != (duration_ms + grid - 1) / grid || n_slots < 1 || n_slots > 8640 ||
      !tile_slots || n_tile_slots < 1 || n_tile_slots > (duration_ms + 9999) / 10000) return 0;
  tile_step = 10000 / grid;
  if (tile_step < 1) return 0;
  combined = calloc(1, used);
  seen = calloc((size_t)n_slots, 1);
  if (!combined || !seen) goto done;
  memcpy(combined, "NVMT", 4); write32(combined + 4, 1);
  write32(combined + 8, (uint32_t)grid); write32(combined + 12, (uint32_t)n_slots);
  for (int start = 0; start < n_tile_slots && !(cancelar && *cancelar); start += 12) {
    int n = n_tile_slots - start;
    if (n > 12) n = 12;
    for (int i = 0; i < n; i++) batch[i] = tile_slots[start + i] * tile_step;
    unsigned char *part = NULL; size_t part_size = 0;
    MpoolSampleInfo info = {0};
    if (!minipool_package_get(enabled, fp, profile, duration_ms, batch, n,
                              &part, &part_size, cancelar, &info)) {
      free(part);
      if (info.httpStatus == 404) continue;
      if (count) break;               // Keep verified chunks; local generation fills the rest.
      goto done;
    }
    int merged = nvmt_merge_chunk(&combined, &used, &capacity, seen, (uint32_t)n_slots,
                                  &count, part, part_size, duration_ms, grid);
    free(part);
    if (!merged) { mp_log_rejected("package assembly", "invalid_chunk"); goto done; }
  }
  if (!count || !nvmt_ok(combined, used, duration_ms, grid)) goto done;
  *data = combined; *size = used; combined = NULL;
done:
  free(combined); free(seen); return *data != NULL;
}

int minipool_buscar_candidato(int enabled, const MpoolCandidate *candidate,
                              unsigned char **data, size_t *size, volatile int *cancelar) {
  int *tile_slots = NULL, n_tile_slots = 0;
  long duration = 0;
  if (!candidate || !data || !size ||
      !minipool_manifest_get(enabled, candidate->fp, candidate->profile,
                             candidate->durationMs, NULL, &tile_slots,
                             &n_tile_slots, &duration)) return 0;
  int n_slots = (int)((duration + candidate_grid(candidate->profile) - 1) /
                      candidate_grid(candidate->profile));
  int collected = n_slots == candidate->nSlots && n_tile_slots <= candidate->nTiles &&
      minipool_collect_nvmt(enabled, candidate->fp, candidate->profile,
                            duration, n_slots, tile_slots, n_tile_slots,
                            data, size, cancelar);
  free(tile_slots);
  if (!collected) return 0;
  if (!*data || *size < 20 || read32(*data + 12) != (uint32_t)candidate->nSlots ||
      read32(*data + 16) > (uint32_t)candidate->nTiles) {
    free(*data); *data = NULL; *size = 0; return 0;
  }
  return 1;
}

static int minipool_correction(const char *work_key, const char *fp,
                               const char *kind, int64_t b_ms) {
  char base[256], pin[64], token[96], url[512], body[160];
  RedeResposta r = {0};
  if (!hex_string(work_key, 64) || !hex_string(fp, 64) || !minipool_pareado()) return 0;
  pthread_mutex_lock(&mp_mu);
  snprintf(base, sizeof base, "%s", mp_url);
  snprintf(pin, sizeof pin, "%s", mp_pin);
  snprintf(token, sizeof token, "%s", mp_token);
  pthread_mutex_unlock(&mp_mu);
  if (!pin[0] || !token[0]) { wipe(token, sizeof token); return 0; }
  snprintf(url, sizeof url, "%s/v1/pool/candidates/%s/%s", base, fp, kind);
  if (!strcmp(kind, "offset"))
    snprintf(body, sizeof body, "{\"work_key\":\"%s\",\"b_ms\":%lld}", work_key, (long long)b_ms);
  else snprintf(body, sizeof body, "{\"work_key\":\"%s\",\"days\":30}", work_key);
  int status = request("PUT", url, pin, token, "application/json", body, strlen(body), 4096, 2500, &r);
  if (status < 200 || status >= 300) mp_log_failure("candidate correction", status, &r);
  rede_resposta_limpar(&r); wipe(token, sizeof token);
  if (!status) return -1;
  return status >= 200 && status < 300 ? 1 : 0;
}

int minipool_candidato_offset(const char *work_key, const char *fp, int64_t b_ms) {
  if (!hex_string(work_key, 64) || !hex_string(fp, 64) ||
      b_ms < -86400000LL || b_ms > 86400000LL) return 0;
  return minipool_correction(work_key, fp, "offset", b_ms);
}

int minipool_candidato_rejeitar(const char *work_key, const char *fp) {
  return minipool_correction(work_key, fp, "reject", 0);
}

int minipool_buscar(int enabled, const MiniMeta *meta, unsigned char **data, size_t *size,
                    char profile[16], volatile int *cancelar) {
  static const struct { const char *name; int grid; } profiles[] = { {"nvmt-v1-g5", 5000}, {"nvmt-v1-g10", 10000} };
  char episode_key[65] = ""; int found = 0;
  if (data) *data = NULL; if (size) *size = 0; if (profile) profile[0] = 0;
  if (!enabled || !meta || !data || !size || !profile || !hex_string(meta->fp, 64)) return 0;
  int have_episode_key = minipool_work_key(meta, episode_key);
  if (!minipool_endpoint_configurado() || !minipool_pareado()) {
    mp_log_rejected("exact lookup", "endpoint_or_pairing_not_configured"); return 0;
  }
  for (size_t i = 0; i < sizeof profiles / sizeof *profiles; i++) {
    if (cancelar && *cancelar) break;
    int *tile_slots = NULL, n_tile_slots = 0;
    long duration = 0;
    if (minipool_manifest_get(enabled, meta->fp, profiles[i].name, meta->durMs,
                              have_episode_key ? episode_key : NULL,
                              &tile_slots, &n_tile_slots, &duration)) {
      int n_slots = (int)((duration + profiles[i].grid - 1) / profiles[i].grid);
      found = minipool_collect_nvmt(enabled, meta->fp, profiles[i].name,
                                    duration, n_slots, tile_slots, n_tile_slots,
                                    data, size, cancelar);
      free(tile_slots);
      if (found) snprintf(profile, 16, "%s", profiles[i].name);
    }
    if (found) break;
  }
  wipe(episode_key, sizeof episode_key); return found;
}

int minipool_enviar(int enabled, const MiniMeta *meta, const char *filename,
                    uint64_t sizeBytes, const unsigned char *data, size_t size) {
  char base[256], pin[64], token[96], profile[24], request_url[2048] = "";
  RedeResposta r = {0}; int status, grid, retried_contribution = 0, staging_unavailable = 0;
  int queued = 0;
  if (!enabled || !meta || !hex_string(meta->fp, 64) || !data || size > MP_MAX_PACKAGE || !minipool_pareado()) return 0;
  if (size < 20) return 0;
  grid = (int)read32(data + 8); if (grid != 5000 && grid != 10000) return 0;
  snprintf(profile, sizeof profile, "nvmt-v1-g%d", grid / 1000);
  if (!nvmt_ok(data, size, meta->durMs, grid)) return 0;
  pthread_mutex_lock(&mp_mu); snprintf(base, sizeof base, "%s", mp_url); snprintf(pin, sizeof pin, "%s", mp_pin); snprintf(token, sizeof token, "%s", mp_token); pthread_mutex_unlock(&mp_mu);
  if (!token[0]) { wipe(token, sizeof token); return 0; }
  mp_contribution_result(0, NULL);
  int staged = upload_package_staged(base, meta, profile, meta->durMs, filename, sizeBytes,
                                     data, size, pin, token, &r, &staging_unavailable);
  if (staged < 0) {
    rede_resposta_limpar(&r); wipe(token, sizeof token); return 0;
  }
  if (staged == 1) status = 201;
  else if (staged == 2) { status = 202; queued = 1; }
  else if (staging_unavailable) {
    rede_resposta_limpar(&r);
    status = upload_package_direct(base, meta, profile, meta->durMs, filename, sizeBytes,
                                   data, size, pin, token, &r, request_url);
  } else status = r.status;
  if (status == 409 && response_error(&r, "media_metadata_conflict")) {
    char storedText[32] = "", *end = NULL;
    if (response_header(r.cabecalhos, "X-OpenSeek-Duration-Ms", storedText, sizeof storedText)) {
      long storedDuration = strtol(storedText, &end, 10);
      // Retrying only works when the existing duration describes the same NVMT
      // slot count and every frame timestamp still fits that duration.
      if (end && *end == 0 && storedDuration > 0 && storedDuration != meta->durMs &&
          nvmt_ok(data, size, storedDuration, grid)) {
        mp_log_note("upload retrying after media duration conflict");
        rede_resposta_limpar(&r);
        status = upload_package_direct(base, meta, profile, storedDuration, filename, sizeBytes,
                                       data, size, pin, token, &r, request_url);
      }
    }
  }
  if (status == 403 && response_error(&r, "read_only_channel"))
    status = retry_read_only_upload(request_url, data, size, &r, &retried_contribution);
  int denied = status == 403 && response_error(&r, "not_contribute");
  if (status != 200 && status != 201 && status != 202)
    mp_log_failure(retried_contribution ? "contribution retry" : "package upload", status, &r);
  if (denied && !retried_contribution) mp_contribution_result(1, NULL);
  if (status == 200 || status == 201) mp_contribution_result(0, NULL);
  rede_resposta_limpar(&r); wipe(token, sizeof token);
  if (status == 202 && queued) return 2;
  return status == 200 || status == 201 ? 1 : 0;
}

#endif
