#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Bring up master + viewer, run for TEST_DURATION_SEC, tear down, verify.

set -euo pipefail

cd "$(dirname "$0")"

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

echo ">>> Building images (cached after first run)..."
docker compose build

echo ">>> Starting master + viewer (test duration: ${TEST_DURATION_SEC}s)..."
# --abort-on-container-exit makes compose tear everything down once one
# container exits. We don't pin --exit-code-from because the master and
# viewer both use `timeout` to bound their lifetime; either is a valid
# steady-state exit.
docker compose up --abort-on-container-exit || true

echo ">>> Tearing down..."
docker compose down --remove-orphans >/dev/null 2>&1 || true

echo ">>> Verifying logs..."
./verify.sh
