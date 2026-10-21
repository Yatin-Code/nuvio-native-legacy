// Ver colecoesui.h. As COLECOES (P7): a lista de GERENCIAMENTO das categorias
// de salvosorg.h (tela 1) e o FolderDetail de uma delas (tela 2) — criar,
// renomear, excluir e ver os titulos de cada pasta. A tela 3 do spec (o seletor
// "Mover para categoria") NAO e desta fatia: ela ja existe no painel de Salvos
// (POP_MOVER, salvospainel.c) e o reuso pela rota e P7.2b.
//
// O QUE P7.1 DEIXOU E P7.2 ACRESCENTA. P7.1 entregou a rota (TELA_COLECOES, o
// app.c inteiro ja ligado) e a lista de gerenciamento com a contagem por
// categoria. P7.2a acrescenta: (1) a fileira de acao "Nova coleção" no pe do
// gerenciamento, com o teclado da TV; (2) o FolderDetail como ESTADO desta tela
// (nao uma Tela nova) — cabecalho com nome/contagem e as acoes Renomear/Excluir
// (teclado + confirmacao de sempre); (3) o corpo do FolderDetail: os titulos da
// pasta em secoes empilhadas, e OK numa fileira abrindo o titulo PELA ROTA DE
// SEMPRE; (4) os chips de ordenacao/agrupamento/estilo/recomendacoes,
// escrevendo so pelos sorg_definir_* para o painel de Salvos nao divergir;
// (5) a volta ao gerenciamento quando a categoria aberta some (perfil/sync).
//
// GEOMETRIA (collections-spec §Geometry): painel "ilha" com as pecas de vidro
// de sempre (ctx_menu_ilha) e, dentro dele, as fileiras do seletor de pastas da
// Biblioteca (NV_BIB_PICK_*). O foco de FILEIRA e so uma SUPERFICIE um degrau
// mais clara (o mesmo do menu do cartaz, linhaCtxTinta); as ACOES ("Nova
// coleção", Renomear, Excluir) e os chips acendem na pilula cheia do acento. As
// fileiras de titulo do FolderDetail reusam as medidas da linha de lista da
// Biblioteca (BIB_LIN_*: capa 96x144, altura 168) — aquelas medidas sao
// privadas daquele arquivo, entao as constantes abaixo as ESPELHAM com o mesmo
// valor, sem numero inventado. Secoes do FolderDetail: 46 de cabecalho
// (NV_DETF_CAB_H) e NV_DETF_SEC_GAP entre uma e a seguinte.
#include "colecoesui.h"
#include "salvos.h"
#include "salvosorg.h"
#include "catalogo.h"
#include "ajustes.h"
#include "idioma.h"
#include "layout.h"
#include "detail.h"     // NV_DETF_CAB_H / NV_DETF_SEC_GAP (secoes do FolderDetail)
#include "teclado.h"
#include "text.h"
#include "gfx.h"
#include "tex_cache.h"
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

// A linha de TITULO do FolderDetail espelha a linha de lista da Biblioteca
// (src/biblioteca.c: BIB_LIN_*), privada daquele arquivo. Mesmos valores, mesma
// capa 96x144, mesma folga 12 entre linhas.
#define COL_DET_LIN_H     168.0f
#define COL_DET_LIN_GAP    12.0f
#define COL_DET_LIN_PASSO  (COL_DET_LIN_H + COL_DET_LIN_GAP)
#define COL_DET_MINI_W     96.0f
#define COL_DET_MINI_H    144.0f
#define COL_DET_MINI_PAD   12.0f
#define COL_DET_TX_GAP     24.0f    // da capa ao texto

// As medidas do cabecalho do FolderDetail (todas derivadas das pecas de sempre:
// pad 40 da folha, NV_CTRL_H 56 das pilulas, 12 entre vizinhas).
#define COL_DET_CAB_H    100.0f    // nome (TXT_TITULO2) + contagem (TXT_G19M)
#define COL_DET_ACOES_Y  (COL_DET_CAB_H)
#define COL_DET_CHIPS_Y  (COL_DET_ACOES_Y + NV_CTRL_H + 16.0f)
#define COL_DET_CORPO_Y  (COL_DET_CHIPS_Y + NV_CTRL_H + 24.0f)
// O rodape de ajuda, no pe do painel (altura reservada fora da janela do corpo).
#define COL_DET_RODAPE   40.0f

// O NOME DO TECLADO: letras, numeros, espaco e hifen, 24 letras — a MESMA
// receita do painel de Salvos (salvospainel.c, ALFA_NOME/SP_NOME_LETRAS). A
// normalizacao (primeira maiuscula, espacos dobrados) fica no salvosorg.
static const char *COL_ALFA_NOME = "abcdefghijklmnopqrstuvwxyz0123456789 -";
#define COL_NOME_LETRAS 24

// --- Estado ------------------------------------------------------------------
typedef enum { COL_VISTA_LISTA = 0, COL_VISTA_PASTA } ColVista;
// O teclado aberto: para criar no gerenciamento ou renomear no FolderDetail.
enum { COL_TK_NADA = 0, COL_TK_CRIAR, COL_TK_RENOMEAR };
// A ilha por cima da tela: a confirmacao de excluir ou a escolha de uma opcao.
enum { COL_POP_NADA = 0, COL_POP_CONFIRMA, COL_POP_OPCAO };
// As quatro opcoes de exibicao do editor (mesmas do painel de Salvos).
enum { COL_OP_ORDEM = 0, COL_OP_GRUPO, COL_OP_ESTILO, COL_OP_SOCIAL, COL_OP_N };

// Perfil da familia de fileiras de gerenciamento (o pseudo "Sem categoria" e
// as categorias): contagem pequena em cinza, nome grande em branco.
typedef struct { int cat; int cont; char nome[SORG_NOME_MAX]; } ColLinha;

// A fileira de TITULO do FolderDetail. `catIdx` e o INDICE NO CATALOGO da copia
// que tem o progresso (-1 quando so a lista local conhece o titulo): e ele que
// colecoes_pediu_abrir entrega ao app.c, que abre pela mesma rota da Biblioteca
// (abrirPorIndice). NUNCA tocamos em detail_pedir_reproduzir/cwTocar.
typedef struct {
  char id[24];        // id de TITULO (sem ":t:e"), o mesmo de sorg_categoria_de
  char titulo[160];
  char poster[1024];
  int  prog;          // progresso no catalogo (0 = nao comecado)
  int  catIdx;        // indice no catalogo, -1 = so local
  float y;            // y relativo ao topo do CORPO (preenchido por detLayout)
} ColTit;

// As secoes do corpo do FolderDetail (o mesmo empilhamento do painel de Salvos).
#define COL_DET_SEC_MAX (SORG_GRUPO_N + 4)
typedef struct { float y; char rot[SORG_NOME_MAX + 16]; } ColDetSec;
static ColDetSec detSec[COL_DET_SEC_MAX];
static int       detSecN;
static float     detCorpoAlt;

