#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/nuvio-minipool-invite-test"
cc -O1 -ffunction-sections -fdata-sections -Isrc tests/minipool_invite.c \
  -Wl,--gc-sections -pthread -o "$out"
"$out"
echo "minipool invite parsing: ok"
