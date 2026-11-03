# D3 — Accept the ranked candidate without sample verification

Status: completed (commit `e8854871`)
Blocked by: 03
External: openseek-telegram#5 (A4 — candidate ranking)
Size: L
Part of: ../spec.md

**Why:** the current loop fetches up to 8 candidates and decodes/compares frames (`src/miniatura.c:702+`), which is exactly the request-time cost that was rejected.
**Scope:** `src/miniatura.c`, `src/minipool.c`.
**Do:**
- Call the candidates endpoint once; take `candidates[0]` when `score >= 60` and `relation != length_differs`.
- Validate structurally only: NVMT magic/version/grid, slot bounds, tile count, package duration.
- On structural failure or HTTP error, try the next candidate (max 2), then fall back to local generation.
- Remove the frame-compare loop from the normal path; keep it behind a debug flag.
- Log one line: `hint hit fp=... score=... relation=...`.
- If the candidates route is absent (404), do exact-only and fall back to local generation; do not reintroduce sample verification.
**Acceptance:** a second source of the same film downloads one package and starts previews without decoding any local frame; a wrong candidate falls back safely.
**Tests:** fake server returning a ranked candidate; corrupt package; score below threshold; no candidates; route missing.
**Depends:** D2; external A4.

## Comments

- 2026-11-04: Switched candidate handling to ranked results and removed request-time sample verification in commit `e8854871`.
