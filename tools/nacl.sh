#!/bin/bash
# tools/nacl.sh — build do Nuvio como Native Client (.nexe ARM dentro de um .wgt)
# para TVs Samsung Tizen 4/5 (2018/2019, e 2020/21), onde o .tpk com .so propria
# e barrado pelo UEP. Espelha o alvo emscripten/WASM (tools/tizen.sh): a PAGINA e
# dona da rede e do video; o .nexe desenha a interface em GLES2.
#
# USO
#   NACL_SDK_ROOT=/caminho/pepper_63 tools/nacl.sh [glue|link|wgt]
#     glue  (padrao) compila so a cola PPAPI (src/plat_nacl.c) — nao precisa das
#           bibliotecas de midia; e a checagem que roda hoje.
#     link  compila o app INTEIRO e linka o .nexe (precisa das deps abaixo).
#     wgt   link + empacota out/Nuvio-nacl.wgt (SEM assinatura).
#
# DEPENDENCIAS ESTATICAS (arm-nacl, newlib) — o SDK NAO as traz. Aponte
#   NACL_DEPS=/caminho/prefixo  com include/ e lib/ contendo:
#     libSDL2.a  libSDL2_image.a  libSDL2_ttf.a  libfreetype.a  libwebp.a  libz.a
#   Receita para gera-las: ver o cabecalho "COMO GERAR AS DEPS" no fim do arquivo.
#   Se o SDL2 escolhido nao tiver backend NaCl (removido apos ~2.24), use um SDL2
#   <= 2.0.16 (tem src/video/nacl + src/audio/nacl) OU o shim de src/sdl_nacl_*.
#
# CREDENCIAIS: nada de art/*.txt entra no pacote (mesma regra do arm.sh/tizen.sh).
set -euo pipefail
cd "$(dirname "$0")/.."
RAIZ="$(pwd)"

ESTAGIO="${1:-glue}"
: "${NACL_SDK_ROOT:?defina NACL_SDK_ROOT apontando para o pepper_63}"
case "$(uname -s)" in Darwin) HOST=mac_pnacl ;; *) HOST=linux_pnacl ;; esac
CC="$NACL_SDK_ROOT/toolchain/$HOST/bin/arm-nacl-clang"
[ -x "$CC" ] || { echo "nacl.sh: nao achei arm-nacl-clang em $CC" >&2; exit 2; }
LIBDIR="$NACL_SDK_ROOT/lib/clang-newlib_arm/Release"

OUT="$RAIZ/out"
OBJ="$RAIZ/build/nacl-obj"
mkdir -p "$OBJ" "$OUT"

# -D de servidor/versao, iguais aos outros alvos (nunca entram no codigo-fonte).
ENV_D=$(tools/env.sh)

# Flags comuns. NV_NACL liga todos os ramos deste alvo; -pthread para newlib.
CFLAGS="-std=gnu99 -O2 -Wall -DNV_NACL -pthread -I$NACL_SDK_ROOT/include -Isrc"
NACL_LIBS="-L$LIBDIR -lppapi_gles2 -lppapi -lnacl_io -lpthread"

# ------------------------------------------------------------------ ESTAGIO glue
# Compila so a cola PPAPI. Verificavel sem nenhuma dependencia de midia.
"$CC" $CFLAGS -Wextra -c src/plat_nacl.c -o "$OBJ/plat_nacl.o"
echo "nacl.sh: cola PPAPI compilada -> $OBJ/plat_nacl.o"
[ "$ESTAGIO" = "glue" ] && exit 0

# ------------------------------------------------------------- deps para link/wgt
: "${NACL_DEPS:?para 'link'/'wgt' defina NACL_DEPS (prefixo com include/ e lib/ das deps estaticas)}"
for a in libSDL2 libSDL2_image libSDL2_ttf libfreetype libwebp libz; do
  [ -f "$NACL_DEPS/lib/$a.a" ] || { echo "nacl.sh: falta $NACL_DEPS/lib/$a.a" >&2; exit 2; }
done
CFLAGS="$CFLAGS -I$NACL_DEPS/include -I$NACL_DEPS/include/SDL2 -DNV_WEBP_ANIM"
DEP_LIBS="-L$NACL_DEPS/lib -lSDL2_image -lSDL2_ttf -lSDL2 -lfreetype -lwebp -lz"

# ------------------------------------------------------------------ ESTAGIO link
# Compila todos os .c do app (o shim de SDL entra como qualquer outro .c) e linka.
SRCS=$(ls src/*.c)
echo "nacl.sh: compilando o app ($(echo "$SRCS" | wc -w | tr -d ' ') arquivos)..."
OBJS=""
for c in $SRCS; do
  o="$OBJ/$(basename "${c%.c}").o"
  eval "\"$CC\" $CFLAGS $ENV_D -c \"$c\" -o \"$o\""
  OBJS="$OBJS $o"
done
"$CC" $CFLAGS $OBJS -o "$OUT/nuvio_arm.nexe" $DEP_LIBS $NACL_LIBS
echo "nacl.sh: linkado -> $OUT/nuvio_arm.nexe"
[ "$ESTAGIO" = "link" ] && exit 0

# ------------------------------------------------------------------- ESTAGIO wgt
PKG="$OUT/pkg-nacl"
rm -rf "$PKG"; mkdir -p "$PKG"
cp "$OUT/nuvio_arm.nexe" "$PKG/"
cp pkg/nacl/index.html pkg/nacl/config.xml pkg/nacl/app.nmf "$PKG/"
# icone: reusa o do deploy se existir
[ -f deploy/app/icon.png ] && cp deploy/app/icon.png "$PKG/icon.png"
[ -f pkg/nacl/icon.png ] && cp pkg/nacl/icon.png "$PKG/icon.png"
# assets reduzidos (fontes/arte) — mesma lista do alvo Tizen, se a receita existir
[ -x tools/tizen-art.sh ] && echo "nacl.sh: (assets: rode tools/tizen-art.sh e copie para $PKG/assets)"

# CREDENCIAIS: barra o empacotamento se qualquer arquivo de pessoa vazou.
if find "$PKG" -type f | grep -qiE 'addons\.txt|trakt\.txt|tmdb\.txt|mdblist|sessao\.txt|collections\.json'; then
  echo "nacl.sh: ABORTADO — credencial de pessoa no pacote" >&2; exit 4
fi

( cd "$PKG" && zip -qr "$OUT/Nuvio-nacl.wgt" . )
echo "nacl.sh: pacote (SEM assinatura) -> $OUT/Nuvio-nacl.wgt"
ls -la "$OUT/Nuvio-nacl.wgt"

# COMO GERAR AS DEPS (arm-nacl, newlib) — resumo; cada uma e um cross-build:
#   export NACL=$NACL_SDK_ROOT/toolchain/$HOST/bin
#   CC=$NACL/arm-nacl-clang AR=$NACL/arm-nacl-ar RANLIB=$NACL/arm-nacl-ranlib
#   zlib:     ./configure --static --prefix=$NACL_DEPS && make && make install
#   libwebp:  ./configure --host=arm-nacl --enable-static --disable-shared ...
#   freetype: ./configure --host=arm-nacl --without-zlib --without-png ...
#   SDL2:     ./configure --host=arm-nacl (backend nacl); ou usar o shim sdl_nacl_*
#   SDL2_image/_ttf: --host=arm-nacl apontando SDL2/freetype acima
