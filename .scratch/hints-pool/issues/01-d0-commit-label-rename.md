# D0 — Commit the pending label rename first

Status: completed (commit `bac3b4ac`)
Blocked by: none
External: none
Size: S
Part of: ../spec.md

**Why:** the tree had 35 modified files (OpenSeek→SeekDB labels) and the hint diff needed to be reviewable.
**Scope:** `Nuvio-native-tizen-fixes`.
**Do:** review and commit the label-only changes as one commit before touching `minipool.c`/`miniatura.c`.
**Acceptance:** label-only changes committed before hint work. OpenSeek remains the product name; the label rename does not create a new public product or change the shared-pool direction in server issue #1.
**Tests:** manual — the commit diff contains only label/string changes, no behavior change.

## Comments
