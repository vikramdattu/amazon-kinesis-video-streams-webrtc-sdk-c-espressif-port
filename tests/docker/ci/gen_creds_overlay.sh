#!/usr/bin/env bash
#
# Write a Kconfig overlay file with the AWS credentials and KVS
# channel name pulled from the environment. The QEMU jobs chain this
# file last in SDKCONFIG_DEFAULTS so the device firmware embeds the
# secrets at build time (no runtime provisioning in QEMU).
#
# Usage:  gen_creds_overlay.sh <output-path>
# Env:    AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_SESSION_TOKEN,
#         AWS_DEFAULT_REGION, KVS_CHANNEL_NAME

set -euo pipefail

OUT="${1:-}"
if [ -z "$OUT" ]; then
    echo "::error::gen_creds_overlay.sh: missing <output-path> argument" >&2
    exit 2
fi

cat > "$OUT" <<EOF
CONFIG_AWS_ACCESS_KEY_ID="${AWS_ACCESS_KEY_ID}"
CONFIG_AWS_SECRET_ACCESS_KEY="${AWS_SECRET_ACCESS_KEY}"
CONFIG_AWS_SESSION_TOKEN="${AWS_SESSION_TOKEN:-}"
CONFIG_AWS_DEFAULT_REGION="${AWS_DEFAULT_REGION}"
CONFIG_AWS_KVS_CHANNEL_NAME="${KVS_CHANNEL_NAME}"
EOF

echo "wrote credentials overlay → $OUT"
