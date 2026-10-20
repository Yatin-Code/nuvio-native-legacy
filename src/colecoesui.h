// Colecoes (P7.1): a lista de GERENCIAMENTO das categorias salvas
// (salvosorg.h) — "Sem categoria" primeiro e depois uma fileira por categoria,
// com a contagem de titulos de cada uma. Aberta pela 4a aba da Biblioteca
// ("Colecoes…") e roteada como TELA_COLECOES, sem sobrecarregar TELA_BIBLIOTECA.
//
// P7.1 entrega SO a lista: OK numa fileira nao abre nada (o FolderDetail e
// P7.2). O contrato existe APENAS NA FORMA: P7.2 ainda tem de acrescentar um
// indice de catalogo por fileira (ColLinha), armar `pediu` de verdade e so
// entao colecoes_pediu_abrir devolve 1 com esse indice. O roteador do app.c
// (o ramo de abrirPorIndice em TELA_COLECOES) ja esta pronto e NAO muda.
#ifndef NV_COLECOESUI_H
#define NV_COLECOESUI_H
#include <SDL2/SDL.h>

void colecoes_iniciar(void);
void colecoes_evento(const SDL_Event *e);
void colecoes_atualizar(float dt, Uint32 agora);
void colecoes_desenhar(Uint32 agora);
int  colecoes_quer_sair(void);   // 1 quando o Back deve fechar a tela (limpa ao ler)
void colecoes_encerrar(void);

// OK numa fileira: consome o pedido e devolve 1, escrevendo em
// *indiceCatalogo o indice NO CATALOGO. Forma so: em P7.1 `pediu` nunca arma e
// devolve 0; P7.2 preenche `pediu` com o indice de catalogo por fileira.
int  colecoes_pediu_abrir(int *indiceCatalogo);

// Ponteiro (#99): foca a fileira i (PonteiroFn). colecoes_foco para os testes.
void colecoes_ponteiro(int i, int b);
int  colecoes_foco(void);

#endif
