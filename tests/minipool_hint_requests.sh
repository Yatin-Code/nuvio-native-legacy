#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/nuvio-minipool-hint-requests-test"
cc -O1 -ffunction-sections -fdata-sections -Isrc tests/minipool_hint_requests.c src/js.c src/admin.c \
  -Wl,--gc-sections -pthread -o "$out"
"$out"
echo "minipool raw hint request encoding and compatibility fallback: ok"
