// Ver colecoesui.h. A lista de gerenciamento das categorias de salvosorg.h.
//
// GEOMETRIA (collections-spec §Geometry): painel "ilha" com as pecas de vidro
// de sempre (ctx_menu_ilha) e, dentro dele, as fileiras do seletor de pastas
// da Biblioteca (NV_BIB_PICK_*, o primeiro consumidor desses tokens): contagem
// pequena em cinza por cima, nome grande em branco embaixo. O foco e so uma
// SUPERFICIE um degrau mais clara (o mesmo do menu do cartaz, linhaCtxTinta),
// sem aro nem escala — a lista e de gerenciamento, nao de escolha de destino.
//
// P7.1 e SO a lista: OK numa fileira nao abre nada (FolderDetail e P7.2). O
// contrato colecoes_pediu_abrir existe SO NA FORMA: o roteador do app.c (ramo
// abrirPorIndice em TELA_COLECOES) ja esta pronto e nao muda, mas P7.2 ainda
// tem de (1) guardar um indice de catalogo por fileira em ColLinha, (2) armar
// `pediu` de verdade no OK e (3) fazer colecoes_pediu_abrir devolver 1 com ele.
#include "colecoesui.h"
#include "salvos.h"
#include "salvosorg.h"
#include "catalogo.h"
#include "ajustes.h"
#include "idioma.h"
#include "layout.h"
#include "text.h"
#include "gfx.h"
#include "ponteiro.h"
#include "anim.h"
#include "ctxmenu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A fileira do mockup e HORIZONTAL no web (PASSO 888 = avanco em x); aqui a
// lista e vertical, entao o avanco e altura + folga.
#define COL_GAPO    12.0f
#define COL_PASSO   (NV_BIB_PICK_H + COL_GAPO)
#define COL_Y0      112.0f
#define COL_ALT_MAX (NV_TELA_H - COL_Y0 - 40.0f)

typedef struct { int cat; int cont; char nome[SORG_NOME_MAX]; } ColLinha;

static ColLinha linhas[SORG_CAT_MAX + 1];   // "Sem categoria" + uma por categoria
static int      nLinhas, foco, sair;
static float    animFoco[SORG_CAT_MAX + 1];
static float    scroll, velScroll;
// Um-passe do FolderDetail (P7.2): o OK da fileira armara isto com o indice de
// catalogo da fileira. Em P7.1 ele nunca arma, e colecoes_pediu_abrir devolve 0
// — o roteador do app ja consome o contrato na forma.
static int      pediu = -1;
// As revisoes que mudam a lista: salvo local, catalogo (a uniao le os dois) e
// o arquivo de categorias em si.
static unsigned ultRevSalvos, ultRevCat, ultRevSorg;

// MARGEM DA FOLHA (collections-spec §Geometry: 24 da folha ate a borda) na
// coluna da rail, o mesmo NV_FOLHA_MARGEM da folha de Ajustes. O TOPO nao vem
// desta margem: COL_Y0 (112) e a faixa de cabecalho, onde o titulo em y=64
// respira — a margem de 24 vale para os lados, e o teto COL_ALT_MAX deixa 40
// embaixo (mais que os 24, para a rolagem nunca encostar na borda). A
// largura continua abracando o conteudo (NV_BIB_PICK_W fixo), entao a borda
// direita e a do conteudo, nao 24 da tela.
static float colX0(void) { return NV_FOLHA_MARGEM + ajustes_rail_largura_fixa(); }

// CONTEUDO ABERTO: n fileiras de 110 com 12 de folga; o painel abraca o
// conteudo, com teto na altura util da tela — acima disso a lista rola.
static float painelAlt(void) {
  float alt = NV_FOLHA_PAD * 2.0f;
  if (nLinhas > 0) alt += nLinhas * NV_BIB_PICK_H + (nLinhas - 1) * COL_GAPO;
  if (alt > COL_ALT_MAX) alt = COL_ALT_MAX;
  return alt;
}

