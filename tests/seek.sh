#!/bin/bash
# Phase 0 of player_implementation.md: the seek policy's rules and its bench.
# See tests/seek.c.
#
#   bash tests/seek.sh
#
# Runs all three profiles and prints the baseline table. NV_BENCH_SEEK_MS sets
# the fake pipeline's settle latency (default 320 ms) when the numbers need to
# be read against a slower TV.
set -eu
cd "$(dirname "$0")/.."
bin="${TMPDIR:-/tmp}/nuvio-seek-tests"
trap 'rm -f "$bin"' EXIT
flags=()
if [ "${SANITIZE:-0}" = 1 ]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc tests/seek.c src/seek.c -Isrc -o "$bin" -std=gnu99 -O1 -g -Wall -Wextra \
  ${flags[@]+"${flags[@]}"}

echo "=== rules ==="
for profile in current parity1 parity2; do
  echo "-- $profile"
  "$bin" "$profile" >/dev/null
  echo "   ok"
done

echo
echo "=== bench (fake pipeline, 60 fps loop, 100 ms key repeat) ==="
for profile in current parity1 parity2; do
  "$bin" "$profile"
done
