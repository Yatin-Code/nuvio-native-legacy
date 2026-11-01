#define NV_ANDROID 1
#include "../src/minipool.c"

static int flow, calls;
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

int rede_pedir(const RedePedido *p, RedeResposta *r) {
  const char *response = NULL;
  if (!p || !r || strcmp(p->metodo, "POST") || !p->url || !p->corpo) return 0;
  calls++;
  if (strstr(p->url, "/v1/pair/start")) {
    if (calls != 1) return 0;
    if (flow == 1 && !strstr(p->corpo, "\"invite\":\"ABCD\"")) return 0;
    if (flow == 2 && !strstr(p->corpo, "\"invite\":null")) return 0;
    response = start_body;
  } else if (strstr(p->url, "/v1/pair/claim")) {
    if (calls != 2 || !strstr(p->corpo, "\"device_name\":\"Nuvio TV\"")) return 0;
    if (flow == 1 && !strstr(p->corpo, "1165d71d70a7745c89e2d52541039ddc6dcf4e3eda9dff009207ecde09e6415c")) return 0;
    if (flow == 2 && !strstr(p->corpo, "4292c60afe0a76e011f973617f5fdb8c78205e6a070b058db921253c0d1721a3")) return 0;
    response = flow == 1 ? claim_invite : claim_window;
  } else return 0;
  r->corpo = strdup(response);
  if (!r->corpo) return 0;
  r->n_corpo = strlen(response);
  r->cabecalhos = strdup("");
  r->status = 200;
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

int main(void) {
  if (!check_flow(1, "ABCD-EFGH-JKMN-PQRS")) return 1;
  if (!check_flow(2, "ABCD-EFGH")) return 2;
  return 0;
}
