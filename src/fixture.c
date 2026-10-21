// EVIDENCE-Fixture Goal-C. EVERYTHING here compiles only under -DNV_FIXTURE
// (see fixture.h). No release script defines it; without it this file is an
// empty translation unit and the app behaves byte-for-byte as before.
#ifdef NV_FIXTURE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "fixture.h"
#include "app.h"
#include "catalogo.h"
#include "home.h"
#include "detail.h"
#include "streams.h"
#include "addons.h"
#include "player.h"
#include "video.h"
#include "salvos.h"
#include "salvosorg.h"
#include "colecoesui.h"
#include "teclado.h"
#include "posplay.h"
#include "layout.h"
#include "esmaecer.h"
#include "menu.h"
// First-run cards: closed-loop dismissal needs to SEE them. One _aberto()
// query each; the .c files already exist, this only includes their headers.
#include "novidades.h"
#include "novidades11.h"
#include "novidades12.h"
#include "novidades13.h"
#include "novidades131.h"
#include "novidades1312.h"
#include "novidades132.h"
#include "novidades133.h"
#include "novidades134.h"
#include "novidades139.h"
#include "novidades142.h"
#include "novidades148.h"
#include "novidades151.h"
#include "novidades160.h"
#include "novidades170.h"
#include "novidades172.h"
#include "novidades174.h"
#include "novidades180.h"
#include "novidades20.h"
#include "novidades201.h"
#include "novidades202.h"
#include "novidades_cartao.h"
#include "registro.h"
#include "salvosintro.h"
#include "diagnostico.h"
#include "pipintro.h"
#include "atualizacao.h"
#include "avisos.h"
#include "telemetria.h"
#include "recintro.h"
#include "agendaviso.h"
#ifdef NV_SHOT_HOOKS
#include "extras.h"
#endif

#define FX_CMD "/tmp/nuvio-fixture"
#define FX_SEED_PADRAO "tools/parity-fixtures/titulos.tsv"
#define FX_MAX_ITENS 16
#define FX_N_SALVOS 6

typedef struct {
  char imdb[64], tipo[8], titulo[160], ano[8], dur[24], genero[160], sinopse[900];
  int nota;
} FxSemente;

static int ativo = 0;
static unsigned quadro = 0;
static FxSemente sementes[FX_MAX_ITENS];
static int nSementes = 0;
static int injetado = 0, reinjeta = 0, quadroInjetou = 0;

// --- sequencer ---------------------------------------------------------------
typedef enum { ST_PARADO, ST_FECHAR, ST_CARTOES, ST_TOPO, ST_ACAO, ST_ESPERA, ST_SETTLE } StFase;
static StFase fase = ST_PARADO;
static char telaAlvo[24];
static int detalheIdx = 0, passo = 0, espera = 0, tentativas = 0;
static int telaPedida = -1; // consumed by app.c via fixture_tela_pedida()
static Uint32 tEstado = 0;   // instante da entrada na fase (timeouts em ms)
static Uint32 tProntaEm = 0; // settle: parede de relogio (fades em ms)
static unsigned qProntaEm = 0; // settle: 20 quadros (molas andam 100ms/quadro, ver main.c)

int fixture_tela_pedida(void) {
  int r = telaPedida;
  telaPedida = -1;
  return r;
}

static void flog(const char *msg) {
  printf("fixture: %s\n", msg);
  fflush(stdout);
}

// A synthetic D-pad press, identical to what /tmp/nuvio-key injection feeds
// app_evento (main.c teclasInjetadas sets only .sym too).
static void tecla(SDL_Keycode k) {
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_KEYDOWN; e.key.keysym.sym = k; app_evento(&e);
  e.type = SDL_KEYUP; app_evento(&e);
}

// P7.2a: os estados do editor podem rodar DEPOIS de um estado que deixou o
// teclado, a ilha, o detalhe ou o PROPRIO FolderDetail abertos (as capturas
// rodam em sequencia no mesmo app). Voltar fecha a camada de cima conforme ela
// aparece — cancelar teclado e fechar confirmar nao escrevem nada, e Voltar de
// uma pasta volta ao gerenciamento — e devolve 1 quando a tela esta limpa: no
// gerenciamento, sem nada por cima.
static int fecharSobra(void) {
  if (detail_aberto() || colecoes_pop_aberto() || colecoes_vista() == 1) {
    tecla(SDLK_AC_BACK);
    return 0;
  }
  return 1;
}

