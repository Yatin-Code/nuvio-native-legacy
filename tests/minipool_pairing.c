#define NV_ANDROID 1
#include "../src/minipool.c"
#include <unistd.h>

static int flow, calls;
static char saved_url[256], saved_pin[64], saved_token[96];
static const char *start_body =
  "{\"pair_id\":\"0123456789abcdef0123456789abcdef\","
  "\"salt\":\"fedcba9876543210fedcba9876543210\","
  "\"spki_pin\":\"sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\","
  "\"iterations\":200000}";
static const char *claim_invite =
  "{\"device_id\":\"0123456789abcdef\","
  "\"token\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
  "\"server_proof\":\"3dd5a078347a1441599d8f367a22602088dc3af340a3cfe330a3a7efa2f2ef50\"}";
static const char *claim_window =
  "{\"device_id\":\"0123456789abcdef\","
  "\"token\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
  "\"server_proof\":\"54ccc7c07cd11a6e90f793034e2cff5dc3e484016baaa639715c1d89290291a8\"}";
static const char *start_vercel =
  "{\"pair_id\":\"0123456789abcdef0123456789abcdef\","
  "\"salt\":\"fedcba9876543210fedcba9876543210\","
  "\"spki_pin\":\"\",\"iterations\":200000}";
static const char *claim_vercel =
  "{\"device_id\":\"0123456789abcdef\","
  "\"token\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
  "\"server_proof\":\"7303b5d361a5265bed9ef0256d71e6086bc0c716a9e381ba03c945a6953f56b7\"}";

int dados_gravar(const char *name, const char *contents) {
  char *dst = NULL; size_t cap = 0;
  if (!name || !contents) return 0;
  if (!strcmp(name, "openseek-server.txt")) { dst = saved_url; cap = sizeof saved_url; }
  else if (!strcmp(name, "openseek-pin.txt")) { dst = saved_pin; cap = sizeof saved_pin; }
  else if (!strcmp(name, "openseek-device-token.txt")) { dst = saved_token; cap = sizeof saved_token; }
  if (!dst || strlen(contents) >= cap) return 0;
  snprintf(dst, cap, "%s", contents); return 1;
}

char *dados_ler(const char *name) { (void)name; return NULL; }

int dados_apagar(const char *name) {
  char *dst = NULL; size_t cap = 0;
  if (!name) return 0;
  if (!strcmp(name, "openseek-server.txt")) { dst = saved_url; cap = sizeof saved_url; }
  else if (!strcmp(name, "openseek-pin.txt")) { dst = saved_pin; cap = sizeof saved_pin; }
  else if (!strcmp(name, "openseek-device-token.txt")) { dst = saved_token; cap = sizeof saved_token; }
  if (!dst) return 0;
  memset(dst, 0, cap); return 1;
}

