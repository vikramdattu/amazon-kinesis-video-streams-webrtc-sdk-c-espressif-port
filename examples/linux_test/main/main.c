/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linux-target integration test for the espressif-port KVS WebRTC SDK.
 *
 * Acts as a master peer over KVS signaling, streaming pre-recorded H.264 +
 * Opus sample frames (from the upstream submodule's samples/h264SampleFrames/
 * and samples/opusSampleFrames/) to a connected viewer. Driven by env vars:
 *
 *   AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_DEFAULT_REGION
 *   KVS_CHANNEL_NAME
 *
 * Phase 1 (this file): minimal app_main that prints config and exits cleanly.
 * Used as a build-pipeline canary before adding signaling + peer-connection
 * logic in Phase 1b.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *get_or_default(const char *name, const char *fallback)
{
    const char *v = getenv(name);
    return (v && v[0]) ? v : fallback;
}

int app_main(void)
{
    printf("linux_test: espressif-port KVS WebRTC integration test (Phase 1 canary)\n");
    printf("  AWS_DEFAULT_REGION = %s\n", get_or_default("AWS_DEFAULT_REGION", "(unset)"));
    printf("  KVS_CHANNEL_NAME   = %s\n", get_or_default("KVS_CHANNEL_NAME", "(unset)"));
    printf("  AWS_ACCESS_KEY_ID  = %s\n",
           getenv("AWS_ACCESS_KEY_ID") ? "(set)" : "(unset)");

    /* Phase 1b will replace this with: app_webrtc_init -> kvs_signaling_connect ->
     * peer_connection -> feed sample frames -> wait for viewer to disconnect. */

    return 0;
}
