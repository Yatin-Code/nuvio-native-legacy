// plat_nacl.c — implementacao da camada de plataforma NaCl (ver plat_nacl.h).
//
// Compila SO com NV_NACL (arm-nacl-clang, newlib) contra o Samsung NaCl SDK
// pepper_63. Nao depende de SDL: e a cola PPAPI que fica ABAIXO do shim de SDL
// e da rede.c. Este .c pode ser compilado sozinho (checagem de sintaxe) sem o
// resto do app; nvnacl_app_main() e um simbolo externo (main.c).
//
// O que faz:
//   1. Modulo PPAPI: recebe o contexto GLES2 e as dimensoes da <embed>.
//   2. Bombeia eventos de entrada (tecla/ponteiro) — entregues ao shim de SDL
//      por nvnacl_input_* (ver plat_nacl_input.h, ainda a escrever).
//   3. Ponte de rede: nvnacl_http() bloqueia um fio de trabalho enquanto a
//      pagina faz o XHR; a resposta volta como um dicionario com o corpo em
//      ArrayBuffer (binario intacto, sem o truque de charset do emscripten).
//   4. Marechaliza todo PostMessage para o fio principal (exigencia do PPB).
#ifdef NV_NACL

#include "plat_nacl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLES2/gl2.h>
#include <ppapi/c/pp_completion_callback.h>
#include <ppapi/c/pp_errors.h>
#include <ppapi/c/pp_var.h>
#include <ppapi/c/ppb.h>
#include <ppapi/c/ppb_core.h>
#include <ppapi/c/ppb_graphics_3d.h>
#include <ppapi/c/ppb_instance.h>
#include <ppapi/c/ppb_messaging.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppb_var_array_buffer.h>
#include <ppapi/c/ppb_var_dictionary.h>
#include <ppapi/c/ppb_view.h>
#include <ppapi/c/ppp.h>
#include <ppapi/c/ppp_instance.h>
#include <ppapi/c/ppp_messaging.h>
#include <ppapi/gles2/gl2ext_ppapi.h>

static const PPB_Core *core;
static const PPB_Graphics3D *g3d;
static const PPB_Instance *inst_if;
static const PPB_Messaging *msg;
static const PPB_Var *var;
static const PPB_VarArrayBuffer *vab;
static const PPB_VarDictionary *vdict;
static const PPB_View *view;

static PP_Instance g_inst;
static PP_Resource g_ctx;
static int g_w, g_h;
static volatile int g_pronto;
static pthread_t g_app_thread;
static int g_app_lancado;

// --- utilitarios de PP_Var -------------------------------------------------
static struct PP_Var vstr(const char *s) {
  return var->VarFromUtf8(s, (uint32_t)strlen(s));
}
static struct PP_Var dget(struct PP_Var d, const char *k) {
  struct PP_Var key = vstr(k), v = vdict->Get(d, key);
  var->Release(key);
  return v;
}
static int dgeti(struct PP_Var d, const char *k) {
  struct PP_Var v = dget(d, k);
  int r = (v.type == PP_VARTYPE_INT32) ? v.value.as_int
        : (v.type == PP_VARTYPE_DOUBLE) ? (int)v.value.as_double : 0;
  var->Release(v);
  return r;
}
static void dgetstr(struct PP_Var d, const char *k, char *dst, int tam) {
  struct PP_Var v = dget(d, k);
  if (dst && tam > 0) {
    dst[0] = 0;
    if (v.type == PP_VARTYPE_STRING) {
      uint32_t n = 0;
      const char *s = var->VarToUtf8(v, &n);
      if ((int)n >= tam) n = (uint32_t)tam - 1;
      memcpy(dst, s, n);
      dst[n] = 0;
    }
  }
  var->Release(v);
}

// --- Ponte de rede ---------------------------------------------------------
// Um pedido em voo: o fio de trabalho preenche a saida, o fio principal manda
// o PostMessage, e handle_msg preenche a resposta e acorda o fio de trabalho.
typedef struct Pedido {
  int id;
  // entrada (o fio principal le para montar o PostMessage)
  const char *metodo, *url, *cabs, *corpo;
  int corpo_tam;
  // saida (handle_msg preenche)
  char *resp; int resp_tam, status;
  char *url_final; int url_final_tam;
  char *etag; int etag_tam;
  volatile int pronto;
  pthread_mutex_t mtx;
  pthread_cond_t cv;
  struct Pedido *prox;
} Pedido;

static Pedido *g_pedidos;           // lista dos pedidos em voo
static pthread_mutex_t g_pedidos_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_prox_id = 1;

