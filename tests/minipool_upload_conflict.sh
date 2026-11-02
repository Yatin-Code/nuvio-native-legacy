#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/nuvio-minipool-upload-conflict-test"
cc -O1 -ffunction-sections -fdata-sections -Isrc tests/minipool_upload_conflict.c src/js.c src/admin.c \
  -Wl,--gc-sections -pthread -o "$out"
"$out"
echo "minipool upload duration conflict retry and contribution denial: ok"
