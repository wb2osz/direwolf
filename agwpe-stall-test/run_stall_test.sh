#!/bin/bash
#
# Reproduce the AGWPE stalled-client stall.
#
#   ./run_stall_test.sh <path-to-direwolf-binary> <label>
#
# Feeds Dire Wolf a paced 9600 baud audio stream on stdin, attaches an
# AGWPE client that enables 'k' and 'm', reads for 3 seconds, then stops
# reading for 40 seconds, and samples how many packets Dire Wolf decodes
# over time.
#
# On a build WITHOUT the send-queue fix, the decode count stops advancing
# entirely a few seconds into the stall and does not recover.  On a fixed
# build the rate stays flat throughout.

set -u
set +m		# no job-control chatter when we kill the pipeline at the end

BIN="${1:?usage: run_stall_test.sh <direwolf-binary> <label>}"
LABEL="${2:?usage: run_stall_test.sh <direwolf-binary> <label>}"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/work"
OUT="$WORK/dw-$LABEL.log"
CLIENT_OUT="$WORK/client-$LABEL.log"
WAV="$WORK/test9600.wav"
PKTS="$WORK/pkts.txt"

mkdir -p "$WORK"
rm -f "$OUT" "$CLIENT_OUT"

# Resolve the binary to an absolute path.  A bare name like "direwolf_bad"
# would otherwise be looked up in $PATH when used as a command word, even
# though it sits right here in the current directory.
if [ -x "$BIN" ]; then
  BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
else
  RESOLVED="$(command -v "$BIN" 2>/dev/null || true)"
  if [ -n "$RESOLVED" ]; then
    BIN="$RESOLVED"
  else
    echo "error: '$BIN' is not an executable file and is not on \$PATH" >&2
    echo "       (if it is in the current directory, ./$BIN also works)" >&2
    exit 1
  fi
fi

# gen_packets and direwolf are normally built side by side.
GEN="$(dirname "$BIN")/gen_packets"
if [ ! -x "$GEN" ]; then
  echo "error: gen_packets not found next to $BIN" >&2
  echo "       copy it there, or point at the binary in your build tree" >&2
  exit 1
fi

# A leftover direwolf from an earlier run would keep port 8000 and the
# client would silently measure the wrong process.
if command -v lsof > /dev/null 2>&1 && lsof -ti tcp:8000 > /dev/null 2>&1; then
  echo "error: something is already listening on port 8000:" >&2
  lsof -i tcp:8000 >&2
  echo "       kill it before running this test" >&2
  exit 1
fi

# 3000 packets of test traffic -> ~22 MB of 9600 baud audio.
if [ ! -f "$WAV" ]; then
  echo "generating test audio (once) ..."
  python3 - "$PKTS" <<'EOF'
import sys
with open(sys.argv[1], 'w') as f:
    for i in range(3000):
        f.write('WB2OSZ-1>APDW12,WIDE1-1:!4237.14NS07120.83W#test packet %d\n' % i)
EOF
  "$GEN" -B 9600 -o "$WAV" "$PKTS" > /dev/null 2>&1
fi

cat > "$WORK/test.conf" <<'EOF'
ADEVICE  stdin null
ACHANNELS 1
CHANNEL 0
MYCALL TEST-1
MODEM 9600
AGWPORT 8000
KISSPORT 0
EOF

# Paced at 4 MB/s and looped, so audio keeps arriving for the whole test
# and the AGWPE byte rate is high enough to actually fill the socket.
python3 "$HERE/pace.py" "$WAV" 4000000 20 | \
    "$BIN" -c "$WORK/test.conf" -t 0 stdin > "$OUT" 2>&1 &
DW_PID=$!
disown %% 2>/dev/null || true	# don't report the pipeline when we kill it

# Fail loudly and immediately if Dire Wolf did not come up, rather than
# reporting "decoded=0" ten times and blaming the client.
for _ in $(seq 1 20); do
  if python3 -c "
import socket, sys
s = socket.socket()
s.settimeout(0.3)
sys.exit(0 if s.connect_ex(('127.0.0.1', 8000)) == 0 else 1)
" 2>/dev/null; then
    READY=1
    break
  fi
  sleep 0.5
done

if [ "${READY:-0}" != "1" ]; then
  echo "error: Dire Wolf is not listening on port 8000 after 10s." >&2
  echo "       last lines of $OUT:" >&2
  tail -20 "$OUT" >&2
  kill $DW_PID 2>/dev/null
  pkill -f "pace.py" 2>/dev/null
  exit 1
fi

sleep 4
BASE=$(grep -c "test packet" "$OUT" || true)
echo "[$LABEL] decoded before client attaches: $BASE"

echo "[$LABEL] attaching client now: reads for 3s, then stops reading for 40s"

python3 "$HERE/stall_client.py" 8000 3 40 > "$CLIENT_OUT" 2>&1 &
CL_PID=$!

# The client's own progress is echoed into the timeline as it happens.
# Dumping it only at the end made it look like the client attached after
# the run rather than at the start of it.
CL_SEEN=0
show_new_client_lines () {
  local total
  total=$(wc -l < "$CLIENT_OUT" 2>/dev/null || echo 0)
  if [ "$total" -gt "$CL_SEEN" ]; then
    tail -n +$((CL_SEEN + 1)) "$CLIENT_OUT" | sed "s/^/[$LABEL]   /"
    CL_SEEN=$total
  fi
}

PREV=$BASE
for i in $(seq 1 10); do
  sleep 5
  NOW=$(grep -c "test packet" "$OUT" || true)
  show_new_client_lines		# events that happened during this interval
  echo "[$LABEL] t=+$((i*5))s  decoded=$NOW  (+$((NOW-PREV)) since last sample)"
  PREV=$NOW
done
show_new_client_lines

kill $CL_PID 2>/dev/null
kill $DW_PID 2>/dev/null
pkill -f "pace.py" 2>/dev/null
wait 2>/dev/null

show_new_client_lines		# anything the client emitted while shutting down

echo "[$LABEL] --- Dire Wolf diagnostics ---"
grep -oE "Received frame queue is out of control|DLQ memory leak|Memory leak for packet objects|send queue full|send queue recovered|Only [0-9]+ of [0-9]+ bytes|Closing connection|has disappeared" "$OUT" \
    | sort | uniq -c
echo "[$LABEL] FINAL decoded=$(grep -c 'test packet' "$OUT" || true)"
echo "[$LABEL] full log: $OUT"
