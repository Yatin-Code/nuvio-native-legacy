# OpenSeek shared thumbnail pool

Native thumbnail builds (Samsung `.tpk`, LG webOS, and Android) can read and
contribute TV-generated thumbnail packages to an OpenSeek server. Each TV still
reads and decodes its own video locally. The pool receives only the bounded
`NVMT` thumbnail package; it never receives the video, playback URL, or source
headers.

## Connect a TV

Open **Settings → Miniatures** on the TV and set:

1. **OpenSeek server** to the server's HTTPS origin, with no path (for example
   `https://20.244.104.107`).
2. **OpenSeek TLS pin** to the server's `sha256//…` SPKI pin, obtained from the
   server owner through a trusted channel. The pin authenticates the server's
   self-signed certificate; do not accept a pin copied from an unverified
   network response.
3. **Link OpenSeek** to enter either the owner's temporary 8-character pairing
   code or a one-use 16-character invitation. The invitation must still be
   valid when entered.

The server owner must enable sharing. To contribute, the invitation must grant
contribution permission and the TV's **Share generated miniatures** setting
must be enabled. **Receive from OpenSeek** and **Share generated miniatures**
both start off, so each TV opts in separately. Receiving public packages needs
the server pin but does not require a paired account when the server exposes
its public read listener.

## Playback behavior

When receiving is enabled, Nuvio checks the shared pool after fingerprinting
the exact video. A complete 5-second package satisfies the seek bar directly;
a partial package is merged with locally generated frames. If more frames are
needed, the TV decodes them locally and, when contribution is enabled, uploads
the resulting package. The server merges new slots while preserving the first
accepted frame for each slot.

Google Drive remains the personal backup path. When a shared 5-second package
is incomplete, Nuvio can still check that TV owner's Drive set before decoding.
Drive and OpenSeek have separate settings and permissions.

The pool stores multiple profile variants for one exact video fingerprint,
including the 5-second and 10-second grids. Different cuts or encodes have
different fingerprints and stay separate.
