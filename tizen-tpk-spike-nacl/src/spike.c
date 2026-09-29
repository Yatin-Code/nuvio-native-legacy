// Spike NaCl para Tizen 4/5 (2018/2019): prova TRES coisas com um .nexe so.
//   1. GLES2 via PPB_Graphics3D: quadrado girando (cor = estado da rede).
//   2. Um pthread: contador que o laco de quadro le e publica.
//   3. HTTPS via ponte JS: o modulo manda "fetch <url>" por postMessage, a
//      pagina faz o fetch e devolve "fetched <status> <bytes>" ou "fetcherr".
// Sem libcurl, sem SDL: C99 puro contra a API C do Pepper.
// Cor: azul = esperando, verde = HTTPS ok, vermelho = HTTPS falhou.

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <GLES2/gl2.h>
#include <ppapi/c/pp_completion_callback.h>
#include <ppapi/c/pp_errors.h>
#include <ppapi/c/pp_module.h>
#include <ppapi/c/pp_var.h>
#include <ppapi/c/ppb.h>
#include <ppapi/c/ppb_core.h>
#include <ppapi/c/ppb_graphics_3d.h>
#include <ppapi/c/ppb_instance.h>
#include <ppapi/c/ppb_messaging.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppb_view.h>
#include <ppapi/c/ppp.h>
#include <ppapi/c/ppp_instance.h>
#include <ppapi/c/ppp_messaging.h>
#include <ppapi/gles2/gl2ext_ppapi.h>

static PPB_GetInterface get_iface;
static const PPB_Core *core;
static const PPB_Graphics3D *g3d;
static const PPB_Instance *inst_if;
static const PPB_Messaging *msg;
static const PPB_Var *var;
static const PPB_View *view;

static PP_Instance g_inst;
static PP_Resource g_ctx;
static int g_w, g_h, g_started;
static volatile int g_thread_count;
static volatile int g_net;       // 0 esperando, 1 ok, 2 falhou
static GLuint g_prog;
static GLint g_u_ang, g_u_cor;
static int g_frames;

static void post(const char *s) {
  struct PP_Var v = var->VarFromUtf8(s, (uint32_t)strlen(s));
  msg->PostMessage(g_inst, v);
  var->Release(v);
}

static void *worker(void *arg) {
  (void)arg;
  struct timespec ts = {0, 100 * 1000 * 1000};
  for (;;) { nanosleep(&ts, NULL); g_thread_count++; }
  return NULL;
}

static GLuint compila(GLenum t, const char *src) {
  GLuint s = glCreateShader(t);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  return s;
}

static void gl_init(void) {
  static const char *vs =
    "attribute vec2 p; uniform float a;\n"
    "void main(){ float c=cos(a), s=sin(a);\n"
    " gl_Position=vec4(p.x*c-p.y*s, p.x*s+p.y*c, 0.0, 1.0); }";
  static const char *fs =
    "precision mediump float; uniform vec3 c;\n"
    "void main(){ gl_FragColor=vec4(c,1.0); }";
  g_prog = glCreateProgram();
  glAttachShader(g_prog, compila(GL_VERTEX_SHADER, vs));
  glAttachShader(g_prog, compila(GL_FRAGMENT_SHADER, fs));
  glBindAttribLocation(g_prog, 0, "p");
  glLinkProgram(g_prog);
  g_u_ang = glGetUniformLocation(g_prog, "a");
  g_u_cor = glGetUniformLocation(g_prog, "c");
}

static void quadro(void *u, int32_t r);

static void desenha(void) {
  static const GLfloat q[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
  glViewport(0, 0, g_w, g_h);
  glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(g_prog);
  glUniform1f(g_u_ang, g_frames * 0.03f);
  if (g_net == 1)      glUniform3f(g_u_cor, 0.2f, 0.85f, 0.3f);
  else if (g_net == 2) glUniform3f(g_u_cor, 0.9f, 0.2f, 0.2f);
  else                 glUniform3f(g_u_cor, 0.2f, 0.4f, 0.95f);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void quadro(void *u, int32_t r) {
  (void)u; (void)r;
  desenha();
  g_frames++;
  if (g_frames % 120 == 0) {
    char b[96];
    snprintf(b, sizeof b, "tick frames=%d thread=%d glerr=%d", g_frames,
             g_thread_count, (int)glGetError());
    post(b);
  }
  g3d->SwapBuffers(g_ctx, PP_MakeCompletionCallback(quadro, NULL));
}

static void inicia(void) {
  int32_t at[] = {PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
                  PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 0,
                  PP_GRAPHICS3DATTRIB_STENCIL_SIZE, 0,
                  PP_GRAPHICS3DATTRIB_WIDTH, g_w,
                  PP_GRAPHICS3DATTRIB_HEIGHT, g_h,
                  PP_GRAPHICS3DATTRIB_NONE};
  g_ctx = g3d->Create(g_inst, 0, at);
  if (!g_ctx || !inst_if->BindGraphics(g_inst, g_ctx)) { post("erro graphics3d"); return; }
  glSetCurrentContextPPAPI(g_ctx);
  gl_init();
  pthread_t t;
  post(pthread_create(&t, NULL, worker, NULL) == 0 ? "pthread ok" : "pthread FALHOU");
  post("fetch https://example.com/");
  quadro(NULL, 0);
}

static PP_Bool did_create(PP_Instance i, uint32_t n, const char *k[], const char *v[]) {
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
  if (!g_started && g_w > 0) { g_started = 1; inicia(); }
}
static void did_change_focus(PP_Instance i, PP_Bool f) { (void)i; (void)f; }
static PP_Bool handle_doc(PP_Instance i, PP_Resource l) { (void)i; (void)l; return PP_FALSE; }

static void handle_msg(PP_Instance i, struct PP_Var m) {
  uint32_t n = 0;
  const char *s;
  (void)i;
  if (m.type != PP_VARTYPE_STRING) return;
  s = var->VarToUtf8(m, &n);
  if (strncmp(s, "fetched ", 8) == 0) g_net = 1;
  else if (strncmp(s, "fetcherr", 8) == 0) g_net = 2;
}

static struct PPP_Instance_1_1 ppp_inst = {did_create, did_destroy, did_change_view,
                                    did_change_focus, handle_doc};
static struct PPP_Messaging_1_0 ppp_msg = {handle_msg};

PP_EXPORT int32_t PPP_InitializeModule(PP_Module m, PPB_GetInterface gi) {
  (void)m;
  get_iface = gi;
  core = gi(PPB_CORE_INTERFACE_1_0);
  g3d = gi(PPB_GRAPHICS_3D_INTERFACE_1_0);
  inst_if = gi(PPB_INSTANCE_INTERFACE_1_0);
  msg = gi(PPB_MESSAGING_INTERFACE_1_0);
  var = gi(PPB_VAR_INTERFACE_1_1);
  view = gi(PPB_VIEW_INTERFACE_1_0);
  if (!core || !g3d || !inst_if || !msg || !var || !view) return PP_ERROR_NOINTERFACE;
  if (!glInitializePPAPI(gi)) return PP_ERROR_FAILED;
  return PP_OK;
}
PP_EXPORT const void *PPP_GetInterface(const char *n) {
  if (!strcmp(n, PPP_INSTANCE_INTERFACE_1_1)) return &ppp_inst;
  if (!strcmp(n, PPP_MESSAGING_INTERFACE_1_0)) return &ppp_msg;
  return NULL;
}
PP_EXPORT void PPP_ShutdownModule(void) {}
