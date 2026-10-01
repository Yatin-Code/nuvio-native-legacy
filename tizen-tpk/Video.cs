// Player do Nuvio .tpk, comum aos dois hosts (NUI no Tizen 6+, ElmSharp no
// 4/5). Tizen.Multimedia.Player no plano de video da TV; a janela GL fica por
// cima, translucida, e o C abre o furo onde o video aparece. O C pede pelas
// funcoes registradas em nv_tpk_video_registrar e ouve pelo
// nv_tpk_video_evento (src/video_tpk.c).
//
// Toda chamada ao player passa pelo fio principal (Principal), porque o C
// chama do fio dele. A posicao e lida pelo relogio do host (Tique) e o C so le
// o numero guardado, sem esperar ninguem.
using System;
using System.IO;
using System.Runtime.InteropServices;
using Tizen.Multimedia;

namespace NuvioTpk
{
    // Despacho dos pontos de entrada de video/log do libnuvio, comum aos dois
    // hosts. POR PADRAO cada delegate aponta para o [DllImport("libnuvio.so")]
    // correspondente — que resolve pelo soname. E o caminho dos hosts Tizen 6+
    // (NuvioTpk/60/65) e tambem da rota memfd do NuvioTpk40, onde a lib entra no
    // link map do loader: comportamento identico ao codigo anterior.
    //
    // So o NuvioTpk40, quando a lib e carregada pelo carregador de ELF proprio
    // (a lib NAO entra no link map, entao DllImport-por-soname NAO resolveria),
    // chama NvVid.Ligar(resolve) para repontar estes delegates para ponteiros de
    // funcao vindos do dynsym do carregador. Nada disso e acionado nos 6+.
    static class NvVid
    {
        [DllImport("libnuvio.so")] static extern void nv_tpk_video_registrar(IntPtr abrir, IntPtr parar, IntPtr pausar,
                                                                              IntPtr buscar, IntPtr volume, IntPtr janela, IntPtr pos);
        [DllImport("libnuvio.so")] static extern void nv_tpk_video_evento(int tipo, int a, int b);
        [DllImport("libnuvio.so")] static extern void nv_tpk_video_registrar_faixas(IntPtr escolher);
        [DllImport("libnuvio.so")] static extern void nv_tpk_video_faixa(int tipo, int idx, string lingua);
        [DllImport("libnuvio.so")] static extern void nv_tpk_video_faixas_fim(int selAudio, int selLeg);
        [DllImport("libnuvio.so")] static extern void nv_tpk_video_legenda(string texto, int durMs);
        [DllImport("libnuvio.so")] static extern void nv_tpk_log(string linha);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void RegistrarDel(IntPtr abrir, IntPtr parar, IntPtr pausar, IntPtr buscar, IntPtr volume, IntPtr janela, IntPtr pos);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void EventoDel(int tipo, int a, int b);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void RegistrarFaixasDel(IntPtr escolher);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void FaixaDel(int tipo, int idx, string lingua);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void FaixasFimDel(int selAudio, int selLeg);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void LegendaDel(string texto, int durMs);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void LogDel(string linha);

        // Padrao: as thunks de P/Invoke acima (soname). O NuvioTpk40 repontа na
        // rota ELF.
        public static RegistrarDel Registrar = nv_tpk_video_registrar;
        public static EventoDel Evento = nv_tpk_video_evento;
        public static RegistrarFaixasDel RegistrarFaixas = nv_tpk_video_registrar_faixas;
        public static FaixaDel Faixa = nv_tpk_video_faixa;
        public static FaixasFimDel FaixasFim = nv_tpk_video_faixas_fim;
        public static LegendaDel Legenda = nv_tpk_video_legenda;
        public static LogDel LogNativo = nv_tpk_log;