// --- Gerenciamento -----------------------------------------------------------
static ColLinha linhas[SORG_CAT_MAX + 1];   // "Sem categoria" + uma por categoria
static int      nLinhas, foco, sair;
static float    animFoco[SORG_CAT_MAX + 2]; // +1: a acao "Nova coleção" no pe
static float    scroll, velScroll;
// As revisoes que mudam a lista: salvo local, catalogo (a uniao le os dois) e
// o arquivo de categorias em si.
static unsigned ultRevSalvos, ultRevCat, ultRevSorg;

// --- FolderDetail ------------------------------------------------------------
static ColVista vista;
static int      detCat;                     // id da categoria aberta (0 = "Sem categoria")
static ColTit  *detLin;                     // heap: so os titulos DESTA pasta
static int      detN, detCap;
static float    detScroll, detVel;
static float   *detAnim;                    // heap, junto com detLin: molas das fileiras (i absoluto)
// Foco do FolderDetail: detSel 0 = acoes do cabecalho, 1 = fileira do corpo,
// 2 = faixa de chips.
static int      detSel, detAcao;            // detAcao: 0 = Renomear, 1 = Excluir
static int      detRow;
static float    detAcaoAnim[2];
static float    detChipAnim[COL_OP_N];
static int      detChip;                    // chip focado
static float    detEntrada;                 // nasce da ilha ao abrir a pasta

// Um-passe do FolderDetail: o OK da fileira arma isto com o indice de catalogo
// da fileira; colecoes_pediu_abrir consome e entrega ao app.c.
static int      pediu = -1;

// --- Teclado / ilha / recado -------------------------------------------------
static int      tecladoPara;                // COL_TK_*
static int      pop;                        // COL_POP_*
static int      popFoco, popN;
static CtxLinha popL[8];
static char     popTitulo[200], popSub[220];
static char     recado[160];                // o aviso curto (toast) da ultima acao
static float    recadoAte;

// MARGEM DA FOLHA (collections-spec §Geometry: 24 da folha ate a borda) na
// coluna da rail, o mesmo NV_FOLHA_MARGEM da folha de Ajustes. O TOPO nao vem
// desta margem: COL_Y0 (112) e a faixa de cabecalho, onde o titulo em y=64
// respira. A largura abraca o conteudo (NV_BIB_PICK_W fixo), entao a borda
// direita e a do conteudo, nao 24 da tela.
static float colX0(void) { return NV_FOLHA_MARGEM + ajustes_rail_largura_fixa(); }

// A superficie de FOCO de uma fileira: um degrau mais clara, a mesma do menu do
// cartaz (linhaCtxTinta) — nunca aro, nunca escala (a lista e de gerenciamento,
// nao de escolha de destino).
static void focoFileira(GfxRect r, float raioPx, float k) {
  if (k <= 0.01f) return;
  if (ajustes_vidro()) gfx_cor(r, raioPx / r.h, 1, 1, 1, .14f * k);
  else                 gfx_cor(r, raioPx / r.h, .19f, .195f, .215f, k);
}

// FOCO DE BOTAO/CHIP: pilula cheia no acento (vidro) ou no acento com a luz
// atras (solido), a receita do chipFolha/botaoSup do painel de Salvos. A tinta
// do texto em foco e ajustes_acento_tinta.
static float focoSuave(float f) {
  f = anim_clamp(f, 0.0f, 1.0f);
  return f * f * (3.0f - 2.0f * f);
}
static void focoBotao(GfxRect r, float raioPx, float f, float a) {
  float v = focoSuave(f), ar, ag, ab;
  if (ajustes_vidro()) gfx_cor(r, raioPx / r.h, 1, 1, 1, .08f * (1.0f - v) * a);
  else gfx_cor(r, raioPx / r.h, .14f, .148f, .17f, (1.0f - v) * a);
  if (v <= .001f) return;
  if (ajustes_vidro()) { gfx_vidro_pilula_cheia(r, raioPx / r.h, v, a); return; }
  ajustes_acento(&ar, &ag, &ab);
  gfx_cor(r, raioPx / r.h, ar, ag, ab, v * a);
}

// O aviso curto (toast) por 4 s — o mesmo `dizer`/recado da Biblioteca.
static void dizer(const char *s) {
  snprintf(recado, sizeof recado, "%s", s);
  recadoAte = 4.0f;
}

// --- Contagem das categorias (a uniao do painel de Salvos) -------------------
// O id vem da lista LOCAL quando existe e do catalogo quando so ele sabe. O
// pseudo-indice 0 e "Sem categoria" (sorg_categoria_de devolve 0 sem pasta); os
// demais sao o INDICE da categoria, na ordem de criacao.
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

// A categoria existe? O pseudo "Sem categoria" (0) sempre; uma categoria
// entregue a exclusao some.
static int catValida(int id) { return id == 0 || sorg_categoria_indice(id) >= 0; }

// O pseudo "Sem categoria" (cat 0) abre como pasta, mas nao tem dono para
// renomear nem apagar — sorg_renomear/renomear com id 0 nao tem o que fazer.
// O cabecalho nao desenhas as acoes e o foco as salta (as fileiras sobem direto
// para os chips).
static int detTemAcoes(void) { return detCat != 0; }

// --- FolderDetail: os titulos da pasta aberta --------------------------------
// Le a MESMA uniao do gerenciamento e guarda so os titulos cujo
// sorg_categoria_de cai na pasta `detCat`, na ordem de insercao da uniao. A
// primeira copia de cada titulo ganha (o catalogo pode repeti-lo).
static void detReconstruir(void) {
  static SalvosEntrada *uniao;
  static int capUniao;
  int i, n, k;
  detN = 0;
  n = salvos_n() + cat_n();
  if (n > capUniao) {
    SalvosEntrada *novo = (SalvosEntrada *)realloc(uniao, sizeof *uniao * (size_t)n);
    if (novo) { uniao = novo; capUniao = n; }
  }
  n = salvos_uniao(uniao, capUniao);
  if (n > detCap) {
    ColTit *novo = (ColTit *)realloc(detLin, sizeof *detLin * (size_t)n);
    if (!novo) return;
    detLin = novo;
    detCap = n;
    detAnim = (float *)realloc(detAnim, sizeof(float) * (size_t)detCap);
    // As molas nascem em repouso: a reconstrucao re-ancora o foco a vista.
    memset(detAnim, 0, sizeof(float) * (size_t)detCap);
  }
  for (i = 0; i < n; i++) {
    const SalvoItem *s = uniao[i].local >= 0 ? salvos_item(uniao[i].local) : NULL;
    const CatItem *c = uniao[i].cat >= 0 ? cat_item(uniao[i].cat) : NULL;
    const char *raw = s ? s->id : (c ? c->imdb : NULL);
    char base[24];
    ColTit *d;
    if (!raw || !raw[0]) continue;
    salvos_id_titulo(raw, base, sizeof base);
    if (sorg_categoria_de(base) != detCat) continue;
    for (k = 0; k < detN; k++) if (!strcmp(detLin[k].id, base)) break;
    if (k < detN) continue;   // o mesmo titulo so entra uma vez
    d = &detLin[detN++];
    memset(d, 0, sizeof *d);
    snprintf(d->id, sizeof d->id, "%s", base);
    if (s) {
      snprintf(d->titulo, sizeof d->titulo, "%s", s->titulo);
      snprintf(d->poster, sizeof d->poster, "%s", s->poster);
    } else if (c) {
      snprintf(d->titulo, sizeof d->titulo, "%s", c->titulo);
      snprintf(d->poster, sizeof d->poster, "%s", c->poster);
    }
    d->prog = c ? c->progresso : 0;
    d->catIdx = uniao[i].cat;   // -1 quando so a lista local tem o titulo
  }
  // As posicoes (secoes + y de cada fileira) saem da MESMA reconstrucao: entre
  // uma mudanca e outra o quadro so le pronto.
  {
    int gAnt = -999;
    float y = 0.0f;
    detSecN = 0;
    for (i = 0; i < detN; i++) {
      int g = (sorg_grupo() == SORG_GRUPO_PROGRESSO) ? (detLin[i].prog > 0 ? 0 : 1) : 0;
      if (g != gAnt) {
        if (detSecN < COL_DET_SEC_MAX) {
          detSec[detSecN].y = y;
          if (sorg_grupo() == SORG_GRUPO_PROGRESSO)
            snprintf(detSec[detSecN].rot, sizeof detSec[detSecN].rot, "%s",
                     g == 0 ? "Continuar" : "Não começados");
          else
            snprintf(detSec[detSecN].rot, sizeof detSec[detSecN].rot, "%s",
                     "Nesta coleção");
          detSecN++;
        }
        // Cabecalho da secao + o ar ate a primeira fileira dela.
        y += NV_DETF_CAB_H + 20.0f;
        gAnt = g;
      }
      detLin[i].y = y;
      y += COL_DET_LIN_PASSO;
    }
    if (detN > 0) y -= COL_DET_LIN_GAP;
    detCorpoAlt = y;
  }
}

