#!/bin/bash
# End-to-end test of the real transmitter/receiver binaries.
#
# Mercury's broadcast port takes ONE client, so a real transfer is TX on one
# station and RX on another with the HF path in between.  bcast_relay.py stands
# in for the air: it accepts both clients and pipes the TX byte stream to the RX
# one.  The KISS framing, the reduced OTI/tag layout and the RaptorQ carousel
# are therefore all exercised exactly as on the air -- only the modem is absent.
#
# Usage: ./bcast_loopback_test.sh [mode] [bytes] [port]
set -u
MODE=${1:-0}
SIZE=${2:-20000}
PORT=${3:-18100}
D=$(mktemp -d)
RELAY=""; TX=""; RX=""
cleanup() { [ -n "$TX" ] && kill $TX 2>/dev/null; [ -n "$RX" ] && kill $RX 2>/dev/null
            [ -n "$RELAY" ] && kill $RELAY 2>/dev/null; rm -rf "$D"; }
trap cleanup EXIT

head -c "$SIZE" /dev/urandom > "$D/src.bin"

python3 ./bcast_relay.py "$PORT" & RELAY=$!
sleep 0.5

./receiver -i 127.0.0.1 -p "$PORT" "$D/out.bin" "$MODE" > "$D/rx.log" 2>&1 & RX=$!
sleep 0.5
./transmitter -i 127.0.0.1 -p "$PORT" "$D/src.bin" "$MODE" > "$D/tx.log" 2>&1 & TX=$!

# The receiver exits once the file is decoded; the transmitter carousels forever.
for i in $(seq 1 120); do
    kill -0 $RX 2>/dev/null || break
    sleep 1
done

kill $TX 2>/dev/null; TX=""
if kill -0 $RX 2>/dev/null; then
    echo "FAIL: receiver did not finish within 120s (mode=$MODE size=$SIZE)"
    tail -3 "$D/rx.log"; exit 1
fi
RX=""

if [ ! -s "$D/out.bin" ]; then echo "FAIL: no output file"; tail -3 "$D/rx.log"; exit 1; fi
if cmp -s "$D/src.bin" "$D/out.bin"; then
    echo "ok   mode=$MODE size=$SIZE -> recovered byte-identical ($(stat -c%s "$D/out.bin") B)"
    exit 0
else
    echo "FAIL: mode=$MODE size=$SIZE recovered file differs"
    ls -l "$D/src.bin" "$D/out.bin"; exit 1
fi
