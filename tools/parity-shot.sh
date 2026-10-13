#!/bin/bash
# tools/parity-shot.sh — laco de captura headless para evidencia de paridade.
#
# Constroi o app com a sonda de assentamento de mola (NV_EVIDENCIA_MOLA, ver
# src/anim.h), sobe-o sob xvfb em modo preview, espera assentar, captura uma
# rajada estatica e depois uma rajada de N quadros em intervalos fixos para
# CADA tecla de --keys (injetada via /tmp/nuvio-key, consumida pelo app; ver
# teclasInjetadas em src/main.c). O protocolo de quadro e /tmp/nuvio-shot-req
# nao-vazio (ver capturaSeSolicitado em src/main.c:623), BMP->PNG via PIL.
# Tudo vai para um diretorio proprio com manifest.txt. Re-executavel.
#
#   bash tools/parity-shot.sh [--frames 8] [--interval 0.15] [--settle 4]
#                             [--keys "right left"] [--out evidencias-parity/<auto>]
set -euo pipefail
cd "$(dirname "$0")/.."

FRAMES=8
INTERVAL=0.15
SETTLE=4
KEYS="right left"
OUT=""
TIMEOUT_FIRST=120
TIMEOUT_SHOT=15

while [ "$#" -gt 0 ]; do
  case "$1" in
    --frames) FRAMES="$2"; shift 2 ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    --settle) SETTLE="$2"; shift 2 ;;
    --keys) KEYS="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "parity-shot.sh: opcao desconhecida: $1" >&2; exit 2 ;;
  esac
done

[ -z "$OUT" ] && OUT="evidencias-parity/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
LOG="$OUT/app.log"
MANIFEST="$OUT/manifest.txt"
SEQ=0

echo "== parity-shot: build com sonda de mola =="
NUVIO_EXTRA_CFLAGS="-DNV_EVIDENCIA_MOLA" bash tools/linux.sh --build

echo "== parity-shot: limpa protocolo e instancias antigas =="
pkill -f 'lane/build/linux/nuvio-native-legacy' 2>/dev/null || true
sleep 1
rm -f /tmp/nuvio-shot-req /tmp/nuvio-shot.bmp /tmp/.nuvio-shot.tmp /tmp/nuvio-key

BIN="$PWD/build/linux/nuvio-native-legacy"
ART="$PWD/deploy/app/art"
export NUVIO_UI_PREVIEW=1
export NUVIO_DADOS="$OUT/dados"
mkdir -p "$NUVIO_DADOS"

echo "== parity-shot: sobe app sob xvfb (log: $LOG) =="
xvfb-run -a -s "-screen 0 1920x1080x24" "$BIN" "$ART" > "$LOG" 2>&1 &
APP=$!
limpar() { kill "$APP" 2>/dev/null || true; }
trap limpar EXIT

echo "== parity-shot: espera primeiro quadro (ate ${TIMEOUT_FIRST}s) =="
for ((i = 0; i < TIMEOUT_FIRST; i++)); do
  if grep -q 'primeiro quadro' "$LOG" 2>/dev/null; then break; fi
  if ! kill -0 "$APP" 2>/dev/null; then
    echo "ERRO: app morreu antes do primeiro quadro; ultimas linhas:" >&2
    tail -n 30 "$LOG" >&2
    exit 1
  fi
  sleep 1
done
if ! grep -q 'primeiro quadro' "$LOG" 2>/dev/null; then
  echo "ERRO: sem primeiro quadro em ${TIMEOUT_FIRST}s; ultimas linhas:" >&2
  tail -n 30 "$LOG" >&2
  exit 1
fi

echo "== parity-shot: settle ${SETTLE}s (entradas iniciais animam aqui) =="
sleep "$SETTLE"

captura_quadro() { # $1 = rotulo (vai ao nome do arquivo e ao pedido)
  SEQ=$((SEQ + 1))
  rm -f /tmp/nuvio-shot.bmp
  printf 'shot-%s\n' "$1" > /tmp/nuvio-shot-req
  ok=0
  for ((t = 0; t < TIMEOUT_SHOT * 10; t++)); do
    if [ -s /tmp/nuvio-shot.bmp ]; then ok=1; break; fi
    sleep 0.1
  done
  if [ "$ok" = 0 ]; then
    echo "ERRO: quadro '$1' sem BMP em ${TIMEOUT_SHOT}s; ultimas linhas:" >&2
    tail -n 5 "$LOG" >&2
    exit 1
  fi
  nome="frame-$(printf %03d "$SEQ")-$1"
  mv /tmp/nuvio-shot.bmp "$OUT/$nome.bmp"
  python3 - "$OUT/$nome.bmp" "$OUT/$nome.png" <<'PYEOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
PYEOF
  rm -f "$OUT/$nome.bmp"
  echo "  $nome.png"
}

rajada() { # $1 = prefixo da rajada
  for ((f = 1; f <= FRAMES; f++)); do
    captura_quadro "$1-f$f"
    sleep "$INTERVAL"
  done
}

injeta_tecla() { # $1 = nome (up/down/left/right/ok/back, ver codigoDaTecla)
  printf '%s\n' "$1" > /tmp/nuvio-key
  for ((t = 0; t < 50; t++)); do
    [ -s /tmp/nuvio-key ] || return 0
    sleep 0.1
  done
  echo "AVISO: tecla '$1' nao consumida em 5s (segue mesmo assim)" >&2
}

echo "== parity-shot: rajada estatica ($FRAMES quadros) =="
rajada static

n=0
for tecla in $KEYS; do
  n=$((n + 1))
  echo "== parity-shot: tecla '$tecla' + rajada de movimento =="
  injeta_tecla "$tecla"
  sleep 0.06
  rajada "key$n-$tecla"
done

limpar
trap - EXIT
wait "$APP" 2>/dev/null || true

NPNG="$(ls "$OUT"/frame-*.png 2>/dev/null | wc -l)"
[ "$NPNG" -ge 1 ] || { echo "ERRO: nenhum PNG em $OUT" >&2; exit 1; }

{
  echo "parity-shot $(date -u +%FT%TZ)"
  echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo ?)"
  echo "frames=$FRAMES interval=${INTERVAL}s settle=${SETTLE}s keys='$KEYS' out=$OUT"
  echo "pngs: $NPNG"
  echo "--- mola-evidencia (assentamentos medidos) ---"
  grep 'mola-evidencia:' "$LOG" || echo "(nenhum)"
  echo "--- captura (quadros servidos) ---"
  grep -c '^captura:' "$LOG" || echo 0
} > "$MANIFEST"
tail -n 30 "$MANIFEST"
echo "== parity-shot: OK ($NPNG PNG em $OUT) =="