// A CONTAGEM por fileira: a mesma uniao do painel de Salvos (salvospainel.c) —
// o id vem da lista LOCAL quando existe e do catalogo quando so ele sabe. O
// pseudo-indice 0 e "Sem categoria" (sorg_categoria_de devolve 0 sem pasta);
// os demais sao o INDICE da categoria, na ordem de criacao.
static void reconstruir(void) {
  static SalvosEntrada *uniao;
  static int capUniao;
  static int cont[SORG_CAT_MAX + 1];   // 0 = sem categoria, k+1 = categoria k
  int i, n, nc, tot = 0;
  // As revisoes sao lidas ANTES da lista: se algo mudar no meio, o quadro
  // seguinte reconstroi de novo em vez de guardar a revisao velha.
  ultRevSalvos = salvos_revisao();
  ultRevCat = cat_revisao_itens();
  ultRevSorg = sorg_revisao();
  nc = sorg_n_categorias();
  if (nc > SORG_CAT_MAX) nc = SORG_CAT_MAX;
  memset(cont, 0, sizeof cont);
  n = salvos_n() + cat_n();
  if (n > capUniao) {
    SalvosEntrada *novo = (SalvosEntrada *)realloc(uniao, sizeof *uniao * (size_t)n);
    if (novo) { uniao = novo; capUniao = n; }
  }
  n = salvos_uniao(uniao, capUniao);
  for (i = 0; i < n; i++) {
    const SalvoItem *s = uniao[i].local >= 0 ? salvos_item(uniao[i].local) : NULL;
    const CatItem *c = uniao[i].cat >= 0 ? cat_item(uniao[i].cat) : NULL;
    const char *id = s ? s->id : (c ? c->imdb : NULL);
    int k;
    if (!id || !id[0]) continue;
    tot++;
    k = sorg_categoria_de(id);
    if (k == 0) cont[0]++;
    else {
      int j = sorg_categoria_indice(k);
      if (j >= 0 && j < nc) cont[j + 1]++;
    }
  }
  nLinhas = 0;
  // VAZIO DE VERDADE (sem pastas e sem titulos): a tela diz isso, sem painel
  // nenhum — a mesma leitura da grade vazia da Biblioteca.
  if (nc == 0 && tot == 0) return;
  linhas[0].cat = 0;
  linhas[0].cont = cont[0];
  snprintf(linhas[0].nome, sizeof linhas[0].nome, "%s", i18n("Sem categoria"));
  nLinhas = 1;
  for (i = 0; i < nc; i++) {
    int id = sorg_categoria_id(i);
    const char *nm = sorg_categoria_nome_id(id);
    linhas[nLinhas].cat = id;
    linhas[nLinhas].cont = cont[i + 1];
    snprintf(linhas[nLinhas].nome, sizeof linhas[nLinhas].nome, "%s",
             nm && nm[0] ? nm : "?");
    nLinhas++;
  }
}

void colecoes_iniciar(void) {
  foco = 0; sair = 0; pediu = -1;
  scroll = velScroll = 0.0f;
  memset(animFoco, 0, sizeof animFoco);
  reconstruir();
}

void colecoes_encerrar(void) { sair = 0; }

int colecoes_quer_sair(void) { int v = sair; sair = 0; return v; }

int colecoes_pediu_abrir(int *indiceCatalogo) {
  // FORMA apenas. P7.2: o OK da fileira armara `pediu` com o indice de catalogo
  // da fileira (via ColLinha); enquanto isso a acao e inerte — nenhuma UX
  // inventada para a pagina que ainda nao existe.
  int v = pediu;
  pediu = -1;
  (void)v;
  if (indiceCatalogo) *indiceCatalogo = -1;
  return 0;
}

void colecoes_ponteiro(int i, int b) {
  (void)b;
  if (sair || i < 0 || i >= nLinhas || i == foco) return;
  foco = i;
}
int colecoes_foco(void) { return foco; }

void colecoes_evento(const SDL_Event *e) {
  if (e->type != SDL_KEYDOWN) return;
  switch (e->key.keysym.sym) {
    case SDLK_UP:   if (foco > 0) foco--; break;
    case SDLK_DOWN: if (foco < nLinhas - 1) foco++; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
      // P7.2: FolderDetail da fileira focada. Inerte aqui (ver pediu_abrir).
      break;
    case SDLK_AC_BACK: case SDLK_ESCAPE: case SDLK_BACKSPACE: sair = 1; break;
    default: break;
  }
  // webOS/Android: o Voltar do firmware chega como scancode 482.
  if (e->key.keysym.scancode == NV_SCANCODE_BACK) sair = 1;
}

