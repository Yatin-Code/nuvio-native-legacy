// Colecoes (P7.2a): a MAQUINA DE ESTADO do gerenciamento e do FolderDetail —
// criar/renomear/excluir, abrir/voltar, e a queda para o gerenciamento quando a
// pasta aberta some (perfil/sync). Sem GL: so evento + atualizar (o desenho e a
// evidencia fotografica da fixture).
//
// O TECLADO DA TV nao e digitado por tecla fisica (grade D-pad), entao a
// ESCRITA (criar/renomear por texto) e exercitada pelo efeito no salvosorg, que
// e o que a tela chama. Aqui cobramos os CONTRATOS de estado e navegacao:
//
//   1. iniciar -> gerenciamento; a contagem acompanha sorg_* (o "Sem categoria"
//      e a 1a fileira, as categorias seguem);
//   2. DOWN+OK numa fileira abre o FolderDetail daquela categoria
//      (colecoes_vista/cat_aberta) e o Back volta ao gerenciamento;
//   3. OK numa fileira de TITULO arma o pedido com o INDICE DE CATALOGO
//      (colecoes_pediu_abrir) e NUNCA toca em play;
//   4. a pasta aberta que SOME (excluida por fora) devolve a tela ao
//      gerenciamento na proxima atualizacao;
//   5. excluir uma categoria devolve os titulos a "Sem categoria" (id some da
//      lista, nenhum titulo fica pendurado).
//
//   bash tests/colecoesui.sh
#include "ajustes.h"
#include "catalogo.h"
#include "colecoesui.h"
#include "dados.h"
#include "salvos.h"
#include "salvosorg.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
static void confere(const char *o_que, int obtido, int esperado) {
  int ok = obtido == esperado;
  printf("  %-62s %s (obtido %d, esperado %d)\n", o_que, ok ? "ok   " : "FALHOU",
         obtido, esperado);
  if (!ok) falhas++;
}

static CatItem itens[8];
static void montar(void) {
  int i;
  memset(itens, 0, sizeof itens);
  for (i = 0; i < 8; i++) {
    CatItem *c = &itens[i];
    snprintf(c->imdb, sizeof c->imdb, "tt%07d", 1000000 + i);
    snprintf(c->tipo, sizeof c->tipo, "movie");
    snprintf(c->titulo, sizeof c->titulo, "Titulo %d", i);
    snprintf(c->poster, sizeof c->poster, "deploy/app/art/%02d.jpg", i % 40);
    if (i < 4) c->progresso = 20 + i;   // os quatro primeiros "continuam"
  }
  cat_definir(itens, 8);
}

static void tecla(SDL_Keycode k) {
  SDL_Event e;
  memset(&e, 0, sizeof e);
  e.type = SDL_KEYDOWN;
  e.key.keysym.sym = k;
  colecoes_evento(&e);
}

static void quadros(int n) {
  int i;
  for (i = 0; i < n; i++) colecoes_atualizar(1.0f / 60.0f, SDL_GetTicks());
}