static Pedido *pedido_por_id(int id) {
  Pedido *p;
  pthread_mutex_lock(&g_pedidos_mtx);
  for (p = g_pedidos; p; p = p->prox)
    if (p->id == id) break;
  pthread_mutex_unlock(&g_pedidos_mtx);
  return p;
}

// Roda no FIO PRINCIPAL (via CallOnMainThread): monta e envia o pedido.
static void enviar_no_principal(void *u, int32_t r) {
  Pedido *p = (Pedido *)u;
  struct PP_Var d = vdict->Create();
  struct PP_Var kv;
  (void)r;
  #define PUT(k, val) do { struct PP_Var _k = vstr(k), _v = (val); \
      vdict->Set(d, _k, _v); var->Release(_k); var->Release(_v); } while (0)
  PUT("t", vstr("fetch"));
  memset(&kv, 0, sizeof kv); kv.type = PP_VARTYPE_INT32; kv.value.as_int = p->id;
  PUT("id", kv);
  PUT("m", vstr(p->metodo ? p->metodo : "GET"));
  PUT("u", vstr(p->url ? p->url : ""));
  PUT("h", vstr(p->cabs ? p->cabs : ""));
  if (p->corpo && p->corpo_tam > 0) {
    struct PP_Var b = vab->Create((uint32_t)p->corpo_tam);
    void *m = vab->Map(b);
    if (m) memcpy(m, p->corpo, (size_t)p->corpo_tam);
    vab->Unmap(b);
    PUT("b", b);
  }
  #undef PUT
  msg->PostMessage(g_inst, d);
  var->Release(d);
}

char *nvnacl_http(const char *metodo, const char *url, const char *cabs,
                  const char *corpo, int corpo_tam, int *tam, int *status,
                  char *url_final, int url_final_tam,
                  char *etag, int etag_tam) {
  Pedido p;
  char *r;
  memset(&p, 0, sizeof p);
  p.metodo = metodo; p.url = url; p.cabs = cabs;
  p.corpo = corpo; p.corpo_tam = corpo_tam;
  p.url_final = url_final; p.url_final_tam = url_final_tam;
  p.etag = etag; p.etag_tam = etag_tam;
  pthread_mutex_init(&p.mtx, NULL);
  pthread_cond_init(&p.cv, NULL);

  pthread_mutex_lock(&g_pedidos_mtx);
  p.id = g_prox_id++;
  p.prox = g_pedidos; g_pedidos = &p;
  pthread_mutex_unlock(&g_pedidos_mtx);

  // Marechaliza o envio para o fio principal.
  core->CallOnMainThread(0, PP_MakeCompletionCallback(enviar_no_principal, &p), 0);

  // Bloqueia ate handle_msg marcar pronto.
  pthread_mutex_lock(&p.mtx);
  while (!p.pronto) pthread_cond_wait(&p.cv, &p.mtx);
  pthread_mutex_unlock(&p.mtx);

  // Desenfileira.
  pthread_mutex_lock(&g_pedidos_mtx);
  { Pedido **pp = &g_pedidos;
    while (*pp && *pp != &p) pp = &(*pp)->prox;
    if (*pp) *pp = p.prox; }
  pthread_mutex_unlock(&g_pedidos_mtx);

  if (status) *status = p.status;
  if (tam) *tam = p.resp_tam;
  r = p.resp;    // pode ser NULL se o pedido falhou
  pthread_cond_destroy(&p.cv);
  pthread_mutex_destroy(&p.mtx);
  return r;
}

// --- Mensagens da pagina ---------------------------------------------------
static void handle_msg(PP_Instance i, struct PP_Var m) {
  struct PP_Var vt;
  uint32_t tn = 0;
  const char *t;
  (void)i;
  if (m.type != PP_VARTYPE_DICTIONARY) return;
  vt = dget(m, "t");
  if (vt.type != PP_VARTYPE_STRING) { var->Release(vt); return; }
  t = var->VarToUtf8(vt, &tn);
  if (strncmp(t, "fetched", 7) == 0) {
    int id = dgeti(m, "id");
    Pedido *p = pedido_por_id(id);
    if (p) {
      struct PP_Var b = dget(m, "b");
      p->status = dgeti(m, "status");
      dgetstr(m, "final", p->url_final, p->url_final_tam);
      dgetstr(m, "etag", p->etag, p->etag_tam);
      if (b.type == PP_VARTYPE_ARRAY_BUFFER) {
        uint32_t n = 0;
        if (vab->ByteLength(b, &n)) {
          void *mp = vab->Map(b);
          p->resp = (char *)malloc(n + 1);
          if (p->resp) {
            if (mp && n) memcpy(p->resp, mp, n);
            p->resp[n] = 0;
            p->resp_tam = (int)n;
          }
          vab->Unmap(b);
        }
      }
      var->Release(b);
      pthread_mutex_lock(&p->mtx);
      p->pronto = 1;
      pthread_cond_signal(&p->cv);
      pthread_mutex_unlock(&p->mtx);
    }
  }
  var->Release(vt);
}

