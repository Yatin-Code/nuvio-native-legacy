// Colecoes (P7): a lista de GERENCIAMENTO das categorias salvas (salvosorg.h)
// — "Sem categoria" primeiro e depois uma fileira por categoria, com a contagem
// de titulos de cada uma — e o FolderDetail de uma pasta, como ESTADO desta tela
// (nao uma Tela nova). Aberta pela 4a aba da Biblioteca ("Colecoes…") e roteada
// como TELA_COLECOES, sem sobrecarregar TELA_BIBLIOTECA.
//
// P7.1 entregou a rota (app.c inteiro ligado) e a lista de gerenciamento. P7.2a
// acrescenta: a fileira de acao "Nova coleção" no pe (teclado da TV, criar);
// o FolderDetail com cabecalho (nome/contagem) e as acoes Renomear/Excluir;
// o corpo em secoes empilhadas com OK abrindo o titulo PELA ROTA DE SEMPRE
// (abrirPorIndice — nunca play); os chips de ordenacao/agrupamento/estilo/
// recomendacoes (escrevem so pelos sorg_definir_*); e a volta automatica ao
// gerenciamento quando a categoria aberta some por perfil/sync. O seletor
// "Mover para categoria" (a 3a tela do spec) NAO e desta fatia: ele ja existe
// no painel de Salvos (POP_MOVER) e o reuso pela rota e P7.2b.
//
// O roteador do app.c (o ramo de abrirPorIndice em TELA_COLECOES) ja esta
// pronto e NAO muda.
#ifndef NV_COLECOESUI_H
#define NV_COLECOESUI_H
#include <SDL2/SDL.h>

void colecoes_iniciar(void);
void colecoes_evento(const SDL_Event *e);
void colecoes_atualizar(float dt, Uint32 agora);
void colecoes_desenhar(Uint32 agora);
int  colecoes_quer_sair(void);   // 1 quando o Back deve fechar a tela (limpa ao ler)
void colecoes_encerrar(void);

// OK numa fileira do FolderDetail: consome o pedido e devolve 1, escrevendo em
// *indiceCatalogo o indice NO CATALOGO da fileira (ColTit.catIdx). Devolve 0
// quando o OK nao pediu abertura (foco nas acoes/chips, fileira sem copia no
// catalogo, Back, ou o gerenciamento). O app.c passa esse indice ao
// abrirPorIndice — a MESMA rota da Biblioteca; nunca ha play aqui.
int  colecoes_pediu_abrir(int *indiceCatalogo);

// Ponteiro (#99): foca a fileira i (PonteiroFn). colecoes_foco para os testes.
void colecoes_ponteiro(int i, int b);
int  colecoes_foco(void);

// ESTADO PARA TESTE/FIXTURE (nao e UI): o FolderDetail e um ESTADO desta tela,
// nao uma Tela nova — nao ha app_tela_nome() para ele. Estes acessores deixam o
// sequenciador da fixture e os testes C esperarem/afirmarem a vista certa sem
// inventar uma Tela. `*pop`: 1 quando o teclado modal OU a ilha (confirmar/
// opcao) estao por cima. `*cat`: o id da categoria aberta (0 = "Sem categoria").
int  colecoes_vista(void);          // 0 = gerenciamento, 1 = FolderDetail
int  colecoes_cat_aberta(void);     // id da categoria aberta no FolderDetail
int  colecoes_pop_aberto(void);     // teclado ou ilha abertos
int  colecoes_n_categorias(void);   // fileiras do gerenciamento (sem o "Sem categoria")
int  colecoes_n_titulos_pasta(void); // fileiras de titulo do FolderDetail

#endif
