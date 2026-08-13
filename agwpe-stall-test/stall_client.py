#!/usr/bin/env python3
"""
Connect to Dire Wolf's AGWPE port, enable raw + monitor output,
then STOP READING to simulate a stalled / slow client application.

Prints how many bytes it managed to read before going silent, then
holds the socket open without reading for the requested duration.
"""
import socket, struct, sys, time

port = int(sys.argv[1])
stall_after = float(sys.argv[2])   # seconds of normal reading before stalling
hold = float(sys.argv[3])          # seconds to hold without reading


def agw_hdr(kind, port_=0):
    # struct agwpe_s: portx, reserved1,2,3, datakind, reserved4,
    # call_from[10], call_to[10], data_len (LE), user_reserved
    return struct.pack('<BBBBcBxx10s10sII', port_, 0, 0, 0, kind, 0,
                       b'TEST', b'', 0, 0)


s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# Shrink the receive buffer so back-pressure reaches the server quickly,
# the way a genuinely wedged client application would.
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
s.connect(('127.0.0.1', port))
print(f"[client] SO_RCVBUF = {s.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)}", flush=True)
s.sendall(agw_hdr(b'k'))   # toggle raw frames on
s.sendall(agw_hdr(b'm'))   # toggle monitor frames on
print(f"[client] connected, enabled 'k' and 'm'", flush=True)

# Read normally for a bit so we know data really is flowing.
s.settimeout(0.5)
got = 0
t0 = time.time()
while time.time() - t0 < stall_after:
    try:
        b = s.recv(65536)
        if not b:
            print("[client] server closed connection during read phase", flush=True)
            sys.exit(0)
        got += len(b)
    except socket.timeout:
        pass
print(f"[client] read {got} bytes, now STALLING (not reading) for {hold}s", flush=True)

# The critical part: hold the socket open and never read.
time.sleep(hold)
print(f"[client] done stalling, exiting", flush=True)
s.close()
