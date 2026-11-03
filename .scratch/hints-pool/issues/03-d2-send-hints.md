# D2 — Send raw hints on lookup and upload

Status: ready-for-agent
Blocked by: 02
External: openseek-telegram#2 (A1 — v2 hint manifest / wire encoding)
Size: M
Part of: ../spec.md

**Why:** the server has nothing to match on today; `minipool_enviar` sends only `duration_ms` + `episode_key` (`src/minipool.c:539-575`).
**Scope:** `src/minipool.c`, `src/minipool.h`.
**Do:**
- Add URL-encoded `imdb`, `season`, `episode`, `duration_ms`, `size_bytes`, `filename` to the candidate GET and the NVMT PUT.
- Source them from `MiniMeta` (`imdb`, `temporada`, `episodio`, `durMs`) and the work item (`w->nome`, `w->bytes`/`l->total`).
- Add a bounded percent-encoder; cap filename at 200 bytes.
- Keep the old request as fallback when the server returns 404/400 for unknown params.
- Follow A1's wire encoding (query params or bounded header). Do not invent a second encoding.
**Acceptance:** a captured request shows the full hint set; a server without the new route still works.
**Tests:** request-builder unit tests with spaces/UTF-8/special characters.
**Depends:** D1; external A1.

## Comments
