// Shared OpenSeek cache for the TV-generated NVMT thumbnail packages.
#ifndef NV_MINIPOOL_H
#define NV_MINIPOOL_H

#include "minidrive.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
  MPOOL_PARADO = 0,
  MPOOL_VINCULANDO,
  MPOOL_LIGADO,
  MPOOL_DESVINCULANDO,
  MPOOL_ERRO
} MpoolEstado;

typedef struct {
  char fp[65];
  char profile[16];
  long durationMs;
  int nSlots;
  int nTiles;
  int score;
  char relation[32];
  int64_t a_num, a_den, b_ms;
} MpoolCandidate;

typedef enum {
  MPOOL_SAMPLE_NOT_ATTEMPTED = 0,
  MPOOL_SAMPLE_HTTP_ERROR,
  MPOOL_SAMPLE_INVALID_RESPONSE,
  MPOOL_SAMPLE_CHECKSUM_MISMATCH,
  MPOOL_SAMPLE_OK
} MpoolSampleStatus;

typedef struct {
  MpoolSampleStatus status;
  int httpStatus;
  int requestedSlots;
  int returnedTiles;
  size_t responseBytes;
} MpoolSampleInfo;

void minipool_carregar(void);
const char *minipool_servidor(void);
int minipool_pin_configurado(void);
int minipool_servidor_definir(const char *url);
int minipool_pin_definir(const char *pin);
int minipool_pareado(void);
MpoolEstado minipool_estado(void);
const char *minipool_estado_texto(void);
const char *minipool_erro(void);
int minipool_contribuicao_recusada(void);
// Returns the validated HTTPS contribution link after a read-only denial, or NULL.
const char *minipool_contribuicao_url(void);
void minipool_vincular(const char *codigo_convite);
void minipool_desvincular(void);

// Blocking; call only from the thumbnail worker. A miss/outage returns 0 and
// local generation continues. A hit transfers a malloc'd package to `*data`.
int minipool_buscar(int enabled, const MiniMeta *meta, unsigned char **data,
                    size_t *size, char profile[16], volatile int *cancelar);
// Cross-encode episode fallback. The key is a stable SHA-256 of IMDb episode
// identity; no title, source URL, or raw catalog ID is sent to the pool.
int minipool_work_key(const MiniMeta *meta, char key[65]);
int minipool_candidates(int enabled, const MiniMeta *meta, const char *filename,
                        uint64_t sizeBytes, MpoolCandidate out[8], int *count,
                        volatile int *cancelar);
// Apply a candidate's source-to-target linear timeline mapping. Returns 0 for
// an invalid denominator, duration, output pointer, or arithmetic overflow.
int minipool_map_pts(int64_t pts, int64_t a_num, int64_t a_den, int64_t b_ms,
                     int64_t dur_ms, long *mapped_ms);
int minipool_buscar_slots(int enabled, const MpoolCandidate *candidate,
                          const int *slots, int nSlots,
                          unsigned char **data, size_t *size, volatile int *cancelar,
                          MpoolSampleInfo *info);
int minipool_buscar_candidato(int enabled, const MpoolCandidate *candidate,
                              unsigned char **data, size_t *size, volatile int *cancelar);
// Authenticated, per-device corrections. Return 1 on a 2xx response, -1 when
// the network request has no HTTP response, and 0 when unavailable or rejected.
int minipool_candidato_offset(const char *work_key, const char *fp, int64_t b_ms);
int minipool_candidato_rejeitar(const char *work_key, const char *fp);
// Blocking; uploads one NVMT package only when enabled and paired.
int minipool_enviar(int enabled, const MiniMeta *meta, const char *filename,
                    uint64_t sizeBytes, const unsigned char *data, size_t size);

#endif
