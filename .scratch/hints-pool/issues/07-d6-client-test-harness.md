# D6 — Client test harness

Status: ready-for-agent
Blocked by: 02, 03
External: none
Size: M
Part of: ../spec.md

**Why:** `minipool.c` currently has no unit harness for URLs/keys.
**Scope:** new `tests/` target or a small host-side test binary.
**Do:** extract pure helpers (key derivation, URL build, mapping) behind `#ifdef` test hooks; add a `make test-minipool` target.
**Acceptance:** `make test-minipool` runs without a TV.
**Depends:** D1, D2.

## Comments
