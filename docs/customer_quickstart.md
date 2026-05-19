# Quick Start — ESP32-P4 RainMaker Camera Streaming

This guide covers bring-up of WebRTC two-way A/V streaming on ESP32-P4 hardware using the AWS KVS WebRTC C SDK ESP-IDF port. The customer delivery is **two source trees**, both required:

| Repo | Branch | Contains |
|---|---|---|
| `esp-rainmaker` | `customer/camera_solution` | RainMaker host application: examples/camera/standalone + examples/camera/split_mode/rmaker_split_camera, shared rmaker_camera component, local-control signaling, doorbell example |
| `amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port` | (branch supplied separately) | KVS WebRTC C SDK ESP-IDF port: webrtc-c + esp_hosted submodules + esp_port components (app_webrtc, media_stream, kvs_webrtc, esp_webrtc_utils, signaling_only, streaming_only) |

---

## Feature overview

What's working end-to-end on the customer branch today:

- **Two-way A/V** — HW H.264 TX (P4 hardware encoder) + SW H.264 RX (tinyh264 decoder) + Opus mono TX/RX. Target spec is 1080 p 25 fps TX and 240 × 240 ≥ 15 fps RX; current numbers are below target and being worked on — see the Media pipeline table and the WIP list at the bottom of this doc for honest current numbers.
- **Two transports for signaling** — cloud KVS (AWS) and local-control over RainMaker's HTTP server (LAN viewers, no AWS round-trip). Both run concurrently and are peer-ID-multiplexed.
- **Two hosting architectures** — *standalone* (single P4 image) and *split-mode* (P4 host + C6/C5 co-processor over SDIO) for deep-sleep / battery scenarios.
- **Onboarding** — BLE provisioning with sec2 (SRP6a + PoP), RainMaker cloud claim, ESP RainMaker mobile app pairs and views.
- **Display preview** — RX H.264 rendered to an LVGL canvas on EK79007 / ILI9881C (DSI) or ST7789 (SPI). Touch optional. Top-right FPS overlay.
- **Doorbell variant** — one-shot snapshot + notify on press, on the split-mode C6.
- **JPEG snapshot CLI** — HW JPEG encoder grabs a frame to SD card (standalone path).
- **OTA** — single-image OTA on standalone; dual OTA (P4 + C6) on split-mode.

---

## Solutions you can try

Every solution is a **two-chip pair** — one image on the ESP32-P4 host and one image on the ESP32-C6 (or C5) co-processor. Pick the row that matches the evaluation goal.

| # | P4 host image | C6 / C5 co-processor image | Architecture | What you get |
|---|---|---|---|---|
| 1 | `examples/webrtc_classic/` *(espressif-port)* | `examples/network_adapter/` *(espressif-port)* | Classic — all WebRTC on P4; C6 is a pure Wi-Fi pipe | SDK-only two-way A/V, no RainMaker dependency. SDK port evaluation. |
| 2 | `examples/camera/standalone/` *(esp-rainmaker)* | `examples/network_adapter/` *(espressif-port)* | Classic + RainMaker — all WebRTC on P4; C6 is a pure Wi-Fi pipe | Full RainMaker camera node: cloud claim, mobile app, OTA, local-control signaling. **Recommended end-to-end node.** |
| 3 | `examples/streaming_only/` *(espressif-port)* | `examples/signaling_only/` *(espressif-port)* | Split — media on P4, signaling on C6 | SDK-only split-mode evaluation, no RainMaker. Deep-sleep-friendly architecture. |
| 4 | `examples/streaming_only/` *(espressif-port)* | `examples/camera/split_mode/rmaker_split_camera/` *(esp-rainmaker)* | Split + RainMaker — media on P4, signaling + RainMaker on C6 | Production split-mode camera: deep-sleep, battery, RainMaker cloud + mobile app. |

**Two architectures, two stack variants.**
- *Rows 1–2 (classic)* keep all of WebRTC on the P4 — the C6 is a transparent Wi-Fi co-processor running `network_adapter`. Easiest to bring up; use this when the device is mains-powered and deep sleep is not a goal.
- *Rows 3–4 (split mode)* push signaling onto the co-processor so the P4 can deep-sleep between sessions. Required for battery / doorbell-style devices.
- Within each architecture, the lower row adds the full RainMaker stack on top of the bare SDK port.

