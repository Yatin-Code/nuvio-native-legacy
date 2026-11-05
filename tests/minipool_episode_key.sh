#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/nuvio-minipool-episode-key-test"
cc -O1 -ffunction-sections -fdata-sections -Isrc tests/minipool_episode_key.c src/js.c src/admin.c \
  -Wl,--gc-sections -pthread -o "$out"
"$out"
echo "minipool episode identity keys: ok"
