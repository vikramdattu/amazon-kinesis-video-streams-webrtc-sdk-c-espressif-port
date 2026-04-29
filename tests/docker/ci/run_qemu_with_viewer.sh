#!/usr/bin/env bash
#
# Boot the firmware under qemu-xtensa as the master peer, give it
# enough time to set up signaling + ICE, then run the aiortc-based
# python_viewer against the same KVS channel. Records to
# `$OUT_PATH` (the viewer pipes its received tracks into an MKV).
#
# Cleans up on exit so failed runs don't leak processes.
#
# Caller's responsibility:
#   - cd into the example directory (e.g. examples/webrtc_classic)
#   - source the IDF env (`. /opt/esp/idf/export.sh`)
#   - have already built the firmware via `idf.py build`
#   - export OUT_PATH, LOG_PATH, KVS_CHANNEL_NAME, AWS_*, GITHUB_WORKSPACE,
#     and TEST_DURATION_SEC
#
# Args:
#   $@ — extra qemu arguments forwarded via `--qemu-extra-args=...`.
#        Always appends `-no-reboot`.

set -euo pipefail

EXTRA_ARGS=("$@")

mkdir -p "$(dirname "$OUT_PATH")"

# `( ... & echo $! > pid )` so we can SIGTERM the qemu wrapper later.
QEMU_EXTRA_FLAGS="-no-reboot ${EXTRA_ARGS[*]}"
( idf.py qemu --qemu-extra-args="${QEMU_EXTRA_FLAGS}" \
    > qemu_serial.log 2>&1 & echo $! > qemu.pid )

# Boot, time-sync, signaling-init typically lands by ~25 s.
sleep 25
echo "::group::Firmware boot snippet"
tail -40 qemu_serial.log
echo "::endgroup::"

# Run the python viewer; it self-terminates after $TEST_DURATION_SEC.
# `python -u` for unbuffered output so tee/$LOG_PATH actually has
# content if the script crashes mid-run.
python -u "${GITHUB_WORKSPACE}/tests/docker/python_viewer/viewer.py" 2>&1 | tee "$LOG_PATH" &
VIEWER_PID=$!
wait "$VIEWER_PID" || true

# Cleanup
if [ -f qemu.pid ]; then kill -TERM "$(cat qemu.pid)" 2>/dev/null || true; fi
pkill -TERM qemu-system-xtensa 2>/dev/null || true
sleep 2
pkill -KILL qemu-system-xtensa 2>/dev/null || true