// A fileira `i` cabe na janela visivel do corpo? (a janela e o resto do painel
// abaixo do cabecalho, menos o rodape). painelAltDet vem depois, no bloco de
// layout: a declaracao adiante evita a chamada implicita.
static float painelAltDet(void);
static float detJanela(void) {
  float h = painelAltDet();
  float vp = h - NV_FOLHA_PAD - COL_DET_CORPO_Y - COL_DET_RODAPE;
  return vp > COL_DET_LIN_H ? vp : COL_DET_LIN_H;
}

// --- Layout do gerenciamento -------------------------------------------------
// CONTEUDO ABERTO: n fileiras de 110 com 12 de folga, mais a acao do pe (56) e
// o ar entre elas; o painel abraca o conteudo, com teto na altura util da tela —
// acima disso a lista rola. A acao do pe fica DENTRO do painel, colada ao fim.
static float painelAlt(void) {
  float alt = NV_FOLHA_PAD * 2.0f;
  if (nLinhas > 0) alt += nLinhas * COL_PASSO + 16.0f + NV_CTRL_H;
  if (alt > COL_ALT_MAX) alt = COL_ALT_MAX;
  return alt;
}

// Altura do painel do FolderDetail: o cabecalho + o corpo (com teto na altura
// util da tela; o corpo rola por dentro).
static float painelAltDet(void) {
  float alt = NV_FOLHA_PAD + COL_DET_CORPO_Y + detCorpoAlt + COL_DET_RODAPE;
  float teto = COL_ALT_MAX;
  if (alt > teto) alt = teto;
  return alt;
}

// --- Abrir / fechar ----------------------------------------------------------
static void listaIniciar(void) {
  vista = COL_VISTA_LISTA;
  foco = 0; sair = 0; pediu = -1;
  scroll = velScroll = 0.0f;
  memset(animFoco, 0, sizeof animFoco);
  detN = 0; detCat = 0;
  pop = COL_POP_NADA; tecladoPara = COL_TK_NADA;
  recado[0] = 0; recadoAte = 0.0f;
  reconstruir();
}

void colecoes_iniciar(void) { listaIniciar(); }

void colecoes_encerrar(void) { sair = 0; }

int colecoes_quer_sair(void) {
  // SO o Back no GERENCIAMENTO pede a saida (o app.c devolve a Biblioteca, a
  // origem de P7.1). O Back DENTRO do FolderDetail volta ao gerenciamento e e
  // tratado no proprio colecoes_evento — NAO aqui: esta funcao e consultada a
  // CADA quadro pelo app.c, e voltar de vista aqui devolveria a lista no mesmo
  // quadro em que a pasta abriu, sem a pessoa ver nada.
  { int v = sair; sair = 0; return v; }
}

int colecoes_pediu_abrir(int *indiceCatalogo) {
  int v = pediu;
  pediu = -1;
  if (indiceCatalogo) *indiceCatalogo = v;
  return v >= 0;
}

// --- Teclado (criar / renomear) ----------------------------------------------
static void tecladoAbrir(int para, const char *inicial) {
  tecladoPara = para;
  pop = COL_POP_NADA;
  teclado_abrir_com(para == COL_TK_RENOMEAR ? "Renomear categoria" : "Nova categoria",
                    "Ex.: Fim de semana, Kids. Até 24 letras.",
                    COL_NOME_LETRAS, COL_ALFA_NOME, inicial);
}

// Para onde volta o aviso do teclado: o indice da categoria `id` na lista.
static int listaIndiceDe(int id) {
  int k;
  for (k = 0; k < nLinhas; k++) if (linhas[k].cat == id) return k;
  return -1;
}

// O que o teclado devolveu. Chamado por quadro enquanto ha pedido pendente.
// REGRAS (collections-spec §Back-stack): so TECLADO_PRONTO com texto NAO VAZIO
// escreve. Voltar/Cancelar nao escreve nada — "Nova coleção" cancelada nao cria
// pasta vazia, e "Renomear" cancelada deixa o nome intacto (o id nunca muda).
static void tecladoResultado(void) {
  int r;
  const char *t;
  if (!tecladoPara || teclado_aberto()) return;
  r = teclado_resultado();
  if (r == TECLADO_NADA) return;
  t = teclado_texto();
  if (r == TECLADO_PRONTO && t && t[0]) {
    if (tecladoPara == COL_TK_CRIAR) {
      // A criacao devolve o ID: um id NOVO quando criou, o id que JA existia
      // quando o nome era de outra pasta (duplicado nunca cria). 0 e lista
      // cheia OU nome sem nada imprimivel — sorg_nome_limpo corta os espacos,
      // entao "   " tambem cai no 0; a distincao e feita AQUI com ele, no
      // padrao do pluginsui (cada resultado tem o seu recado): sem isso um
      // espaco so ganhava o recado de lista cheia.
      char limpo[SORG_NOME_MAX];
      int antes = sorg_n_categorias();
      int vazio = sorg_nome_limpo(t, limpo, sizeof limpo) < 1;
      int novo = vazio ? 0 : sorg_criar_categoria(t);
      int k;
      if (vazio) {
        dizer(i18n("Escreva um nome para a coleção."));
      } else if (novo == 0) {
        dizer(i18n("Coleção cheia"));
      } else if (sorg_n_categorias() == antes) {
        dizer(i18n("Já existe uma coleção com esse nome"));
        reconstruir();
        k = listaIndiceDe(novo);
        if (k >= 0) foco = k;
      } else {
        dizer(i18n("Coleção criada"));
        reconstruir();
        // O foco acompanha a pasta nova.
        k = listaIndiceDe(novo);
        if (k >= 0) foco = k;
      }
    } else if (tecladoPara == COL_TK_RENOMEAR) {
      // Devolve 0 quando o nome ja e de OUTRA pasta (ou ficou vazio: os
      // espacos o sorg corta — distinguimos AQUI, como no criar, no padrao do
      // pluginsui); nome igual devolve 1 sem escrever. O id NAO muda ao
      // renomear.
      char limpo[SORG_NOME_MAX];
      int vazio = sorg_nome_limpo(t, limpo, sizeof limpo) < 1;
      int ok = vazio ? 0 : sorg_renomear_categoria(detCat, t);
      if (sorg_categoria_indice(detCat) < 0) {
        // A pasta sumiu por fora (perfil/sync): nenhum id pendurado.
        vista = COL_VISTA_LISTA; detN = 0; detCat = 0;
        reconstruir();
      } else if (vazio) dizer(i18n("Escreva um nome para a coleção."));
      else if (ok)     dizer(i18n("Coleção renomeada"));
      else             dizer(i18n("Já existe uma coleção com esse nome"));
    }
  }
  tecladoPara = COL_TK_NADA;
}

