#!/usr/bin/env python3
"""
A second, perfectly well-behaved AGWPE client.

Connects, enables raw + monitor, then reads continuously and reports how
many complete AGWPE frames it received during each interval.

This is the client that should be unaffected by what any *other* client
does.  Without the send-queue fix its frame rate drops to zero the moment
a different client stops reading, because the single DLQ consumer thread
is blocked in send() to that other client's socket.
"""
import socket, struct, sys, time

port = int(sys.argv[1])
run_for = float(sys.argv[2])
interval = float(sys.argv[3]) if len(sys.argv) > 3 else 5.0

HDRLEN = 36


def agw_hdr(kind, port_=0):
    return struct.pack('<BBBBcBxx10s10sII', port_, 0, 0, 0, kind, 0,
                       b'VICTIM', b'', 0, 0)


s = socket.create_connection(('127.0.0.1', port))
s.sendall(agw_hdr(b'k'))
s.sendall(agw_hdr(b'm'))
s.settimeout(0.5)
print("[victim] connected and reading normally", flush=True)

buf = b''
frames = 0
last_frames = 0
total_bytes = 0
t0 = time.time()
next_report = t0 + interval
starved = 0

while time.time() - t0 < run_for:
    try:
        b = s.recv(262144)
        if not b:
            print("[victim] *** server closed our connection ***", flush=True)
            break
        buf += b
        total_bytes += len(b)
    except socket.timeout:
        pass

    while len(buf) >= HDRLEN:
        dlen = struct.unpack('<I', buf[28:32])[0]
        if dlen > 100000:
            print(f"[victim] *** FRAMING ERROR: absurd data_len {dlen} ***",
                  flush=True)
            sys.exit(1)
        if len(buf) < HDRLEN + dlen:
            break
        buf = buf[HDRLEN + dlen:]
        frames += 1

    now = time.time()
    if now >= next_report:
        got = frames - last_frames
        note = ''
        if got == 0:
            starved += 1
            note = '   <-- RECEIVING NOTHING'
        print(f"[victim] +{got} frames this interval "
              f"(total {frames}){note}", flush=True)
        last_frames = frames
        next_report += interval

print(f"[victim] finished: {frames} frames, {total_bytes} bytes, "
      f"{starved} interval(s) with nothing received", flush=True)
s.close()
sys.exit(1 if starved else 0)
