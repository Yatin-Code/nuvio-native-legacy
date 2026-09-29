// Host .NET do Nuvio .tpk no Tizen 6+. Nao tem tela propria: abre um GLWindow
// de tela cheia e, a cada quadro, entrega o contexto GL ao C (libnuvio.so,
// src/tpk.c), que roda o app inteiro num fio seu. Teclas do controle vao pelo
// nome (XF86Back, Up, ...) e o C traduz para SDL. O player esta em Video.cs.
//
// Por que GLWindow e nao GLView: GLView so existe a partir da API11 e o alvo
// comeca na API8 (Tizen 6.0). O spike mediu GLWindow + .so a ~45 fps no Tizen 6.
//
// A janela padrao do NUI (Window.Instance) fica transparente por baixo do
// GLWindow: e nela que o player prende o video.
#pragma warning disable CS0618
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using Tizen.Multimedia;
using Tizen.NUI;
using IOPath = System.IO.Path;
using NuiWindow = Tizen.NUI.Window;
using NuiRect = Tizen.NUI.Rectangle;
using NuiColor = Tizen.NUI.Color;
using NuiTimer = Tizen.NUI.Timer;

namespace NuvioTpk
{
    class Program : NUIApplication
    {
        [DllImport("libnuvio.so")] static extern int nv_tpk_iniciar(string arte, string dados, int w, int h);
        [DllImport("libnuvio.so")] static extern int nv_tpk_quadro();
        [DllImport("libnuvio.so")] static extern void nv_tpk_config(int esperaMs, int swapZero);
        [DllImport("libnuvio.so")] static extern void nv_tpk_tecla(string nome, int apertou);
        [DllImport("libdl.so.2")] static extern IntPtr dlopen(string path, int flags);
        [DllImport("libdl.so.2")] static extern IntPtr dlerror();

        const int W = 1920, H = 1080;

        GLWindow gl;
        volatile bool fim;
        NuiTimer vigia;
        Video video;
        AudioStreamPolicy foco;
        string logArq;

        void Log(string t)
        {
            try { if (logArq != null) File.AppendAllText(logArq, DateTime.Now.ToString("HH:mm:ss.fff") + " " + t + "\n"); } catch { }
        }

        // Foco de audio de midia: sem ele o audio de outro app (YouTube, canal
        // ao vivo) continua tocando por baixo do Nuvio ate um filme comecar.
        // Tirado uma vez ao abrir e devolvido ao sair; qualquer falha so vai
        // para o log e nunca impede o app de abrir.
        void PegaFoco()
        {
            try
            {
                if (foco == null) foco = new AudioStreamPolicy(AudioStreamType.Media);
                foco.AcquireFocus(AudioStreamFocusOptions.Playback, (AudioStreamBehaviors)0, null);
                Log("foco de audio: tomado");
            }
            catch (Exception e) { Log("foco de audio: " + e.GetType().Name + ": " + e.Message); }
        }

        void SoltaFoco()
        {
            try
            {
                if (foco == null) return;
                foco.ReleaseFocus(AudioStreamFocusOptions.Playback, (AudioStreamBehaviors)0, null);
                Log("foco de audio: devolvido");
            }
            catch (Exception e) { Log("soltar foco: " + e.GetType().Name + ": " + e.Message); }
        }

        protected override void OnCreate()
        {
            base.OnCreate();
            var principal = SynchronizationContext.Current;
            NuiWindow.Instance.BackgroundColor = NuiColor.Transparent;

            string dados = DirectoryInfo.Data;
            string arte = IOPath.Combine(DirectoryInfo.Resource, "art");
            logArq = IOPath.Combine(dados, "tpk-host.log");

            // A .so aberta por caminho absoluto ANTES do primeiro DllImport, como
            // no pacote do Tizen 4/5: se o launcher desta TV nao procurar no lib/
            // do pacote (4/5 nao procurava; no 6.5 ninguem mediu, #170), o
            // DllImport("libnuvio.so") casa pelo soname com a ja carregada.
            string so = IOPath.Combine(IOPath.GetFullPath(IOPath.Combine(DirectoryInfo.Resource, "..")), "lib", "libnuvio.so");
            dlerror();
            if (dlopen(so, 2 | 0x100) == IntPtr.Zero)
            {
                string e = Marshal.PtrToStringAnsi(dlerror());
                Erro("The TV did not let Nuvio load its native library.", so + ": " + (string.IsNullOrEmpty(e) ? "refused without a message" : e));
                return;
            }
            try
            {
                video = new Video(() => new Display(NuiWindow.Instance),
                                  a => { if (principal != null) principal.Post(_ => a(), null); else a(); },
                                  dados, W, H);
            }
            catch (Exception e) { Erro("Nuvio could not start the player.", e.GetType().Name + ": " + e.Message); return; }

            // API8: o callback roda no fio principal e o DALi troca os buffers
            // mesmo em quadro pulado, entao espera o app terminar o quadro.
            // API9+: fio de render proprio, respeita o 0 de "pular", e o swap
            // com vsync e que derrubava o Tizen 9 a 3 fps no spike.
            try
            {
#if NV_API8
                nv_tpk_config(-1, 0);
#else
                nv_tpk_config(50, 1);
#endif
                if (nv_tpk_iniciar(arte, dados, W, H) != 0) throw new Exception("nv_tpk_iniciar failed");
            }
            catch (Exception e)
            {
                try { File.WriteAllText(IOPath.Combine(dados, "tpk-erro.txt"), e.ToString()); } catch { }
                Erro("Nuvio could not start.", e.GetType().Name + ": " + e.Message);
                return;
            }

            PegaFoco();
            try
            {
                CriaJanelaGL();
            }
            catch (Exception e) { Erro("Nuvio could not open its window.", e.GetType().Name + ": " + e.Message); return; }
        }

