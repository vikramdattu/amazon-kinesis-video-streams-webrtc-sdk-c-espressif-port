/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linux-target equivalent of examples/webrtc_classic. Same SDK
 * components (app_webrtc + kvs_signaling + kvs_webrtc + esp_webrtc_utils)
 * with the ESP-only bits skipped:
 *   - no NVS init: not needed when creds come from env vars
 *   - no WiFi/BLE provisioning: Linux uses the host's networking stack
 *   - no SNTP time sync: the host clock is already synced
 *   - no media capture: video_capture/audio_capture left NULL so the
 *     kvs_media path runs in signaling+peer-connection-only mode
 *
 * AWS credentials and channel name come from environment variables, NOT
 * Kconfig (Linux-target builds don't have device-config flows). This is
 * the same env that the docker-compose harness passes through for the
 * upstream-master container.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "app_webrtc.h"
#include "kvs_signaling.h"
#include "kvs_peer_connection.h"
#include "esp_log.h"
#include "file_capture.h"

static const char *TAG = "linux_test";

static volatile int s_should_exit = 0;

static void on_signal(int sig)
{
    (void) sig;
    s_should_exit = 1;
}

static const char *getenv_or_die(const char *name)
{
    const char *val = getenv(name);
    if (!val || !*val) {
        fprintf(stderr, "linux_test: required env var %s not set\n", name);
        exit(2);
    }
    return val;
}

static void event_handler(app_webrtc_event_data_t *event_data, void *user_ctx)
{
    (void) user_ctx;
    if (!event_data) {
        return;
    }
    switch (event_data->event_id) {
        case APP_WEBRTC_EVENT_INITIALIZED:
            ESP_LOGI(TAG, "[KVS Event] WebRTC initialized");
            break;
        case APP_WEBRTC_EVENT_SIGNALING_CONNECTING:
            ESP_LOGI(TAG, "[KVS Event] Signaling connecting");
            break;
        case APP_WEBRTC_EVENT_SIGNALING_CONNECTED:
            ESP_LOGI(TAG, "[KVS Event] Signaling connected");
            break;
        case APP_WEBRTC_EVENT_SIGNALING_DISCONNECTED:
            ESP_LOGI(TAG, "[KVS Event] Signaling disconnected");
            break;
        case APP_WEBRTC_EVENT_PEER_CONNECTION_REQUESTED:
            ESP_LOGI(TAG, "[KVS Event] Peer connection requested");
            break;
        case APP_WEBRTC_EVENT_PEER_CONNECTED:
            ESP_LOGI(TAG, "[KVS Event] Peer connected: %s", event_data->peer_id);
            break;
        case APP_WEBRTC_EVENT_PEER_DISCONNECTED:
            ESP_LOGI(TAG, "[KVS Event] Peer disconnected: %s", event_data->peer_id);
            break;
        case APP_WEBRTC_EVENT_RECEIVED_OFFER:
            ESP_LOGI(TAG, "[KVS Event] Received offer");
            break;
        case APP_WEBRTC_EVENT_SENT_ANSWER:
            ESP_LOGI(TAG, "[KVS Event] Sent answer");
            break;
        case APP_WEBRTC_EVENT_ERROR:
        case APP_WEBRTC_EVENT_SIGNALING_ERROR:
        case APP_WEBRTC_EVENT_PEER_CONNECTION_FAILED:
            ESP_LOGE(TAG, "[KVS Event] Error %d: %d %s",
                     (int) event_data->event_id,
                     (int) event_data->status_code,
                     event_data->message ? event_data->message : "");
            break;
        default:
            ESP_LOGI(TAG, "[KVS Event] Other event %d", (int) event_data->event_id);
            break;
    }
}

#ifdef __cplusplus
extern "C" {
#endif
void app_main(void);
#ifdef __cplusplus
}
#endif

void app_main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("linux_test: espressif-port KVS WebRTC SDK Linux-target master\n");

    const char *channel = getenv_or_die("KVS_CHANNEL_NAME");
    const char *region  = getenv("AWS_DEFAULT_REGION");
    if (!region || !*region) region = "us-west-2";

    const char *access_key   = getenv_or_die("AWS_ACCESS_KEY_ID");
    const char *secret_key   = getenv_or_die("AWS_SECRET_ACCESS_KEY");
    const char *session_tok  = getenv("AWS_SESSION_TOKEN");

    ESP_LOGI(TAG, "channel=%s region=%s", channel, region);

    if (app_webrtc_register_event_callback(event_handler, NULL) != 0) {
        ESP_LOGE(TAG, "register_event_callback failed");
        return;
    }

    static kvs_signaling_config_t sig = {0};
    sig.pChannelName = (char *) channel;
    sig.useIotCredentials = false;
    sig.awsAccessKey = (char *) access_key;
    sig.awsSecretKey = (char *) secret_key;
    sig.awsSessionToken = session_tok ? (char *) session_tok : (char *) "";
    sig.awsRegion = (char *) region;
    /* No SPIFFS on Linux — let mbedtls fall back to its bundled CA list. */
    sig.caCertPath = NULL;

    app_webrtc_config_t cfg = APP_WEBRTC_CONFIG_DEFAULT();
    cfg.signaling_client_if = kvs_signaling_client_if_get();
    cfg.signaling_cfg = &sig;
    cfg.peer_connection_if = kvs_peer_connection_if_get();

    /* File-backed capture: loops the same H.264 + Opus sample frames
     * the upstream KVS C SDK ships under `samples/`. Set
     * KVS_FRAMES_DIR=<path> to point at a checkout of those samples
     * (default "samples", relative to cwd). With these wired up the
     * peer connection actually pumps RTP after DTLS handshake,
     * letting the docker viewer record an MKV that ffprobe can
     * verify. */
    const char *frames_dir = getenv("KVS_FRAMES_DIR");
    if (frames_dir && *frames_dir) {
        file_capture_set_frames_dir(frames_dir);
    }
    cfg.video_capture = file_capture_get_video_if();
    cfg.audio_capture = file_capture_get_audio_if();

    ESP_LOGI(TAG, "Initializing WebRTC (master role, signaling-only on Linux)");
    WEBRTC_STATUS rc = app_webrtc_init(&cfg);
    if (rc != WEBRTC_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "app_webrtc_init failed: 0x%08x", (unsigned) rc);
        return;
    }

    rc = app_webrtc_run();
    if (rc != WEBRTC_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "app_webrtc_run failed: 0x%08x", (unsigned) rc);
        app_webrtc_terminate();
        return;
    }
    ESP_LOGI(TAG, "linux_test: WebRTC running. Press Ctrl+C to exit.");

    /* Run for TEST_DURATION_SEC (default 60s) or until SIGINT/SIGTERM. */
    int duration = 60;
    const char *dur_env = getenv("TEST_DURATION_SEC");
    if (dur_env && *dur_env) {
        duration = atoi(dur_env);
    }
    for (int i = 0; i < duration && !s_should_exit; ++i) {
        sleep(1);
    }

    ESP_LOGI(TAG, "linux_test: shutting down");
    app_webrtc_terminate();
    /* Linux IDF target keeps FreeRTOS spinning otherwise. */
    exit(0);
}