void nvnacl_post(const char *s) {
  struct PP_Var v = vstr(s);
  msg->PostMessage(g_inst, v);
  var->Release(v);
}

// --- GLES2 / ciclo de vida -------------------------------------------------
int nvnacl_pronto(void) { return g_pronto; }
void nvnacl_tamanho(int *w, int *h) { if (w) *w = g_w; if (h) *h = g_h; }
void nvnacl_swap(void) {
  if (g_ctx) g3d->SwapBuffers(g_ctx, PP_BlockUntilComplete());
}

static void iniciar_contexto(void) {
  int32_t at[] = {PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
                  PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 0,
                  PP_GRAPHICS3DATTRIB_STENCIL_SIZE, 0,
                  PP_GRAPHICS3DATTRIB_WIDTH, g_w,
                  PP_GRAPHICS3DATTRIB_HEIGHT, g_h,
                  PP_GRAPHICS3DATTRIB_NONE};
  g_ctx = g3d->Create(g_inst, 0, at);
  if (!g_ctx || !inst_if->BindGraphics(g_inst, g_ctx)) {
    nvnacl_post("erro graphics3d");
    return;
  }
  glSetCurrentContextPPAPI(g_ctx);
  g_pronto = 1;
}

static void *app_trampolim(void *u) {
  (void)u;
  nvnacl_app_main();
  return NULL;
}

static PP_Bool did_create(PP_Instance i, uint32_t n, const char *k[],
                          const char *v[]) {
  (void)n; (void)k; (void)v;
  g_inst = i;
  return PP_TRUE;
}
static void did_destroy(PP_Instance i) { (void)i; }
static void did_change_view(PP_Instance i, PP_Resource vw) {
  struct PP_Rect r;
  (void)i;
  if (!view->GetRect(vw, &r)) return;
  g_w = r.size.width; g_h = r.size.height;
  if (!g_pronto && g_w > 0) iniciar_contexto();
  if (g_pronto && !g_app_lancado) {
    g_app_lancado = 1;
    pthread_create(&g_app_thread, NULL, app_trampolim, NULL);
  }
}
static void did_change_focus(PP_Instance i, PP_Bool f) { (void)i; (void)f; }
static PP_Bool handle_doc(PP_Instance i, PP_Resource l) {
  (void)i; (void)l; return PP_FALSE;
}

static struct PPP_Instance_1_1 ppp_inst = {
    did_create, did_destroy, did_change_view, did_change_focus, handle_doc};
static struct PPP_Messaging_1_0 ppp_msg = {handle_msg};

PP_EXPORT int32_t PPP_InitializeModule(PP_Module m, PPB_GetInterface gi) {
  (void)m;
  core = gi(PPB_CORE_INTERFACE_1_0);
  g3d = gi(PPB_GRAPHICS_3D_INTERFACE_1_0);
  inst_if = gi(PPB_INSTANCE_INTERFACE_1_0);
  msg = gi(PPB_MESSAGING_INTERFACE_1_0);
  var = gi(PPB_VAR_INTERFACE_1_1);
  vab = gi(PPB_VAR_ARRAY_BUFFER_INTERFACE_1_0);
  vdict = gi(PPB_VAR_DICTIONARY_INTERFACE_1_0);
  view = gi(PPB_VIEW_INTERFACE_1_0);
  if (!core || !g3d || !inst_if || !msg || !var || !vab || !vdict || !view)
    return PP_ERROR_NOINTERFACE;
  if (!glInitializePPAPI(gi)) return PP_ERROR_FAILED;
  return PP_OK;
}
PP_EXPORT const void *PPP_GetInterface(const char *n) {
  if (!strcmp(n, PPP_INSTANCE_INTERFACE_1_1)) return &ppp_inst;
  if (!strcmp(n, PPP_MESSAGING_INTERFACE_1_0)) return &ppp_msg;
  return NULL;
}
PP_EXPORT void PPP_ShutdownModule(void) {}

#endif  /* NV_NACL */