// --- A ilha por cima (confirmar / escolher opcao) ---------------------------
static const char *opTitulo(int k) {
  switch (k) {
    case COL_OP_ORDEM:  return "Ordenar por";
    case COL_OP_GRUPO:  return "Agrupar por";
    case COL_OP_ESTILO: return "Estilo de exibição";
    default:            return "Organizar recomendações";
  }
}

// As opcoes de cada chip, com os MESMOS rotulos do painel de Salvos (sem chave
// nova). O valor gravado e o indice.
static void popAbrirOpcao(int k) {
  static const char *ordem[SORG_ORDEM_N] = {
    "Salvos mais antigos primeiro", "Salvos mais recentes primeiro", "Nome (A–Z)",
    "Ano (mais novo primeiro)", "Nota do IMDb", "Menos tempo restante" };
  static const char *grupo[SORG_GRUPO_N] = {
    "Continuar e não começados", "Tipo: filmes, séries, coleções, canais",
    "Minhas categorias", "Sem agrupar" };
  static const char *estilo[SORG_ESTILO_N] = {
    "Lista com capa", "Grade de pôsteres", "Cartões paisagem" };
  static const char *social[SORG_SOCIAL_N] = {
    "Mais recentes primeiro", "Agrupar por pessoa" };
  const char *const *src;
  int i, n, atual;
  pop = COL_POP_OPCAO;
  popN = 0; popFoco = 0;
  snprintf(popTitulo, sizeof popTitulo, "%s", opTitulo(k));
  popSub[0] = 0;
  switch (k) {
    case COL_OP_ORDEM:  src = ordem;  n = SORG_ORDEM_N;  atual = sorg_ordem();  break;
    case COL_OP_GRUPO:  src = grupo;  n = SORG_GRUPO_N;  atual = sorg_grupo();  break;
    case COL_OP_ESTILO: src = estilo; n = SORG_ESTILO_N; atual = sorg_estilo(); break;
    default:            src = social; n = SORG_SOCIAL_N; atual = sorg_social(); break;
  }
  for (i = 0; i < n && popN < (int)(sizeof popL / sizeof popL[0]); i++) {
    popL[popN].rot = src[i];
    popL[popN].icone = NULL;
    if (i == atual) popFoco = popN;
    popN++;
  }
}

// A confirmacao de excluir: a MESMA copia do painel de Salvos (POP_EXCLUIR) —
// pergunta com o nome, a frase que diz para onde vao os titulos, e as duas
// fileiras Cancelar/Excluir. O foco NASCE em Cancelar: "Excluir nunca nasce com
// o foco" (salvospainel.c), e OK duas vezes sem ler cancela.
static void popAbrirConfirmar(void) {
  const char *nome = sorg_categoria_nome_id(detCat);
  pop = COL_POP_CONFIRMA;
  popN = 0; popFoco = 0;
  snprintf(popTitulo, sizeof popTitulo, i18n("Excluir \xe2\x80\x9c%s\xe2\x80\x9d?"),
           nome ? nome : "");
  snprintf(popSub, sizeof popSub, "%s",
           i18n("Os títulos continuam nos Salvos, só saem da categoria."));
  popL[popN].rot = i18n("Cancelar"); popL[popN].icone = NULL; popN++;
  popL[popN].rot = i18n("Excluir");  popL[popN].icone = NULL; popN++;
}

// OK numa fileira da ilha.
static void popOk(void) {
  int k = popFoco;
  if (k < 0 || k >= popN) return;
  if (pop == COL_POP_CONFIRMA) {
    if (k == 1) {
      // Os titulos continuam salvos (sorg_excluir_categoria so tira a pasta).
      sorg_excluir_categoria(detCat);
      vista = COL_VISTA_LISTA; detN = 0; detCat = 0;
      reconstruir();
      dizer(i18n("Coleção excluída"));
    }
    pop = COL_POP_NADA;
    return;
  }
  // As opcoes de exibicao escrevem SO pelos sorg_definir_*, para o painel de
  // Salvos reorganizar pela mesma revisao (sorg_revisao).
  switch (detChip) {
    case COL_OP_ORDEM:  sorg_definir_ordem(k);  break;
    case COL_OP_GRUPO:  sorg_definir_grupo(k);  break;
    case COL_OP_ESTILO: sorg_definir_estilo(k); break;
    default:            sorg_definir_social(k); break;
  }
  pop = COL_POP_NADA;
}

static void popVoltar(void) { pop = COL_POP_NADA; }

// --- Foco / navegacao --------------------------------------------------------
// Abre a pasta da fileira focada (o pseudo "Sem categoria" tambem abre: os
// titulos sem pasta sao uma pasta como qualquer outra).
static void abrirPasta(void) {
  int cat;
  if (foco < 0 || foco >= nLinhas) return;
  cat = linhas[foco].cat;
  if (!catValida(cat)) { reconstruir(); return; }
  detCat = cat;
  detReconstruir();
  vista = COL_VISTA_PASTA;
  // Comeca no TOPO: com titulos, a primeira fileira; vazia, a acao Renomear (a
  // acao destrutiva, Excluir, NUNCA nasce com o foco). Em "Sem categoria" nao
  // ha acoes: o topo vazio e a faixa de chips.
  detSel = detN > 0 ? 1 : (detTemAcoes() ? 0 : 2);
  detRow = 0; detAcao = 0; detChip = 0;
  detScroll = detVel = 0.0f;
  detEntrada = 0.0f;
  memset(detAnim, 0, sizeof detAnim);
  memset(detAcaoAnim, 0, sizeof detAcaoAnim);
  memset(detChipAnim, 0, sizeof detChipAnim);
  recadoAte = 0.0f;
}