        // Repontа tudo por ponteiro de funcao (rota do carregador ELF do
        // NuvioTpk40). resolve(nome) devolve o endereco do simbolo no dynsym.
        public static void Ligar(Func<string, IntPtr> resolve)
        {
            IntPtr p;
            if ((p = resolve("nv_tpk_video_registrar")) != IntPtr.Zero) Registrar = Marshal.GetDelegateForFunctionPointer<RegistrarDel>(p);
            if ((p = resolve("nv_tpk_video_evento")) != IntPtr.Zero) Evento = Marshal.GetDelegateForFunctionPointer<EventoDel>(p);
            if ((p = resolve("nv_tpk_video_registrar_faixas")) != IntPtr.Zero) RegistrarFaixas = Marshal.GetDelegateForFunctionPointer<RegistrarFaixasDel>(p);
            if ((p = resolve("nv_tpk_video_faixa")) != IntPtr.Zero) Faixa = Marshal.GetDelegateForFunctionPointer<FaixaDel>(p);
            if ((p = resolve("nv_tpk_video_faixas_fim")) != IntPtr.Zero) FaixasFim = Marshal.GetDelegateForFunctionPointer<FaixasFimDel>(p);
            if ((p = resolve("nv_tpk_video_legenda")) != IntPtr.Zero) Legenda = Marshal.GetDelegateForFunctionPointer<LegendaDel>(p);
            if ((p = resolve("nv_tpk_log")) != IntPtr.Zero) LogNativo = Marshal.GetDelegateForFunctionPointer<LogDel>(p);
        }
    }

    class Video
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FnAbrir(IntPtr url, IntPtr cabecalhos);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FnSemArg();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FnInt(int v);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FnRet(int x, int y, int w, int h);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int FnPos();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FnEscolher(int tipo, int idx);

        const int EV_PRONTO = 1, EV_TOCANDO = 2, EV_PAUSADO = 3, EV_FIM = 4, EV_ERRO = 5, EV_TAMANHO = 6, EV_BUFFER = 7;

        readonly Func<Display> fazDisplay;
        readonly Action<Action> principal;
        readonly string logArq;
        readonly int telaW, telaH;
        // Referencias vivas: o C guarda os ponteiros, o GC nao pode recolher.
        FnAbrir fAbrir; FnSemArg fParar; FnInt fPausar, fBuscar, fVolume; FnRet fJanela; FnPos fPos; FnEscolher fEscolher;

        // Chamado no fio principal logo depois do Start() de cada filme (#203).
        public Action AoTocar;

        Player player;
        int sessao;
        volatile int posMs;

        public Video(Func<Display> fazDisplay, Action<Action> principal, string dados, int telaW, int telaH)
        {
            this.fazDisplay = fazDisplay;
            this.principal = principal;
            this.telaW = telaW;
            this.telaH = telaH;
            logArq = Path.Combine(dados, "tpk-host.log");
            fAbrir = (u, c) => { string url = Marshal.PtrToStringAnsi(u), cab = Marshal.PtrToStringAnsi(c); Principal(() => Abrir(url, cab)); };
            fParar = () => Principal(Parar);
            fPausar = p => Principal(() => Pausar(p != 0));
            fBuscar = ms => Principal(() => Buscar(ms));
            fVolume = v => Principal(() => { if (player != null) player.Volume = Math.Max(0, Math.Min(100, v)) / 100f; });
            fJanela = (x, y, w, h) => Principal(() => Janela(x, y, w, h));
            fPos = () => posMs;
            NvVid.Registrar(Marshal.GetFunctionPointerForDelegate(fAbrir), Marshal.GetFunctionPointerForDelegate(fParar),
                                   Marshal.GetFunctionPointerForDelegate(fPausar), Marshal.GetFunctionPointerForDelegate(fBuscar),
                                   Marshal.GetFunctionPointerForDelegate(fVolume), Marshal.GetFunctionPointerForDelegate(fJanela),
                                   Marshal.GetFunctionPointerForDelegate(fPos));
            fEscolher = (tipo, idx) => Principal(() => Escolher(tipo, idx));
            NvVid.RegistrarFaixas(Marshal.GetFunctionPointerForDelegate(fEscolher));
        }

        // 0 = audio, 1 = legenda embutida, 2 = atraso da legenda (ms).
        void Escolher(int tipo, int idx)
        {
            if (player == null) return;
            try
            {
                if (tipo == 0) player.AudioTrackInfo.Selected = idx;
                else if (tipo == 1) player.SubtitleTrackInfo.Selected = idx;
                else player.SetSubtitleOffset(idx);
            }
            catch (Exception e) { if (tipo != 2) Log("escolher " + tipo + "/" + idx + ": " + e.Message); }
        }

