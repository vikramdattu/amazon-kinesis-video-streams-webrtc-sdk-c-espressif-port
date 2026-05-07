# Quick Start — ESP32-P4 Function EV Board

This guide covers two paths to bring up WebRTC streaming on the **Espressif ESP32-P4 Function EV Board** (P4 host + ESP32-C6 co-processor over SDIO):

1. **RainMaker camera examples** *(recommended for evaluation)* — production ESP-IDF examples with cloud claim, mobile app integration, and OTA. Built against the [beta-reference-esp-port](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/tree/beta-reference-esp-port) branch of the AWS KVS WebRTC C SDK fork. **Best for "make it work end to end on a board you already own".**
2. **Standalone `streaming_only` / `signaling_only` examples** *(for SDK evaluation)* — minimal ESP-IDF examples shipped with the upcoming dedicated port repo [awslabs/...-espressif-port](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port) (PR #2). **Best for "evaluate the C SDK port itself".**

Pick whichever fits the goal; both run on the same EV Function Board.

---

## Path 1 — RainMaker camera example (recommended)

### Prerequisites

| Requirement | Version / link |
|---|---|
| ESP-IDF | v5.5 — [Get Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/index.html) |
| esp-rainmaker | `master` (default, pulls everything else as components) |
| AWS KVS C SDK fork | branch `beta-reference-esp-port` of `awslabs/amazon-kinesis-video-streams-webrtc-sdk-c` (referenced via `KVS_SDK_PATH` env var) |
| AWS account | KVS WebRTC signaling channel ([create](https://docs.aws.amazon.com/kinesisvideostreams-webrtc-dg/latest/devguide/kvswebrtc-prereq.html)) |
| Hardware | ESP32-P4 Function EV Board v1.x + on-board ESP32-C6 |

### Clone

```bash
# RainMaker (host firmware + camera examples)
git clone --recursive https://github.com/espressif/esp-rainmaker.git
cd esp-rainmaker

# AWS KVS C SDK fork (provides ESP-IDF port under esp_port/)
git clone -b beta-reference-esp-port \
  https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c.git \
  ~/work/amazon-kinesis-video-streams-webrtc-sdk-c
export KVS_SDK_PATH=~/work/amazon-kinesis-video-streams-webrtc-sdk-c
```


### Recommended bring-up sequence

Start with **`examples/camera/standalone/`** — single firmware on the P4 host (C6 just runs `network_adapter` as a Wi-Fi co-processor). It's the simplest path to a working camera node, with the fewest moving parts. Use it as the smoke test that everything (toolchain, AWS creds, KVS channel, board wiring, RainMaker provisioning) is wired up correctly.

Once standalone is streaming end-to-end, move to **`examples/camera/split_mode/rmaker_split_camera/`** — that's the two-firmware split (camera on P4, signaling / MQTT / RainMaker on C6) which enables **deep-sleep / power-save scenarios** (doorbell-style devices, battery-powered cameras). All the AWS configuration carries over.

| Example | What you get |
|---|---|
| `examples/camera/standalone/` | Single firmware on P4. Easiest. Use this first. |
| `examples/camera/split_mode/rmaker_split_camera/` | Camera on P4, signaling on C6. Required for power-save / deep-sleep devices. |

### Configure AWS credentials + KVS channel

```bash
cd examples/camera/standalone
idf.py menuconfig
```

Under **`Example Configuration`** / **`Camera Configuration`** menus, set:
- AWS region (must match your KVS channel)
- AWS access key ID + secret key (or AWS IoT thing certificate path)
- KVS signaling channel name

### Quick try via ESP RainMaker Launchpad (browser flash, ESP32-S3)

If you just want to see the RainMaker camera example running before committing to the EV Board, the **standalone** firmware also runs on the ESP32-S3 (single chip, on-board camera + Wi-Fi, no co-processor wiring). Browser-flash a pre-built image from [ESP RainMaker Launchpad](https://espressif.github.io/esp-launchpad/?solution=rainmaker), provision via the RainMaker app, and you have a live camera node in under 5 minutes — no toolchain install required. Use this as a sanity check or a demo path while the EV Board build is still being provisioned.

### Build, flash, run

Build the host image in `examples/camera/standalone/` (target `esp32p4`) and flash it over the EV Board's USB port; full build / partition / provisioning detail is in the upstream camera README:

- [examples/camera/README.md](https://github.com/espressif/esp-rainmaker/blob/master/examples/camera/README.md)
- [examples/camera/standalone/README.md](https://github.com/espressif/esp-rainmaker/blob/master/examples/camera/standalone/README.md)

#### Flashing the C6 co-processor

Build and flash `network_adapter` from `${KVS_SDK_PATH}/esp_port/examples/network_adapter` on the ESP32-C6. The C6 does **not** have an on-board UART, so use an [ESP-Prog](https://docs.espressif.com/projects/esp-iot-solution/en/latest/hw-reference/ESP-Prog_guide.html) (or any JTAG adapter) wired into the **J2 / Prog-C6** header on the EV Board:

| ESP32-C6 (J2 / Prog-C6) | ESP-Prog |
|---|---|
| IO0 | IO9 |
| TX0 | TXD0 |
| RX0 | RXD0 |
| EN  | EN |
| GND | GND |

```bash
cd ${KVS_SDK_PATH}/esp_port/examples/network_adapter
idf.py set-target esp32c6
idf.py build
idf.py -p [ESP32-C6-PORT] flash monitor
```

Once both chips are flashed:
1. P4 host boots and opens the SDIO link to the C6.
2. Wi-Fi provisioning kicks off (BLE on P4) — pair via the [ESP RainMaker mobile app](https://docs.espressif.com/projects/esp-rainmaker/en/latest/c-api-reference/index.html#mobile-applications).
3. After provisioning the device claims into your RainMaker account and exposes a "Camera" device with a viewer panel.

### Live view

- **ESP RainMaker mobile app** (iOS / Android) — open the claimed camera and press *View Live Stream*.
- **AWS Console** — KVS WebRTC console → channel → *Media playback viewer*.

### Reference docs

- esp-rainmaker top-level README: [github.com/espressif/esp-rainmaker](https://github.com/espressif/esp-rainmaker)
- Camera example READMEs:
  - `examples/camera/README.md` (general overview)
  - `examples/camera/standalone/README.md`
  - `examples/camera/split_mode/rmaker_split_camera/README.md`
- ESP RainMaker docs: [docs.espressif.com/projects/esp-rainmaker](https://docs.espressif.com/projects/esp-rainmaker/en/latest/)
- KVS C SDK fork: [awslabs/...-webrtc-sdk-c@beta-reference-esp-port](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/tree/beta-reference-esp-port)

---

## Path 2 — Standalone `streaming_only` / `signaling_only`

For evaluating the upcoming dedicated port repo (no RainMaker dependency). Currently behind awslabs PR #2; fetch from the PR head:

```bash
gh pr checkout 2 --repo awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port
# OR clone the fork directly:
git clone -b feature/migration_from_beta_and_additional_setup \
  https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port.git
cd amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port
git submodule update --init --recursive

# Apply the SDK patches (CI does this automatically; manual for local builds)
cd amazon-kinesis-video-streams-webrtc-sdk-c
git am ../patches/*.patch
cd ..
```

Examples:

| Example | What it does |
|---|---|
| `examples/streaming_only/`  | Camera + audio capture → KVS WebRTC publish |
| `examples/signaling_only/`  | Full WebRTC peer connection (bidirectional) |

Configure AWS creds + KVS channel via `idf.py menuconfig` (each example has its own README with the menu paths). Build/flash same as Path 1.

---

## Console + chip-revision notes

### Console (P4 host)
The P4 host firmware's console is wired to the on-chip **USB-Serial-JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in defaults). Plug a USB-C cable into the EV Board's USB port and the host shows up as `/dev/cu.usbmodem*` (macOS) or `/dev/ttyACM*` (Linux); no separate UART adapter required. Default baud is **115200**.

```bash
idf.py -p /dev/cu.usbmodem<XYZ> monitor
```

### Console (C6 co-processor)
The C6 has no on-board UART exposed by the EV Board; its console comes out of the ESP-Prog adapter wired into J2 / Prog-C6 (see the wiring table above). Same baud (115200).

### P4 chip revision (board "latest" vs older units)
Newer EV Boards ship with **P4 silicon rev v3.0+**. Older units carry rev v1.x and the default bootloader rejects them with:

```
bootloader.bin requires chip revision in range [v3.1 - v3.99]
```

Set `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` in `sdkconfig.defaults` (or via `menuconfig` → *ESP32-P4-Specific* → *Minimum Supported ESP32-P4 Revision*) and rebuild. The image then accepts rev v0.0–v1.99 alongside v3.x. Keep the option off if every board you ship to is rev ≥ v3.0.

---

## Common bring-up issues (both paths)

| Symptom | Likely cause / fix |
|---|---|
| `slave_flasher: ESP_FAIL` | C5/C6 firmware partition empty or RESET pin not strapped — check the EV Board jumpers per the schematic |
| `signalingFetchSync ... 0x5d000002` repeatedly | AWS creds or channel name wrong, OR clock not yet SNTP-synced (TLS rejects). Wait 30 s or fix creds |
| `Slow video frame send: NNms` warnings | WiFi congestion or C6 SDIO backpressure — try a 5 GHz AP or reduce `CONFIG_VIDEO_ENCODER_BITRATE` |

For deeper diagnostics, raise the SDK's internal log level from app code:

```c
#include "app_webrtc.h"
app_webrtc_set_log_level(2);  // 1 = VERBOSE / 2 = DEBUG / 3 = INFO / WARN / ERROR
```

This is independent of `ESP_LOG_LEVEL` and controls only the AWS KVS C SDK internal logger.

---

## Where to file issues

- **RainMaker / camera example bugs:** open an issue or MR at [github.com/espressif/esp-rainmaker](https://github.com/espressif/esp-rainmaker/issues).
- **C SDK port bugs:** comment on [PR #2](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port/pull/2) or open an issue on that repo.
- **Customer support routing (Espressif):** Application Solutions team (Amey Inamdar).

---

## What's coming next — PR #2

The dedicated ESP-IDF port repo lives behind [awslabs PR #2](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port/pull/2). Once merged it will replace the `beta-reference-esp-port` flow above and bring several quality-of-life improvements out of the box, including:

- `slave_flasher` integrated on the P4 host (single-port flash; the C6 image is delivered automatically over SDIO at boot).
- Cleaner, dedicated repo layout (`components/` + `examples/streaming_only` / `examples/signaling_only`) for evaluating the SDK port without the RainMaker stack.
- Per-target sdkconfig defaults and pinned submodule revisions.
- Public CI matrix (esp32 / s3 / c3 / c6 / p4 across IDF v5.4 + v5.5).

Track progress and ship feedback on the PR.