// P7.2a: CAMINHADA DE TECLAS do teclado da TV (a modal do nome so aceita
// D-pad: cada caractere e uma caminhada ate a celula e um OK). Para o
// alfabeto de nome do editor (COL_ALFA_NOME, 38 celulas, 6 colunas por
// fileira) a grade fica:
//   f0 a-f | f1 g-l | f2 m-r | f3 s-x | f4 y-3 | f5 4-9 | f6 ' ' '-' |
//   f7 apagar(0) / limpar(1) / Concluir(2)
// A caminhada digita "kids" a partir do foco (0,0) — a primeira maiuscula
// ("Kids") vem da normalizacao do sorg. O teclado e deterministico: o OK numa
// tecla de caractere TYPE e o foco fica na celula.
static const SDL_Keycode COL_TECLAS_KIDS[] = {
  // k = f1c4
  SDLK_DOWN, SDLK_RIGHT, SDLK_RIGHT, SDLK_RIGHT, SDLK_RIGHT, SDLK_RETURN,
  // i = f1c2
  SDLK_LEFT, SDLK_LEFT, SDLK_RETURN,
  // d = f0c3
  SDLK_UP, SDLK_RIGHT, SDLK_RETURN,
  // s = f3c0
  SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_LEFT, SDLK_LEFT, SDLK_LEFT, SDLK_RETURN,
  // Concluir = f7c2 (o OK com o campo vazio nao confirma; "kids" esta no campo)
  SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_RIGHT, SDLK_RIGHT, SDLK_RETURN,
};
#define COL_TECLAS_KIDS_N (int)(sizeof COL_TECLAS_KIDS / sizeof COL_TECLAS_KIDS[0])

// O PREFACIO do renomear: desce ate a fileira de acoes (f7), limpar (coluna 1)
// esvazia o campo que nasceu com o nome atual, e sobe de volta a (0,0) — de
// onde COL_TECLAS_KIDS segue igual.
static const SDL_Keycode COL_TECLAS_LIMPA[] = {
  SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_DOWN,
  SDLK_RIGHT, SDLK_RETURN,
  SDLK_UP, SDLK_UP, SDLK_UP, SDLK_UP, SDLK_UP, SDLK_UP, SDLK_UP,
};
#define COL_TECLAS_LIMPA_N (int)(sizeof COL_TECLAS_LIMPA / sizeof COL_TECLAS_LIMPA[0])

static int qualquer_cartao(void) {
  return novidades_aberto() || novidades11_aberto() || novidades12_aberto()
      || novidades13_aberto() || novidades131_aberto() || novidades1312_aberto()
      || novidades132_aberto() || novidades133_aberto() || novidades134_aberto()
      || novidades139_aberto() || novidades142_aberto() || novidades148_aberto()
      || novidades151_aberto() || novidades160_aberto() || novidades170_aberto()
      || novidades172_aberto() || novidades174_aberto() || novidades180_aberto()
      || novidades20_aberto() || novidades201_aberto() || novidades202_aberto()
      || novcartao_aberto() || registro_aberto() || sintro_aberto()
      || diagnostico_intro_aberto() || pipintro_aberto()
      || atualizacao_aberta() || avisos_aberto() || avisos_cartao_aberto()
      || telemetria_aberto() || recintro_aberto() || agendaviso_aberto();
}

static int estado_base(void) {
  return !strcmp(app_tela_nome(), "home") && !detail_aberto() && !menu_aberto()
      && !stream_folha_aberta() && !player_aberto();
}

// --- seed --------------------------------------------------------------------
static void carregar_seed(void) {
  const char *caminho = getenv("NUVIO_FIXTURE_SEED");
  FILE *f;
  char linha[2048];
  if (!caminho || !*caminho) caminho = FX_SEED_PADRAO;
  f = fopen(caminho, "r");
  if (!f) {
    printf("fixture: ERRO sem seed (%s)\n", caminho);
    fflush(stdout);
    return;
  }
  nSementes = 0;
  while (fgets(linha, sizeof linha, f) && nSementes < FX_MAX_ITENS) {
    FxSemente *s = &sementes[nSementes];
    char *c[8];
    int i;
    size_t n = strlen(linha);
    while (n && (linha[n - 1] == '\n' || linha[n - 1] == '\r')) linha[--n] = 0;
    if (!linha[0] || linha[0] == '#') continue;
    c[0] = linha;
    for (i = 1; i < 8; i++) {
      char *t = strchr(c[i - 1], '\t');
      if (!t) break;
      *t = 0;
      c[i] = t + 1;
    }
    if (i < 8) continue;
    snprintf(s->imdb, sizeof s->imdb, "%s", c[0]);
    snprintf(s->tipo, sizeof s->tipo, "%s", c[1]);
    snprintf(s->titulo, sizeof s->titulo, "%s", c[2]);
    snprintf(s->ano, sizeof s->ano, "%s", c[3]);
    snprintf(s->dur, sizeof s->dur, "%s", c[4]);
    snprintf(s->genero, sizeof s->genero, "%s", c[5]);
    s->nota = atoi(c[6]);
    snprintf(s->sinopse, sizeof s->sinopse, "%s", c[7]);
    nSementes++;
  }
  fclose(f);
  printf("fixture: seed %d titulos (%s)\n", nSementes, caminho);
  fflush(stdout);
}

