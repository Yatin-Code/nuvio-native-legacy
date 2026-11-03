# D8 — Vercel staged upload and Telegram acceptance polling

Status: waiting-external (OpenSeek F1/#20 is open; its B1/B2 prerequisites are complete)
Blocked by: 03
External: openseek-telegram#20 (F1 — Vercel deployment profile)
Size: L
Part of: `../spec.md`

**Why:** Vercel cannot accept the full NVMT package in the function request body, and Nuvio must know when Telegram—not temporary staging—has accepted its contribution.
**Scope:** `src/minipool.c`, `src/minipool.h`, HTTP/TLS helpers.
**Do:**
- Keep D2's direct `PUT /v1/pool/{fp}/nvmt` for local/VPS nodes.
- On a Vercel node, request a short-lived signed upload target with `fp`, profile, and validated hints; upload NVMT bytes directly to that target using CA-validated TLS.
- Finalize with OpenSeek, then poll the returned upload ID until Telegram accepts it, a terminal error occurs, or a bounded timeout expires.
- Use only the exact HTTPS target returned by OpenSeek; do not forward pool credentials to storage or follow arbitrary redirects.
- Retry safe transient upload/status failures within a bounded policy; never report contribution success before Telegram acceptance.
**Acceptance:** a package larger than the Vercel function body limit reaches Telegram through staging; the client reports success only after acceptance; local/VPS direct PUT is unchanged; credentials never appear in the storage request.
**Tests:** signed-target parsing, CA validation, status progression, timeout/retry, rejection and expiry cases.
**Depends:** D2; external F1.

## Comments

- 2026-11-04: OpenSeek B1/B2 are complete. D8 still waits on OpenSeek F1 (#20), which must define the signed staging and acceptance polling contract.