        void CriaJanelaGL()
        {
            gl = new GLWindow("nuvio", new NuiRect(0, 0, W, H), true);
#if NV_API8
            gl.SetEglConfig(false, false, 0, GLWindow.GLESVersion.Version_2_0);
            gl.RegisterGlCallback(() => { }, () => { Quadro(); }, () => { });
#elif NV_API9
            gl.SetEglConfig(false, false, 0, GLESVersion.Version20);
            gl.RegisterGlCallback(() => { }, () => Quadro(), () => { });
            gl.RenderingMode = GLRenderingMode.Continuous;
#else
            gl.SetGraphicsConfig(false, false, 0, GLESVersion.Version20);
            gl.RegisterGLCallbacks(() => { }, () => Quadro(), () => { });
            gl.RenderingMode = GLRenderingMode.Continuous;
#endif
            // As duas janelas repassam tecla: qual delas fica com o foco depende
            // do firmware, e so uma recebe de cada vez.
            gl.KeyEvent += (s, e) => Tecla(e.Key);
            NuiWindow.Instance.KeyEvent += (s, e) => Tecla(e.Key);
            gl.Show();

            // Exit() tem de sair do fio principal, e Quadro() roda no de desenho.
            vigia = new NuiTimer(250);
            vigia.Tick += (s, e) =>
            {
                if (fim) { video.Parar(); Exit(); return false; }
                video.Tique();
                return true;
            };
            vigia.Start();
        }

        // Em vez de fechar em silencio: o motivo na tela, com a TV, para foto.
        void Erro(string titulo, string detalhe)
        {
            // Sem `fim`: o relogio (se ja existir) fecharia o app antes da foto.
            if (gl != null) { try { gl.Hide(); } catch { } }
            var w = NuiWindow.Instance;
            w.BackgroundColor = NuiColor.Black;
            string versao = "?", modelo = "?";
            try { Tizen.System.Information.TryGetValue<string>("http://tizen.org/feature/platform.version", out versao); } catch { }
            try { Tizen.System.Information.TryGetValue<string>("http://tizen.org/system/model_name", out modelo); } catch { }
            var t = new Tizen.NUI.BaseComponents.TextLabel
            {
                Text = titulo + "\n\n" + detalhe + "\n\nTV " + modelo + " / Tizen " + versao + " / " + RuntimeInformation.FrameworkDescription +
                       "\n\nPlease post a PHOTO of this screen in issue #137 on GitHub (iqui27/nuvio-native-legacy). Back closes.",
                MultiLine = true, TextColor = NuiColor.White, PointSize = 20,
                Size2D = new Size2D(W - 160, H - 160), Position2D = new Position2D(80, 80),
            };
            w.Add(t);
            w.KeyEvent += (s, e) => { if (e.Key.State == Key.StateType.Down && (e.Key.KeyPressedName == "XF86Back" || e.Key.KeyPressedName == "Escape")) Exit(); };
        }

        void Tecla(Key k)
        {
            nv_tpk_tecla(k.KeyPressedName, k.State == Key.StateType.Down ? 1 : 0);
        }

        // Fio de desenho do NUI. 1 = troca, 0 = pula, -1 = o app acabou.
        int Quadro()
        {
            if (fim) return 0;
            int r = nv_tpk_quadro();
            if (r < 0) fim = true;
            return r > 0 ? 1 : 0;
        }

        protected override void OnResume()
        {
            base.OnResume();
            PegaFoco();
        }

        protected override void OnPause()
        {
            video?.PausarPeloSistema();
            base.OnPause();
        }

        protected override void OnTerminate()
        {
            video?.Parar();
            SoltaFoco();
            base.OnTerminate();
        }

        static void Main(string[] args)
        {
            new Program().Run(args);
        }
    }
}
