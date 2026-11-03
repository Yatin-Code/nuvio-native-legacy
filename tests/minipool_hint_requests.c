#define NV_ANDROID 1
#include "../src/minipool.c"

static int status[4], calls;
static char urls[4][2048], methods[4][8];
static size_t request_body_bytes[4];
static const char *bodies[4];
static const char *response_headers[4];
static unsigned char response_binary[512];
static size_t response_binary_size;

int rede_pedir(const RedePedido *p, RedeResposta *r) {
  int i = calls++;
  if (!p || !r || i >= 4) return 0;
  snprintf(methods[i], sizeof methods[i], "%s", p->metodo ? p->metodo : "GET");
  snprintf(urls[i], sizeof urls[i], "%s", p->url ? p->url : "");
  request_body_bytes[i] = p->n_corpo;
  r->status = status[i];
  if (response_binary_size) {
    r->corpo = malloc(response_binary_size);
    if (r->corpo) memcpy(r->corpo, response_binary, response_binary_size);
    r->n_corpo = response_binary_size;
  } else {
    r->corpo = strdup(bodies[i] ? bodies[i] : "");
    r->n_corpo = strlen(r->corpo);
  }
  r->cabecalhos = strdup(response_headers[i] ? response_headers[i] : "");
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
  memset(response_headers, 0, sizeof response_headers);
  memset(response_binary, 0, sizeof response_binary);
  response_binary_size = 0;
  strcpy(mp_url, "https://pool.example");
  strcpy(mp_pin, "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
  strcpy(mp_token, "paired-device-token");
}

static void put32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static int test_linear_mapping(void) {
  static const struct {
    int64_t pts, a_num, a_den, b_ms, duration, expected;
  } cases[] = {
    { 25000, 24, 25, 0, 60000, 24000 },
    { 25000, 25, 24, 0, 60000, 26041 },
    { 12345, 1, 1, 0, 60000, 12345 },
    { 10000, 1, 1, 1500, 60000, 11500 },
    { -100, 1, 1, 0, 60000, 0 },
    { 65000, 1, 1, 0, 60000, 60000 },
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    long mapped = -1;
    if (!minipool_map_pts(cases[i].pts, cases[i].a_num, cases[i].a_den,
                          cases[i].b_ms, cases[i].duration, &mapped) ||
        mapped != cases[i].expected) return 0;
  }
  long mapped = -1;
  return !minipool_map_pts(1000, 1, 0, 0, 60000, &mapped) && mapped == -1;
}

int main(void) {
  if (!test_linear_mapping()) return 15;
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
  bodies[0] = "{\"candidates\":[{\"fp\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"profile\":\"nvmt-v1-g5\",\"duration_ms\":7200000,\"n_slots\":1440,\"n_tiles\":100,\"score\":88,\"relation\":\"same_length\",\"a_num\":24,\"a_den\":25,\"b_ms\":1200}]}";
  if (!minipool_candidates(1, &meta, "Caf\xc3\xa9 & Friends #1.mkv", 1500000000,
                           candidates, &count, NULL) || count != 1 ||
      candidates[0].score != 88 || strcmp(candidates[0].relation, "same_length") ||
      candidates[0].a_num != 24 || candidates[0].a_den != 25 || candidates[0].b_ms != 1200) return 1;
  if (strcmp(methods[0], "GET") || strncmp(urls[0], "https://pool.example/v1/pool/candidates?", 40) ||
      !strstr(urls[0], "imdb=tt1234567") || !strstr(urls[0], "season=0") ||
      !strstr(urls[0], "episode=0") || !strstr(urls[0], "duration_ms=7200000") ||
      !strstr(urls[0], "size_bytes=1500000000") ||
      !strstr(urls[0], "filename=Caf%C3%A9%20%26%20Friends%20%231.mkv")) return 2;

  reset(); status[0] = 200;
  bodies[0] = "{\"candidates\":[{\"fp\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"profile\":\"nvmt-v1-g5\",\"duration_ms\":7200000,\"n_slots\":1440,\"n_tiles\":100,\"score\":59,\"relation\":\"same_length\"},{\"fp\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\",\"profile\":\"nvmt-v1-g5\",\"duration_ms\":7200000,\"n_slots\":1440,\"n_tiles\":100,\"score\":100,\"relation\":\"same_length\"}]}";
  if (minipool_candidates(1, &meta, "Film.mkv", 1000, candidates, &count, NULL) ||
      count || calls != 1) return 13;
  reset(); status[0] = 200;
  bodies[0] = "{\"candidates\":[{\"fp\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"profile\":\"nvmt-v1-g5\",\"duration_ms\":7200000,\"n_slots\":1440,\"n_tiles\":100,\"score\":90,\"relation\":\"length_differs\"}]}";
  if (minipool_candidates(1, &meta, "Film.mkv", 1000, candidates, &count, NULL) ||
      count || calls != 1) return 14;
  reset(); status[0] = 200;
  bodies[0] = "{\"candidates\":[{\"fp\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"profile\":\"nvmt-v1-g5\",\"duration_ms\":7200000,\"n_slots\":1440,\"n_tiles\":100,\"score\":90,\"relation\":\"same_length\",\"a_num\":24,\"a_den\":0,\"b_ms\":0}]}";
  if (minipool_candidates(1, &meta, "Film.mkv", 1000, candidates, &count, NULL) ||
      count || calls != 1) return 16;

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

  // A missing candidate route never falls back to the legacy episode endpoint.
  strcpy(meta.imdb, "tt4158110"); meta.temporada = 3; meta.episodio = 1;
  reset(); status[0] = 404; bodies[0] = "{\"error\":\"not_found\"}";
  minipool_candidates(1, &meta, "show.mkv", 1000, candidates, &count, NULL);
  char key[65] = "";
  if (calls != 1 || count || !strcmp(methods[0], "") || strstr(urls[0], "/v1/pool/episodes/") ||
      !minipool_work_key(&meta, key)) return 6;

  // A legacy server rejecting unknown query fields still gets the D2 fallback.
  reset(); status[0] = 400; bodies[0] = "{\"error\":\"bad_request\"}";
  status[1] = 200;
  bodies[1] = "{\"candidates\":[{\"fp\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\",\"profile\":\"nvmt-v1-g5\",\"duration_ms\":7200000,\"n_slots\":1440,\"n_tiles\":100,\"score\":75,\"relation\":\"same_length\"}]}";
  if (!minipool_candidates(1, &meta, "show.mkv", 1000, candidates, &count, NULL) ||
      calls != 2 || count != 1 || !strstr(urls[1], "/v1/pool/episodes/")) return 9;

  // Candidate fetch rejects corrupt structure, then accepts a structurally
  // valid package. Each failure leaves the caller free to try the next rank.
  MpoolCandidate candidate = {0}; unsigned char *download = NULL; size_t download_size = 0;
  strcpy(candidate.fp, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  strcpy(candidate.profile, "nvmt-v1-g5"); candidate.durationMs = 5000;
  candidate.nSlots = candidate.nTiles = 1;
  unsigned char package[157] = {0}; char digest[65], headers[160];
  memcpy(package, "NVMT", 4); put32(package + 4, 1); put32(package + 8, 5000);
  put32(package + 12, 1); put32(package + 16, 1);
  put32(package + 20, 1); put32(package + 24, 5000); put32(package + 28, 125);
  package[32] = 0xff; package[33] = 0xd8; package[155] = 0xff; package[156] = 0xd9;
  reset(); status[0] = 200; response_binary_size = sizeof package;
  memcpy(response_binary, package, sizeof package); // slot 1 is outside the one-slot package.
  admin_sha256_hex(response_binary, (unsigned long)response_binary_size, digest);
  snprintf(headers, sizeof headers, "X-OpenSeek-Duration-Ms: 5000\nX-OpenSeek-Sha256: %s\n", digest);
  response_headers[0] = headers;
  if (minipool_buscar_candidato(1, &candidate, &download, &download_size, NULL) ||
      download || calls != 1) return 10;
  candidate.nTiles = 2;
  reset(); status[0] = 200; response_binary_size = sizeof package;
  put32(package + 20, 0); memcpy(response_binary, package, sizeof package);
  admin_sha256_hex(response_binary, (unsigned long)response_binary_size, digest);
  snprintf(headers, sizeof headers, "X-OpenSeek-Duration-Ms: 5000\nX-OpenSeek-Sha256: %s\n", digest);
  response_headers[0] = headers;
  if (minipool_buscar_candidato(1, &candidate, &download, &download_size, NULL) ||
      download || calls != 1) return 11;
  candidate.nTiles = 1;
  reset(); status[0] = 200; response_binary_size = sizeof package;
  memcpy(response_binary, package, sizeof package);
  admin_sha256_hex(response_binary, (unsigned long)response_binary_size, digest);
  snprintf(headers, sizeof headers, "X-OpenSeek-Duration-Ms: 5000\nX-OpenSeek-Sha256: %s\n", digest);
  response_headers[0] = headers;
  if (!minipool_buscar_candidato(1, &candidate, &download, &download_size, NULL) ||
      !download || download_size != sizeof package || calls != 1) return 12;
  free(download);

  // Local/VPS upload sends the same hints with the NVMT body in a direct PUT.
  unsigned char upload_package[20 + 12 + 125] = {0};
  strcpy(meta.fp, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  meta.durMs = 21000; meta.temporada = 0; meta.episodio = 0;
  strcpy(meta.imdb, "tt1234567");
  memcpy(upload_package, "NVMT", 4); put32(upload_package + 4, 1); put32(upload_package + 8, 5000);
  put32(upload_package + 12, 5); put32(upload_package + 16, 1);
  put32(upload_package + 20, 0); put32(upload_package + 24, 0); put32(upload_package + 28, 125);
  upload_package[32] = 0xff; upload_package[33] = 0xd8; upload_package[sizeof upload_package - 2] = 0xff; upload_package[sizeof upload_package - 1] = 0xd9;
  reset(); status[0] = 201; bodies[0] = "{}";
  if (!minipool_enviar(1, &meta, "Caf\xc3\xa9 & Friends #1.mkv", 1500000000, upload_package, sizeof upload_package) ||
      calls != 1 || strcmp(methods[0], "PUT") || request_body_bytes[0] != sizeof upload_package ||
      !strstr(urls[0], "imdb=tt1234567") || !strstr(urls[0], "season=0") ||
      !strstr(urls[0], "episode=0") || !strstr(urls[0], "duration_ms=21000") ||
      !strstr(urls[0], "size_bytes=1500000000") ||
      !strstr(urls[0], "filename=Caf%C3%A9%20%26%20Friends%20%231.mkv")) return 7;

  // A server rejecting unknown query params receives one legacy direct PUT.
  reset(); status[0] = 400; bodies[0] = "{\"error\":\"bad_request\"}";
  status[1] = 201; bodies[1] = "{}";
  if (!minipool_enviar(1, &meta, "film.mkv", 1500000000, upload_package, sizeof upload_package) ||
      calls != 2 || strcmp(methods[1], "PUT") || strstr(urls[1], "imdb=") ||
      strstr(urls[1], "filename=") || !strstr(urls[1], "duration_ms=21000")) return 8;

  return 0;
}
