#!/bin/bash
# tools/parity-shot-screen.sh — captura de telas REAIS via fixture (Goal-C).
#
# O --preview sozinho nao tem conta/dados: toda captura mostra a capa. Este
# script sobe o app com a fixture (src/fixture.*, -DNV_FIXTURE -DNV_SHOT_HOOKS,
# NUVIO_FIXTURE=1), que injeta catalogo/salvos/fontes/relacionados e executa
# saltos de tela por /tmp/nuvio-fixture ("screen:<nome>"). Para cada tela
# espera "fixture: pronta <nome>" no log e captura 2 PNGs pelo protocolo
# /tmp/nuvio-shot-req (ver capturaSeSolicitado em src/main.c).
#
#   bash tools/parity-shot-screen.sh [--screens "home detail sheet player postplay search library"]
#                                    [--out evidencias-parity/fixtures-demo]
#
# Telas com passo extra de teclado (igual ao usuario faria):
#   player -> "up" sobe o OSD; search -> "texto:<termo>" digita a busca.
set -euo pipefail
cd "$(dirname "$0")/.."

SCREENS="home detail sheet player postplay search library"
OUT=""
PULAR_BUILD=0
TIMEOUT_PRONTA=240
TIMEOUT_SHOT=15
TERMO_BUSCA="matrix"

# Dispensa os cartoes de primeira vez antes de subir o app: cada marca "1\n"
# e o "visto" que o cartao gravaria ao ser dispensado (ver marcarVisto em
# src/novidades*.c; diagnostico espera "versao=1"). Sem isto, toda passada
# comecaria na capa do tour 2.0 em vez da home. A fixture mantem a dispensa
# por tecla como rede de seguranca para qualquer cartao nao listado aqui.
semear_marcas() { # $1 = dir dados
  local m
  for m in novidades-11 novidades-12 novidades-13 novidades-131 novidades-132 \
      novidades-133 novidades-134 novidades-139 novidades-14-celebracao \
      novidades-142 novidades-150 novidades-152-ui novidades-160-ui \
      novidades-170-ui novidades-172-ui novidades-174-ui novidades-180-ui \
      novidades-20-guia novidades-201 novidades-202 novidades-guia \
      aviso-log registro-aberto salvos-intro; do
    printf '1\n' > "$1/$m.txt"
  done
  printf 'versao=1\n' > "$1/diagnostico-otimizacao-intro.cfg"
  # Telemetria: "0" = recusada de antemao (a pergunta nunca abre na passada).
  printf '0\n' > "$1/telemetria-perguntado.txt"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --screens) SCREENS="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --termo) TERMO_BUSCA="$2"; shift 2 ;;
    --no-build) PULAR_BUILD=1; shift ;;  # reusa o binario atual (capturas sem recompilar)
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "parity-shot-screen.sh: opcao desconhecida: $1" >&2; exit 2 ;;
  esac
done

[ -z "$OUT" ] && OUT="evidencias-parity/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
LOG="$OUT/app.log"
MANIFEST="$OUT/manifest.txt"

echo "== parity-shot-screen: build com fixture =="
if [ "$PULAR_BUILD" = 1 ]; then
  echo "(pulando build por --no-build)"
else
  NUVIO_EXTRA_CFLAGS="-DNV_FIXTURE -DNV_SHOT_HOOKS" bash tools/linux.sh --build
fi

echo "== parity-shot-screen: limpa protocolo e instancias antigas =="
# So instancias com CWD neste repo: o binario e lancado por caminho relativo
# (./build/linux/...), entao o padrao casa as lanes vizinhas tambem — o CWD
# filtra. Nunca pkill pelo padrao sozinho.
for p in $(pgrep -f 'build/linux/nuvio-native-legacy' 2>/dev/null); do
  if [ "$(readlink "/proc/$p/cwd" 2>/dev/null)" = "$PWD" ]; then kill "$p" 2>/dev/null || true; fi
done
sleep 1
rm -f /tmp/nuvio-shot-req /tmp/nuvio-shot.bmp /tmp/nuvio-key /tmp/nuvio-fixture

BIN="$PWD/build/linux/nuvio-native-legacy"
ART="$PWD/deploy/app/art"
export NUVIO_UI_PREVIEW=1
export NUVIO_FIXTURE=1
export NUVIO_DADOS="$OUT/dados"
mkdir -p "$NUVIO_DADOS"
semear_marcas "$NUVIO_DADOS"

echo "== parity-shot-screen: sobe app sob xvfb (log: $LOG) =="
xvfb-run -a -s "-screen 0 1920x1080x24" "$BIN" "$ART" > "$LOG" 2>&1 &
APP=$!
limpar() { kill "$APP" 2>/dev/null || true; }
trap limpar EXIT