void colecoes_atualizar(float dt, Uint32 agora) {
  int i;
  (void)agora;
  // A lista pode mudar por fora (outro "+", painel de Salvos, sync da conta):
  // as revisoes sao o gatilho, nunca um realloc por quadro.
  if (salvos_revisao() != ultRevSalvos || cat_revisao_itens() != ultRevCat ||
      sorg_revisao() != ultRevSorg)
    reconstruir();
  if (foco >= nLinhas) foco = nLinhas > 0 ? nLinhas - 1 : 0;
  for (i = 0; i < nLinhas; i++) {
    float alvo = (i == foco) ? 1.0f : 0.0f;
    animFoco[i] = anim_mola(animFoco[i], alvo, dt,
                            alvo > 0.5f ? NV_MOLA_FOCO : NV_MOLA_DESFOCO);
  }
  // ROLAGEM: a fileira focada fica visivel (a mesma conta do menu.c), com a
  // mola2 lenta de sempre no lugar de um salto seco.
  if (nLinhas > 0) {
    float win = painelAlt() - NV_FOLHA_PAD * 2.0f;
    float alt = nLinhas * NV_BIB_PICK_H + (nLinhas - 1) * COL_GAPO;
    float alvo = scroll;
    float topo = (float)foco * COL_PASSO;
    if (topo < alvo) alvo = topo;
    else if (topo + NV_BIB_PICK_H > alvo + win) alvo = topo + NV_BIB_PICK_H - win;
    if (alvo > alt - win) alvo = alt - win;
    if (alvo < 0.0f) alvo = 0.0f;
    scroll = anim_mola2(&velScroll, scroll, alvo, dt, NV_MOLA2_SCROLL);
  } else scroll = velScroll = 0.0f;
}

// ESTADO VAZIO: a mesma forma da Biblioteca — icone grande, frase branca e a
// dica do proximo passo, centrados na largura util. As tres frases sao CHAVES
// de idioma_tab.h (reuso, sem entrada nova): as duas de P7.1 vazavam em
// portugues na UI em ingles (revisao P7.1).
static void desenhaVazio(void) {
  const char *l1 = "Nada salvo por enquanto";
  const char *l2 = "Segure OK num título para mover para uma categoria.";
  char voltar[48];
  float cx = colX0() + (NV_TELA_W - 40.0f - colX0()) * 0.5f;
  float y = COL_Y0 + 190.0f;
  TxtLinha t1, t2, d;
  gfx_icone((GfxRect){ cx - 32.0f, y - 100.0f, 64.0f, 64.0f },
            "menu_library", 0.70f, 0.70f, 0.72f, 1.0f);
  t1 = txt_linha(TXT_TITULO2, i18n(l1), 255, 255, 255, 255);
  t2 = txt_linha_corta(TXT_CALLOUT, i18n(l2), 150, 153, 162, 255, NV_TELA_W - 80.0f);
  txt_desenhar_alpha(t1, cx - t1.w * 0.5f, y, 0.96f);
  txt_desenhar_alpha(t2, cx - t2.w * 0.5f, y + t1.h + 18.0f, 0.85f);
  // "Voltar: Biblioteca" nao e chave da tabela — compoe duas que sao, ja
  // traduzidas; o resultado composto nao casa nenhuma chave e sai como esta.
  snprintf(voltar, sizeof voltar, "%s: %s", i18n("Voltar"), i18n("Biblioteca"));
  d = txt_linha(TXT_CAPTION2, voltar, 150, 153, 162, 255);
  txt_desenhar(d, cx - d.w * 0.5f, y + t1.h + t2.h + 58.0f);
}

