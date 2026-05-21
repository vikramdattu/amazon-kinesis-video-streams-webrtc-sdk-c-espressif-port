#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Bring up master + viewer, run for TEST_DURATION_SEC, tear down, verify.
#
# Default: master + upstream C kvsWebrtcClientViewer (interop signal,
# log-grep verification).
# `--python`: master + aiortc Python viewer (records MKV, ffprobe-verified).
# `--all`:    master + both viewers (C and Python on the same channel).

set -euo pipefail

cd "$(dirname "$0")"

MODE="c"
case "${1:-}" in
    "" | "--c" | "c")     MODE="c" ;;
    "--python" | "python") MODE="python" ;;
    "--all" | "all")       MODE="all" ;;
    "-h" | "--help")
        echo "Usage: $0 [--c | --python | --all]"
        exit 0
        ;;
    *)
        echo "Unknown arg: $1 (use --c, --python, or --all)"
        exit 2
        ;;
esac

if [ ! -f .env ]; then
    echo "Error: tests/docker/.env not found. Copy .env.example to .env and fill in AWS creds + KVS_CHANNEL_NAME."
    exit 2
fi

# shellcheck disable=SC1091
set -a; . ./.env; set +a

: "${AWS_ACCESS_KEY_ID:?must be set in .env}"
: "${AWS_SECRET_ACCESS_KEY:?must be set in .env}"
: "${KVS_CHANNEL_NAME:?must be set in .env}"
: "${AWS_DEFAULT_REGION:=us-west-2}"
: "${TEST_DURATION_SEC:=45}"

mkdir -p out
: > out/master.log
: > out/viewer.log
: > out/python_viewer.log
rm -f out/python_viewer.mkv

# Compose profiles + explicit service list together control what runs:
# - C-only:     `up master viewer` (no profile needed)
# - Python:     `--profile python up master python_viewer`
# - Both:       `--profile python up master viewer python_viewer`
PROFILE_ARGS=()
SERVICES=(master viewer)
case "$MODE" in
    c)      ;;
    python) PROFILE_ARGS+=(--profile python); SERVICES=(master python_viewer) ;;
    all)    PROFILE_ARGS+=(--profile python); SERVICES=(master viewer python_viewer) ;;
esac

echo ">>> Mode: $MODE   Services: ${SERVICES[*]}"
echo ">>> Building images (cached after first run)..."
docker compose "${PROFILE_ARGS[@]}" build "${SERVICES[@]}"

echo ">>> Starting (test duration: ${TEST_DURATION_SEC}s)..."
docker compose "${PROFILE_ARGS[@]}" up --abort-on-container-exit "${SERVICES[@]}" || true

echo ">>> Tearing down..."
docker compose "${PROFILE_ARGS[@]}" down --remove-orphans >/dev/null 2>&1 || true

# Best-effort channel cleanup so CI runs (which use a unique
# `espressif-port-ci-{run_id}` per run) don't pile up orphan channels.
# Set KEEP_CHANNEL=1 in .env to disable (e.g. for local iteration on a
# fixed channel name).
if [ "${KEEP_CHANNEL:-0}" != "1" ]; then
    if command -v aws >/dev/null 2>&1; then
        echo ">>> Deleting KVS channel '$KVS_CHANNEL_NAME'..."
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
    else
        echo ">>> aws CLI not installed — skipping channel cleanup. Install awscli to enable."
    fi
fi

echo ">>> Verifying logs..."
MODE="$MODE" ./verify.sh