echo "== parity-shot-screen: espera fixture injetar =="
for ((i = 0; i < 120; i++)); do
  if grep -q 'fixture: injeta' "$LOG" 2>/dev/null; then break; fi
  if ! kill -0 "$APP" 2>/dev/null; then
    echo "ERRO: app morreu antes de injetar; ultimas linhas:" >&2
    tail -n 30 "$LOG" >&2
    exit 1
  fi
  sleep 1
done
grep -q 'fixture: injeta' "$LOG" || { echo "ERRO: fixture nao injetou em 120s" >&2; tail -n 30 "$LOG" >&2; exit 1; }
grep 'fixture:' "$LOG" | tail -n 5

captura_quadro() { # $1 = nome
  rm -f /tmp/nuvio-shot.bmp
  printf 'shot-%s\n' "$1" > /tmp/nuvio-shot-req
  ok=0
  for ((t = 0; t < TIMEOUT_SHOT * 10; t++)); do
    if [ -s /tmp/nuvio-shot.bmp ]; then ok=1; break; fi
    sleep 0.1
  done
  if [ "$ok" = 0 ]; then
    echo "ERRO: quadro '$1' sem BMP em ${TIMEOUT_SHOT}s" >&2
    exit 1
  fi
  mv /tmp/nuvio-shot.bmp "$OUT/$1.bmp"
  python3 - "$OUT/$1.bmp" "$OUT/$1.png" <<'PYEOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
PYEOF
  rm -f "$OUT/$1.bmp"
  echo "  $1.png"
}

injeta_tecla() { # $1 = linha para /tmp/nuvio-key
  printf '%s\n' "$1" > /tmp/nuvio-key
  for ((t = 0; t < 50; t++)); do
    [ -s /tmp/nuvio-key ] || return 0
    sleep 0.1
  done
  echo "AVISO: tecla '$1' nao consumida em 5s (segue mesmo assim)" >&2
}

ir_para() { # $1 = tela
  printf 'screen:%s\n' "$1" > /tmp/nuvio-fixture
  for ((i = 0; i < TIMEOUT_PRONTA; i++)); do
    if grep -q "fixture: pronta $1" "$LOG" 2>/dev/null; then return 0; fi
    if grep -q "fixture: falha $1" "$LOG" 2>/dev/null; then
      echo "ERRO: fixture falhou em '$1'" >&2
      grep 'fixture:' "$LOG" | tail -n 8 >&2
      return 1
    fi
    if ! kill -0 "$APP" 2>/dev/null; then
      echo "ERRO: app morreu indo para '$1'" >&2
      tail -n 20 "$LOG" >&2
      exit 1
    fi
    sleep 1
  done
  echo "ERRO: timeout indo para '$1'" >&2
  grep 'fixture:' "$LOG" | tail -n 8 >&2
  return 1
}

for tela in $SCREENS; do
  echo "== tela: $tela =="
  ir_para "$tela" || exit 1
  case "$tela" in
    player)
      injeta_tecla "up"   # D-pad sobe o OSD, igual ao usuario
      sleep 7             # a 0,6 fps a tecla assenta e o fade precisa de quadros
      captura_quadro "$tela-1-osd"
      sleep 3
      captura_quadro "$tela-2-osd"
      ;;
    search)
      injeta_tecla "texto:$TERMO_BUSCA"  # digita na busca, fora da grade
      sleep 8             # filtro + fileiras + arte a 0,6 fps
      captura_quadro "$tela-1-result"
      sleep 3
      captura_quadro "$tela-2-result"
      ;;
    *)
      captura_quadro "$tela-1"
      sleep 0.6
      captura_quadro "$tela-2"
      ;;
  esac
done

limpar
trap - EXIT
wait "$APP" 2>/dev/null || true

NPNG="$(ls "$OUT"/*.png 2>/dev/null | wc -l)"
[ "$NPNG" -ge 1 ] || { echo "ERRO: nenhum PNG em $OUT" >&2; exit 1; }

{
  echo "parity-shot-screen $(date -u +%FT%TZ)"
  echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo ?)"
  echo "screens='$SCREENS' out=$OUT"
  echo "pngs: $NPNG"
  ls "$OUT"/*.png | xargs -n1 basename
  echo "--- fixture (sequencia) ---"
  grep 'fixture:' "$LOG" || echo "(nenhum)"
} > "$MANIFEST"
cat "$MANIFEST"
echo "== parity-shot-screen: OK ($NPNG PNG em $OUT) =="