int main(void) {
  int id, i, abertos;
  const char *nome;
  int catIdx = -1;

  if (!getenv("NUVIO_DADOS")) { fprintf(stderr, "sem NUVIO_DADOS\n"); return 2; }
  montar();
  // Todos os oito na lista local; os quatro COM progresso vao depois para uma
  // categoria, para o FolderDetail ter o que mostrar.
  for (i = 0; i < 8; i++) salvos_definir(&itens[i], 1);
  salvos_aplicar_catalogo();

  printf("== criar / listar ==\n");
  colecoes_iniciar();
  quadros(2);
  confere("sem categoria: gerenciamento", colecoes_vista(), 0);
  confere("sem categoria: 0 pastas", colecoes_n_categorias(), 0);

  id = sorg_criar_categoria("Fim de semana");
  confere("criar devolve id != 0", id != 0, 1);
  for (i = 0; i < 4; i++) sorg_mover(itens[i].imdb, id);
  quadros(3);
  confere("1 pasta depois de criar", colecoes_n_categorias(), 1);

  // Duplicado: mesmo nome devolve o MESMO id e nao cria outra pasta.
  { int id2 = sorg_criar_categoria("Fim de semana");
    confere("duplicado devolve o mesmo id", id2, id);
    quadros(2);
    confere("duplicado nao cria pasta", colecoes_n_categorias(), 1); }

  printf("== abrir a pasta / voltar ==\n");
  // foco 0 = "Sem categoria", foco 1 = "Fim de semana": DOWN e OK.
  tecla(SDLK_DOWN);
  quadros(1);
  tecla(SDLK_RETURN);
  quadros(2);
  confere("OK abre o FolderDetail", colecoes_vista(), 1);
  confere("FolderDetail e a pasta certa", colecoes_cat_aberta(), id);
  confere("a pasta tem os 4 titulos", colecoes_n_titulos_pasta(), 4);

  printf("== OK numa fileira arma o pedido (indice de catalogo) ==\n");
  // O corpo comeca focado na 1a fileira (detSel=1). OK nela arma `pediu`.
  tecla(SDLK_RETURN);
  confere("pediu abrir (rota abrirPorIndice)", colecoes_pediu_abrir(&catIdx), 1);
  confere("indice de catalogo valido", catIdx >= 0 && catIdx < cat_n(), 1);
  confere("leitura zera o pedido", colecoes_pediu_abrir(&catIdx), 0);

  printf("== Back volta ao gerenciamento ==\n");
  tecla(SDLK_AC_BACK);
  confere("Back sai do FolderDetail", colecoes_vista(), 0);

  printf("== renomear (nome troca, id NAO) ==\n");
  { int ok = sorg_renomear_categoria(id, "Kids");
    nome = sorg_categoria_nome_id(id);
    confere("renomear aceito", ok, 1);
    confere("nome trocado", nome && !strcmp(nome, "Kids"), 1);
    confere("id preservado", sorg_categoria_indice(id) >= 0, 1);
    // O id antigo NUNCA muda: a pasta continua a mesma.
    quadros(2);
    confere("1 pasta ainda", colecoes_n_categorias(), 1); }

  printf("== queda quando a pasta SOME por fora ==\n");
  tecla(SDLK_DOWN);
  quadros(1);
  tecla(SDLK_RETURN);
  quadros(2);
  confere("reabriu a pasta", colecoes_vista(), 1);
  sorg_excluir_categoria(id);        // como uma troca de perfil/sync faria
  quadros(3);
  confere("pasta sumida -> gerenciamento", colecoes_vista(), 0);
  quadros(2);
  confere("a pasta saiu da lista", colecoes_n_categorias(), 0);

  printf("== excluir devolve os titulos a 'Sem categoria' ==\n");
  abertos = 0;
  for (i = 0; i < 4; i++) if (sorg_categoria_de(itens[i].imdb) == 0) abertos++;
  confere("os 4 titulos voltaram a Sem categoria", abertos, 4);

  printf("== 'Nova coleção' cancelada nao cria nada ==\n");
  // Recria uma pasta para o pe do gerenciamento existir com contagem propria:
  // foco 0 = "Sem categoria", 1 = a recriada, 2 = a acao do pe.
  { int idc = sorg_criar_categoria("Pra cancelar");
    int antes;
    quadros(3);
    antes = sorg_n_categorias();
    tecla(SDLK_DOWN); tecla(SDLK_DOWN);     // ate a acao do pe
    quadros(1);
    tecla(SDLK_RETURN);                      // abre o teclado
    quadros(1);
    confere("teclado de criar abriu", colecoes_pop_aberto(), 1);
    tecla(SDLK_AC_BACK);                     // Voltar cancela
    quadros(2);
    confere("teclado fechou no Voltar", colecoes_pop_aberto(), 0);
    confere("cancelar NAO cria pasta", sorg_n_categorias(), antes);
    // A pasta recriada continua la (nada foi criado nem apagado).
    confere("a pasta cancelada segue intacta", sorg_categoria_indice(idc) >= 0, 1); }

  printf("\n%s (%d falha%s)\n", falhas ? "FALHOU" : "PASSOU", falhas,
         falhas == 1 ? "" : "s");
  return falhas ? 1 : 0;
}