        // Lista de faixas para o C, logo depois do prepare. Devolve quantas
        // faixas de audio o player listou.
        int Faixas(Player p, bool soAudio = false)
        {
            int selA = -1, selL = -1, nA = 0;
            try
            {
                var a = p.AudioTrackInfo;
                nA = a.GetCount();
                for (int i = 0; i < nA; i++) NvVid.Faixa(0, i, Lingua(() => a.GetLanguageCode(i)));
                try { selA = a.Selected; } catch { }
            }
            catch (Exception e) { Log("faixas de audio: " + e.Message); }
            // #165: legenda listada e audio nao. Se o video tem som, a faixa que
            // toca aparece como unica, em vez de "nenhuma faixa".
            if (nA == 0)
            {
                try
                {
                    var ap = p.StreamInfo.GetAudioProperties();
                    if (ap.Channels > 0) { NvVid.Faixa(0, 0, ""); selA = 0; Log($"audio sem lista do player: {ap.Channels} canais, {ap.SampleRate} Hz"); }
                }
                catch (Exception e) { Log("propriedades de audio: " + e.Message); }
            }
            if (soAudio) { NvVid.FaixasFim(selA, -1); return nA; }
            try
            {
                var l = p.SubtitleTrackInfo;
                int n = l.GetCount();
                for (int i = 0; i < n; i++) NvVid.Faixa(1, i, Lingua(() => l.GetLanguageCode(i)));
                try { selL = l.Selected; } catch { }
            }
            catch (Exception e) { Log("faixas de legenda: " + e.Message); }
            Log($"faixas do player: {nA} audio (sel={selA}), legenda sel={selL}");
            NvVid.FaixasFim(selA, selL);
            return nA;
        }

        static string Lingua(Func<string> f)
        {
            try { return f() ?? ""; } catch { return ""; }
        }

        // App foi para segundo plano: o player pausa (e o C fica sabendo).
        public void PausarPeloSistema() { SoltaPrimer(); Pausar(true); }

        // Relogio do host, no fio principal.
        public void Tique()
        {
            try { if (player != null && player.State == PlayerState.Playing) posMs = player.GetPlayPosition(); } catch { }
        }

        public void Log(string s)
        {
            try { NvVid.LogNativo(s); } catch { }
            try { File.AppendAllText(logArq, DateTime.Now.ToString("HH:mm:ss ") + s + "\n"); } catch { }
        }

        void Principal(Action a)
        {
            principal(() => { try { a(); } catch (Exception e) { Log("principal: " + e); } });
        }

        async void Abrir(string url, string cabecalhos)
        {
            Parar();
            int minha = ++sessao;
            posMs = 0;
            try
            {
                var p = new Player();
                player = p;
                p.PlaybackCompleted += (s, e) => { if (minha == sessao) NvVid.Evento(EV_FIM, 0, 0); };
                p.ErrorOccurred += (s, e) => { if (minha == sessao) { Log("erro " + e.Error); NvVid.Evento(EV_ERRO, (int)e.Error, 0); } };
                p.BufferingProgressChanged += (s, e) => { if (minha == sessao) NvVid.Evento(EV_BUFFER, e.Percent, 0); };
                p.PlaybackInterrupted += (s, e) => { if (minha == sessao) { Log("interrompido: " + e.Reason); NvVid.Evento(EV_PAUSADO, 0, 0); } };
                p.SubtitleUpdated += (s, e) => { if (minha == sessao) NvVid.Legenda(e.Text ?? "", (int)e.Duration); };
                foreach (var linha in (cabecalhos ?? "").Split('\n'))
                {
                    int i = linha.IndexOf(':');
                    if (i <= 0) continue;
                    string nome = linha.Substring(0, i).Trim(), valor = linha.Substring(i + 1).Trim();
                    if (nome.Equals("User-Agent", StringComparison.OrdinalIgnoreCase)) p.UserAgent = valor;
                    else if (nome.Equals("Cookie", StringComparison.OrdinalIgnoreCase)) p.Cookie = valor;
                    else Log("cabecalho ignorado pelo player: " + nome);
                }
                p.SetSource(new MediaUriSource(url));
                p.Display = fazDisplay();
                p.DisplaySettings.Mode = PlayerDisplayMode.LetterBox;
                await p.PrepareAsync();
                if (minha != sessao) { p.Unprepare(); p.Dispose(); return; }
                int dur = 0;
                try { dur = p.StreamInfo.GetDuration(); } catch { }
                try { var v = p.StreamInfo.GetVideoProperties(); NvVid.Evento(EV_TAMANHO, v.Size.Width, v.Size.Height); } catch { }
                int nAudio = Faixas(p);
                NvVid.Evento(EV_PRONTO, dur, 0);
                p.Start();
                NvVid.Evento(EV_TOCANDO, 0, 0);
                try { AoTocar?.Invoke(); } catch (Exception e) { Log("ao tocar: " + e.Message); }
                // Alguns contêineres/HLS so publicam as faixas de audio depois
                // que a reproducao comeca: le de novo, uma vez.
                if (nAudio == 0)
                {
                    await System.Threading.Tasks.Task.Delay(2000);
                    if (minha == sessao && player == p) Faixas(p, true);
                }
            }
            catch (Exception e)
            {
                Log("abrir: " + e);
                if (minha == sessao) NvVid.Evento(EV_ERRO, -1, 0);
            }
        }