// Volta ao gerenciamento pelo topo: o foco volta para a primeira fileira
// (nunca fica preso na fileira da pasta que acabou de fechar — que poderia nem
// existir mais) e o curso zera. E o MESMO reinicio do que `listaIniciar`, so
// sem mexer no teclado/ilha (quem chama ja os fechou).
static void voltarParaLista(void) {
  vista = COL_VISTA_LISTA; detN = 0; detCat = 0;
  foco = 0; sair = 0;
  scroll = velScroll = 0.0f;
  memset(animFoco, 0, sizeof animFoco);
  reconstruir();
}

// OK no gerenciamento: a acao do pe (foco == nLinhas) cria; a fileira abre.
static void listaOk(void) {
  if (nLinhas > 0 && foco == nLinhas) { tecladoAbrir(COL_TK_CRIAR, NULL); return; }
  abrirPasta();
}

// OK no FolderDetail: acoes do cabecalho, chips ou a fileira de titulo (que
// arma `pediu` com o indice de catalogo — a rota de abrirPorIndice, sem play).
static void pastaOk(void) {
  if (detSel == 0) {
    if (detAcao == 0) tecladoAbrir(COL_TK_RENOMEAR, sorg_categoria_nome_id(detCat));
    else popAbrirConfirmar();
    return;
  }
  if (detSel == 2) { popAbrirOpcao(detChip); return; }
  if (detRow < 0 || detRow >= detN) return;
  // M1: a fileira carrega o indice NO CATALOGO. Sem copia no catalogo
  // (catIdx < 0) nao ha o que abrir — nada acontece, sem crash.
  if (detLin[detRow].catIdx >= 0) pediu = detLin[detRow].catIdx;
}

// --- Eventos -----------------------------------------------------------------
void colecoes_ponteiro(int i, int b) {
  (void)b;
  if (sair || teclado_aberto() || pop != COL_POP_NADA) return;
  if (vista == COL_VISTA_LISTA) {
    if (i < 0 || i > nLinhas || i == foco) return;
    foco = i;
    return;
  }
  if (i < 0 || i >= detN || i == detRow) return;
  detSel = 1; detRow = i;
}

int colecoes_foco(void) { return foco; }

// ESTADO PARA TESTE/FIXTURE (ver colecoesui.h).
int colecoes_vista(void) { return vista == COL_VISTA_PASTA ? 1 : 0; }
int colecoes_cat_aberta(void) { return vista == COL_VISTA_PASTA ? detCat : -1; }
int colecoes_pop_aberto(void) { return (teclado_aberto() || pop != COL_POP_NADA) ? 1 : 0; }
int colecoes_n_categorias(void) { return nLinhas > 0 ? nLinhas - 1 : 0; }
int colecoes_n_titulos_pasta(void) { return vista == COL_VISTA_PASTA ? detN : 0; }

void colecoes_evento(const SDL_Event *e) {
  SDL_Keycode k;
  // O teclado modal tem a tela (o mesmo primeiro passo do pluginsui).
  if (teclado_aberto()) { teclado_evento(e); return; }
  if (e->type != SDL_KEYDOWN) return;
  k = e->key.keysym.sym;

  // A ilha por cima (confirmar/escolher): Voltar fecha so ela.
  if (pop != COL_POP_NADA) {
    if (k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE ||
        k == SDLK_DELETE || e->key.keysym.scancode == NV_SCANCODE_BACK) { popVoltar(); return; }
    if (k == SDLK_DOWN) { if (popFoco + 1 < popN) popFoco++; return; }
    if (k == SDLK_UP)   { if (popFoco > 0) popFoco--; return; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
      if (!e->key.repeat) popOk();
      return;
    }
    return;
  }

  if (vista == COL_VISTA_LISTA) {
    switch (k) {
      case SDLK_UP:   if (foco > 0) foco--; break;
      case SDLK_DOWN: if (foco < nLinhas) foco++; break;   // nLinhas = a acao do pe
      case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
        if (!e->key.repeat) listaOk();
        break;
      case SDLK_AC_BACK: case SDLK_ESCAPE: case SDLK_BACKSPACE: sair = 1; break;
      default: break;
    }
    if (e->key.keysym.scancode == NV_SCANCODE_BACK) sair = 1;
    return;
  }

  // FolderDetail: detSel 0 = acoes, 1 = fileiras, 2 = chips.
  switch (k) {
    case SDLK_UP:
      if (detSel == 2) detSel = detTemAcoes() ? 0 : (detN > 0 ? 1 : 2);
      else if (detSel == 1) {
        if (detRow > 0) detRow--;
        else detSel = detTemAcoes() ? 0 : 2;
      }
      break;
    case SDLK_DOWN:
      if (detSel == 0) detSel = detN > 0 ? 1 : 2;
      else if (detSel == 1) { if (detRow + 1 < detN) detRow++; else detSel = 2; }
      break;
    case SDLK_LEFT:
      if (detSel == 0) { if (detAcao > 0) detAcao--; }
      else if (detSel == 2) { if (detChip > 0) detChip--; }
      break;
    case SDLK_RIGHT:
      if (detSel == 0) { if (detAcao < 1) detAcao++; }
      else if (detSel == 2) { if (detChip + 1 < COL_OP_N) detChip++; }
      break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
      if (!e->key.repeat) pastaOk();
      break;
    case SDLK_AC_BACK: case SDLK_ESCAPE: case SDLK_BACKSPACE: voltarParaLista(); break;
    default: break;
  }
  if (e->key.keysym.scancode == NV_SCANCODE_BACK) voltarParaLista();
}

