#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/nuvio-minipool-pairing-test"
cc -O1 -ffunction-sections -fdata-sections -Isrc tests/minipool_pairing.c src/js.c \
  -Wl,--gc-sections -pthread -lcrypto -o "$out"
"$out"
echo "minipool OSP1 pairing flows: ok"