        // CANARIO (#137, Samsung TV Plus tocando por baixo do Nuvio). O relato:
        // o som do canal so para quando um filme comeca, isto e, quando um
        // Player deste arquivo prepara e toca. A aposta (NAO provada) e que e o
        // gerenciador de recursos da TV que tira o decodificador/saida de audio
        // do TV Plus nesse momento. Entao, logo que a janela sobe, o host 6+
        // toca pelo MESMO caminho do filme (Player + Display da janela NUI) um
        // clipe de 2 s preto e mudo (res/silencio.mp4, H.264 Main + AAC-LC) por
        // ~1 s e solta tudo (Stop/Unprepare/Dispose), como ao fim de um filme.
        // Nada fica preso: o filme de verdade e a saida seguem como hoje. So o
        // host 6+ (Program.cs) chama isto; no 4/5 `primer` e sempre null e os
        // SoltaPrimer() de Parar/PausarPeloSistema nao fazem nada.
        Player primer;
        int primerGen;

        public async void PrimeAudio(string arquivo)
        {
            int minha = ++primerGen;
            Log("[audio] prime begin " + Path.GetFileName(arquivo));
            try
            {
                if (!File.Exists(arquivo)) { Log("[audio] prime fail sem arquivo " + arquivo); return; }
                if (player != null) { Log("[audio] prime skip: player do app ja aberto"); return; }
                var p = new Player();
                primer = p;
                p.ErrorOccurred += (s, e) => Log("[audio] prime erro do player " + e.Error);
                p.PlaybackInterrupted += (s, e) => Log("[audio] prime interrompido " + e.Reason);
                p.SetSource(new MediaUriSource(arquivo));
                p.Display = fazDisplay();
                p.DisplaySettings.Mode = PlayerDisplayMode.LetterBox;
                var prep = p.PrepareAsync();
                // Excecao de um prepare abandonado (timeout) nao pode sobrar solta.
                _ = prep.ContinueWith(t => { var _e = t.Exception; }, System.Threading.Tasks.TaskContinuationOptions.OnlyOnFaulted);
                if (await System.Threading.Tasks.Task.WhenAny(prep, System.Threading.Tasks.Task.Delay(8000)) != prep)
                {
                    Log("[audio] prime fail prepare demorou mais de 8 s");
                    return;
                }
                await prep;
                if (minha != primerGen) { Log("[audio] prime cancelado (filme/saida antes de preparar)"); return; }
                Log("[audio] prime prepared");
                p.Start();
                Log("[audio] prime started");
                await System.Threading.Tasks.Task.Delay(1200);
                if (minha != primerGen) { Log("[audio] prime cancelado (filme/saida durante o clipe)"); return; }
                Log("[audio] prime ok");
            }
            catch (Exception e) { Log("[audio] prime fail " + e.GetType().Name + ": " + e.Message); }
            finally { if (minha == primerGen) SoltaPrimer(); }
        }

