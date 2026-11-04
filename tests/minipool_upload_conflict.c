#define NV_ANDROID 1
#include "../src/minipool.c"

#include <assert.h>
#include <stdio.h>

static struct {
  int status;
  const char *body;
  const char *headers;
} replies[8];
static int calls;
static char urls[8][512];

int rede_pedir(const RedePedido *p, RedeResposta *r) {
  int i = calls++;
  if (!p || !r || i >= 8 || strcmp(p->metodo, "PUT") || !p->url || !p->corpo) return 0;
  snprintf(urls[i], sizeof urls[i], "%s", p->url);
  r->status = replies[i].status;
  r->corpo = strdup(replies[i].body ? replies[i].body : "");
  r->n_corpo = strlen(r->corpo);
  r->cabecalhos = strdup(replies[i].headers ? replies[i].headers : "");
  return r->corpo && r->cabecalhos;
}

void rede_resposta_limpar(RedeResposta *r) {
  if (!r) return;
  free(r->corpo);
  free(r->cabecalhos);
  memset(r, 0, sizeof *r);
}

static void put32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void reset(int firstStatus, const char *firstBody, const char *firstHeaders,
                  int secondStatus, const char *secondBody) {
  memset(replies, 0, sizeof replies);
  calls = 0;
  memset(urls, 0, sizeof urls);
  replies[0].status = firstStatus; replies[0].body = firstBody; replies[0].headers = firstHeaders;
  replies[1].status = secondStatus; replies[1].body = secondBody;
}

static int send(MiniMeta *meta, const unsigned char *package, size_t size) {
  return minipool_enviar(1, meta, "film.mkv", 100000, package, size);
}

int main(void) {
  MiniMeta meta = {0};
  unsigned char package[20 + 12 + 125] = {0};
  const char *conflict = "{\"error\":\"media_metadata_conflict\"}";
  strcpy(meta.fp, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  strcpy(meta.imdb, "tt1234567");
  meta.durMs = 21000;
  strcpy(mp_url, "https://pool.example");
  strcpy(mp_pin, "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
  strcpy(mp_token, "paired-device-token");
  memcpy(package, "NVMT", 4); put32(package + 4, 1); put32(package + 8, 5000);
  put32(package + 12, 5); put32(package + 16, 1);
  put32(package + 20, 0); put32(package + 24, 0); put32(package + 28, 125);
  package[32] = 0xff; package[33] = 0xd8; package[20 + 12 + 123] = 0xff; package[20 + 12 + 124] = 0xd9;

  reset(409, conflict, "X-OpenSeek-Duration-Ms: 20500\r\n", 201, "{}");
  if (!send(&meta, package, sizeof package) || calls != 2 ||
      !strstr(urls[0], "duration_ms=21000") || !strstr(urls[1], "duration_ms=20500")) return 1;

  reset(409, conflict, "X-OpenSeek-Duration-Ms: 10000\r\n", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1) return 2;

  reset(409, conflict, "", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1) return 3;

  reset(403, "{\"error\":\"not_contribute\"}", "", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1 || !minipool_contribuicao_recusada()) return 4;

  reset(201, "{}", "", 0, NULL);
  if (!send(&meta, package, sizeof package) || calls != 1 || minipool_contribuicao_recusada()) return 5;

  // D7: a read-only channel returns 403 read_only_channel with an optional
  // contribution origin. The same package is retried exactly once there.
  reset(403, "{\"error\":\"read_only_channel\",\"contribute_url\":\"https://host.example\"}", "", 201, "{}");
  if (!send(&meta, package, sizeof package) || calls != 2 ||
      strncmp(urls[1], "https://host.example/v1/pool/", 29) ||
      !strstr(urls[1], "/nvmt?") || !strstr(urls[1], "imdb=tt1234567") ||
      minipool_contribuicao_recusada() || minipool_contribuicao_url()) return 6;

  // A contribution URL that already has a path keeps it and receives the
  // original query string.
  reset(403, "{\"error\":\"read_only_channel\",\"contribute_url\":\"https://host.example/intake\"}", "", 201, "{}");
  if (!send(&meta, package, sizeof package) || calls != 2 ||
      strncmp(urls[1], "https://host.example/intake?", 28) ||
      !strstr(urls[1], "duration_ms=21000") || minipool_contribuicao_recusada()) return 7;

  // A Telegram link is a manual intake channel, not a PUT target: no retry,
  // but the link must be surfaced in Settings.
  reset(403, "{\"error\":\"read_only_channel\",\"contribute_url\":\"https://t.me/openseek_pool\"}", "", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1 ||
      !minipool_contribuicao_recusada() ||
      !minipool_contribuicao_url() ||
      strcmp(minipool_contribuicao_url(), "https://t.me/openseek_pool")) return 8;

  // No contribute_url: a clear denial, no retry, no stale link.
  reset(403, "{\"error\":\"read_only_channel\"}", "", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1 ||
      !minipool_contribuicao_recusada() || minipool_contribuicao_url()) return 9;

  // Malformed JSON, non-HTTPS, and over-long URLs are refused without a retry.
  reset(403, "{\"error\":\"read_only_channel\"", "", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1 ||
      !minipool_contribuicao_recusada() || minipool_contribuicao_url()) return 10;
  reset(403, "{\"error\":\"read_only_channel\",\"contribute_url\":\"http://host.example\"}", "", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1 ||
      !minipool_contribuicao_recusada() || minipool_contribuicao_url()) return 11;
  {
    char overlong[600];
    snprintf(overlong, sizeof overlong, "https://host.example/%0590d", 0);
    char body[700];
    snprintf(body, sizeof body, "{\"error\":\"read_only_channel\",\"contribute_url\":\"%s\"}", overlong);
    reset(403, body, "", 201, "{}");
    if (send(&meta, package, sizeof package) || calls != 1 ||
        !minipool_contribuicao_recusada() || minipool_contribuicao_url()) return 12;
  }

  // A failed retry keeps the denial and the destination; there is no loop.
  reset(403, "{\"error\":\"read_only_channel\",\"contribute_url\":\"https://host.example\"}", "", 500, "{}");
  if (send(&meta, package, sizeof package) || calls != 2 ||
      !minipool_contribuicao_recusada() ||
      strcmp(minipool_contribuicao_url(), "https://host.example")) return 13;

  // read_only_channel on a non-403 never triggers the contribution retry.
  reset(500, "{\"error\":\"read_only_channel\",\"contribute_url\":\"https://host.example\"}", "", 201, "{}");
  if (send(&meta, package, sizeof package) || calls != 1) return 14;

  return 0;
}
