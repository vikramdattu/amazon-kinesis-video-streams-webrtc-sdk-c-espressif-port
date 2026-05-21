/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "file_capture.h"

static const char *TAG = "file_capture";

/* Hard upper bound — upstream samples ship 1500 H.264 + 618 Opus. */
#define MAX_VIDEO_FRAMES 1500
#define MAX_AUDIO_FRAMES 618

static char s_frames_dir[256] = "samples";

void file_capture_set_frames_dir(const char *dir)
{
    if (!dir || !*dir) {
        return;
    }
    snprintf(s_frames_dir, sizeof(s_frames_dir), "%s", dir);
}

/* --- Shared file-loading helpers ------------------------------------ */

static uint8_t *load_file(const char *path, uint32_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_len = (uint32_t)sz;
    return buf;
}

static uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/* --- Video capture --------------------------------------------------- */

typedef struct {
    int next_index;          /* 1-based; loops 1..MAX_VIDEO_FRAMES */
    uint32_t bitrate_kbps;
    uint8_t fps;
} fc_video_state_t;

static esp_err_t fc_video_init(video_capture_config_t *cfg, video_capture_handle_t *out_handle)
{
    if (!cfg || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    fc_video_state_t *st = (fc_video_state_t *)calloc(1, sizeof(*st));
    if (!st) {
        return ESP_ERR_NO_MEM;
    }
    st->next_index   = 1;
    st->bitrate_kbps = cfg->bitrate ? cfg->bitrate : 1000;
    st->fps          = cfg->resolution.fps ? cfg->resolution.fps : 30;
    *out_handle = (video_capture_handle_t)st;
    ESP_LOGI(TAG, "video capture init: fps=%u, dir=%s/h264SampleFrames",
             (unsigned)st->fps, s_frames_dir);
    return ESP_OK;
}

static esp_err_t fc_video_start(video_capture_handle_t h)    { (void)h; return ESP_OK; }
static esp_err_t fc_video_stop(video_capture_handle_t h)     { (void)h; return ESP_OK; }

static esp_err_t fc_video_get_frame(video_capture_handle_t h, video_frame_t **out, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!h || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    fc_video_state_t *st = (fc_video_state_t *)h;

    char path[512];
    snprintf(path, sizeof(path), "%s/h264SampleFrames/frame-%04d.h264",
             s_frames_dir, st->next_index);

    uint32_t len = 0;
    uint8_t *buf = load_file(path, &len);
    if (!buf) {
        ESP_LOGE(TAG, "failed to read %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    video_frame_t *vf = (video_frame_t *)calloc(1, sizeof(*vf));
    if (!vf) {
        free(buf);
        return ESP_ERR_NO_MEM;
    }
    vf->buffer    = buf;
    vf->len       = len;
    vf->timestamp = now_us();
    /* Crude I-frame heuristic: index 1 always I, every 30th too.
     * Upstream samples are ~30fps with periodic IDR; the SDK doesn't
     * critically depend on accuracy here. */
    vf->type = (st->next_index == 1 || (st->next_index % 30) == 1)
                   ? VIDEO_FRAME_TYPE_I
                   : VIDEO_FRAME_TYPE_P;

    st->next_index++;
    if (st->next_index > MAX_VIDEO_FRAMES) {
        st->next_index = 1;
    }

    *out = vf;
    return ESP_OK;
}

static esp_err_t fc_video_release_frame(video_capture_handle_t h, video_frame_t *frame)
{
    (void)h;
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }
    free(frame->buffer);
    free(frame);
    return ESP_OK;
}

static esp_err_t fc_video_deinit(video_capture_handle_t h)
{
    free(h);
    return ESP_OK;
}

static esp_err_t fc_video_set_bitrate(video_capture_handle_t h, uint32_t bitrate_kbps)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    ((fc_video_state_t *)h)->bitrate_kbps = bitrate_kbps;
    return ESP_OK;
}

static esp_err_t fc_video_get_bitrate(video_capture_handle_t h, uint32_t *bitrate_kbps)
{
    if (!h || !bitrate_kbps) return ESP_ERR_INVALID_ARG;
    *bitrate_kbps = ((fc_video_state_t *)h)->bitrate_kbps;
    return ESP_OK;
}

static media_stream_video_capture_t s_video_if = {
    .init          = fc_video_init,
    .start         = fc_video_start,
    .stop          = fc_video_stop,
    .get_frame     = fc_video_get_frame,
    .release_frame = fc_video_release_frame,
    .deinit        = fc_video_deinit,
    .set_bitrate   = fc_video_set_bitrate,
    .get_bitrate   = fc_video_get_bitrate,
};

media_stream_video_capture_t *file_capture_get_video_if(void)
{
    return &s_video_if;
}

/* --- Audio capture --------------------------------------------------- */

typedef struct {
    int next_index;      /* 1-based; loops 1..MAX_AUDIO_FRAMES */
    uint16_t frame_ms;
} fc_audio_state_t;

static esp_err_t fc_audio_init(audio_capture_config_t *cfg, audio_capture_handle_t *out_handle)
{
    if (!cfg || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    fc_audio_state_t *st = (fc_audio_state_t *)calloc(1, sizeof(*st));
    if (!st) {
        return ESP_ERR_NO_MEM;
    }
    st->next_index = 1;
    st->frame_ms   = cfg->frame_duration_ms ? cfg->frame_duration_ms : 20;
    *out_handle = (audio_capture_handle_t)st;
    ESP_LOGI(TAG, "audio capture init: frame_ms=%u, dir=%s/opusSampleFrames",
             (unsigned)st->frame_ms, s_frames_dir);
    return ESP_OK;
}

static esp_err_t fc_audio_start(audio_capture_handle_t h)    { (void)h; return ESP_OK; }
static esp_err_t fc_audio_stop(audio_capture_handle_t h)     { (void)h; return ESP_OK; }

static esp_err_t fc_audio_get_frame(audio_capture_handle_t h, audio_frame_t **out, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!h || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    fc_audio_state_t *st = (fc_audio_state_t *)h;

    char path[512];
    snprintf(path, sizeof(path), "%s/opusSampleFrames/sample-%03d.opus",
             s_frames_dir, st->next_index);

    uint32_t len = 0;
    uint8_t *buf = load_file(path, &len);
    if (!buf) {
        ESP_LOGE(TAG, "failed to read %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    audio_frame_t *af = (audio_frame_t *)calloc(1, sizeof(*af));
    if (!af) {
        free(buf);
        return ESP_ERR_NO_MEM;
    }
    af->buffer    = buf;
    af->len       = len;
    af->timestamp = now_us();

    st->next_index++;
    if (st->next_index > MAX_AUDIO_FRAMES) {
        st->next_index = 1;
    }

    *out = af;
    return ESP_OK;
}

static esp_err_t fc_audio_release_frame(audio_capture_handle_t h, audio_frame_t *frame)
{
    (void)h;
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }
    free(frame->buffer);
    free(frame);
    return ESP_OK;
}

static esp_err_t fc_audio_deinit(audio_capture_handle_t h)
{
    free(h);
    return ESP_OK;
}

static media_stream_audio_capture_t s_audio_if = {
    .init          = fc_audio_init,
    .start         = fc_audio_start,
    .stop          = fc_audio_stop,
    .get_frame     = fc_audio_get_frame,
    .release_frame = fc_audio_release_frame,
    .deinit        = fc_audio_deinit,
};

media_stream_audio_capture_t *file_capture_get_audio_if(void)
{
    return &s_audio_if;
}
