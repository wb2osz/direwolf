# Stalled-client reproduction

Scratch harness used to verify the "don't let a stalled client block
the rest of Dire Wolf" change.

Requires `python3` and a built `direwolf` + `gen_packets`.

## The bug

Every AGWPE client write went through a blocking `send()` on the caller's
thread. Two callers make that serious:

* `recv_process()` (`src/recv.c`) is the single DLQ consumer. It also drives
  `dl_timer_expiry()` and the connected-mode state machine, so blocking there
  stops all AX.25 link-layer timers and the DLQ grows without bound.
* `server_send_monitored()` is also called from `send_one_frame()` on the xmit
  thread, between `ptt_set(...,1)` and `ptt_set(...,0)` — so a stalled client
  could block the transmit thread with PTT still asserted.

The worst trigger is a client whose TCP connection is black-holed rather
than closed: an AGWPE terminal over ssh when the link drops, a laptop that
sleeps, a NAT entry that gets evicted. No FIN or RST ever arrives and
keepalive is off by default, so `send()` waits out the retransmission
timeout — about 15 minutes on Linux with the default `tcp_retries2`. A client
that merely crashes sends a RST and fails fast, which is why this only shows
up on flaky links.

## Reproducing

```sh
./run_stall_test.sh /path/to/build/src/direwolf mylabel
```

Feeds a 9600 baud stream on stdin, then attaches a client that enables
`k` + `m`, reads for 3 s, and stops reading for 40 s. First run generates
~22 MB of test audio into `work/` (takes a minute); later runs reuse it.

`gen_packets` must sit next to the direwolf binary you point at — it does in
a build tree, so the simplest thing is to copy both binaries here:

```sh
cp /path/to/build/src/gen_packets .
cp /path/to/build/src/direwolf ./direwolf_good
./run_stall_test.sh direwolf_good good_test
```

The test needs port 8000; it refuses to start if something already holds it,
since a leftover direwolf would otherwise be measured instead of yours.

Measured here, comparing the commit against its parent:

| t | without the fix | with the fix |
|---|---|---|
| +5s | +5394 | +5390 |
| +10s | +2878 | +5386 |
| +15s | **+34** | +5394 |
| +20s … +50s | **+0 each** | +5398 … +5422 |
| total decoded | 12598 | 58318 |

Without the fix, decoding stops completely and had not resumed 7 s after the
client finally closed the socket, having emitted 188,536 diagnostic lines:
5,007 × "Received frame queue is out of control", 18,559 × "DLQ memory leak",
164,970 × "Memory leak for packet objects". With the fix the decode rate is
flat throughout and the output is 2 × "send queue full" plus the disconnect.

## Cross-client interference

```sh
./run_two_client_test.sh /path/to/build/src/direwolf mylabel
```

Two clients are connected at once: a **victim** that reads continuously and
behaves perfectly, and a **staller** that stops reading for 40 s. The victim
does nothing wrong. It should be unaffected by the other client.

Without the fix, the victim receives **nothing at all** for the whole
duration of the other client's stall — the single DLQ consumer is blocked
in `send()` to the staller's socket, so it never reaches the victim and
never processes another received frame for anybody:

```
[dev2c]   [victim] +4386 frames this interval (total 14623)
[dev2c]   [victim] +108 frames this interval (total 14731)
[dev2c]   [victim] +0 frames this interval (total 14731)   <-- RECEIVING NOTHING
[dev2c]   [victim] +0 frames this interval (total 14731)   <-- RECEIVING NOTHING
      ... six consecutive intervals, ~30 seconds ...
[dev2c] t=+45s  decoded=19098  direwolf: *** DIED ***
```

Measured side by side:

| | without the fix | with the fix |
|---|---|---|
| frames the victim received | 14,734 | 66,081 |
| intervals the victim got nothing | **6** (~30 s) | **0** |
| Dire Wolf resident size | 13.7 MB → **78.6 MB**, still climbing | flat at 13.9 MB |
| Dire Wolf at end of run | **died, exit 141 (SIGPIPE)** | alive, exit 0 |

The script exits non-zero if the victim was ever starved or Dire Wolf died,
so it works as a pass/fail check.

### About that SIGPIPE

The crash above is a *separate, pre-existing* macOS-only bug, not something
the send-queue change introduces or targets. `SOCK_SEND` in `direwolf.h` is:

```c
#if __WIN32__ || __APPLE__
#define SOCK_SEND(s,data,size) send(s,data,size,0)
#else
#define SOCK_SEND(s,data,size) send(s,data,size, MSG_NOSIGNAL)
#endif
```

macOS has no `MSG_NOSIGNAL`, and Dire Wolf sets neither `SO_NOSIGPIPE` on
the socket nor a `SIGPIPE` handler, so on macOS *any* write to a socket
whose peer has already closed takes down the whole process. The unfixed
build reaches that state here because it is still blocked in `send()` to
the staller when the staller finally exits and closes its socket.

The fixed build never gets there — `SO_SNDTIMEO` drops the staller after
10 s, long before the client process exits — but the underlying exposure
is still in the fixed build too, since it uses the same macro. Worth a
separate one-line fix (`SO_NOSIGPIPE`, or `signal(SIGPIPE, SIG_IGN)`).

## Regression check

```sh
./run_healthy_test.sh /path/to/build/src/direwolf
```

The fix treats a short `send()` count as an error and drops the client, so
this confirms a well-behaved client is not caught by that stricter test.
Expect `closed_by_server=False`, `leftover_partial=0`, and no framing error.
Measured: 3.9 MB, 32,157 complete frames, stream ending exactly on a frame
boundary.

## Why short sends need handling at all

Adding `SO_SNDTIMEO` newly makes `send()` able to return `0 < n < len`.
AGWPE is a length-prefixed stream with no resync, so a truncated frame would
desynchronize the client for the rest of the session.

```sh
cc -O0 -DFRAMESZ=4000 -o partial_send_test partial_send_test.c && ./partial_send_test
```

Note this is platform-dependent, and macOS understates the risk:

* **macOS/BSD** — `sosend()` waits for `space >= resid` and is atomic below
  the high-water mark. At Dire Wolf's frame sizes (max ~2.3 KB: 36-byte header
  plus `128+AX25_MAX_PACKET_LEN`) it returns `EAGAIN`, which the plain
  `err <= 0` test already caught. Short counts only appear above ~3 KB.
* **Linux** — `tcp_sendmsg_locked()` copies whatever fits and returns `copied`
  on timeout, so a 300-byte frame landing on a nearly-full buffer returns a
  short count. This is the case the fix is actually for.

The Linux half is derived from the kernel implementation and the documented
`SO_SNDTIMEO` semantics in `socket(7)`; it was **not** reproduced on Linux
here. Everything else in this README was measured on macOS.

## Files

| file | what it does |
|---|---|
| `run_stall_test.sh` | main before/after reproduction |
| `run_two_client_test.sh` | shows one client's stall starving another |
| `run_healthy_test.sh` | confirms healthy clients aren't dropped |
| `stall_client.py` | AGWPE client that stops reading |
| `victim_client.py` | well-behaved client, reports per-interval frame counts |
| `good_client.py` | AGWPE client that reads and validates framing |
| `pace.py` | feeds a wav to stdout at a controlled byte rate |
| `partial_send_test.c` | standalone `SO_SNDTIMEO` short-send probe |

`work/` holds generated audio and logs. Delete it to reset.
