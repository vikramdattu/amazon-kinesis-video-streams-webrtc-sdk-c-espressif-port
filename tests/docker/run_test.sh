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

echo ">>> Verifying logs..."
MODE="$MODE" ./verify.sh