// --- Atualizar ---------------------------------------------------------------
void colecoes_atualizar(float dt, Uint32 agora) {
  int i, n;
  (void)agora;
  if (teclado_aberto()) teclado_atualizar(dt, agora);
  tecladoResultado();
  if (recadoAte > 0.0f) recadoAte -= dt;
  if (detEntrada < 1.0f) detEntrada = anim_suave(detEntrada + dt * 6.0f);

  // A lista pode mudar por fora (outro "+", painel de Salvos, sync da conta):
  // as revisoes sao o gatilho, nunca um realloc por quadro.
  if (salvos_revisao() != ultRevSalvos || cat_revisao_itens() != ultRevCat ||
      sorg_revisao() != ultRevSorg) {
    if (vista == COL_VISTA_PASTA) {
      // A categoria aberta ainda existe? Troca de perfil / sync pode te-la
      // apagado — e nenhum id pendurado: volta ao gerenciamento.
      if (sorg_categoria_indice(detCat) < 0) { voltarParaLista(); return; }
      detReconstruir();
      if (detRow >= detN) detRow = detN > 0 ? detN - 1 : 0;
      if (detSel == 1 && detN == 0) detSel = detTemAcoes() ? 0 : 2;
    } else reconstruir();
  }

  if (vista == COL_VISTA_LISTA) {
    if (foco > nLinhas) foco = nLinhas;
    n = nLinhas + 1;   // fileiras + a acao do pe
    for (i = 0; i < n && i < (int)(sizeof animFoco / sizeof animFoco[0]); i++) {
      float alvo = (i == foco) ? 1.0f : 0.0f;
      animFoco[i] = anim_mola(animFoco[i], alvo, dt,
                              alvo > 0.5f ? NV_MOLA_FOCO : NV_MOLA_DESFOCO);
    }
    if (nLinhas > 0) {
      // A janela de rolagem e o miolo do painel (entre os dois recuos), e a
      // acao do pe participa do curso como se fosse mais uma fileira.
      float win = painelAlt() - NV_FOLHA_PAD * 2.0f;
      float alt = nLinhas * COL_PASSO + 16.0f + NV_CTRL_H;
      float topo = (float)foco * COL_PASSO;
      float alvo = scroll;
      float fim = topo + (foco < nLinhas ? NV_BIB_PICK_H : NV_CTRL_H);
      if (topo < alvo) alvo = topo;
      else if (fim > alvo + win) alvo = fim - win;
      if (alvo > alt - win) alvo = alt - win;
      if (alvo < 0.0f) alvo = 0.0f;
      scroll = anim_mola2(&velScroll, scroll, alvo, dt, NV_MOLA2_SCROLL);
    } else scroll = velScroll = 0.0f;
    return;
  }

  // FolderDetail: molas das acoes, dos chips e das fileiras visiveis.
  for (i = 0; i < 2; i++) {
    float alvo = (detSel == 0 && detAcao == i) ? 1.0f : 0.0f;
    detAcaoAnim[i] = anim_mola(detAcaoAnim[i], alvo, dt,
                               alvo > 0.5f ? NV_MOLA_FOCO : NV_MOLA_DESFOCO);
  }
  for (i = 0; i < COL_OP_N; i++) {
    float alvo = (detSel == 2 && detChip == i) ? 1.0f : 0.0f;
    detChipAnim[i] = anim_mola(detChipAnim[i], alvo, dt,
                               alvo > 0.5f ? NV_MOLA_FOCO : NV_MOLA_DESFOCO);
  }
  n = detN < detCap ? detN : detCap;   // molas tem um slot por fileira alocada
  for (i = 0; i < n; i++) {
    float alvo = (detSel == 1 && detRow == i) ? 1.0f : 0.0f;
    detAnim[i] = anim_mola(detAnim[i], alvo, dt,
                           alvo > 0.5f ? NV_MOLA_FOCO : NV_MOLA_DESFOCO);
  }
  {
    float vp = detJanela();
    float alvo = detScroll;
    if (detSel == 1 && detRow >= 0 && detRow < detN) {
      float topo = detLin[detRow].y;
      if (topo < alvo) alvo = topo;
      else if (topo + COL_DET_LIN_H > alvo + vp) alvo = topo + COL_DET_LIN_H - vp;
    }
    if (alvo > detCorpoAlt - vp) alvo = detCorpoAlt - vp;
    if (alvo < 0.0f) alvo = 0.0f;
    detScroll = anim_mola2(&detVel, detScroll, alvo, dt, NV_MOLA2_SCROLL);
    if (detCorpoAlt <= vp) detScroll = detVel = 0.0f;
  }
}

// --- Desenho: cabecalho da tela ---------------------------------------------
// TITULO NA COLUNA DE CONTEUDO do app: ajustes_conteudo_x() (104 com a rail
// recolhida, a mesma familia dos 96 da Biblioteca). O subtitulo e uma chave
// PROPRIA ("Organize seus títulos salvos em pastas."), como em P7.1.
static void desenhaCabecalhoTela(void) {
  float x = ajustes_conteudo_x();
  TxtLinha tit = txt_linha(TXT_AJ_TIT28, i18n("Coleções"), 243, 242, 239, 255);
  TxtLinha sub = txt_linha_corta(TXT_AJ_ESTADO,
                                 i18n("Organize seus títulos salvos em pastas."),
                                 243, 242, 239, 255, NV_TELA_W - 60 - x - tit.w - 18);
  txt_desenhar(tit, x, 64 - tit.h * 0.5f);
  txt_desenhar_alpha(sub, x + tit.w + 18, 64 - sub.h * 0.5f, 0.62f);
}

// ESTADO VAZIO: a mesma forma da Biblioteca — icone grande, frase branca e a
// dica do proximo passo, centrados na largura util. As tres frases sao CHAVES
// de idioma_tab.h (reuso): as duas de P7.1 vazavam em portugues na UI em ingles.
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

