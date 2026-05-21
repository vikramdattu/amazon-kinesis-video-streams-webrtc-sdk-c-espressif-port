#!/usr/bin/env bash
#
# Same shape as run_qemu_with_viewer.sh, but runs the IDF Linux-target
# binary directly on the runner host instead of inside qemu-xtensa.
# No slirp NAT, no QEMU PSRAM gymnastics — just two processes.
#
# Caller's responsibility:
#   - cd into examples/linux_test
#   - source the IDF env
#   - have built `./build/linux_test.elf`
#   - export OUT_PATH, LOG_PATH, KVS_FRAMES_DIR, KVS_CHANNEL_NAME,
#     AWS_*, GITHUB_WORKSPACE, TEST_DURATION_SEC

set -euo pipefail

mkdir -p "$(dirname "$OUT_PATH")"

# Allow callers to override the build dir, e.g. for the relay-only
# probe job that builds into `./build_relay/`. Defaults to `./build/`.
LINUX_TEST_BUILD_DIR="${LINUX_TEST_BUILD_DIR:-./build}"

# Master in background; linux_test's app_main loops for $TEST_DURATION_SEC
# then exit(0)s.
( "$LINUX_TEST_BUILD_DIR/linux_test.elf" > linux_master.log 2>&1 & echo $! > master.pid )

# Boot, time-sync, signaling-init typically lands by ~15 s on a
# Linux runner (no QEMU emulation overhead).
sleep 15
echo "::group::Master boot snippet"
tail -40 linux_master.log
echo "::endgroup::"

python -u "${GITHUB_WORKSPACE}/tests/docker/python_viewer/viewer.py" 2>&1 | tee "$LOG_PATH" &
VIEWER_PID=$!
wait "$VIEWER_PID" || true

if [ -f master.pid ]; then kill -TERM "$(cat master.pid)" 2>/dev/null || true; fi
sleep 2
pkill -KILL -f linux_test.elf 2>/dev/null || true