        void SoltaPrimer()
        {
            primerGen++;
            var p = primer;
            primer = null;
            if (p == null) return;
            try { if (p.State == PlayerState.Playing || p.State == PlayerState.Paused) p.Stop(); } catch (Exception e) { Log("[audio] prime stop: " + e.Message); }
            try { if (p.State != PlayerState.Idle) p.Unprepare(); } catch (Exception e) { Log("[audio] prime unprepare: " + e.Message); }
            try { p.Dispose(); } catch (Exception e) { Log("[audio] prime dispose: " + e.Message); }
            Log("[audio] prime released");
        }

        public void Parar()
        {
            SoltaPrimer();
            sessao++;
            var p = player;
            player = null;
            if (p == null) return;
            try { if (p.State == PlayerState.Playing || p.State == PlayerState.Paused) p.Stop(); } catch { }
            try { if (p.State != PlayerState.Idle) p.Unprepare(); } catch { }
            try { p.Dispose(); } catch { }
        }

        // SAIDA do app (canario de janela, #137 "fechar deixa a TV preta"): o
        // mesmo Parar(), mas com o plano de video devolvido de forma explicita
        // (Display nenhum com o player ja em Idle, antes do Dispose) e uma linha
        // por passo no log. Cada passo isolado: nenhum segura a saida.
        public void Encerrar()
        {
            SoltaPrimer();
            sessao++;
            var p = player;
            player = null;
            if (p == null) { Log("[janela] saida: sem player aberto"); return; }
            PlayerState st = PlayerState.Idle;
            try { st = p.State; } catch { }
            try { if (st == PlayerState.Playing || st == PlayerState.Paused) p.Stop(); } catch (Exception e) { Log("[janela] saida: stop " + e.Message); }
            try { if (p.State != PlayerState.Idle) p.Unprepare(); } catch (Exception e) { Log("[janela] saida: unprepare " + e.Message); }
            try { p.Display = null; } catch (Exception e) { Log("[janela] saida: display nenhum " + e.GetType().Name + ": " + e.Message); }
            try { p.Dispose(); } catch (Exception e) { Log("[janela] saida: dispose " + e.Message); }
            Log("[janela] saida: player de " + st + " solto (stop/unprepare/display/dispose)");
        }

        void Pausar(bool pausa)
        {
            if (player == null) return;
            try
            {
                if (pausa && player.State == PlayerState.Playing) { player.Pause(); NvVid.Evento(EV_PAUSADO, 0, 0); }
                else if (!pausa && player.State == PlayerState.Paused) { player.Start(); NvVid.Evento(EV_TOCANDO, 0, 0); }
            }
            catch (Exception e) { Log("pausar: " + e.Message); }
        }

        async void Buscar(int ms)
        {
            if (player == null) return;
            try { posMs = ms; await player.SetPlayPositionAsync(ms, false); }
            catch (Exception e) { Log("buscar: " + e.Message); }
        }

        void Janela(int x, int y, int w, int h)
        {
            if (player == null) return;
            try
            {
                // QUADRO CHEIO SEM ZOOM: LetterBox, o mesmo caminho da reproducao
                // normal nos 4 hosts. A reproducao cheia pede exatamente a tela
                // (0,0,telaW,telaH), entao esta guarda continua sendo um no-op
                // para ela — 6+ e o host 4/5 nao mudam.
                //
                // ZOOM (#178): o recorte de fonte emulado (src/video_tpk.c
                // video_janela_fonte) manda um ROI que RECUA a origem para
                // negativo ou ESTOURA a tela, para que a fatia desejada do quadro
                // preencha o destino. Antes, `w >= telaW && h >= telaH` engolia um
                // ROI ampliado ancorado em 0,0 de volta para LetterBox, matando o
                // zoom. Agora so o quadro EXATO da tela vira LetterBox; qualquer
                // ROI de zoom (origem negativa OU maior que a tela) passa cru ao
                // SetRoi.
                if (x == 0 && y == 0 && w == telaW && h == telaH) { player.DisplaySettings.Mode = PlayerDisplayMode.LetterBox; return; }
                player.DisplaySettings.Mode = PlayerDisplayMode.Roi;
                player.DisplaySettings.SetRoi(new Rectangle(x, y, w, h));
            }
            catch (Exception e) { Log("janela: " + e.Message); }
        }
    }
}