// --- Desenho: o gerenciamento -----------------------------------------------
static void desenhaLista(Uint32 agora) {
  GfxRect p;
  int i;
  (void)agora;
  p = (GfxRect){ colX0(), COL_Y0, NV_FOLHA_PAD * 2.0f + NV_BIB_PICK_W, painelAlt() };
  ctx_menu_ilha(p, NV_FOLHA_RAIO, 1.0f);
  // As fileiras dentro do recorte do painel: com o teto de altura a rolagem
  // esconde o que passa da borda, e o ponteiro nao alcanca o pedaco cortado.
  gfx_recorte(p.x, p.y, p.w, p.h);
  for (i = 0; i < nLinhas; i++) {
    GfxRect r = { p.x + NV_FOLHA_PAD,
                  p.y + NV_FOLHA_PAD + (float)i * COL_PASSO - scroll,
                  NV_BIB_PICK_W, NV_BIB_PICK_H };
    float k = animFoco[i];
    TxtLinha cont, nome;
    char txt[64];
    if (r.y + r.h < p.y || r.y > p.y + p.h) continue;   // fora da janela
    focoFileira(r, NV_BIB_PICK_RAIO, k);
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
  // A FILEIRA DE ACAO "Nova coleção", no pe do painel: um botao de NV_CTRL_H
  // (nao uma fileira de pasta) — a pilula cheia do acento no foco diz que ela
  // FAZ algo, e nao que ela abre uma pasta.
  {
    GfxRect a = { p.x + NV_FOLHA_PAD,
                  p.y + NV_FOLHA_PAD + (float)nLinhas * COL_PASSO + 16.0f - scroll,
                  NV_BIB_PICK_W, NV_CTRL_H };
    float k = animFoco[nLinhas];
    int tf = ajustes_acento_tinta(NULL, NULL, NULL);
    TxtLinha t = txt_linha(TXT_G30M, i18n("Nova coleção"), tf, tf, tf, 255);
    if (a.y + a.h >= p.y && a.y <= p.y + p.h) {
      focoBotao(a, a.h * 0.5f, k, 1.0f);
      txt_desenhar_alpha(t, a.x + NV_BIB_PICK_PADX,
                         a.y + (a.h - t.h) * 0.5f, 1.0f);
    }
  }
  gfx_sem_recorte();
  for (i = 0; i < nLinhas; i++) {
    float ry = p.y + NV_FOLHA_PAD + (float)i * COL_PASSO - scroll;
    if (ry + NV_BIB_PICK_H < p.y || ry > p.y + p.h) continue;
    ponteiro_alvo(p.x + NV_FOLHA_PAD, ry, NV_BIB_PICK_W, NV_BIB_PICK_H,
                  colecoes_ponteiro, NULL, i, 0);
  }
  {
    float ry = p.y + NV_FOLHA_PAD + (float)nLinhas * COL_PASSO + 16.0f - scroll;
    if (ry + NV_CTRL_H >= p.y && ry <= p.y + p.h)
      ponteiro_alvo(p.x + NV_FOLHA_PAD, ry, NV_BIB_PICK_W, NV_CTRL_H,
                    colecoes_ponteiro, NULL, nLinhas, 0);
  }
}

// --- Desenho: o FolderDetail -------------------------------------------------
// Uma fileira de TITULO: a mesma gramatica da linha de lista da Biblioteca —
// capa 96x144 a esquerda, titulo em TXT_HEADLINE e a meta em TXT_CAPTION2. Sem
// barra de retomada (a lista e de organizacao, nao de progresso) e sem nota.
static void desenhaTitulo(const ColTit *d, GfxRect r, float f, float a) {
  float v = focoSuave(f);
  int tf = ajustes_acento_tinta(NULL, NULL, NULL);
  int c = (int)(243.0f + ((float)tf - 243.0f) * (v > 0.5f ? 1.0f : 0.0f));
  float yc = r.y + r.h * 0.5f;
  GfxRect mini = { r.x + COL_DET_MINI_PAD, yc - COL_DET_MINI_H * 0.5f,
                   COL_DET_MINI_W, COL_DET_MINI_H };
  const char *arte = d->poster[0] ? d->poster : NULL;
  GLuint tex = arte ? tex_obter_larg(arte, COL_DET_MINI_W) : 0;
  float raioMini = 8.0f / COL_DET_MINI_H;
  focoFileira(r, NV_BIB_PICK_RAIO, f);
  if (tex) {
    gfx_tex_aspect_atual = tex_aspecto(arte);
    gfx_rect(mini, tex, GFX_CARD, 0.0f, 0.0f, 0.0f, raioMini, 0, 0, 0, a);
    gfx_tex_aspect_atual = 0.0f;
  } else
    gfx_cor(mini, raioMini, NV_COR_ESQUELETO_R, NV_COR_ESQUELETO_G,
            NV_COR_ESQUELETO_B, a);
  { float tx = mini.x + mini.w + COL_DET_TX_GAP;
    float larg = r.x + r.w - 28.0f - tx;
    char meta[96];
    TxtLinha t, m;
    if (larg < 80.0f) larg = 80.0f;
    // A meta repete o estado que agrupa a fileira: "Continuar" vs "Na lista".
    snprintf(meta, sizeof meta, "%s", i18n(d->prog > 0 ? "Continuar" : "Na lista"));
    t = txt_linha_corta(TXT_HEADLINE, d->titulo[0] ? d->titulo : d->id, c, c, c, 255, larg);
    m = txt_linha_corta(TXT_CAPTION2, meta, 150, 153, 162, 255, larg);
    { float hb = (float)t.h + 7.0f + (float)m.h;
      float ty = yc - hb * 0.5f;
      txt_desenhar_alpha(t, tx, ty, a);
      txt_desenhar_alpha(m, tx, ty + (float)t.h + 7.0f, a * 0.95f); } }
}

// A faixa de chips do editor (Ordenar/Agrupar/Estilo/Recomendações): a mesma
// receita do painel de Salvos — caixa alta pequena por cima, valor em 19 —, com
// o rotulo curto do valor atual.
static void chipTextos(int k, const char **cap, const char **val) {
  static const char *ORDEM_CURTO[SORG_ORDEM_N] = {
    "Mais antigos", "Recentes", "Nome", "Ano", "Nota", "Restante" };
  static const char *GRUPO_CURTO[SORG_GRUPO_N] = {
    "Progresso", "Tipo", "Categoria", "Nenhum" };
  static const char *ESTILO_CURTO[SORG_ESTILO_N] = { "Lista", "Grade", "Paisagem" };
  static const char *SOCIAL_CURTO[SORG_SOCIAL_N] = { "Recentes", "Por pessoa" };
  switch (k) {
    case COL_OP_ORDEM:  *cap = "Ordenar"; *val = ORDEM_CURTO[sorg_ordem()]; break;
    case COL_OP_GRUPO:  *cap = "Agrupar"; *val = GRUPO_CURTO[sorg_grupo()]; break;
    case COL_OP_ESTILO: *cap = "Estilo";  *val = ESTILO_CURTO[sorg_estilo()]; break;
    default:            *cap = "Organizar"; *val = SOCIAL_CURTO[sorg_social()]; break;
  }
}

static void desenhaPasta(Uint32 agora) {
  GfxRect p;
  float x, y;
  int i;
  (void)agora;
  p = (GfxRect){ colX0(), COL_Y0, NV_FOLHA_PAD * 2.0f + NV_BIB_PICK_W, painelAltDet() };
  ctx_menu_ilha(p, NV_FOLHA_RAIO, 1.0f);
  x = p.x + NV_FOLHA_PAD;
  y = p.y + NV_FOLHA_PAD + (1.0f - detEntrada) * 24.0f;

  // CABECALHO: nome da pasta (TXT_TITULO2) e contagem (TXT_G19M).
  {
    const char *nome = (detCat == 0) ? i18n("Sem categoria")
                                     : sorg_categoria_nome_id(detCat);
    char cont[64];
    TxtLinha t, c;
    snprintf(cont, sizeof cont, "%d %s", detN, i18n(detN == 1 ? "título" : "títulos"));
    t = txt_linha_corta(TXT_TITULO2, nome ? nome : "?", 255, 255, 255, 255,
                        NV_BIB_PICK_W - 40.0f);
    c = txt_linha(TXT_G19M, cont, 128, 128, 128, 255);
    txt_desenhar_alpha(t, x, y, 1.0f);
    txt_desenhar_alpha(c, x + (float)t.w + 16.0f, y + (float)t.h - (float)c.h - 4.0f, 1.0f);
  }
  // ACOES: Renomear / Excluir, em pilulas de NV_CTRL_H. O foco e um so (o
  // par); LEFT/RIGHT troca de qual acao. "Sem categoria" nao tem acoes (ver
  // detTemAcoes): o cabecalho acaba na contagem e os chips sobem.
  if (detTemAcoes()) {
    float ax = x;
    const char *rot[2] = { "Renomear", "Excluir categoria" };
    for (i = 0; i < 2; i++) {
      TxtLinha t = txt_linha(TXT_G19M, i18n(rot[i]), 243, 242, 239, 255);
      GfxRect r = { ax, y + COL_DET_ACOES_Y, (float)t.w + 48.0f, NV_CTRL_H };
      float f = detAcaoAnim[i];
      int tf = ajustes_acento_tinta(NULL, NULL, NULL);
      TxtLinha tr = txt_linha(TXT_G19M, i18n(rot[i]), tf, tf, tf, 255);
      focoBotao(r, NV_CTRL_H * 0.5f, f, 1.0f);
      txt_desenhar_alpha(f > 0.5f ? tr : t, r.x + 24.0f, r.y + (r.h - t.h) * 0.5f, 1.0f);
      ax += r.w + NV_CTRL_VAO;
    }
  }
  // CHIPS: as quatro opcoes de exibicao. Foco = pilula cheia no acento.
  {
    float cx = x, cy = y + COL_DET_CHIPS_Y;
    for (i = 0; i < COL_OP_N; i++) {
      const char *cap, *val;
      char up[64];
      size_t k;
      float cw, w;
      TxtLinha vr, vf;
      GfxRect r;
      int tf = ajustes_acento_tinta(NULL, NULL, NULL);
      chipTextos(i, &cap, &val);
      vr = txt_linha(TXT_ILHA_SEG, i18n(val), 243, 242, 239, 255);
      vf = txt_linha(TXT_ILHA_SEG, i18n(val), tf, tf, tf, 255);
      // Caixa alta pequena do rotulo (o mesmo caixaAltaIlha do painel), medida
      // antes para a pilula abracar o mais largo.
      snprintf(up, sizeof up, "%s", i18n(cap));
      for (k = 0; up[k]; k++)
        if (up[k] >= 'a' && up[k] <= 'z') up[k] = (char)(up[k] - 32);
      cw = txt_tracking(TXT_MINI, up, 255, 255, 255, -1.0f, 0.0f, 1.0f, 2.0f);
      w = (float)vr.w > cw ? (float)vr.w : cw;
      w += 44.0f;
      r = (GfxRect){ cx, cy, w, NV_CTRL_H };
      focoBotao(r, NV_CTRL_H * 0.5f, detChipAnim[i], 1.0f);
      txt_tracking(TXT_MINI, up, 255, 255, 255, cx + 22.0f,
                   cy + 12.0f, 0.45f + 0.35f * focoSuave(detChipAnim[i]), 2.0f);
      txt_desenhar_alpha(focoSuave(detChipAnim[i]) > 0.5f ? vf : vr,
                         cx + 22.0f, cy + 28.0f, 1.0f);
      cx += w + NV_CTRL_VAO;
    }
  }

  // CORPO: as secoes empilhadas, com o recorte do painel e a rolagem. O corpo
  // nasce do MESMO `y` do cabecalho (com o deslocamento de entrada), para a
  // folha inteira deslizar como uma peca so.
  {
    float vp = detJanela();
    float corpoTop = y + COL_DET_CORPO_Y;
    gfx_recorte(p.x, corpoTop, p.w, vp);
    for (i = 0; i < detSecN; i++) {
      float sy = corpoTop + detSec[i].y - detScroll;
      if (sy + NV_DETF_CAB_H >= corpoTop && sy <= corpoTop + vp) {
        TxtLinha t = txt_linha_corta(TXT_HEADLINE, i18n(detSec[i].rot),
                                     243, 242, 239, 255, NV_BIB_PICK_W);
        txt_desenhar_alpha(t, x, sy + (NV_DETF_CAB_H - t.h) * 0.5f, 0.92f);
      }
    }
    for (i = 0; i < detN; i++) {
      GfxRect r = { x, corpoTop + detLin[i].y - detScroll,
                    NV_BIB_PICK_W, COL_DET_LIN_H };
      if (r.y + r.h < corpoTop || r.y > corpoTop + vp) continue;
      desenhaTitulo(&detLin[i], r, detAnim[i], 1.0f);
    }
    // COLECAO VAZIA: a mesma leitura da secao vazia do painel de Salvos — uma
    // frase no lugar das fileiras, sem crash.
    if (detN == 0) {
      TxtLinha t = txt_linha_corta(TXT_CALLOUT,
                       i18n("Nada nesta coleção ainda."), 150, 153, 162, 255,
                       NV_BIB_PICK_W);
      txt_desenhar_alpha(t, x, corpoTop + 8.0f, 0.9f);
    }
    gfx_sem_recorte();
  }

  // RODAPE: a dica de navegacao, no pe do painel.
  {
    TxtLinha t = txt_linha(TXT_CAPTION2,
                    i18n("↑ ↓ navegam, ← → trocam, OK escolhe, Voltar volta."),
                    150, 153, 162, 255);
    txt_desenhar_alpha(t, x, p.y + p.h - NV_FOLHA_PAD * 0.5f - (float)t.h, 0.6f);
  }

  // PONTEIRO: so as fileiras do corpo (as acoes/chips exigem D-pad, como no
  // painel de Salvos).
  {
    float vp = detJanela();
    float corpoTop = y + COL_DET_CORPO_Y;
    for (i = 0; i < detN; i++) {
      float ry = corpoTop + detLin[i].y - detScroll;
      if (ry + COL_DET_LIN_H < corpoTop || ry > corpoTop + vp) continue;
      ponteiro_alvo(x, ry, NV_BIB_PICK_W, COL_DET_LIN_H, colecoes_ponteiro, NULL, i, 0);
    }
  }
}

// --- Desenho: a ilha por cima (confirmar / escolher) ------------------------
static void desenhaPop(float a) {
  GfxRect m;
  float w, h, x, y;
  int i;
  float focoL[8];
  if (pop == COL_POP_NADA) return;
  ponteiro_camada();   // a ilha aberta tem o teclado: quem esta atras nao vale
  ctx_menu_veu(a);
  w = ctx_menu_largura(popL, popN);
  h = ctx_menu_altura(popN);
  x = (NV_TELA_W - w) * 0.5f;
  y = (NV_TELA_H - h) * 0.5f;
  for (i = 0; i < popN && i < 8; i++) focoL[i] = (i == popFoco) ? 1.0f : 0.0f;
  m = (GfxRect){ x, y, w, h };
  ctx_menu_desenhar(m, popTitulo, popSub[0] ? popSub : NULL, 0.62f,
                    popL, popN, focoL, 1.0f, a, NULL);
}

// O aviso curto (toast): uma linha no topo do painel, por 4 s.
static void desenhaRecado(void) {
  if (recadoAte <= 0.0f || !recado[0]) return;
  { float a = recadoAte > 3.0f ? (4.0f - recadoAte) : (recadoAte < 0.4f ? recadoAte / 0.4f : 1.0f);
    GfxRect r = { colX0(), COL_Y0 - 4.0f, NV_FOLHA_PAD * 2.0f + NV_BIB_PICK_W, NV_CTRL_H };
    TxtLinha t = txt_linha(TXT_G19M, recado, 243, 242, 239, 255);
    ctx_menu_ilha(r, NV_CTRL_H * 0.5f, 0.92f * a);
    txt_desenhar_alpha(t, r.x + 24.0f, r.y + (r.h - t.h) * 0.5f, a); }
}

void colecoes_desenhar(Uint32 agora) {
  ajustes_ui_fundo();
  desenhaCabecalhoTela();
  if (vista == COL_VISTA_PASTA) desenhaPasta(agora);
  else if (nLinhas == 0) desenhaVazio();
  else desenhaLista(agora);
  desenhaRecado();
  desenhaPop(1.0f);
  if (teclado_aberto()) teclado_desenhar(agora);
}
