#!/bin/sh
# Runs the C# tool against the Python simulator: update, refused image, and a
# hung image rolled back by the watchdog. Needs `make signed` (a signing key).
set -eu
cd "$(dirname "$0")/.."
FW="dotnet host/FwTool/bin/Release/net8.0/FwTool.dll"
PORT=5599
LOG=$(mktemp)

python3 sim/stm32sim.py --pubkey-from keys/signing_key.pem \
    --slot-a build/app-v1-signed.bin --port $PORT > "$LOG" 2>&1 &
SIM=$!
trap 'kill $SIM 2>/dev/null || true; rm -f "$LOG"' EXIT
sleep 1

$FW socket://localhost:$PORT send build/app-v2-signed.bin --watch 2 | tee /dev/stderr \
    | grep -q "app: update confirmed"

if $FW socket://localhost:$PORT send build/app-v2-image.bin --watch 0; then
    echo "unsigned image was accepted" >&2; exit 1
fi

$FW socket://localhost:$PORT send build/app-bad-signed.bin --watch 7 | tee /dev/stderr \
    | grep -q "last reset was the watchdog"

echo "integration test passed"