int rede_pedir(const RedePedido *p, RedeResposta *r) {
  const char *response = NULL;
  if (!p || !r || !p->metodo || !p->url) return 0;
  if (!strcmp(p->metodo, "DELETE")) {
    if (flow != 3 || !strstr(p->url, "https://my-openseek-node.vercel.app/v1/pair") ||
        !p->cabecalhos || !strstr(p->cabecalhos[0], "Authorization: Bearer ")) return 0;
    r->cabecalhos = strdup(""); r->status = 204;
    return r->cabecalhos != NULL;
  }
  if (strcmp(p->metodo, "POST") || !p->corpo) return 0;
  calls++;
  if (strstr(p->url, "/v1/pair/start")) {
    if (calls != 1) return 0;
    if (flow == 1 && !strstr(p->corpo, "\"invite\":\"ABCD\"")) return 0;
    if (flow == 2 && !strstr(p->corpo, "\"invite\":null")) return 0;
    if (flow == 3 || flow == 4) {
      if (strcmp(p->url, "https://my-openseek-node.vercel.app/v1/pair/start") || p->tls_pin ||
          !strstr(p->corpo, "\"invite\":\"ABCD\"")) return 0;
      response = start_vercel;
    } else response = start_body;
  } else if (strstr(p->url, "/v1/pair/claim")) {
    if (calls != 2 || !strstr(p->corpo, "\"device_name\":\"Nuvio TV\"")) return 0;
    if (flow == 1 && !strstr(p->corpo, "1165d71d70a7745c89e2d52541039ddc6dcf4e3eda9dff009207ecde09e6415c")) return 0;
    if (flow == 2 && !strstr(p->corpo, "4292c60afe0a76e011f973617f5fdb8c78205e6a070b058db921253c0d1721a3")) return 0;
    if (flow == 3 || flow == 4) {
      if (strcmp(p->url, "https://my-openseek-node.vercel.app/v1/pair/claim") || p->tls_pin) return 0;
      response = flow == 3 ? claim_vercel : "{\"error\":\"pairing_failed\"}";
    } else response = flow == 1 ? claim_invite : claim_window;
  } else return 0;
  r->corpo = strdup(response);
  if (!r->corpo) return 0;
  r->n_corpo = strlen(response);
  r->cabecalhos = strdup("");
  r->status = flow == 4 && strstr(p->url, "/v1/pair/claim") ? 403 : 200;
  return 1;
}

void rede_resposta_limpar(RedeResposta *r) {
  if (!r) return;
  free(r->corpo);
  free(r->cabecalhos);
  memset(r, 0, sizeof *r);
}

static int check_flow(int kind, const char *code) {
  static const char pin[] = "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  char token[96] = "";
  flow = kind; calls = 0;
  return start_pair("https://pool.example", pin, code, token) && calls == 2 &&
         !strcmp(token, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
}

static int wait_until_idle(MpoolEstado expected) {
  for (int i = 0; i < 500; i++) {
    pthread_mutex_lock(&mp_mu);
    int busy = mp_busy, state = mp_estado;
    pthread_mutex_unlock(&mp_mu);
    if (!busy) return state == expected;
    usleep(10000);
  }
  return 0;
}

static int check_participant_bundle(void) {
  const char *bundle = "https://my-openseek-node.vercel.app|ABCD-EFGH-JKMN-PQRS";
  char clean[256];
  memset(mp_url, 0, sizeof mp_url); memset(mp_token, 0, sizeof mp_token);
  memset(saved_url, 0, sizeof saved_url); memset(saved_token, 0, sizeof saved_token);

  if (url_valida("http://my-openseek-node.vercel.app", clean) ||
      url_valida("https://my-openseek-node.vercel.app/path", clean)) return 1;

  flow = 4; calls = 0;
  minipool_vincular(bundle);
  if (!wait_until_idle(MPOOL_ERRO) || minipool_pareado() || !minipool_erro() ||
      strcmp(mp_url, "https://my-openseek-node.vercel.app")) return 2;

  flow = 3; calls = 0;
  minipool_vincular(bundle);
  if (!wait_until_idle(MPOOL_LIGADO) || !minipool_endpoint_configurado() || !minipool_pareado() ||
      strcmp(mp_url, "https://my-openseek-node.vercel.app") ||
      strcmp(saved_url, "https://my-openseek-node.vercel.app\n") ||
      strcmp(saved_token, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n")) return 3;

  if (minipool_servidor_definir("https://replacement.vercel.app") ||
      strcmp(mp_url, "https://my-openseek-node.vercel.app")) return 4;
  minipool_desvincular();
  if (!wait_until_idle(MPOOL_PARADO) || minipool_pareado() || saved_token[0]) return 5;
  if (!minipool_servidor_definir("https://replacement.vercel.app") ||
      strcmp(mp_url, "https://replacement.vercel.app")) return 6;
  return 0;
}

int main(void) {
  if (!check_flow(1, "ABCD-EFGH-JKMN-PQRS")) return 1;
  if (!check_flow(2, "ABCD-EFGH")) return 2;
  int bundle = check_participant_bundle();
  if (bundle) return 10 + bundle;
  return 0;
}