void colecoes_desenhar(Uint32 agora) {
  GfxRect p;
  int i;
  (void)agora;
  ajustes_ui_fundo();
  // TITULO NA COLUNA DE CONTEUDO do app: ajustes_conteudo_x() (104 com a rail
  // recolhida, a mesma familia dos 96 da Biblioteca). O formato "ao lado da
  // ilha do relogio" (livetvdiag) punha o titulo em x=240 — 192 px a direita
  // da borda do painel, com vazio morto a esquerda; a ilha fica no outro canto
  // e no preview nem aparece (revisao P7.1).
  { float x = ajustes_conteudo_x();
    TxtLinha tit = txt_linha(TXT_AJ_TIT28, i18n("Coleções"), 243, 242, 239, 255);
    TxtLinha sub = txt_linha_corta(TXT_AJ_ESTADO,
                   // Chave PROPRIA do subtitulo de Colecoes (idioma_tab.h):
                   // antes era a frase de P7.1 sem entrada (vazava em
                   // portugues); o reuso de "Organizar Salvos" rendia
                   // "Organize Saved", etiqueta de acao dos Ajustes, e nao o
                   // subtitulo da tela (revisao P7.1, 2a passada).
                   i18n("Organize seus títulos salvos em pastas."),
                   243, 242, 239, 255, NV_TELA_W - 60 - x - tit.w - 18);
    txt_desenhar(tit, x, 64 - tit.h * 0.5f);
    txt_desenhar_alpha(sub, x + tit.w + 18, 64 - sub.h * 0.5f, 0.62f); }
  if (nLinhas == 0) { desenhaVazio(); return; }
  p = (GfxRect){ colX0(), COL_Y0, NV_FOLHA_PAD * 2.0f + NV_BIB_PICK_W, painelAlt() };
  ctx_menu_ilha(p, NV_FOLHA_RAIO, 1.0f);
  // As fileiras dentro do recorte do painel: com o teto de altura a rolagem
  // esconde o que passa da borda, e o ponteiro nao alcance o pedaco cortado.
  gfx_recorte(p.x, p.y, p.w, p.h);
  for (i = 0; i < nLinhas; i++) {
    GfxRect r = { p.x + NV_FOLHA_PAD,
                  p.y + NV_FOLHA_PAD + (float)i * COL_PASSO - scroll,
                  NV_BIB_PICK_W, NV_BIB_PICK_H };
    float k = animFoco[i];
    TxtLinha cont, nome;
    char txt[64];
    if (r.y + r.h < p.y || r.y > p.y + p.h) continue;   // fora da janela
    // FOCO = SUPERFICIE um degrau mais clara, o mesmo do menu do cartaz.
    if (k > 0.01f) {
      if (ajustes_vidro())
        gfx_cor(r, NV_BIB_PICK_RAIO / NV_BIB_PICK_H, 1, 1, 1, .14f * k);
      else
        gfx_cor(r, NV_BIB_PICK_RAIO / NV_BIB_PICK_H, .19f, .195f, .215f, k);
    }
    snprintf(txt, sizeof txt, "%d %s", linhas[i].cont,
             i18n(linhas[i].cont == 1 ? "título" : "títulos"));
    cont = txt_linha_corta(TXT_G19M, txt, 128, 128, 128, 255,
                           r.w - NV_BIB_PICK_PADX * 2.0f);
    nome = txt_linha_corta(TXT_G30M, linhas[i].nome, 243, 242, 239, 255,
                           r.w - NV_BIB_PICK_PADX * 2.0f);
    txt_desenhar_alpha(cont, r.x + NV_BIB_PICK_PADX, r.y + NV_BIB_PICK_PADY, 1.0f);
    txt_desenhar_alpha(nome, r.x + NV_BIB_PICK_PADX,
                       r.y + NV_BIB_PICK_PADY + 24.0f + 4.0f, 1.0f);
  }
  gfx_sem_recorte();
  for (i = 0; i < nLinhas; i++) {
    float ry = p.y + NV_FOLHA_PAD + (float)i * COL_PASSO - scroll;
    if (ry + NV_BIB_PICK_H < p.y || ry > p.y + p.h) continue;
    ponteiro_alvo(p.x + NV_FOLHA_PAD, ry, NV_BIB_PICK_W, NV_BIB_PICK_H,
                  colecoes_ponteiro, NULL, i, 0);
  }
}
