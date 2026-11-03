#define NV_ANDROID 1
#include "../src/minipool.c"

static int status[4], calls;
static char urls[4][2048], methods[4][8];
static size_t request_body_bytes[4];
static const char *bodies[4];

int rede_pedir(const RedePedido *p, RedeResposta *r) {
  int i = calls++;
  if (!p || !r || i >= 4) return 0;
  snprintf(methods[i], sizeof methods[i], "%s", p->metodo ? p->metodo : "GET");
  snprintf(urls[i], sizeof urls[i], "%s", p->url ? p->url : "");
  request_body_bytes[i] = p->n_corpo;
  r->status = status[i];
  r->corpo = strdup(bodies[i] ? bodies[i] : "");
  r->n_corpo = strlen(r->corpo);
  r->cabecalhos = strdup("");
  return r->corpo && r->cabecalhos;
}

void rede_resposta_limpar(RedeResposta *r) {
  if (!r) return;
  free(r->corpo);
  free(r->cabecalhos);
  memset(r, 0, sizeof *r);
}

static void reset(void) {
  calls = 0;
  memset(status, 0, sizeof status);
  memset(urls, 0, sizeof urls);
  memset(methods, 0, sizeof methods);
  memset(request_body_bytes, 0, sizeof request_body_bytes);
  memset(bodies, 0, sizeof bodies);
  strcpy(mp_url, "https://pool.example");
  strcpy(mp_pin, "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
  strcpy(mp_token, "paired-device-token");
}

static void put32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

int main(void) {
  MiniMeta meta = {0};
  MpoolCandidate candidates[8];
  int count = 0;
  strcpy(meta.fp, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  strcpy(meta.imdb, "tt1234567");
  meta.temporada = 0;
  meta.episodio = 0;
  meta.durMs = 7200000L;

  reset();
  status[0] = 200;
  bodies[0] = "{\"candidates\":[{\"fp\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"profile\":\"nvmt-v1-g5\",\"duration_ms\":7200000,\"n_slots\":1440,\"n_tiles\":100}]}";
  if (!minipool_candidates(1, &meta, "Caf\xc3\xa9 & Friends #1.mkv", 1500000000,
                           candidates, &count, NULL) || count != 1) return 1;
  if (strcmp(methods[0], "GET") || strncmp(urls[0], "https://pool.example/v1/pool/candidates?", 40) ||
      !strstr(urls[0], "imdb=tt1234567") || !strstr(urls[0], "season=0") ||
      !strstr(urls[0], "episode=0") || !strstr(urls[0], "duration_ms=7200000") ||
      !strstr(urls[0], "size_bytes=1500000000") ||
      !strstr(urls[0], "filename=Caf%C3%A9%20%26%20Friends%20%231.mkv")) return 2;

  // Without a catalog ID, filename and duration still reach the ranked lookup.
  meta.imdb[0] = 0;
  reset(); status[0] = 200; bodies[0] = "{\"candidates\":[]}";
  minipool_candidates(1, &meta, "Film.mkv", 0, candidates, &count, NULL);
  if (calls != 1 || !strstr(urls[0], "filename=Film.mkv") || strstr(urls[0], "imdb=")) return 3;

  char long_filename[202]; memset(long_filename, 'x', 201); long_filename[201] = 0;
  reset(); status[0] = 200; bodies[0] = "{\"candidates\":[]}";
  minipool_candidates(1, &meta, long_filename, 0, candidates, &count, NULL);
  char *filename_param = strstr(urls[0], "filename=");
  if (!filename_param || strlen(filename_param + strlen("filename=")) != 200) return 4;

  char utf8_boundary[205]; memset(utf8_boundary, 'x', 198);
  memcpy(utf8_boundary + 198, "\xf0\x9f\x98\x80", 4); utf8_boundary[202] = 'z'; utf8_boundary[203] = 0;
  reset(); status[0] = 200; bodies[0] = "{\"candidates\":[]}";
  minipool_candidates(1, &meta, utf8_boundary, 0, candidates, &count, NULL);
  filename_param = strstr(urls[0], "filename=");
  if (!filename_param || strlen(filename_param + strlen("filename=")) != 198) return 5;

  // A pre-A1 server may reject the new query shape. Series keep the old route.
  strcpy(meta.imdb, "tt4158110"); meta.temporada = 3; meta.episodio = 1;
  reset(); status[0] = 404; bodies[0] = "{\"error\":\"not_found\"}";
  status[1] = 200; bodies[1] = "{\"candidates\":[]}";
  minipool_candidates(1, &meta, "show.mkv", 1000, candidates, &count, NULL);
  char key[65] = "";
  if (calls != 2 || strcmp(methods[1], "GET") ||
      !minipool_work_key(&meta, key) || !strstr(urls[1], "/v1/pool/episodes/") ||
      !strstr(urls[1], key) || !strstr(urls[1], "duration_ms=7200000")) return 6;

  // Local/VPS upload sends the same hints with the NVMT body in a direct PUT.
  unsigned char package[20 + 12 + 125] = {0};
  strcpy(meta.fp, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  meta.durMs = 21000; meta.temporada = 0; meta.episodio = 0;
  strcpy(meta.imdb, "tt1234567");
  memcpy(package, "NVMT", 4); put32(package + 4, 1); put32(package + 8, 5000);
  put32(package + 12, 5); put32(package + 16, 1);
  put32(package + 20, 0); put32(package + 24, 0); put32(package + 28, 125);
  package[32] = 0xff; package[33] = 0xd8; package[sizeof package - 2] = 0xff; package[sizeof package - 1] = 0xd9;
  reset(); status[0] = 201; bodies[0] = "{}";
  if (!minipool_enviar(1, &meta, "Caf\xc3\xa9 & Friends #1.mkv", 1500000000, package, sizeof package) ||
      calls != 1 || strcmp(methods[0], "PUT") || request_body_bytes[0] != sizeof package ||
      !strstr(urls[0], "imdb=tt1234567") || !strstr(urls[0], "season=0") ||
      !strstr(urls[0], "episode=0") || !strstr(urls[0], "duration_ms=21000") ||
      !strstr(urls[0], "size_bytes=1500000000") ||
      !strstr(urls[0], "filename=Caf%C3%A9%20%26%20Friends%20%231.mkv")) return 7;

  // A server rejecting unknown query params receives one legacy direct PUT.
  reset(); status[0] = 400; bodies[0] = "{\"error\":\"bad_request\"}";
  status[1] = 201; bodies[1] = "{}";
  if (!minipool_enviar(1, &meta, "film.mkv", 1500000000, package, sizeof package) ||
      calls != 2 || strcmp(methods[1], "PUT") || strstr(urls[1], "imdb=") ||
      strstr(urls[1], "filename=") || !strstr(urls[1], "duration_ms=21000")) return 8;

  return 0;
}
