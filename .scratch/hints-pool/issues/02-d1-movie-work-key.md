# D1 — Work key for movies (the actual bug)

Status: ready-for-agent
Blocked by: 01
External: none
Size: S
Part of: ../spec.md

**Why:** `minipool_episode_key` returns 0 when `episodio <= 0` (`src/minipool.c:361-389`), so films never enter the candidate path and every source is a fresh miss.
**Scope:** `src/minipool.c`, `src/minipool.h`.
**Do:** rename to `minipool_work_key`; allow `(imdb, 0, 0)` for films; keep the existing hash input format for series compatibility, add an `openseek-work-v1` variant for films. Keep the 64-hex output.
**Acceptance:** a film with a valid IMDb produces a key; a film without IMDb produces 0 and the client falls back to hints-only lookup.
**Tests:** unit cases: movie, series, id-with-suffix, no IMDb, invalid IMDb.
**Depends:** D0.

## Comments
