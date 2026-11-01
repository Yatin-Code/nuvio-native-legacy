// Shared OpenSeek cache for the TV-generated NVMT thumbnail packages.
#ifndef NV_MINIPOOL_H
#define NV_MINIPOOL_H

#include "minidrive.h"

typedef enum {
  MPOOL_PARADO = 0,
  MPOOL_VINCULANDO,
  MPOOL_LIGADO,
  MPOOL_DESVINCULANDO,
  MPOOL_ERRO
} MpoolEstado;

void minipool_carregar(void);
const char *minipool_servidor(void);
int minipool_pin_configurado(void);
int minipool_servidor_definir(const char *url);
int minipool_pin_definir(const char *pin);
int minipool_pareado(void);
MpoolEstado minipool_estado(void);
const char *minipool_estado_texto(void);
const char *minipool_erro(void);
void minipool_vincular(const char *codigo_convite);
void minipool_desvincular(void);

// Blocking; call only from the thumbnail worker. A miss/outage returns 0 and
// local generation continues. A hit transfers a malloc'd package to `*data`.
int minipool_buscar(int enabled, const MiniMeta *meta, unsigned char **data,
                    size_t *size, char profile[16]);
// Blocking; uploads one NVMT package only when enabled and paired.
int minipool_enviar(int enabled, const MiniMeta *meta,
                    const unsigned char *data, size_t size);

#endif
