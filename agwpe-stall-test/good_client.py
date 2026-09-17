#!/usr/bin/env python3
"""
A well-behaved AGWPE client: reads continuously and validates the framing.

Confirms the stricter `err != len` check does not cause spurious
disconnects, and that every frame arrives intact (no truncation).
"""
import socket, struct, sys, time

port = int(sys.argv[1])
run_for = float(sys.argv[2])

HDRLEN = 36


def agw_hdr(kind, port_=0):
    return struct.pack('<BBBBcBxx10s10sII', port_, 0, 0, 0, kind, 0,
                       b'TEST', b'', 0, 0)


s = socket.create_connection(('127.0.0.1', port))
s.sendall(agw_hdr(b'k'))
s.sendall(agw_hdr(b'm'))
s.settimeout(1.0)

buf = b''
frames = 0
kinds = {}
total = 0
t0 = time.time()
closed = False
while time.time() - t0 < run_for:
    try:
        b = s.recv(262144)
        if not b:
            closed = True
            print("[good] SERVER CLOSED THE CONNECTION -- unexpected!", flush=True)
            break
        buf += b
        total += len(b)
    except socket.timeout:
        continue

    # Parse complete AGWPE frames out of the stream.
    while len(buf) >= HDRLEN:
        dlen = struct.unpack('<I', buf[28:32])[0]
        if dlen > 100000:
            print(f"[good] FRAMING ERROR: absurd data_len {dlen} "
                  f"at frame {frames} -- stream desynchronized", flush=True)
            sys.exit(1)
        if len(buf) < HDRLEN + dlen:
            break
        kind = chr(buf[4])
        kinds[kind] = kinds.get(kind, 0) + 1
        buf = buf[HDRLEN + dlen:]
        frames += 1

print(f"[good] closed_by_server={closed}  bytes={total}  "
      f"complete_frames={frames}  leftover_partial={len(buf)}", flush=True)
print(f"[good] frame kinds: {kinds}", flush=True)
s.close()
