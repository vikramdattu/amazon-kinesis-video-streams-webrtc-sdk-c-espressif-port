# Linux build canary

This example is built **only for the Linux IDF target** and validates that the
espressif-port SDK's Linux-buildable subset compiles end-to-end through
ESP-IDF's component manager, mbedtls, lwip, and the Linux-FreeRTOS shim.

## Current scope (Phase 1)

Depends only on `esp_webrtc_utils`, which is the one in-tree component with
proper `IDF_TARGET STREQUAL "linux"` branches in its CMakeLists today (see
`components/esp_webrtc_utils/CMakeLists.txt:1-9`). At runtime the canary
runs an `esp_work_queue` task to prove the queue works on POSIX-FreeRTOS.

## Future scope (Phase 1b — separate work)

Adding `kvs_signaling`, `kvs_webrtc`, and `app_webrtc` requires those
components to grow Linux-target paths first; today they directly include
ESP-only headers (`media_stream.h`, `driver`, `spi_flash`, `nvs_flash`).
Tracked separately in Athena.

For the **end-to-end media integration test** (Docker, KVS signaling, recording
+ verification), see [`../../tests/docker/`](../../tests/docker/) — that test
runs upstream's `kvsWebRTCClientMaster`/`Viewer` (with our patches applied),
not the in-tree SDK components.

## Build

Requires ESP-IDF release/v5.5 or newer with the Linux-target preview enabled.

```bash
. $IDF_PATH/export.sh
cd examples/linux_test
idf.py --preview set-target linux
idf.py build
```

The build produces `build/linux_test` (a normal Linux ELF).

## Run

```bash
./build/linux_test
```

Expected output ends with `linux_test: canary OK`.
