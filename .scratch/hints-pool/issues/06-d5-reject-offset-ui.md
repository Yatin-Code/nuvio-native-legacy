# D5 — Reject/offset UI (phase 2)

Status: completed (commits `232f7e05`, `523f2350`)
Blocked by: 05
External: openseek-telegram#16 (C3 — per-device offsets and rejections)
Size: M
Part of: ../spec.md

**Why:** the correction path is what makes no-verification acceptable.
**Scope:** `src/miniatura.c`, `src/ajustes.c`, `src/ajustes_ux_tela.inc`, i18n files.
**Do:** a small "wrong previews?" action on the player overlay: shift ±, reject this source, retry. Wire to the C3 endpoints. Persist the per-device choice.
**Acceptance:** rejection re-requests the next candidate; offset persists across restart; no crash when offline.
**Depends:** D4; external C3.

## Comments

- 2026-11-04: Added per-device preview offset/reject controls and reset the OSD state between player sessions. The native TV UI follows Hallmark and Impeccable guidance while preserving existing player conventions. `make test-minipool`, `python3 tools/idiomas.py`, and `git diff --check` pass. A local player render/compile was unavailable because SDL2 development headers and libraries are absent from this host.
