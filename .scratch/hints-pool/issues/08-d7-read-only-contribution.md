# D7 — Handle read-only channel contributions

Status: completed (commit `4a2270d6`)
Blocked by: 03
External: openseek-telegram#13 (B7 — read-only channel routing; complete)
Size: M
Part of: ../spec.md

**Why:** a `POOL_MODE=channel` node pointed at a foreign channel returns `403 read_only_channel` with a `contribute_url`. The client currently maps any write failure to `mp_contribution_denied` and gives up (`src/minipool.c:539-575`).
**Scope:** `src/minipool.c`, `src/miniatura.c`, `src/ajustes.c`, i18n files.
**Do:**
- Parse the 403 JSON for `error` and `contribute_url`; store the result in minipool state.
- If `contribute_url` is a valid HTTPS intake endpoint, retry the same PUT exactly once against it. A Telegram bot link is for manual contribution and must be surfaced in Settings, not used as a PUT target. The client refuses redirects, so this is a direct retry, not a 3xx follow.
- Require `https://` and cap the URL length; reject anything else.
- If the retry succeeds, mark the contribution accepted. Otherwise surface a clear denial with the URL in settings/logs.
- Do not retry on other 4xx/5xx, and never loop.
**Acceptance:** read-only node + HTTPS intake endpoint: the package reaches the endpoint once; a Telegram link is shown for manual use; missing/invalid URL shows a clear denial; no infinite retry.
**Tests:** fake HTTP 403 with and without `contribute_url`; malformed JSON; overlong/non-https URL; retry-once behavior.
**Depends:** D2; external B7.

## Comments

- 2026-11-04: Done. `403 read_only_channel` is parsed once; a valid HTTPS intake URL is retried exactly once with the same body and without forwarding the pool token. Telegram links are surfaced in Settings instead of being PUT targets. Missing/malformed/over-long URLs are logged and denied without a retry. Covered by `tests/minipool_upload_conflict.c` (cases 6–15).

- 2026-11-04: OpenSeek B7 is complete and pushed on `integration/hints-pool`; D7 is unblocked and ready for client implementation.
