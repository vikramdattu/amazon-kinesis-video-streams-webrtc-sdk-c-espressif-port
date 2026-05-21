# linux_test — Linux-target equivalent of webrtc_classic

Same KVS WebRTC SDK component graph (`app_webrtc` + `kvs_signaling` +
`kvs_webrtc` + `esp_webrtc_utils`) compiled for the **IDF Linux target**.
Builds and runs on a normal POSIX host (Linux/macOS), connects to a
real Kinesis Video Streams signaling channel as **MASTER**, and
exchanges offer/answer/ICE with any peer that connects as VIEWER —
exactly like `examples/webrtc_classic` does on a real ESP target,
minus the camera / mic / WiFi.

This is what makes the docker-based integration test meaningful: the
device side under test is *the SDK we ship*, not a stand-in.

## Differences from webrtc_classic

| Concern | `webrtc_classic` (ESP target) | `linux_test` (Linux target) |
|---|---|---|
| AWS credentials | `Kconfig` (`CONFIG_AWS_*`) | env vars (`AWS_ACCESS_KEY_ID` etc.) |
| Channel name    | Kconfig (`CONFIG_AWS_KVS_CHANNEL_NAME`) | env var (`KVS_CHANNEL_NAME`) |
| WiFi / BLE prov | `app_wifi_prov` + provisioning flow | host's networking stack |
| NVS init        | `nvs_flash_init` | not needed (creds are env) |
| SNTP time sync  | `esp_webrtc_time_sntp_*` | host clock |
| Video / audio capture | `media_stream_get_*_capture_if()` | `NULL` — signaling-only |
| Signaling stack | `esp_websocket_client` (default) | `libwebsockets` (`CONFIG_USE_ESP_WEBSOCKET_CLIENT=n`) |
| Termination     | `app_webrtc_run` keeps task alive | `TEST_DURATION_SEC` countdown + SIGINT |

## What got adapted in the SDK

Component-side changes were small and surgical — gated on
`IDF_TARGET STREQUAL "linux"`:

- `components/app_webrtc/CMakeLists.txt` — drop `driver` + `spi_flash`
  REQUIRES on Linux; they don't exist as components there.
- `components/app_webrtc/idf_component.yml` — drop the (already-unused)
  `media_stream` dep so the chain doesn't pull esp32-camera transitively.
- `components/app_webrtc/src/app_webrtc.c` — remove a stale
  `#include "media_stream.h"` (no symbols from it were ever used).
- `components/kvs_signaling/CMakeLists.txt` — drop `esp_wifi` from
  PRIV_REQUIRES on Linux.
- `components/kvs_webrtc/CMakeLists.txt` — drop `driver`, `spi_flash`,
  `esp_wifi` from REQUIRES on Linux.
- `components/libwebsockets/CMakeLists.txt` — drop `driver` REQUIRES on
  Linux.
- `components/media_stream/CMakeLists.txt` — register as headers-only
  on Linux (every src under `src/` depends on ESP-only peripherals;
  consumers that genuinely need a capture/player accept `NULL`).
- `components/media_stream/idf_component.yml` — gate
  `espressif/esp32-camera`, `espressif/esp_audio_codec`, and
  `espressif/esp_codec_dev` to `target != linux` so the registry
  doesn't try to fetch them.

No source-file changes inside `kvs_webrtc.c` / `kvs_signaling.c` /
`app_webrtc.c` were needed beyond removing the dead include — the
upstream KVS code path handles Linux correctly when the right Kconfig
flag is set (`USE_ESP_WEBSOCKET_CLIENT=n`).

## Build

Requires ESP-IDF release/v5.5 or newer with the Linux-target preview enabled.

```bash
. $IDF_PATH/export.sh
cd examples/linux_test
idf.py --preview set-target linux
idf.py build
```

Produces `build/linux_test.elf`.

## Run

```bash
export AWS_ACCESS_KEY_ID=...
export AWS_SECRET_ACCESS_KEY=...
export AWS_DEFAULT_REGION=us-west-2
export KVS_CHANNEL_NAME=my-test-channel
export TEST_DURATION_SEC=60        # optional; default 60s
./build/linux_test.elf
```

Drives a real KVS channel for `TEST_DURATION_SEC` seconds, then exits.
Send `SIGINT` to exit early.

## Future work

- File-based video capture: read upstream's `samples/h264SampleFrames/`
  and feed them via a Linux-only `video_capture_t` adapter so the test
  exercises the full media path, not just signaling.
- Wire this into `tests/docker/` to replace the upstream `master`
  container with our actual SDK build.
