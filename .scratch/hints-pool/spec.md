# Spec: hint-based shared pool — Nuvio client (epic D)

Status: in progress — D0–D7 complete; D8 waits on OpenSeek F1 (#20).
Scope: the native client in this repo (`src/minipool.c`, `src/minipool.h`, `src/miniatura.c`, `src/minidrive.h`, UI strings).
Server spec: `Yatin-Code/openseek-telegram` GitHub issue #1; source ticket text `docs/TICKETS-HINTS-POOL.md` on `integration/hints-pool`.

## Goal

The client keeps generating NVMT locally on a miss, but on an exact-`fp` miss it asks the pool
for a hint-ranked candidate, applies it immediately, and contributes its own generated package
with the hints that make future reuse possible.

## Hard invariants

1. **NVMT only.** No VTT, no `/v1/previews`, no `/v1/lookup`, no sprite-sheet URL protocol.
2. `fp` remains the exact blob key (size + first/last 64 KiB). Hints never replace it.
3. No full-file hash and no hash of hints. Hints are raw IMDb/season/episode/duration/size/filename.
4. No request-time frame or sample verification. The server ranks; the client accepts the best
   candidate and applies a timing mapping.
5. Vercel is a supported server profile. Local/VPS uploads keep the direct PUT; Vercel uploads use a short-lived signed staging target and poll Telegram publication status. Downloads continue through OpenSeek from Telegram.
6. OpenSeek generation is a separate owner action in the browser dashboard. Nuvio still generates locally on a playback miss; the player never waits for server generation.

## What the client has today

- `MiniMeta` (`src/minidrive.h:47-54`): `fp[65]`, `nk[33]`, `durMs`, `imdb[24]`, `temporada`,
  `episodio`, `quadros`. Populated in `src/miniatura.c:845-868`.
- Available at that point: IMDb, season, episode, duration, filename (`w->nome`), declared size
  (`w->bytes`), actual remote size (`l->total`), and the content fingerprint.
- Not available in `MiniMeta`: TMDB. No full-file hash.
- Endpoints: only `/v1/pool/*` plus pairing. `/v1/previews` and `/v1/lookup` are not called.
- `minipool_episode_key` (`src/minipool.c:361-389`) returns 0 when `episodio <= 0`, so films never
  enter the candidate path.
- `minipool_buscar` (`src/minipool.c:509-537`) tries exact `fp`, then episode candidates.
- `minipool_enviar` (`src/minipool.c:539-575`) sends `duration_ms` and `episode_key` only.
- `miniatura.c:661` maps a candidate by `pts * j->dur / sourceDuration`.
- `miniatura.c:702+` fetches up to 8 candidates and sample-verifies frames before accepting one.

## Server contract this client targets

The server work is tracked in `openseek-telegram`:

| Server ticket | Issue | Contract |
|---|---|---|
| A1 | #2 | v2 hint manifest, wire encoding, candidate response fields |
| A4 | #5 | ranked candidates, `score`/`relation`/`a_num`/`a_den`/`b_ms` |
| A5 | #6 | legacy `/v1/pool/episodes/{key}` remains parseable |
| B7 | #13 | read-only channel returns `403 read_only_channel` + `contribute_url` |
| C3 | #16 | per-device offset/reject endpoints |

Candidate request:

```
GET /v1/pool/candidates?imdb=tt1234567&season=0&episode=0&duration_ms=...
    &size_bytes=...&filename=...
```

Candidate response: an array under `candidates`, best first. Each candidate carries `fp`,
`profile`, `duration_ms`, `n_slots`, `n_tiles`, `score`, `match`, `relation`,
`approximate: true`, `a_num`, `a_den`, `b_ms`, `source_duration_ms`.

Local/VPS upload:

```
PUT /v1/pool/{fp}/nvmt?profile=...&duration_ms=...
```

plus the same hints (A1 defines their encoding; D2 follows it). Vercel upload uses D8's signed staging and
finalization flow; the package body does not pass through a Vercel function.

Read-only contribution:

```
403 {"error":"read_only_channel","contribute_url":"https://..."}
```

## Ticket graph

| # | Ticket | Blocked by | External |
|---|---|---|---|
| 01 | D0 — commit the pending label rename | — | — |
| 02 | D1 — work key for movies | 01 | — |
| 03 | D2 — send raw hints on lookup and local/VPS upload | 02 | server #2 (A1) |
| 04 | D3 — accept the ranked candidate without sample verification | 03 | server #5 (A4) |
| 05 | D4 — apply linear timing mapping | 04 | — |
| 06 | D5 — reject/offset UI (phase 2) | 05 | server #16 (C3) |
| 07 | D6 — client test harness | 02, 03 | — |
| 08 | D7 — handle read-only channel contributions | 03 | server #13 (B7) |
| 09 | D8 — Vercel staged upload and Telegram acceptance polling | 03 | server #20 (F1) |

## Contribution topologies (client-visible)

- **Hosted channel:** the node can post. `PUT` succeeds; the client marks the contribution accepted.
- **Foreign channel:** the node returns `403 read_only_channel`. If `contribute_url` is present,
  D7 retries the same PUT once against a valid HTTPS intake URL. A Telegram bot link is shown in
  Settings for manual contribution; it is not treated as an HTTP upload endpoint. Otherwise the
  client surfaces the denial and any validated URL in Settings and logs the reason.
- The client never follows HTTP redirects or forwards the pool token to the contribution host.

## Deployment-specific transfers

- **Local/VPS:** Nuvio sends its NVMT package to OpenSeek with the existing direct PUT and hints.
- **Vercel:** Nuvio requests a short-lived upload target, sends the package directly to temporary Supabase Storage, finalizes with OpenSeek, and polls the upload ID until Telegram accepts it. Supabase and Telegram credentials never enter the client.
- **Downloads:** both profiles return the package as a stream through OpenSeek from Telegram; the client does not receive Telegram credentials or a Supabase download URL.

## Definition of done

Two different files of the same film (different `fp`, same IMDb id, near-equal duration):
the second client gets a candidate hit, downloads one NVMT package, applies `a*pts+b`, renders,
and performs **zero local frame extraction**. The log shows `hint hit fp=... score=... relation=...`.
If the node is read-only, an HTTPS intake URL receives exactly one PUT retry; a Telegram link is shown for manual use.

## Comments

- 2026-11-04: D0–D7 are implemented and committed on `integration/hints-pool`. D8 still waits on F1. See issues 08 and 09.
