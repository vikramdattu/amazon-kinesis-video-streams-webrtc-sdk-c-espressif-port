/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linux-target build canary for the espressif-port KVS WebRTC SDK.
 *
 * Today (Phase 1) this exercises only the components in the SDK that already
 * have IDF_TARGET=linux build paths — currently just esp_webrtc_utils. The
 * goal is to validate the Linux build pipeline (component-manager + libsrtp2
 * registry component + esp_usrsctp registry component eventually flowing
 * through here) before Phase 1b expands to the full WebRTC stack once
 * app_webrtc / kvs_webrtc / kvs_signaling grow Linux-target conditionals.
 *
 * For the real end-to-end media test (master + viewer over KVS), see
 * tests/docker/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_work_queue.h"
#include "esp_log.h"

static const char *TAG = "linux_test";

static void canary_task(void *priv)
{
    (void)priv;
    ESP_LOGI(TAG, "esp_work_queue task ran on Linux target");
}

void app_main(void)
{
    printf("linux_test: espressif-port KVS WebRTC SDK Linux-target canary\n");
    printf("  AWS_DEFAULT_REGION = %s\n",
           getenv("AWS_DEFAULT_REGION") ? getenv("AWS_DEFAULT_REGION") : "(unset)");
    printf("  KVS_CHANNEL_NAME   = %s\n",
           getenv("KVS_CHANNEL_NAME") ? getenv("KVS_CHANNEL_NAME") : "(unset)");
    printf("  AWS_ACCESS_KEY_ID  = %s\n",
           getenv("AWS_ACCESS_KEY_ID") ? "(set)" : "(unset)");

    /* Exercise the Linux subset of esp_webrtc_utils — proves the SDK component
     * graph builds and the work queue runs on a POSIX-FreeRTOS host. */
    esp_err_t err = esp_work_queue_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_work_queue_init failed: %d", err);
        return;
    }
    err = esp_work_queue_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_work_queue_start failed: %d", err);
        return;
    }
    err = esp_work_queue_add_task(canary_task, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_work_queue_add_task failed: %d", err);
        return;
    }

    ESP_LOGI(TAG, "canary OK");
}
