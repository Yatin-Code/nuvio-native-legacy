#!/bin/bash
# Compila o spike NaCl para ARM (.nexe) e monta o .wgt (zip sem assinar).
# Precisa do Samsung NaCl SDK pepper_63 (1,68 GB, download publico):
#   https://developer.samsung.com/smarttv/file/55e181c4-fb5e-4d25-82df-f85b3b864dab
#   (e um .zip com extensao .bin; unzip 'pepper_63/*')
# NACL_SDK_ROOT=/caminho/pepper_63 bash build.sh
# No Mac com Apple Silicon o toolchain (Mach-O x86_64) roda por Rosetta.
# O .wgt sai SEM assinatura: assinar com o Tizen Studio (cert de autor+distribuidor
# com o DUID da TV) ou reassinar pelo Apps2Samsung.
set -euo pipefail
cd "$(dirname "$0")"
: "${NACL_SDK_ROOT:?defina NACL_SDK_ROOT}"
case "$(uname -s)" in Darwin) TC=mac_pnacl ;; *) TC=linux_pnacl ;; esac
# clang newlib ARM: .nexe estatico, sem .so de runtime a listar no .nmf.
CC="$NACL_SDK_ROOT/toolchain/$TC/bin/arm-nacl-clang"
OUT=out; rm -rf "$OUT"; mkdir -p "$OUT/pkg"
"$CC" -std=gnu99 -O2 -Wall -I"$NACL_SDK_ROOT/include" src/spike.c -o "$OUT/pkg/spike_arm.nexe" \
  -L"$NACL_SDK_ROOT/lib/clang-newlib_arm/Release" -lppapi_gles2 -lppapi_simple_real -lppapi -lpthread
cp pkg/index.html pkg/config.xml pkg/spike.nmf "$OUT/pkg/"
[ -f pkg/icon.png ] && cp pkg/icon.png "$OUT/pkg/"
(cd "$OUT/pkg" && zip -qr ../NuvioNaclSpike.wgt .)
ls -la "$OUT/NuvioNaclSpike.wgt"
