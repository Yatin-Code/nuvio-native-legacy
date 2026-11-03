# D5 — Reject/offset UI (phase 2)

Status: ready-for-agent
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
