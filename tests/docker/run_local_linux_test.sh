#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Local runner for the linux_test (IDF Linux target) integration test
# against the aiortc Python viewer on the host. Wraps
# `tests/docker/ci/run_linux_with_viewer.sh` with sensible defaults so a
# developer can repro the CI's `linux_test_kvs` job without docker.
#
# What this exercises:
#   - Our SDK as the master peer (linux_test.elf), so changes in
#     kvs_signaling / kvs_webrtc / app_webrtc / esp_webrtc_utils are
#     under test (unlike `run_test.sh`, which uses upstream's master).
#   - Real KVS signaling channel + STUN/TURN.
#   - aiortc viewer that records the inbound stream to OUT_PATH so
#     ffprobe can validate the recording afterwards.
#
# Required env (caller exports before invoking):
#   AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_SESSION_TOKEN (if temp creds).
#
# Defaulted env (override only if you need to):
#   AWS_DEFAULT_REGION  (default: us-west-2)
#   KVS_CHANNEL_NAME    (default: vikram-watchdog-fix-<unix-ts>)
#   TEST_DURATION_SEC   (default: 45)
#   KVS_FRAMES_DIR      (default: ../../amazon-kinesis-video-streams-webrtc-sdk-c/samples)
#   KVS_CA_CERT_PATH    (default: macOS /etc/ssl/cert.pem, else /etc/ssl/certs/ca-certificates.crt)
#   OUT_PATH            (default: $TMPDIR/linux_recording.mkv)
#   LOG_PATH            (default: $TMPDIR/python_viewer.log)
#   AIORTC_VENV         (default: $TMPDIR/aiortc_venv — created if missing)
#
# Exit code mirrors the underlying viewer: 0 on success.
#
# Usage:
#   ./tests/docker/run_local_linux_test.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LINUX_TEST_DIR="$REPO_ROOT/examples/linux_test"
SDK_DIR="$REPO_ROOT/amazon-kinesis-video-streams-webrtc-sdk-c"

: "${AWS_ACCESS_KEY_ID:?must be set in env (export AWS_ACCESS_KEY_ID=...)}"
: "${AWS_SECRET_ACCESS_KEY:?must be set in env}"

export AWS_DEFAULT_REGION="${AWS_DEFAULT_REGION:-us-west-2}"
export KVS_CHANNEL_NAME="${KVS_CHANNEL_NAME:-vikram-watchdog-fix-$(date +%s)}"
export TEST_DURATION_SEC="${TEST_DURATION_SEC:-45}"
export KVS_FRAMES_DIR="${KVS_FRAMES_DIR:-$SDK_DIR/samples}"

# CA bundle: macOS LibreSSL, then Debian/Ubuntu, then Fedora/RHEL.
if [ -z "${KVS_CA_CERT_PATH:-}" ]; then
    if   [ -f /etc/ssl/cert.pem ];                   then KVS_CA_CERT_PATH=/etc/ssl/cert.pem
    elif [ -f /etc/ssl/certs/ca-certificates.crt ];  then KVS_CA_CERT_PATH=/etc/ssl/certs/ca-certificates.crt
    elif [ -f /etc/pki/tls/certs/ca-bundle.crt ];    then KVS_CA_CERT_PATH=/etc/pki/tls/certs/ca-bundle.crt
    else
        echo "Error: no CA bundle found. Set KVS_CA_CERT_PATH explicitly." >&2
        exit 2
    fi
fi
export KVS_CA_CERT_PATH

# Defaulted in /tmp so iterations don't pile up under the repo.
TMP="${TMPDIR:-/tmp}"
TMP="${TMP%/}"
export OUT_PATH="${OUT_PATH:-$TMP/linux_recording.mkv}"
export LOG_PATH="${LOG_PATH:-$TMP/python_viewer.log}"
export GITHUB_WORKSPACE="$REPO_ROOT"   # the CI script reads this for the viewer.py path

AIORTC_VENV="${AIORTC_VENV:-$TMP/aiortc_venv}"

# 1) linux_test.elf must be built (idf.py build under examples/linux_test).
if [ ! -x "$LINUX_TEST_DIR/build/linux_test.elf" ]; then
    echo "Error: $LINUX_TEST_DIR/build/linux_test.elf not found." >&2
    echo "       Build it first:" >&2
    echo "         . ~/work/esp-idf/export.sh" >&2
    echo "         cd $LINUX_TEST_DIR && idf.py --preview set-target linux && idf.py build" >&2
    exit 2
fi

