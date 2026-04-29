#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""
End-of-job cleanup: best-effort delete of the KVS signaling channel
this run created. Idempotent — already-gone is OK.

Reads `KVS_CHANNEL_NAME` and `AWS_DEFAULT_REGION` from the env. Picks
up AWS credentials via boto3's standard chain (env vars / IAM role).

Used by every integration_test.yml job's `Delete KVS channel` step.
"""

import os
import sys

import boto3
from botocore.exceptions import ClientError


def main() -> int:
    ch = os.environ["KVS_CHANNEL_NAME"]
    region = os.environ["AWS_DEFAULT_REGION"]
    kvs = boto3.client("kinesisvideo", region_name=region)
    try:
        info = kvs.describe_signaling_channel(ChannelName=ch)["ChannelInfo"]
        kvs.delete_signaling_channel(
            ChannelARN=info["ChannelARN"],
            CurrentVersion=info["Version"],
        )
        print(f"deleted {ch}")
    except ClientError as e:
        if e.response["Error"]["Code"] == "ResourceNotFoundException":
            print(f"{ch} already gone")
        else:
            print(f"delete error (non-fatal): {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
