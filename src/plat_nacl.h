// plat_nacl.h — camada de plataforma do alvo Samsung NaCl (Tizen 4/5, .nexe
// dentro de um .wgt). So existe quando NV_NACL esta definido; nos outros alvos
// (webOS/SDL nativo, TPK com .so, emscripten/WASM) este cabecalho e um no-op
// para nao poluir a compilacao.
//
// DESENHO (mesmo hibrido do alvo emscripten, ver rede.c e tools/tizen-shell.html):
//   - A pagina (index.html) e dona da REDE (XHR) e do VIDEO (AVPlay). O modulo
//     .nexe desenha a interface em GLES2 sobre um <embed> transparente.
//   - O modulo e a pagina conversam por postMessage (PPB_Messaging). O modulo
//     manda pedidos; a pagina responde de forma assincrona.
//   - Como PPB nao permite chamada JS sincrona, rede.c chama nvnacl_http() de um
//     FIO DE TRABALHO: a funcao marechaliza o PostMessage para o fio principal
//     (core->CallOnMainThread) e BLOQUEIA o fio chamador num condvar ate a
//     pagina devolver a resposta. Mesmo contrato de rede_baixar ("BLOQUEIA").
#ifndef PLAT_NACL_H
#define PLAT_NACL_H
#ifdef NV_NACL

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Rede (ponte XHR na pagina) -------------------------------------------
// Faz uma requisicao HTTP(S) pela pagina e BLOQUEIA o fio chamador ate a
// resposta chegar. Devolve um buffer de malloc() com o corpo (com um NUL extra
// no fim) e escreve tamanho/status. Devolve NULL se o pedido nem saiu.
// `cabs` = uma string "Nome: valor" por linha, separadas por '\n' (ou NULL).
// `url_final`/`etag`, quando nao-NULL, recebem o endereco final apos
// redirecionamentos e o cabecalho ETag da resposta.
char *nvnacl_http(const char *metodo, const char *url, const char *cabs,
                  const char *corpo, int corpo_tam, int *tam, int *status,
                  char *url_final, int url_final_tam,
                  char *etag, int etag_tam);

// --- Ciclo de vida / desenho ----------------------------------------------
// Verdadeiro depois que a pagina entregou o contexto GLES2 e as dimensoes.
int  nvnacl_pronto(void);
void nvnacl_tamanho(int *w, int *h);   // dimensoes atuais do drawable
void nvnacl_swap(void);                // troca os buffers (SwapBuffers)

// Ponto de entrada logico do app (o main() real do Nuvio). O modulo o chama
// num fio proprio depois que o contexto GL esta pronto, para nao travar o fio
// PPAPI. Definido em main.c sob NV_NACL.
int  nvnacl_app_main(void);

// Empurra uma mensagem de texto para a pagina (diagnostico, controle de video).
void nvnacl_post(const char *s);

#ifdef __cplusplus
}
#endif

#endif  /* NV_NACL */
#endif  /* PLAT_NACL_H */
