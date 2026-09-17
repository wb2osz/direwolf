#!/bin/bash
#
# Regression check for the partial-send fix.
#
#   ./run_healthy_test.sh <path-to-direwolf-binary>
#
# The fix treats a short send() count as an error and drops the client.
# This confirms that a normal, well-behaved client is NOT disconnected by
# that stricter check, and that the AGWPE byte stream stays intact.
#
# Expected: closed_by_server=False, leftover_partial=0, no framing error,
# and no "Only N of M bytes" on the Dire Wolf side.

set -u
set +m		# no job-control chatter when we kill the pipeline at the end

BIN="${1:?usage: run_healthy_test.sh <direwolf-binary>}"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/work"
OUT="$WORK/dw-healthy.log"
WAV="$WORK/test9600.wav"

if [ ! -f "$WAV" ]; then
  echo "error: run run_stall_test.sh first to generate $WAV" >&2
  exit 1
fi

mkdir -p "$WORK"
rm -f "$OUT"

# Resolve the binary to an absolute path.  A bare name like "direwolf_good"
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

if command -v lsof > /dev/null 2>&1 && lsof -ti tcp:8000 > /dev/null 2>&1; then
  echo "error: something is already listening on port 8000:" >&2
  lsof -i tcp:8000 >&2
  echo "       kill it before running this test" >&2
  exit 1
fi

python3 "$HERE/pace.py" "$WAV" 4000000 20 | \
    "$BIN" -c "$WORK/test.conf" -t 0 stdin > "$OUT" 2>&1 &
DW_PID=$!
disown %% 2>/dev/null || true	# don't report the pipeline when we kill it

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
python3 "$HERE/good_client.py" 8000 30
RC=$?

kill $DW_PID 2>/dev/null
pkill -f "pace.py" 2>/dev/null
wait 2>/dev/null

echo "--- Dire Wolf diagnostics ---"
grep -oE "send queue full|send queue recovered|Only [0-9]+ of [0-9]+ bytes|Closing connection|has disappeared" "$OUT" \
    | sort | uniq -c
echo "(only 'Closing connection'/'has disappeared' from the client exiting is expected)"
exit $RC