static void injetar(void) {
  CatItem itens[FX_MAX_ITENS];
  CatFileira fils[2];
  int i, metade;
  if (!nSementes) { flog("ERRO injecao sem seed"); return; }
  memset(itens, 0, sizeof itens);
  for (i = 0; i < nSementes; i++) {
    CatItem *c = &itens[i];
    const FxSemente *s = &sementes[i];
    snprintf(c->imdb, sizeof c->imdb, "%s", s->imdb);
    snprintf(c->tipo, sizeof c->tipo, "%s", s->tipo);
    snprintf(c->titulo, sizeof c->titulo, "%s", s->titulo);
    snprintf(c->genero, sizeof c->genero, "%s", s->genero);
    snprintf(c->meta, sizeof c->meta, "%s · %s", s->ano, s->dur);
    snprintf(c->classificacao, sizeof c->classificacao, "12");
    snprintf(c->sinopse, sizeof c->sinopse, "%s", s->sinopse);
    c->nota = s->nota;
    // poster/backdrop/logo vazios de proposito: o shot prova layout e texto
    // com o placeholder, sem depender da rede para arte.
  }
  memset(fils, 0, sizeof fils);
  metade = nSementes / 2;
  snprintf(fils[0].chave, sizeof fils[0].chave, "fixture_em_alta");
  snprintf(fils[0].titulo, sizeof fils[0].titulo, "Em alta agora");
  snprintf(fils[0].tipo, sizeof fils[0].tipo, "movie");
  fils[0].ini = 0; fils[0].n = metade; fils[0].estado = 0;
  snprintf(fils[1].chave, sizeof fils[1].chave, "fixture_populares");
  snprintf(fils[1].titulo, sizeof fils[1].titulo, "Filmes populares");
  snprintf(fils[1].tipo, sizeof fils[1].tipo, "movie");
  fils[1].ini = metade; fils[1].n = nSementes - metade; fils[1].estado = 0;
  cat_definir_tudo(itens, nSementes, fils, 2);
  for (i = 0; i < FX_N_SALVOS && i < nSementes; i++) {
    const CatItem *c = cat_item(i);
    if (c) salvos_definir(c, 1);
  }
  salvos_aplicar_catalogo();
  injetado = 1;
  quadroInjetou = (int)quadro;
  printf("fixture: injeta catalogo=%d fileiras=2 salvos=%d\n", nSementes, FX_N_SALVOS);
  fflush(stdout);
}

// --- actions -----------------------------------------------------------------
static void abrir_detalhe(int idx) {
  HomeItem hi;
  const CatItem *c = cat_item(idx);
  memset(&hi, 0, sizeof hi);
  hi.indice = idx;
  // Rect central de poster: a animacao de voo parte daqui (sem ele, do 0,0).
  hi.rect.x = NV_TELA_W * 0.5f - 124.0f;
  hi.rect.y = NV_TELA_H * 0.5f - 186.0f;
  hi.rect.w = 248.0f;
  hi.rect.h = 372.0f;
  hi.titulo = c ? c->titulo : "Fixture";
  hi.genero = c ? c->genero : "";
  hi.meta = c ? c->meta : "";
  hi.arte = NULL;
  detail_abrir(&hi);
}

