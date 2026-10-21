#!/bin/bash
# Colecoes (P7.2a): a maquina de estado do gerenciamento/FolderDetail. Sem GL e
# sem janela — so eventos e atualizar. Ver tests/colecoesui.c.
#
#   bash tests/colecoesui.sh
#
# NUVIO_DADOS aponta para uma pasta temporaria (o teste escreve categorias no
# disco); ela some no fim, deu certo ou nao.
set -eu
cd "$(dirname "$0")/.."

NUVIO_DADOS="$(mktemp -d "${TMPDIR:-/tmp}/nuvio-colui-XXXXXX")"
export NUVIO_DADOS
trap 'rm -rf "$NUVIO_DADOS"' EXIT

# SDL2 + GL: pkg-config (Linux, as MESMAS depencias de tools/linux.sh) ou
# Homebrew (macOS). Sem os pacotes, pula (nao mente).
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2 SDL2_image SDL2_ttf glesv2 egl zlib; then
  SDL_CFLAGS="$(pkg-config --cflags sdl2 SDL2_image SDL2_ttf glesv2 egl zlib)"
  SDL_LIBS="$(pkg-config --libs sdl2 SDL2_image SDL2_ttf glesv2 egl zlib) -ldl -pthread -lm"
elif [ -d /opt/homebrew/include/SDL2 ]; then
  SDL_CFLAGS="-I/opt/homebrew/include -I/opt/homebrew/include/SDL2"
  SDL_LIBS="-L/opt/homebrew/lib -lSDL2 -lSDL2_image -lSDL2_ttf -lz -framework OpenGL"
else
  echo "colecoesui: SDL2/GLES ausente (teste ignorado)"; exit 0
fi

sources=()
for source in src/*.c src/dts/*.c; do
  case "$source" in src/main.c) continue;; esac
  sources+=("$source")
done

# shellcheck disable=SC2086
cc "${sources[@]}" tests/colecoesui.c -Isrc -o /tmp/nuvio-colui-test \
  -O1 -g $SDL_CFLAGS $SDL_LIBS \
  -Wall -Wextra -Wno-deprecated-declarations -Wno-macro-redefined

/tmp/nuvio-colui-test 2>/dev/null | grep -vE '^\[(tex|arte|cat|desc|home|salvos|txt|jpeg|t|extras)\]'
