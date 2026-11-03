# D0 — Commit the pending label rename first

Status: ready-for-agent
Blocked by: none
External: none
Size: S
Part of: ../spec.md

**Why:** the tree has 35 modified files (OpenSeek→SeekDB labels) and the hint diff must be reviewable.
**Scope:** `Nuvio-native-tizen-fixes`.
**Do:** review and commit the label-only changes as one commit before touching `minipool.c`/`miniatura.c`.
**Acceptance:** `git status` clean; hint work starts from a known base.
**Tests:** manual — the commit diff contains only label/string changes, no behavior change.

## Comments
