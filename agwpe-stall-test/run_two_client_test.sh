#!/bin/bash
#
# Cross-client interference test.
#
#   ./run_two_client_test.sh <path-to-direwolf-binary> <label>
#
# Two AGWPE clients are connected at the same time:
#
#   victim  - perfectly well behaved, reads continuously, reports how many
#             frames it receives during each 5 second interval.
#   staller - reads briefly, then stops reading entirely for 40 seconds.
#
# The victim does nothing wrong and should be unaffected by the staller.
#
# Without the send-queue fix, the victim stops receiving frames completely
# for the whole duration of the *other* client's stall: the single DLQ
# consumer thread is blocked inside send() to the staller's socket, so it
# never gets as far as writing to the victim, and never processes another
# received frame for anybody.
#
# The script exits non-zero if the victim ever went an interval without
# receiving anything, so it can be used as a pass/fail check.

set -u
set +m		# no job-control chatter when we kill the pipeline at the end

BIN="${1:?usage: run_two_client_test.sh <direwolf-binary> <label>}"
LABEL="${2:?usage: run_two_client_test.sh <direwolf-binary> <label>}"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/work"
OUT="$WORK/dw-$LABEL.log"
VICTIM_OUT="$WORK/victim-$LABEL.log"
STALLER_OUT="$WORK/staller-$LABEL.log"
WAV="$WORK/test9600.wav"

mkdir -p "$WORK"
rm -f "$OUT" "$VICTIM_OUT" "$STALLER_OUT"

# Resolve the binary to an absolute path.  A bare name like "direwolf_dev"
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

if [ ! -f "$WAV" ]; then
  echo "error: run run_stall_test.sh first to generate $WAV" >&2
  exit 1
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
disown %% 2>/dev/null || true

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
  tail -20 "$OUT" >&2
  kill $DW_PID 2>/dev/null
  pkill -f "pace.py" 2>/dev/null
  exit 1
fi

sleep 3

echo "[$LABEL] attaching VICTIM client (reads continuously)"
python3 "$HERE/victim_client.py" 8000 62 5 > "$VICTIM_OUT" 2>&1 &
VICTIM_PID=$!

# Let the victim establish a baseline before the other client misbehaves.
sleep 8

echo "[$LABEL] attaching STALLER client (reads 3s, then stops reading 40s)"
python3 "$HERE/stall_client.py" 8000 3 40 > "$STALLER_OUT" 2>&1 &
STALLER_PID=$!

V_SEEN=0
S_SEEN=0
show_new () {	# $1 = logfile, $2 = seen-count varname
  local total seen
  eval "seen=\$$2"
  # tr strips the leading whitespace macOS wc emits, which would otherwise
  # turn the eval below into an assignment followed by a stray command.
  total=$(wc -l < "$1" 2>/dev/null | tr -d '[:space:]')
  total=${total:-0}
  if [ "$total" -gt "$seen" ]; then
    tail -n +$((seen + 1)) "$1" | sed "s/^/[$LABEL]   /"
    eval "$2=$total"
  fi
}

DIED=0
for i in $(seq 1 10); do
  sleep 5
  show_new "$STALLER_OUT" S_SEEN
  show_new "$VICTIM_OUT" V_SEEN
  # Resident size shows the DLQ backlog growing while the consumer thread
  # is blocked; liveness catches Dire Wolf dying outright.
  if kill -0 $DW_PID 2>/dev/null; then
    RSS=$(ps -o rss= -p $DW_PID 2>/dev/null | tr -d ' ')
    STATE="alive, rss=${RSS:-?}kB"
  else
    STATE="*** DIED ***"
    DIED=1
  fi
  echo "[$LABEL] t=+$((i*5))s  decoded=$(grep -c 'test packet' "$OUT" || true)  direwolf: $STATE"
done

wait $VICTIM_PID 2>/dev/null
VICTIM_RC=$?

kill $STALLER_PID 2>/dev/null
kill $DW_PID 2>/dev/null
pkill -f "pace.py" 2>/dev/null
wait 2>/dev/null

show_new "$STALLER_OUT" S_SEEN
show_new "$VICTIM_OUT" V_SEEN

echo "[$LABEL] --- Dire Wolf diagnostics ---"
grep -oE "Received frame queue is out of control|DLQ memory leak|Memory leak for packet objects|send queue full|send queue recovered|Only [0-9]+ of [0-9]+ bytes|Closing connection|has disappeared" "$OUT" \
    | sort | uniq -c

if [ "$DIED" -eq 1 ]; then
  echo "[$LABEL] FAIL: Dire Wolf died during the run"
  echo "[$LABEL]       (on macOS SOCK_SEND has no MSG_NOSIGNAL, so writing to a"
  echo "[$LABEL]        socket the peer already closed raises SIGPIPE)"
  VICTIM_RC=1
elif [ "$VICTIM_RC" -eq 0 ]; then
  echo "[$LABEL] PASS: the victim client kept receiving throughout the other client's stall"
else
  echo "[$LABEL] FAIL: the victim client was starved by the other client's stall"
fi
echo "[$LABEL] full log: $OUT"
exit $VICTIM_RC
