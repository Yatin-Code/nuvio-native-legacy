# D4 — Apply linear timing mapping

Status: completed (commit `56e1dee1`)
Blocked by: 04
External: none
Size: M
Part of: ../spec.md

**Why:** the client scales by `pts * j->dur / sourceDuration` (`src/miniatura.c:661`), which cannot express a speed ratio or an offset.
**Scope:** `src/miniatura.c`, `src/minipool.h`.
**Do:** carry `a_num/a_den/b_ms` in `MpoolCandidate`; map `pts -> (pts * a_num) / a_den + b_ms` with 64-bit math; clamp to `[0, durMs]`; skip out-of-range tiles.
**Acceptance:** a 24/25 candidate maps to the right slots; a same-length candidate is identity (`a=1,b=0`).
**Tests:** unit table for 24/25, 25/24, offset, clamping.
**Depends:** D3.

## Comments

- 2026-11-04: Added linear timing mapping and bounds handling in commit `56e1dee1`.