# 2) aiortc venv. If absent, create + install.
if [ ! -x "$AIORTC_VENV/bin/python" ]; then
    echo ">>> Creating aiortc venv at $AIORTC_VENV"
    python3 -m venv "$AIORTC_VENV"
    # av==11 (in requirements.txt) doesn't compile against ffmpeg 8.x on
    # macOS; pull a newer av + matching aiortc that does. Same set the
    # CI image gets via the pinned requirements.txt; this is the
    # local-dev fallback.
    "$AIORTC_VENV/bin/pip" install --quiet --upgrade pip
    "$AIORTC_VENV/bin/pip" install --quiet 'aiortc>=1.10' 'av>=13.0' 'boto3>=1.34' 'websockets>=12.0'
fi

# 3) Cleanup helper: ensure we delete the per-run KVS channel even if the
#    test crashes or is Ctrl-C'd. Local iteration on a fixed channel name
#    can opt out with KEEP_CHANNEL=1.
cleanup_channel() {
    if [ "${KEEP_CHANNEL:-0}" = "1" ]; then return; fi
    if ! command -v aws >/dev/null 2>&1; then
        echo ">>> aws CLI not installed — skipping channel cleanup."
        return
    fi
    echo ">>> Deleting KVS channel '$KVS_CHANNEL_NAME' ..."
    ARN=$(aws kinesisvideo describe-signaling-channel \
            --channel-name "$KVS_CHANNEL_NAME" \
            --region "$AWS_DEFAULT_REGION" \
            --query 'ChannelInfo.ChannelARN' --output text 2>/dev/null) || ARN=""
    if [ -n "$ARN" ] && [ "$ARN" != "None" ]; then
        VERSION=$(aws kinesisvideo describe-signaling-channel \
                --channel-name "$KVS_CHANNEL_NAME" \
                --region "$AWS_DEFAULT_REGION" \
                --query 'ChannelInfo.Version' --output text 2>/dev/null) || VERSION=""
        aws kinesisvideo delete-signaling-channel \
            --channel-arn "$ARN" \
            ${VERSION:+--current-version "$VERSION"} \
            --region "$AWS_DEFAULT_REGION" >/dev/null 2>&1 \
            && echo "    deleted." \
            || echo "    delete failed (may already be gone)."
    fi
}
trap cleanup_channel EXIT

# 4) Ensure the venv's python is what `python` resolves to inside the CI
#    script (it shells out to `python -u .../viewer.py`).
export PATH="$AIORTC_VENV/bin:$PATH"

# 5) Wipe any leftover artefacts from a previous run.
rm -f "$OUT_PATH" "$LOG_PATH" "$LINUX_TEST_DIR/linux_master.log" "$LINUX_TEST_DIR/master.pid"

echo "===================================="
echo "  linux_test + aiortc viewer"
echo "  Region:        $AWS_DEFAULT_REGION"
echo "  Channel:       $KVS_CHANNEL_NAME"
echo "  Duration:      ${TEST_DURATION_SEC}s"
echo "  Recording:     $OUT_PATH"
echo "  Viewer log:    $LOG_PATH"
echo "  Master log:    $LINUX_TEST_DIR/linux_master.log"
echo "===================================="

# 6) Run.
cd "$LINUX_TEST_DIR"
RC=0
bash "$REPO_ROOT/tests/docker/ci/run_linux_with_viewer.sh" || RC=$?

# 7) Quick post-mortem: tail what landed.
echo
echo ">>> Master log (last 60 lines) ----------------------------------"
tail -60 "$LINUX_TEST_DIR/linux_master.log" 2>/dev/null || echo "(no master log)"
echo
echo ">>> Viewer log (last 60 lines) ----------------------------------"
tail -60 "$LOG_PATH" 2>/dev/null || echo "(no viewer log)"
echo
if [ -s "$OUT_PATH" ]; then
    SIZE=$(wc -c <"$OUT_PATH" | tr -d ' ')
    echo ">>> MKV recording: $OUT_PATH ($SIZE bytes)"
    if command -v ffprobe >/dev/null 2>&1; then
        echo ">>> ffprobe (mkv):"
        ffprobe -v error -show_streams "$OUT_PATH" 2>&1 | head -40 || true
    fi
else
    echo ">>> MKV recording: missing or empty at $OUT_PATH"
fi

# Raw H.264 Annex-B dump produced by the depacketizer hook in
# viewer.py. Authoritative when the MKV path fails because aiortc
# rejected individual RTP packets — the dump is what came off the
# wire pre-decode.
RAW_H264="${OUT_PATH%.*}.h264"
if [ -s "$RAW_H264" ]; then
    SIZE=$(wc -c <"$RAW_H264" | tr -d ' ')
    echo ">>> Raw H.264 dump: $RAW_H264 ($SIZE bytes)"
    if command -v ffprobe >/dev/null 2>&1; then
        echo ">>> ffprobe (raw h264):"
        ffprobe -v error -f h264 -count_frames -show_streams "$RAW_H264" 2>&1 | head -20 || true
    fi
else
    echo ">>> Raw H.264 dump: missing or empty at $RAW_H264"
fi

exit "$RC"