**Doorbell variant** — `examples/camera/split_mode/rmaker_doorbell_camera/` (esp-rainmaker, esp32c6 target) is a C6-only one-shot snapshot + notify-on-press example. Use it in place of `rmaker_split_camera` on the C6 side of row 4 when targeting doorbell devices.

---

## Tested hardware (in-house validation)

The branch is validated on:

- **ESP32-P4 Function EV Board v1.2** — the in-house reference board for this customer engagement. Bring-up flows above are exercised against this revision.
- **ESP32-P4-EYE single-PCB kit** — RX render + audio codec validated here (ST7789 SPI panel + ES8311 sub-board).

Other EV Board revisions (v1.4 / v1.5 / v1.6) share the same P4 silicon and BSP layer and should work with the same images, but sensor variants and panel SKUs differ between revisions — re-verify camera + display bring-up on the specific revision in hand. v1.6 in particular saw camera / panel SKU drift during our bench testing; if you have v1.6 on the bench, expect a sensor-Kconfig review before flashing.

---

## Prerequisites

| Requirement | Version / notes |
|---|---|
| ESP-IDF | v5.5 (release branch) — [Get Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/index.html) |
| Hardware | ESP32-P4 (host) + ESP32-C6 or ESP32-C5 (co-processor over SDIO) |
| Supported host boards | ESP32-P4 Function EV Board (v1.2 validated in-house; v1.4 / v1.5 / v1.6 share the same silicon), ESP32-P4-EYE single-PCB kit |
| AWS account | KVS WebRTC signaling channel — see [KVS WebRTC prerequisites](https://docs.aws.amazon.com/kinesisvideostreams-webrtc-dg/latest/devguide/kvswebrtc-prereq.html) |
| Mobile app | [ESP RainMaker iOS / Android](https://docs.espressif.com/projects/esp-rainmaker/en/latest/c-api-reference/index.html#mobile-applications) (for Path 1 provisioning + viewer) |
| Flash size | 16 MB (default standalone partitions assume this; 8 MB needs a custom partition table) |

### P4 silicon revision

**ESP32-P4 silicon revisions are mutually exclusive** — a build targets *either* rev < v3 (ECO1, e.g. EV Board v1.2) *or* rev ≥ v3 (ECO5+, newer boards). The same binary will **not** boot on the other family; the bootloader rejects the image with `chip revision out of range`. Pick one at build time via `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`:

- `=y` — build for ECO1 / rev < v3 (matches our in-house EV Board v1.2 validation).
- `=n` — build for ECO5+ / rev ≥ v3.

If you ship to a mixed fleet, build two firmware images (one per family) and select the right one at flash time.

---

## Clone

Both repositories live under the Espressif **rm_kvs_camera_solution** group on internal GitLab:
`https://glab.espressif.cn/solutions/esp-rainmaker/rm_kvs_camera_solution/esp-camera-solution/`

```bash
# RainMaker host application + camera examples (branch customer/camera_solution)
git clone --recursive -b customer/camera_solution \
  https://glab.espressif.cn/solutions/esp-rainmaker/rm_kvs_camera_solution/esp-camera-solution/esp-camera.git \
  esp-rainmaker

# KVS WebRTC SDK ESP-IDF port (branch customer/camera_solution)
git clone --recursive -b customer/camera_solution \
  https://glab.espressif.cn/solutions/esp-rainmaker/rm_kvs_camera_solution/esp-camera-solution/aws-kvs-esp-port.git \
  amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port

cd amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port
# Apply the local platform patches on top of the webrtc-c submodule SHA
# recorded by the parent repo. Idempotent — safe to re-run.
./scripts/apply_patches.sh
cd ..

# Both examples and the rmaker_camera component reference the SDK by env var
export KVS_SDK_PATH=$PWD/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port
```

`--recursive` is important — the port repo carries `webrtc-c` and `esp_hosted` as submodules. The local patches in `patches/*.patch` are applied on top of `webrtc-c` by `scripts/apply_patches.sh` (must be run once after submodule init; idempotent on subsequent calls).

---

## Path 1 — RainMaker camera (recommended)

### Recommended bring-up sequence

Start with **`examples/camera/standalone/`** — single firmware on the P4 host, the C6 just runs `network_adapter` as a Wi-Fi co-processor. Fewest moving parts; use it as the smoke test that toolchain, AWS creds, KVS channel, board wiring, and RainMaker provisioning are all wired up.

Once standalone streams end-to-end, move to **`examples/camera/split_mode/rmaker_split_camera/`** — two-firmware split (camera on P4, signaling / MQTT / RainMaker on C6). Required for power-save / deep-sleep scenarios.

| Example | What you get |
|---|---|
| `examples/camera/standalone/` | Single firmware on P4. Easiest. Use this first. |
| `examples/camera/split_mode/rmaker_split_camera/` | Camera on P4, signaling on C6. Required for power-save / deep-sleep. |
| `examples/camera/split_mode/rmaker_doorbell_camera/` | Doorbell variant (one-shot snapshot + notify on press) — C6-only target. |

### Per-host-board sdkconfig

Both standalone and split-mode examples ship per-board sdkconfig overlay files. Pick the one that matches the host:

```bash
# Standalone (P4 host)
cd esp-rainmaker/examples/camera/standalone
idf.py set-target esp32p4
# EV Function Board (default)
idf.py build
# OR P4-EYE single-PCB kit
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.p4eye' build
```

```bash
# Split-mode C6 firmware
cd esp-rainmaker/examples/camera/split_mode/rmaker_split_camera
idf.py set-target esp32c6
# Paired with EV Function Board host (stock C6 SDIO pinout)
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.evboard_pair' build
# OR paired with P4-EYE host (custom C6 SDIO pinout)
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.p4_eye_pair' build
```

ESP32-C5 is also supported on the split-mode side (use `set-target esp32c5` + the matching `sdkconfig.defaults.esp32c5`).

### Configure AWS credentials + KVS channel

```bash
cd esp-rainmaker/examples/camera/standalone
idf.py menuconfig
```

Under **Example Configuration** / **Camera Configuration**:

- AWS region (must match the KVS channel's region)
- AWS access key ID + secret (or AWS IoT thing certificate path for production)
- KVS signaling channel name

### Flash + provision

1. Flash the P4 host:
   ```bash
   idf.py -p /dev/cu.usbmodem<XYZ> flash monitor
   ```
   The P4's console is on the on-chip USB-Serial-JTAG (USB-C port on the EV Board), enumerates as `/dev/cu.usbmodem*` (macOS) or `/dev/ttyACM*` (Linux). Default baud 115200.

2. Flash the C6 co-processor — see "Flashing the C6 co-processor" below.

3. Provision via the **ESP RainMaker mobile app** (BLE pairing).

4. After provisioning the device claims into your RainMaker account and exposes a "Camera" device with a live-view panel.

### Flashing the C6 co-processor

The standalone example uses the pre-built `network_adapter` C6 image from `examples/camera/standalone/target-firmware/` (populate this directory before flashing — see its `README.md`). For split-mode, you build the C6 firmware yourself from `examples/camera/split_mode/rmaker_split_camera/`.

> **EV Function Board — direct ESP-Prog flash always.** The host-side `slave_flasher` path (which on some boards lets the P4 host re-flash the C6 over the inter-chip UART) is **disabled** in the customer build (`# CONFIG_SLAVE_FLASHER_ENABLE is not set` in `sdkconfig.defaults.p4_function_ev_board_v12` / `_v16`). The C6 is therefore programmed **directly** via an ESP-Prog wired to the EV board's `J2 / Prog-C6` header — both for first-time flashing and for any subsequent C6 firmware update. Plan accordingly: a customer in the field updating C6 needs the ESP-Prog adapter (no in-system C6 OTA on this variant).

The C6 on the EV Board has no on-board UART exposed; flash it via an ESP-Prog adapter wired into the **J2 / Prog-C6** header:

| ESP32-C6 (J2 / Prog-C6) | ESP-Prog |
|---|---|
| IO0 | IO9 |
| TX0 | TXD0 |
| RX0 | RXD0 |
| EN  | EN |
| GND | GND |

```bash
# Split-mode C6 firmware example
cd esp-rainmaker/examples/camera/split_mode/rmaker_split_camera
idf.py -p /dev/ttyUSB<ESP-Prog> flash monitor
```

`/dev/ttyUSB<ESP-Prog>` is the ESP-Prog adapter's USB-UART node (macOS shows as `/dev/cu.usbserial-*`; Linux `/dev/ttyUSB*`).

### Live view

- **ESP RainMaker mobile app (Android)** — *customer-tuned build*, delivered as a third source tree alongside the two firmware repos. Branch `feature/camera_video_streaming_umbrella` of the **esp-rainmaker-android** repo carries 26 commits on top of upstream `origin/master` covering: WebRTC viewport lifecycle, portrait camera-control UI, low-latency HW decoder (Unisoc backlog-spiral fix), local-control signaling, audio mute / save-clip toggles, 240 × 240 @ 15 fps capture geometry, H.264 Constrained-Baseline negotiation, ICE pre-warm + persistence. Build with Android Studio (JDK from `android-studio/jbr`) → `./gradlew assembleDebug` → install the APK on the viewer phone. Open the claimed camera and tap **View Live Stream**.
- **AWS Console** — KVS WebRTC console → channel → **Media playback viewer**. Useful as a non-mobile sanity check.

### Local-control signaling

Both standalone and split-mode examples carry an additional WebRTC signaling path over RainMaker's local-control HTTP server, enabled by default via `CONFIG_RMAKER_LOCAL_CONTROL_SIGNALING=y`. A viewer on the same LAN can negotiate a peer connection without round-tripping through AWS — useful for low-latency two-way A/V on private networks. The cloud KVS signaling path stays active; the two are multiplexed by peer-ID prefix.

---

## Path 2 — Standalone `streaming_only` / `signaling_only`

Minimal examples in the **espressif-port** repo. No RainMaker, no BLE provisioning — credentials and Wi-Fi come from `menuconfig`.

```bash
cd $KVS_SDK_PATH/examples/streaming_only
idf.py set-target esp32p4
# Pick host-board variant
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.p4_function_ev_board_v16' build
# OR
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.p4_function_ev_board_v12' build
# OR p4_eye — see sdkconfig.defaults.* in the example dir
```

| Example | What it does |
|---|---|
| `examples/streaming_only/` | Camera + audio capture → KVS WebRTC publish (TX path) |
| `examples/signaling_only/` | Full WebRTC peer connection (bidirectional A/V, no media capture wiring) |

Configure AWS region + access keys + KVS channel name via `menuconfig` under **Example Configuration**. C6 image: same `network_adapter` example as Path 1.

---

## Media pipeline — capabilities, targets, and current numbers

> **Honesty note:** the *target* column below is the spec we're delivering against; the *current* column is what we measure today on the in-house bench. **FPS targets are not yet fully met** — see the WIP list at the bottom of the doc. We're sharing real numbers so the customer can plan around the gap, not the spec-sheet aspirations.

| Direction | Pipeline | Target | Current (in-house, WIP) |
|---|---|---|---|
| **TX video** | HW H.264 encoder (`esp_h264_hw`) on ESP32-P4 → SRTP → KVS | 1920 × 1080 @ 25 fps, ≤ 2 Mb/s | Runs at 1920 × 1080. `send_v_fps` reaches 25 on a clean RF link with only TX active; **with full bidirectional A/V running (TX video + RX video + TX audio + RX audio simultaneously), encoder fps drops below 20**. 2.4 GHz congestion, bidirectional load, and SDIO pressure all step it down further. Active fixes in progress. |
| **TX audio** | ES8311 mic → I2S → Opus encoder | 16 kHz mono, 64 kb/s, 20 ms frames | `send_a_fps` ≈ 50 sustained (one frame per 20 ms wall) |
| **RX video** | SRTP → SW H.264 (tinyh264) → I420 → esp_image_effects (asm-optimised) → LVGL canvas → DSI / SPI panel | 240 × 240, ≥ 15 fps sustained | ~15 fps idle (RX-only). **With full bidirectional A/V running, `played_fps` settles around ~10 fps at 240 × 240**; drops further (~6 fps) under full-frame motion. SW decoder is the bottleneck; HW-decode integration is on the roadmap. |
| **RX audio** | SRTP → Opus decoder → I2S → ES8311 speaker | 16 kHz mono | Decode ~6 ms / frame average; queue stays < 5 of 50 under steady load |

The SDP `fmtp` advertises `maxplaybackrate=16000; stereo=0; sprop-stereo=0` so the peer encodes Opus at 16 kHz mono — keeps the on-device decode CPU budget under ~6 ms/frame.

Wi-Fi note: the C6 co-processor is 2.4 GHz only. TX-side 25 fps @ 1080p is sensitive to channel congestion — on a busy 2.4 GHz network FPS may step down before the encoder does. A clean 2.4 GHz channel (or a dedicated AP) gives the most consistent uplink. C5 co-processor adds 5 GHz support; if you have it, prefer 5 GHz.

---

## Display / RX video render

The RX path renders decoded H.264 frames to a BSP-managed LVGL canvas. The render backend is in `media_stream/src/video_render_display.c` and supports two panel families:

| Panel | Interface | Boards | Byte order |
|---|---|---|---|
| EK79007 1024 × 600 | MIPI-DSI 2-lane | EV Function Board (default) | Native RGB565 (LE) |
| ILI9881C 1280 × 800 | MIPI-DSI 4-lane | EV Function Board (alt) | Native RGB565 (LE) |
| ST7789 240 × 240 | SPI | ESP32-P4-EYE | MSB-first RGB565 (BE) |

Byte order is selected automatically via `CONFIG_LV_COLOR_16_SWAP` (the LVGL canonical convention — each BSP / sdkconfig overlay sets it correctly for its panel). Customers adding a new panel need to set this knob to match the panel's wire convention.

**Canvas auto-resize:** the renderer adopts the incoming H.264 stream's actual dimensions on the first frame (e.g. phone sending 240 × 240 portrait → canvas resizes from default 320 × 240 to 240 × 240). No app-side wiring needed.

**Touch optional:** the EV Function Board's GT911 capacitive touch controller is treated as optional. If a touchless EK79007 SKU is wired, display still comes up.

**FPS overlay:** the rendered canvas carries a top-right `-- fps` label that updates once per second with the actual `played_fps` value (LCD refreshes, not just LVGL invalidates). Helpful as a live perf indicator. Strip the label by removing the `lv_label_create()` block in `video_render_display.c` if not wanted in the shipping firmware.

**Disabling the RX render entirely:** set `CONFIG_MEDIA_STREAM_ENABLE_VIDEO_PLAYER=n` in `menuconfig`. Frees ~150 KB and avoids any DSI / LVGL dependencies.

---

## Audio codec

| Path | Codec | Pins (EV Function Board v1.2 — in-house validation) |
|---|---|---|
| Mic + speaker | ES8311 (mono) over I2S + SCCB (I2C) | I2C0 SCL=GPIO 8, SDA=GPIO 7; I2S MCLK=13, BCLK=12, LRCK=10, DOUT=9, DSIN=11; speaker PA=GPIO 53 |

Pins above are taken from the EV Function Board v1.2 schematic — the BSP overlay matches that revision. If you bring up a different EV Board revision, cross-check the codec wiring against its schematic before flashing.

ES8311 is opened with `fs.channel = 1` + `fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)` on both mic and speaker paths so the duplex I2S stays single-slot (LEFT). This is required — leaving `channel_mask = 0` causes the I2S backend to default to `I2S_STD_SLOT_BOTH`, doubling the sample rate the mic delivers and stretching the playback on the peer side. Don't change these without re-verifying TX/RX cadence.

---

## Camera sensor

| Sensor | Interface | Resolutions | Default in |
|---|---|---|---|
| SC2336 | MIPI-CSI | up to 1920 × 1080 @ 25 fps RAW10 | EV Function Board v1.5 / v1.6 |
| OV5647 | MIPI-CSI | up to 1920 × 1080 @ 30 fps RAW10 | Generic Pi-cam compatible adapter |
| OV2710 | MIPI-CSI | up to 1920 × 1080 @ 25 fps RAW10 | EV Function Board v1.4 |

Sensor selection is auto-detected via the `esp_video` component; menu config under **Camera Configuration** lists the available sensors. For non-default sensors, enable only that sensor's `CONFIG_CAMERA_<name>` and disable others to keep the link tight.

---

## Console + debug

### Internal SDK log level
The AWS KVS C SDK has its own log level, independent of `ESP_LOG_LEVEL`. From app code:

```c
#include "app_webrtc.h"
app_webrtc_set_log_level(2);   // 1 = VERBOSE, 2 = DEBUG, 3 = INFO, 4 = WARN, 5 = ERROR
```

### JPEG snapshot CLI (standalone)
The standalone example registers a `jpeg-capture` console command that grabs a JPEG from the camera and writes it to the SD card. Useful as a quick "is the camera alive" smoke test.

### Memory headroom
ESP32-P4 streaming uses substantial internal RAM for SRTP HW-AES DMA descriptors during the encrypt path. The example sdkconfig defaults already set `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, `CONFIG_SPIRAM_USE_MALLOC=y`, and `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` to keep internal RAM available for those allocations. Customers extending the example with additional internal-RAM-heavy code should keep these on.

---

## Network and security

### Bandwidth
1080 p 25 fps H.264 at 2 Mb/s + Opus 64 kb/s + protocol overhead ≈ **2.2–2.5 Mb/s sustained uplink** per camera. For the RX path, peer-sent 240 × 240 H.264 is typically < 200 kb/s. Plan AP / WAN accordingly.

### NAT traversal — TURN
KVS WebRTC supplies its own TURN credentials via the signaling channel; the device picks them up automatically once Wi-Fi + signaling are up. If the device is behind a symmetric NAT, TURN-relayed media is the fallback. Customers running on networks where the device cannot reach the KVS TURN servers (e.g. heavily firewalled LANs) must provide a private TURN server and pass its URL / credentials via the `app_webrtc_config.signaling_cfg` extension.

### TLS / cert handling
- KVS signaling uses TLS to AWS endpoints; the trust chain is the standard AWS root CA, embedded in the firmware at build time via `esp-tls`'s cert bundle.
- SRTP keys are derived from a DTLS handshake on every peer connection (per WebRTC spec). HW AES-GCM on ESP32-P4 accelerates the SRTP encrypt/decrypt path.
- RainMaker MQTT to the cloud uses AWS IoT mTLS with the device's claim certificate.

### Secure provisioning
BLE provisioning runs the standard ESP RainMaker flow:
- **PoP (Proof of Possession)** — the device prints its PoP on the console at boot (`PoP: XXXXXXXX`); the customer's mobile-app onboarding screen prompts for that string before completing the pairing. PoP is also encoded into the QR code printed at boot (sticker on production).
- **Security 2 (SRP6a)** — both standalone and split-mode examples use sec2 by default for the provisioning handshake; the mobile app handles this transparently. PoP-only (sec0) is available as a Kconfig fallback but not recommended for production.

---

## Submodules and build-time patches (espressif-port)

The KVS SDK port repo carries two git submodules:

| Submodule | Path | Note |
|---|---|---|
| `webrtc-c` | `amazon-kinesis-video-streams-webrtc-sdk-c/` | Upstream awslabs C SDK, pinned to a stable SHA. |
| `esp_hosted` | `components/esp_hosted/` | Espressif host driver for the C6 / C5 Wi-Fi co-processor, pinned. |

After a fresh clone, run:
```bash
cd $KVS_SDK_PATH
git submodule update --init --recursive
```

Several upstream-bound patches are applied to `webrtc-c/` at build time by `scripts/apply_patches.sh` (idempotent). These are tracked in `patches/` with one `.patch` per logical change and a `patches/README.md` ledger linking each to its upstream PR. The patches are needed until the upstream merges them — once a patch's upstream PR lands and the submodule SHA is bumped, that patch is dropped from `patches/`.

> Customers building from a fresh clone don't need to do anything manual — the patches apply automatically during `idf.py build`. They just need `--recursive` on the clone.

---

## OTA

Standalone (P4-only) supports standard ESP RainMaker OTA. The default partition table reserves a single `ota_0` app slot of ~5 MB; a customer that wants A/B (rollback-safe) OTA must split this into `ota_0` + `ota_1` and add an `otadata` partition. See the partition layout below.

Split-mode requires **dual OTA** (P4 + C6). On boards where `slave_flasher` is enabled, both images push from the cloud; the P4 host stores the C6 image into its `slave` SPIFFS partition and re-flashes the C6 via the `slave_flasher` UART path on next boot.

> **EV Function Board variant** ships with `CONFIG_SLAVE_FLASHER_ENABLE=n` in the customer overlays (`sdkconfig.defaults.p4_function_ev_board_v12` / `_v16`). The host-side re-flash path is therefore **not compiled in** on this board variant — only the P4 host firmware is OTA-updatable; the C6 firmware update remains a manual ESP-Prog flash. Customers needing in-system C6 OTA on the EV board should enable the Kconfig and validate the slave_flasher path on their hardware first.

---

## Partition layout (standalone, 16 MB flash)

| Offset | Name | Type | Size | Purpose |
|---|---|---|---|---|
| 0x9000 | `nvs` | data, nvs | 24 KB | NVS for Wi-Fi + RainMaker creds |
| 0xF000 | `otadata` | data, ota | 8 KB | OTA boot selector (if A/B added) |
| 0x10000 | `phy_init` | data, phy | 4 KB | RF calibration |
| 0x20000 | `factory` | app, factory | 5 MB | Main P4 app |
| 0x520000 | `fctry` | data, nvs | 24 KB | RainMaker factory data |
| 0x528000 | `storage` | data, spiffs | ~ 1 MB | SPIFFS (assets, JPEG snapshots) |
| 0x628000 | `slave` | data, spiffs | ~ 3 MB | C6 firmware bundle (boot/partition/app) |
| (end)    | (free) | — | ~ 7 MB | Available for additional partitions, A/B OTA, etc. |

Edit `partitions.csv` in the example dir to add `ota_0` / `ota_1` slots if you need A/B OTA. The default leaves room.

---

## Customer extension points

Where to plug customer-specific code without forking the components:

| Concern | Where to extend |
|---|---|
| Custom audio DSP (e.g. XMOS, ESP-SR AFE for AEC) | Replace the `esp_codec_dev` mic/speaker handles in `media_stream/src/OpusFrameGrabber.c::audio_capture_init()` and `OpusAudioPlayer.c::audio_player_init()` with your DSP's handles. The Opus encoder/decoder is unchanged. |
| Custom camera sensor | `examples/streaming_only/main/idf_component.yml` pulls the sensor list from `espressif/esp_video`; add your sensor's driver as an additional component or override path. Sensor probe is automatic via the `esp_video` component. |
| Alternative display panel | Add your panel's `esp_lcd_xxx` driver, populate `bsp_display_cfg_t.hw_cfg.dsi_bus.lane_bit_rate_mbps` in `video_render_display.c`, and set `CONFIG_LV_COLOR_16_SWAP` to match the panel's byte-order convention. |
| Private TURN server | Set `signaling_cfg.iceConfig` in your app-init code; passed through to `app_webrtc_init()`. KVS-provided TURN is then bypassed for that session. |
| Replace BLE provisioning with SoftAP | Disable `CONFIG_RMAKER_USE_BLE_PROVISIONING` and enable `CONFIG_RMAKER_USE_SOFTAP_PROVISIONING` in `menuconfig`. Mobile-app side still works. |
| Storage backend for snapshots / recordings | The standalone example's `jpeg-capture` CLI writes to SD card; customers adding S3 upload should hook into the `media_stream` post-capture path or wire their own. |

---

## Roadmap / known limitations / TODOs

### In scope, on the customer branch today
- Two-way A/V on ESP32-P4 + ESP32-C6 (and ESP32-C5 on the split-mode co-processor side).
- Local-control WebRTC signaling alongside cloud KVS (LAN viewers without AWS round-trip).
- LCD preview on EV Function Board (EK79007 / ILI9881C) and P4-EYE (ST7789).
- BLE provisioning (sec2), RainMaker cloud claim, mobile-app live view.
- HW H.264 encoder (TX), SW H.264 decoder (RX), HW AES-GCM SRTP.
- Doorbell variant under `examples/camera/split_mode/rmaker_doorbell_camera/`.

### Known limitations (today)
- **RX video on full-frame motion** drops to ~6 fps (decoder-bound). The SW H.264 decoder is the bottleneck; HW decode lands when esp_h264 ships a P4 HW-decode path.
- **2.4 GHz only** on the C6 co-processor. Customers needing 5 GHz must use the C5 co-processor (supported on split-mode).
- **No automatic switch to TURN when STUN is reachable but data path fails** — the device picks one of (host / srflx / relay) at ICE-gather time and stays on it. A genuinely flaky NAT may need an ICE restart, currently triggered by app-side `app_webrtc_restart_ice()` (no auto-restart logic yet).
- **Snapshot upload to S3** — not in scope of this baseline; `jpeg-capture` writes to SD card only.
- **A/B OTA** — not in the default partition layout; customers extend `partitions.csv` as needed.

### TODO / WIP — targeted for the next release drop
- **Dual OTA (P4 + C6) on split-mode** — not achieved yet on the customer branch. Single-image OTA on standalone works today; the coordinated P4 + C6 OTA flow is being wired up.
- **Low-power / deep-sleep mode** — not achieved yet. Split-mode architecture is in place (signaling on C6) but the P4-side deep-sleep / wake-on-offer path is not yet validated end-to-end.
- **FPS not yet at target** — both TX and RX are below spec today, and the gap is most visible when running **full bidirectional A/V** (TX video + TX audio + RX video + RX audio all active). In that simultaneous case the **encoder fps drops below 20** (against the 25 fps target) and **the receive side's `played_fps` settles around ~10 fps at 240 × 240** (against the ≥ 15 fps target), falling further under heavy motion. With only one direction active the numbers are closer to target — the simultaneous-A/V case is the realistic workload and the one we're tuning. Acknowledged gap — actively being worked on (SW H.264 decoder profiling, candidate HW-decode integration on P4, A/V clock-drift fixes, SDIO / RF tuning, task priority + core-affinity audit).

### TODO / future work (not blocking customer evaluation)
- **AEC** — ESP-SR AFE integration for full-duplex echo cancellation (today the example assumes the customer's XMOS DSP or external AEC handles this). Tracked.
- **HW H.264 decode on P4** — drops decoder CPU to near-zero, lifts the ~15 fps RX ceiling at full motion.
- **PLI / NACK plumbing** — Picture-Loss-Indication callback registration in the consumer is a stub; peers can't request key-frames mid-stream yet. Long sessions on flaky links may benefit from this.
- **Bitrate adaptation** — currently the encoder runs at a fixed bitrate (configurable via `CONFIG_VIDEO_ENCODER_BITRATE`); no congestion-controlled stepping.
- **ESP32-P4 ECO5+ silicon HW ECDSA** — currently disabled (`MBEDTLS_HARDWARE_ECDSA_SIGN/VERIFY=n`) in the default sdkconfig since our in-house validation board is ECO1. A separate build that targets ECO5+ silicon (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=n`) can flip these on for a faster DTLS handshake — that image won't boot on ECO1 boards.

---

## Common bring-up issues

| Symptom | Likely cause / fix |
|---|---|
| **Board appears dead** on `/dev/cu.usbmodem*` — flash works, monitor shows nothing | The per-board overlay (`p4_function_ev_board_v12` / `_v16` / `p4_eye`) was omitted from the `SDKCONFIG_DEFAULTS` chain. Only those overlays enable `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`. **Use the exact build command from the path section above** — host-board overlay is load-bearing. |
| `GT911 init failed`, `bsp_display_indev_init` aborts | EV Function Board SKUs ship display without touch (or touch I2C wedges). The customer build sets `CONFIG_BSP_ERROR_CHECK=n` to soft-fail this path — confirm it's in your effective sdkconfig if you see the abort. Re-fetching managed_components without applying the overlay chain can drop it. |
| `slave_rpc: No memory allocated for outbuf` / OFFER never reaches P4 | C6 internal SRAM is starved (typically when WiFi / GDMA / FreeRTOS code is pinned in IRAM). Keep `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n`, `CONFIG_GDMA_CTRL_FUNC_IN_IRAM=n`, `CONFIG_FREERTOS_IN_IRAM=n` on C6 builds. The customer baseline keeps these off; do not re-enable for "TX perf" without re-validating heap headroom for ~5 KB protobuf OFFER payloads. |
| `slave_flasher: ESP_FAIL` | C6 firmware partition empty or RESET pin not strapped — check the EV Board jumpers per the schematic, or flash the C6 manually via ESP-Prog. |
| `signalingFetchSync ... 0x5d000002` repeatedly | AWS creds or channel name wrong, OR clock not yet SNTP-synced (TLS rejects). Wait 30 s or fix creds. |
| Phone-side audio plays stretched | Mic / speaker channel-mask misconfigured — confirm `examples/camera/components/rmaker_camera` is at customer-branch HEAD (the channel-mask fix is in baseline). |
| Phone-side video colors look wrong | LVGL panel byte-order mismatch — confirm `CONFIG_LV_COLOR_16_SWAP` matches the panel's wire convention (n for DSI panels like EK79007, y for ST7789 SPI). |
| `bootloader.bin requires chip revision in range [v3.1 - v3.99]` | Image was built for ECO5+ but the board is ECO1 (e.g. EV Board v1.2). Set `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and **rebuild** (the two revisions are mutually exclusive — re-flashing alone won't help). |
| `esp_lcd_new_dsi_bus: invalid lane bit rate 0.00` | Display backend invoked without a DSI panel wired — either wire the EK79007/ILI9881C, or disable the video player via `CONFIG_MEDIA_STREAM_ENABLE_VIDEO_PLAYER=n`. |
| `writeFrame` heap corruption / TLSF store-access fault | Empty Opus frame reached the SDK's `writeFrame()`. Guard at the producer is in `OpusFrameGrabber.c` (skip `encoded_bytes == 0`); confirm your media_stream component is at customer-branch HEAD. |

---

## Where to file issues

- **RainMaker / camera example bugs** — open an issue or MR on the supplied esp-rainmaker repo.
- **KVS C SDK port bugs** — open an issue on the supplied espressif-port repo.
- **Customer support routing (Espressif)** — Application Solutions team.
