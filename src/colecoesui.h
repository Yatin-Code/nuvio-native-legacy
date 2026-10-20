// Colecoes (P7.1): a lista de GERENCIAMENTO das categorias salvas
// (salvosorg.h) — "Sem categoria" primeiro e depois uma fileira por categoria,
// com a contagem de titulos de cada uma. Aberta pela 4a aba da Biblioteca
// ("Colecoes…") e roteada como TELA_COLECOES, sem sobrecarregar TELA_BIBLIOTECA.
//
// P7.1 entrega SO a lista: OK numa fileira ainda nao abre nada (o FolderDetail
// e P7.2); colecoes_pediu_abrir existe com o contrato desde ja, mas so passa a
// devolver indice quando a pagina existir.
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
// *indiceCatalogo o indice NO CATALOGO. Inerte em P7.1 (sem FolderDetail).
int  colecoes_pediu_abrir(int *indiceCatalogo);

// Ponteiro (#99): foca a fileira i (PonteiroFn). colecoes_foco para os testes.
void colecoes_ponteiro(int i, int b);
int  colecoes_foco(void);

#endif
