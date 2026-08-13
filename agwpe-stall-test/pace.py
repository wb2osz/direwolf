#!/usr/bin/env python3
"""Feed a file to stdout at a controlled byte rate, so direwolf keeps
receiving audio for the whole duration of the test."""
import os, sys, time

path = sys.argv[1]
rate = float(sys.argv[2])   # bytes per second
loops = int(sys.argv[3]) if len(sys.argv) > 3 else 1
chunk = 32768
interval = chunk / rate

out = sys.stdout.buffer
next_t = time.time()
for _ in range(loops):
    with open(path, 'rb') as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            try:
                out.write(b)
                out.flush()
            except BrokenPipeError:
                # Dire Wolf went away.  _exit avoids the interpreter's exit
                # flush raising BrokenPipeError again on the way out.
                os._exit(0)
            next_t += interval
            d = next_t - time.time()
            if d > 0:
                time.sleep(d)
