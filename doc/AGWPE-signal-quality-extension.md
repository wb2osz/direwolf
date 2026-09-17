# AGWPE extension: per-frame signal quality in monitor frames

Optional, opt-in extension to Direwolf's AGWPE server.

## What it does

Carries Direwolf's per-frame demodulator metrics (`alevel.rec`, `.mark`,
`.space`, and the retry/FEC level) to AGWPE clients **in-band on the existing
`'U'/'S'/'I'` monitor frames**, so a client can display RF signal quality for
the stations it hears. Direwolf normally emits these only to stdout and the
APRS-only CSV log — never over AGWPE.

## Backwards compatibility

A stock AGWPE client (Xastir, APRSIS32, UI-View, YAAC, …) connected to a patched
Direwolf **still sees byte-identical frames**. Guaranteed by making the extension
per-client opt-in: the extra bytes are written only in the copy sent to a
client that explicitly enabled it.

## Protocol

**Carrier** — the `struct agwpe_s` header `user_reserved` field (bytes 32–35),
already `memset` to 0 in `server_send_monitored`. For opted-in clients only, on
`'U'/'S'/'I'` frames, four raw bytes:

```
user_reserved[0] = rec       (clamped 0..255)
user_reserved[1] = mark      (0..255; 0xFF = N/A for non-AFSK)
user_reserved[2] = space     (0..255; 0xFF = N/A)
user_reserved[3] = retries   (0 = clean copy)
```

Raw bytes (not `host2netle`). The monitor text payload and all lengths are
unchanged.

**Handshake** — new client→server datakind **`'q'`** ("enable extended signal
reporting"):
- On receipt, set `enable_ext_sig_to_client[client]` and reply once with an ACK
  frame (datakind `'q'`, payload `"ExtSig=1"`) so the client can confirm support.
- Reset the flag wherever `enable_send_monitor_to_client` is reset (new
  connection / init).
- A stock Direwolf treats `'q'` as an unknown command (benign default) → never
  sets the flag, never ACKs, `user_reserved` stays 0.

## Code touch points

- `src/server.h` — add `alevel_t alevel, int retries` to `server_send_rec_packet`
  and `server_send_monitored` declarations.
- `src/server.c`
  - `static int enable_ext_sig_to_client[MAX_NET_CLIENTS];` + resets (3 sites).
  - `case 'q':` in the client-command dispatch (+ ACK, + debug label).
  - Thread `alevel`/`retries` through `server_send_rec_packet` →
    `server_send_monitored`; write `user_reserved` for opted-in clients when
    `alevel.rec >= 0`.
- `src/direwolf.c` — pass `alevel, retries` at the two `server_send_rec_packet`
  call sites (`app_process_rec_packet` has both in scope).
- `src/xmit.c` — own-TX `'T'`: pass a `rec = -1` sentinel (no signal).
- `src/tt_user.c` — APRStt object: pass a `rec = -1` sentinel.

Sentinel `rec < 0` ⇒ `server_send_monitored` writes no reserved bytes, so `'T'`
and synthetic frames are unaffected.