static void injetar_streams(void) {
  Stream l[4];
  memset(l, 0, sizeof l);
  snprintf(l[0].rotulo, sizeof l[0].rotulo, "Fixture 4K Remux");
  snprintf(l[0].provedor, sizeof l[0].provedor, "FixtureSeed");
  snprintf(l[0].url, sizeof l[0].url, "https://fixture.invalid/matrix-2160p.mp4");
  l[0].altura = 2160; l[0].tamanhoMB = 18240; l[0].mp4 = 1;
  snprintf(l[1].rotulo, sizeof l[1].rotulo, "Fixture 1080p BluRay");
  snprintf(l[1].provedor, sizeof l[1].provedor, "FixtureSeed");
  snprintf(l[1].url, sizeof l[1].url, "https://fixture.invalid/matrix-1080p.mp4");
  l[1].altura = 1080; l[1].tamanhoMB = 6840; l[1].mp4 = 1;
  snprintf(l[2].rotulo, sizeof l[2].rotulo, "Fixture 720p WEB");
  snprintf(l[2].provedor, sizeof l[2].provedor, "FixtureSeed");
  snprintf(l[2].url, sizeof l[2].url, "https://fixture.invalid/matrix-720p.mp4");
  l[2].altura = 720; l[2].tamanhoMB = 2310; l[2].mp4 = 1;
  snprintf(l[3].rotulo, sizeof l[3].rotulo, "Fixture 480p");
  snprintf(l[3].provedor, sizeof l[3].provedor, "FixtureSeed");
  snprintf(l[3].url, sizeof l[3].url, "https://fixture.invalid/matrix-480p.mp4");
  l[3].altura = 480; l[3].tamanhoMB = 890; l[3].mp4 = 1;
  stream_definir_lista(l, 4);
  stream_folha_nome("The Matrix");
  stream_folha_item(0);
}

static void sim_aplicar(double pos) {
  VideoSimulacao s;
  memset(&s, 0, sizeof s);
  s.aceita = 1;
  s.tocando = 1;
  s.pronto = 1;
  s.pos = pos;
  s.duracao = 7320.0;
  s.bufferFim = 7320.0;
  s.largura = 1920;
  s.altura = 1080;
  video_simular(&s);
}

static void injetar_relacionados(void) {
#ifdef NV_SHOT_HOOKS
  static const char *t[] = { "A Origem", "Interestelar", "Duna: Parte 2", "Whiplash" };
  static const char *a[] = { "2010", "2014", "2024", "2014" };
  static const char *p[] = { "", "", "", "" };
  extras_shot_relacionados(t, a, p, 4);
#else
  printf("fixture: AVISO relacionados exigem NV_SHOT_HOOKS\n");
  fflush(stdout);
#endif
}

// --- command + sequencer -------------------------------------------------------
static void consumir_comando(void) {
  FILE *f = fopen(FX_CMD, "r");
  char linha[128];
  char *arg;
  if (!f) return;
  if (!fgets(linha, sizeof linha, f)) { fclose(f); return; }
  fclose(f);
  f = fopen(FX_CMD, "w"); // consome truncando (mesma disciplina do /tmp do app)
  if (f) fclose(f);
  linha[strcspn(linha, "\r\n")] = 0;
  if (strncmp(linha, "screen:", 7)) {
    printf("fixture: comando ignorado '%s'\n", linha);
    fflush(stdout);
    return;
  }
  arg = strchr(linha + 7, ' ');
  if (arg) *arg++ = 0;
  if (!strcmp(linha + 7, "detail") && arg) detalheIdx = atoi(arg);
  else detalheIdx = 0;
  snprintf(telaAlvo, sizeof telaAlvo, "%s", linha + 7);
  fase = ST_FECHAR;
  passo = 0;
  tentativas = 0;
  tEstado = SDL_GetTicks();
  printf("fixture: comando %s\n", telaAlvo);
  fflush(stdout);
}

static void pronta(void) {
  printf("fixture: pronta %s\n", telaAlvo);
  fflush(stdout);
  fase = ST_PARADO;
}

static void falha(const char *motivo) {
  printf("fixture: falha %s (%s)\n", telaAlvo, motivo);
  fflush(stdout);
  fase = ST_PARADO;
}

