# D7 — Handle read-only channel contributions

Status: ready-for-agent
Blocked by: 03
External: openseek-telegram#13 (B7 — read-only channel routing)
Size: M
Part of: ../spec.md

**Why:** a `POOL_MODE=channel` node pointed at a foreign channel returns `403 read_only_channel` with a `contribute_url`. The client currently maps any write failure to `mp_contribution_denied` and gives up (`src/minipool.c:539-575`).
**Scope:** `src/minipool.c`, `src/miniatura.c`, `src/ajustes.c`, i18n files.
**Do:**
- Parse the 403 JSON for `error` and `contribute_url`; store the result in minipool state.
- If `contribute_url` is present, retry the same PUT exactly once against that URL. The client refuses redirects, so this is a direct retry, not a 3xx follow.
- Require `https://` and cap the URL length; reject anything else.
- If the retry succeeds, mark the contribution accepted. Otherwise surface a clear denial with the URL in settings/logs.
- Do not retry on other 4xx/5xx, and never loop.
**Acceptance:** read-only node + configured host: the package reaches the host; no host: the UI shows a clear denial and the contribute URL; no infinite retry.
**Tests:** fake HTTP 403 with and without `contribute_url`; malformed JSON; overlong/non-https URL; retry-once behavior.
**Depends:** D2; external B7.

## Comments