void fixture_passo(void) {
  quadro++;
  if (!ativo) {
    const char *e = getenv("NUVIO_FIXTURE");
    if (e && !strcmp(e, "1")) {
      ativo = 1;
      flog("ativa (NUVIO_FIXTURE=1)");
      carregar_seed();
    } else {
      return;
    }
  }
  // Sessao assistida: zera o relogio de descanso a cada quadro, como faz a
  // tecla injetada (main.c). Sem isto a vitrine do relogio cobre a tela no
  // meio da passada — a 0,6 fps os 2 min de idle chegam antes da 3a tela.
  esmaecer_entrada(SDL_GetTicks(), 0);
  // Vigia anti-cobertura: cartao assincrono durante a janela de captura.
  // So age com cartao em cena (a tecla vai para ele); com teto, para nao
  // brigar com um cartao que o Voltar nao fecha.
  { static int vigiaSeguidas = 0;
  if (fase != ST_FECHAR && fase != ST_CARTOES && qualquer_cartao()) {
    if (vigiaSeguidas < 12) {
      vigiaSeguidas++;
      printf("fixture: dispensa async (%d)\n", vigiaSeguidas);
      fflush(stdout);
      tecla(vigiaSeguidas % 4 == 3 ? SDLK_RETURN : SDLK_AC_BACK);
    }
    return;
  }
  vigiaSeguidas = 0;
  }
  // Linha de vida do catalogo: a descoberta (sem addons no preview) nao deve
  // republicar por cima, mas se as fileiras sumirem, repoe sem brigar.
  if (!injetado && quadro >= 30 && nSementes) {
    injetar();
  } else if (injetado && cat_n_fileiras() == 0 && reinjeta < 5
             && (int)quadro - quadroInjetou > 120) {
    reinjeta++;
    printf("fixture: repoe fileiras (tentativa %d)\n", reinjeta);
    fflush(stdout);
    injetar();
  }
  if (fase == ST_PARADO) {
    if (injetado) consumir_comando();
    return;
  }
  switch (fase) {
  case ST_FECHAR:
    if (estado_base()) { fase = ST_CARTOES; tentativas = 0; tEstado = SDL_GetTicks(); break; }
    (void)tentativas;
    if (SDL_GetTicks() - tEstado > 25000) {
      printf("fixture: AVISO fechar desistiu em %s\n", app_tela_nome());
      fflush(stdout);
      fase = ST_CARTOES; tentativas = 0; break;
    }
    tecla(SDLK_AC_BACK);
    break;
  case ST_CARTOES:
    if (!qualquer_cartao()) { fase = ST_TOPO; tentativas = 0; tEstado = SDL_GetTicks(); break; }
    if (SDL_GetTicks() - tEstado > 40000) {
      flog("AVISO cartao nao dispensou, segue mesmo assim");
      fase = ST_TOPO; tentativas = 0; break;
    }
    // Voltar dispensa a maioria; alguns cartoes de tutorial so andam com OK.
    tecla(tentativas % 4 == 3 ? SDLK_RETURN : SDLK_AC_BACK);
    break;
  case ST_TOPO: {
    int i;
    home_ir_topo();
    for (i = 0; i < FX_N_SALVOS && i < nSementes; i++) {
      const CatItem *rc = cat_item(i);
      if (rc) salvos_definir(rc, 1);
    }
    salvos_aplicar_catalogo();
    fase = ST_ACAO;
    passo = 0;
    espera = 0;
    break;
  }

  case ST_ACAO:
    if (!strcmp(telaAlvo, "home")) {
      if (!estado_base()) { falha("sem estado base"); break; }
      tProntaEm = SDL_GetTicks() + 2500; qProntaEm = quadro + 20; fase = ST_SETTLE;
    }
    else if (!strcmp(telaAlvo, "detail")) { abrir_detalhe(detalheIdx); espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA; }
    else if (!strcmp(telaAlvo, "sheet")) {
      if (passo == 0) { abrir_detalhe(0); passo = 1; }
      else if (passo == 1 && detail_aberto()) { passo = 2; espera = 0; }
      else if (passo == 2) {
        // A busca de fontes LIMPA a lista ao comecar (buscarPedido): so
        // injeta depois que ela assentar, senao a folha abre vazia.
        if (addons_estado() != ADD_BUSCANDO || ++espera > 150
            || SDL_GetTicks() - tEstado > 90000) {
          injetar_streams();
          stream_folha_abrir();
          passo = 3; espera = 0;
        }
      } else if (passo == 3) { tEstado = SDL_GetTicks(); fase = ST_ESPERA; }
    }
    else if (!strcmp(telaAlvo, "player")) { sim_aplicar(65.0); player_abrir(0, "https://fixture.invalid/matrix-1080p.mp4"); espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA; }
    else if (!strcmp(telaAlvo, "postplay")) {
      if (passo == 0) {
        if (!player_aberto()) { sim_aplicar(6800.0); player_abrir(0, "https://fixture.invalid/matrix-1080p.mp4"); }
        passo = 1;
      } else if (passo == 1 && player_aberto()) {
        injetar_relacionados();
        if (!posplay_abrir_relacionados(0)) { falha("sem relacionados"); break; }
        passo = 2; espera = 0;
      } else if (passo == 2) { tEstado = SDL_GetTicks(); fase = ST_ESPERA; }
    }
    else if (!strcmp(telaAlvo, "search")) { telaPedida = (int)TELA_BUSCA; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA; }
    else if (!strcmp(telaAlvo, "library")) { telaPedida = (int)TELA_BIBLIOTECA; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA; }
    else if (!strcmp(telaAlvo, "colecoes")) {
      // Duas categorias com titulos de verdade (ST_TOPO ja salvou os seis) —
      // a lista de gerenciamento nasce com contagem, nao vazia.
      int c1, c2;
      sorg_carregar();
      c1 = sorg_criar_categoria("Fim de semana");
      c2 = sorg_criar_categoria("Kids");
      if (cat_item(0)) sorg_mover(cat_item(0)->imdb, c1);
      if (cat_item(1)) sorg_mover(cat_item(1)->imdb, c2);
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-vazio")) {
      // ESTADO VAZIO da lista de gerenciamento: sem categoria e sem titulo.
      // ST_TOPO acabou de salvar os seis — aqui eles saem de novo e
      // aplicar_catalogo acompanha a marca do catalogo (naLista), fechando a
      // uniao dos contadores em zero. Nenhuma categoria e criada (o arquivo de
      // categorias desta rodada nem existe: dados frescos por captura).
      int i;
      for (i = 0; i < FX_N_SALVOS && i < nSementes; i++) {
        const CatItem *rc = cat_item(i);
        if (rc) salvos_definir(rc, 0);
      }
      // salvos_aplicar_catalogo SO MARCA (naLista=1) os locais; limpar a lista
      // local nao DESMARCA o que ja estava marcado no catalogo. E a marca que
      // salvos_uniao usa para incluir um titulo so-do-catalogo — sem desmarcar,
      // a tela continuava com contagem e o "vazio" nunca aparecia (captura
      // identica a tela cheia na revisao P7.1).
      for (i = 0; i < cat_n(); i++) cat_definir_na_lista(i, 0);
      salvos_aplicar_catalogo();
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    // P7.2a: ESTADOS do FolderDetail e das camadas por cima. A tela e a MESMA
    // TELA_COLECOES (o FolderDetail e estado, nao Tela): o ST_ESPERA abaixo sabe
    // esperar por colecoes_vista()/colecoes_pop_aberto() antes de capturar.
    // Cada estado RECOMECA o arquivo de categorias (sorg_esquecer): as capturas
    // rodam em sequencia num mesmo app, e um estado nao pode herdar pastas do
    // anterior — a contagem e a navegacao contam com a lista exata do setup.
    else if (!strcmp(telaAlvo, "colecoes-pasta")) {
      // FolderDetail POPULADO com as DUAS secoes do agrupamento padrao: o
      // titulo 0 ganha progresso (cai em "Continuar") e o 1 fica sem (cai em
      // "Não começados"); os dois entram na pasta "Fim de semana".
      int c1;
      sorg_carregar();
      sorg_esquecer();
      c1 = sorg_criar_categoria("Fim de semana");
      cat_salvar_progresso(0, 900.0, 1800.0);
      if (cat_item(0)) sorg_mover(cat_item(0)->imdb, c1);
      if (cat_item(1)) sorg_mover(cat_item(1)->imdb, c1);
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-pasta-vazia")) {
      // FolderDetail de uma pasta VAZIA (a leitura do corpo vazio, sem crash).
      sorg_carregar();
      sorg_esquecer();
      sorg_criar_categoria("Depois");
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-criar")) {
      // Teclado de "Nova coleção": foco na acao do pe (DOWN ate o fim) e OK.
      sorg_carregar();
      sorg_esquecer();
      sorg_criar_categoria("Fim de semana");
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-criada")) {
      // RESULTADO de criar DE VERDADE: o teclado da TV abre pela acao do pe e a
      // caminhada de teclas digita "kids" e conclui — a lista volta com a pasta
      // nova (Kids, primeira maiuscula vem do sorg) e o recado "Coleção criada".
      sorg_carregar();
      sorg_esquecer();
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-renomear")) {
      // FolderDetail com o teclado de RENOMEAR aberto (OK em Renomear).
      int c1;
      sorg_carregar();
      sorg_esquecer();
      c1 = sorg_criar_categoria("Fim de semana");
      if (cat_item(0)) sorg_mover(cat_item(0)->imdb, c1);
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-renomeada")) {
      // RESULTADO de renomear DE VERDADE: limpa o campo (que nasce com o nome
      // atual), digita "kids" e conclui — cabecalho mostra Kids, id intacto e
      // o recado "Coleção renomeada".
      int c1;
      sorg_carregar();
      sorg_esquecer();
      c1 = sorg_criar_categoria("Fim de semana");
      if (cat_item(0)) sorg_mover(cat_item(0)->imdb, c1);
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-excluir")) {
      // FolderDetail com a confirmacao de EXCLUIR (RIGHT em Renomear, OK),
      // nascendo com o foco em Cancelar.
      int c1;
      sorg_carregar();
      sorg_esquecer();
      c1 = sorg_criar_categoria("Fim de semana");
      if (cat_item(0)) sorg_mover(cat_item(0)->imdb, c1);
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else if (!strcmp(telaAlvo, "colecoes-abrir-titulo")) {
      // OK numa FILEIRA DE TITULO do FolderDetail abre o titulo pela rota de
      // sempre (abrirPorIndice, o detalhe por cima desta tela) — e NUNCA toca.
      int c1;
      sorg_carregar();
      sorg_esquecer();
      c1 = sorg_criar_categoria("Fim de semana");
      if (cat_item(0)) sorg_mover(cat_item(0)->imdb, c1);
      telaPedida = (int)TELA_COLECOES; espera = 0; tEstado = SDL_GetTicks(); fase = ST_ESPERA;
    }
    else { falha("tela desconhecida"); }
    break;
  case ST_ESPERA: {
    int ok = 0;
    if (!strcmp(telaAlvo, "detail")) ok = detail_aberto();
    else if (!strcmp(telaAlvo, "sheet")) ok = stream_folha_aberta();
    else if (!strcmp(telaAlvo, "player")) ok = player_aberto();
    else if (!strcmp(telaAlvo, "postplay")) ok = posplay_visivel();
    else if (!strcmp(telaAlvo, "search")) ok = !strcmp(app_tela_nome(), "busca");
    else if (!strcmp(telaAlvo, "library")) ok = !strcmp(app_tela_nome(), "biblioteca");
    else if (!strcmp(telaAlvo, "colecoes")) ok = !strcmp(app_tela_nome(), "colecoes");
    else if (!strcmp(telaAlvo, "colecoes-vazio")) ok = !strcmp(app_tela_nome(), "colecoes");
    // P7.2a: passos de teclado ate o ESTADO pedido (o FolderDetail e um estado
    // de TELA_COLECOES). passo segue a sequencia abaixo; cada passo so avanca
    // quando o quadro anterior assentou (nunca injeta duas teclas no mesmo GAP).
    // O passo 0 FECHA SOBRAS: um estado rodado depois de outro pode herdar o
    // teclado ou o detalhe abertos (as capturas rodam em sequencia) — Voltar
    // cancela os dois sem escrever nada.
    else if (!strcmp(telaAlvo, "colecoes-pasta")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }
        else if (passo == 2) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 3; } }
        else if (passo == 3) { if (++espera > 4) { if (colecoes_vista() == 1) ok = 1; else passo = 2; } }
      }
    }
    else if (!strcmp(telaAlvo, "colecoes-pasta-vazia")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }
        else if (passo == 2) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 3; } }
        else if (passo == 3) { if (++espera > 4) { if (colecoes_vista() == 1) ok = 1; else passo = 2; } }
      }
    }
    else if (!strcmp(telaAlvo, "colecoes-criar")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        // DOWN ate a acao do pe (foco 0 = "Sem categoria"; 1 = "Fim de semana"
        // criada; 2 = a acao), depois OK abre o teclado.
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }
        else if (passo == 2) { tecla(SDLK_DOWN); espera = 0; passo = 3; }
        else if (passo == 3) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 4; } }
        else if (passo == 4) { if (++espera > 4) { if (colecoes_pop_aberto()) ok = 1; else passo = 3; } }
      }
    }
    else if (!strcmp(telaAlvo, "colecoes-criada")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        // Acao do pe -> teclado -> a caminhada de teclas digita "kids" ->
        // Concluir -> a lista volta com a pasta Kids e o recado.
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }   // foco 1 = a acao
        else if (passo == 2) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 3; } }
        else if (passo == 3) { if (++espera > 4) { if (colecoes_pop_aberto()) { passo = 4; } else passo = 2; } }
        else if (passo < 4 + COL_TECLAS_KIDS_N) {
          if (++espera > 4) { tecla(COL_TECLAS_KIDS[passo - 4]); espera = 0; passo++; }
        }
        else if (passo == 4 + COL_TECLAS_KIDS_N) {
          if (++espera > 4) { if (!colecoes_pop_aberto() && colecoes_n_categorias() >= 1) ok = 1; }
        }
      }
    }
    else if (!strcmp(telaAlvo, "colecoes-renomear")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }   // abre a pasta
        else if (passo == 2) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 3; } }
        else if (passo == 3) { if (++espera > 4) { if (colecoes_vista() == 1) passo = 4; else passo = 2; } }
        // Corpo -> acoes do cabecalho (UP), depois OK em Renomear (detAcao 0).
        else if (passo == 4) { if (++espera > 4) { tecla(SDLK_UP); espera = 0; passo = 5; } }
        else if (passo == 5) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 6; } }
        else if (passo == 6) { if (++espera > 4) { if (colecoes_pop_aberto()) ok = 1; else passo = 5; } }
      }
    }
    else if (!strcmp(telaAlvo, "colecoes-renomeada")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        // Teclado de renomear (o campo NASCE com o nome atual): limpar (f7,
        // coluna 1), voltar a grade, digitar "kids" e Concluir.
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }
        else if (passo == 2) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 3; } }
        else if (passo == 3) { if (++espera > 4) { if (colecoes_vista() == 1) passo = 4; else passo = 2; } }
        else if (passo == 4) { if (++espera > 4) { tecla(SDLK_UP); espera = 0; passo = 5; } }
        else if (passo == 5) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 6; } }
        else if (passo == 6) { if (++espera > 4) { if (colecoes_pop_aberto()) { passo = 7; } else passo = 5; } }
        else if (passo < 7 + COL_TECLAS_LIMPA_N) {
          if (++espera > 4) { tecla(COL_TECLAS_LIMPA[passo - 7]); espera = 0; passo++; }
        }
        else if (passo < 7 + COL_TECLAS_LIMPA_N + COL_TECLAS_KIDS_N) {
          if (++espera > 4) { tecla(COL_TECLAS_KIDS[passo - 7 - COL_TECLAS_LIMPA_N]); espera = 0; passo++; }
        }
        else if (passo == 7 + COL_TECLAS_LIMPA_N + COL_TECLAS_KIDS_N) {
          if (++espera > 4) { if (!colecoes_pop_aberto() && colecoes_vista() == 1) ok = 1; }
        }
      }
    }
    else if (!strcmp(telaAlvo, "colecoes-excluir")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }
        else if (passo == 2) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 3; } }
        else if (passo == 3) { if (++espera > 4) { if (colecoes_vista() == 1) passo = 4; else passo = 2; } }
        // Corpo -> acoes (UP), RIGHT ate Excluir (detAcao 1), OK -> confirmacao.
        else if (passo == 4) { if (++espera > 4) { tecla(SDLK_UP); espera = 0; passo = 5; } }
        else if (passo == 5) { if (++espera > 4) { tecla(SDLK_RIGHT); espera = 0; passo = 6; } }
        else if (passo == 6) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 7; } }
        else if (passo == 7) { if (++espera > 4) { if (colecoes_pop_aberto()) ok = 1; else passo = 6; } }
      }
    }
    else if (!strcmp(telaAlvo, "colecoes-abrir-titulo")) {
      if (!strcmp(app_tela_nome(), "colecoes")) {
        if (passo == 0) { if (fecharSobra()) { espera = 0; passo = 1; } else espera = 0; }
        else if (passo == 1) { tecla(SDLK_DOWN); espera = 0; passo = 2; }
        else if (passo == 2) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 3; } }
        else if (passo == 3) { if (++espera > 4) { if (colecoes_vista() == 1) passo = 4; else passo = 2; } }
        // A 1a fileira do corpo ja nasce focada (detSel=1): OK arma o pedido e
        // o app.c abre o detalhe POR CIMA desta tela — sem play nenhum.
        else if (passo == 4) { if (++espera > 4) { tecla(SDLK_RETURN); espera = 0; passo = 5; } }
        else if (passo == 5) { if (++espera > 4) { if (detail_aberto() && !player_aberto()) ok = 1; else passo = 4; } }
      }
    }
    if (ok) { tProntaEm = SDL_GetTicks() + 2500; qProntaEm = quadro + 20; fase = ST_SETTLE; }
    else if (SDL_GetTicks() - tEstado > 75000) falha("timeout de espera");
    break;
  }
  case ST_SETTLE:
    if (quadro >= qProntaEm && (int)(SDL_GetTicks() - tProntaEm) >= 0) pronta();
    break;
  default:
    break;
  }
}

#endif // NV_FIXTURE
