# ESP-IDF Port of Amazon Kinesis Video Streams WebRTC SDK

[![Build Examples & Docs](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port/actions/workflows/build.yml) [<img alt="Try it with ESP Launchpad" src="https://espressif.github.io/esp-launchpad/assets/try_with_launchpad.png" width="160" height="40">](https://espressif.github.io/esp-launchpad/?flashConfigURL=https://awslabs.github.io/amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port/launchpad.toml)

This is a complete ESP-IDF port of the Amazon Kinesis Video Streams WebRTC SDK, enabling real-time audio/video streaming on ESP32 devices. The SDK supports multiple deployment modes and custom signaling protocols for maximum flexibility.

## Quick Start

**Want to get streaming in 5 minutes?**

1. **Clone and setup**: `git clone --recursive <repo>` → Install ESP-IDF v5.4 or v5.5 → Apply patches
2. **Build example**: `cd examples/webrtc_classic` → Configure WiFi & AWS credentials → `idf.py build flash monitor`
3. **Start streaming**: Open [WebRTC Test Page](https://awslabs.github.io/amazon-kinesis-video-streams-webrtc-sdk-js/examples/index.html) → Connect to your channel

**New Features**: Simplified API with 4-line configuration, smart defaults, and advanced configuration APIs

**Detailed setup instructions below** ⬇

## Prerequisites

### Hardware Requirements
- **Single Device**: ESP32, ESP32-S3, ESP32-C6 with camera/microphone
- **Dual Device (Split Mode)**: ESP32-P4 Function EV Board (has both ESP32-P4 + ESP32-C6 onboard)
- **Camera**: Supported modules via esp_video OR esp32-camera
- **Network**: Wi-Fi connection with internet access

### Software Requirements
- **ESP-IDF**: v5.4 or v5.5 (release/v5.4 or release/v5.5 branch)
- **Development Host**: Linux, macOS, or Windows with ESP-IDF environment
- **AWS Account**: For KVS signaling (or use AppRTC for testing)

## Examples Overview - New Simplified API

Choose the right example for your use case. All examples now use the new **simplified API** with smart defaults:

| Example | Architecture | Hardware | Use Case |
|---------|--------------|----------|----------|
| **[webrtc_classic](examples/webrtc_classic/)** | **Start here!** `kvs_signaling + kvs_peer_connection` | ESP32/S3/P4 + camera | Learning WebRTC, single device, AWS integration |
| **[esp_camera](examples/esp_camera/)** | `apprtc_signaling + kvs_peer_connection` | ESP32-CAM modules | Browser compatibility, **no AWS account needed** |
| **[streaming_only](examples/streaming_only/)** | `bridge_signaling + kvs_peer_connection` | ESP32-P4 (main processor) | High-performance streaming, power optimization |
| **[signaling_only](examples/signaling_only/)** | `kvs_signaling + bridge_peer_connection` | ESP32-C6 (network processor) | Always-on connectivity, power optimization |

### **New Simplified API Highlights**
All examples now use **4-line configuration** with smart defaults:
```c
app_webrtc_config_t config = APP_WEBRTC_CONFIG_DEFAULT();
config.signaling_client_if = kvs_signaling_client_if_get(); // Choose your signaling
config.peer_connection_if = kvs_peer_connection_if_get(); // Choose your peer connection
config.video_capture = media_stream_get_video_capture_if(); // Add media interfaces
```

### What Should I Use?
- **New to WebRTC?** → Start with `webrtc_classic` (full AWS integration)
- **No AWS account?** → Try `esp_camera` (works with any browser)
- **Need power optimization?** → Use split mode (`streaming_only` + `signaling_only`)
- **Building custom signaling?** → See [Custom Signaling Guide](CUSTOM_SIGNALING.md)

## Directory Structure

```
amazon-kinesis-video-streams-webrtc-sdk-c-espressif-port/
├── amazon-kinesis-video-streams-webrtc-sdk-c/  # Main SDK (submodule)
├── components/             # ESP-IDF components
│   ├── app_webrtc/         # WebRTC application framework
│   ├── credential/         # Credential management
│   ├── esp_hosted/         # ESP hosted functionality (submodule)
│   ├── esp_usrsctp/        # SCTP protocol implementation for ESP
│   ├── esp_webrtc_utils/   # WebRTC utilities for ESP
│   ├── kvs_signaling/      # KVS signaling component
│   ├── kvs_utils/          # KVS utility functions
│   ├── kvs_webrtc/         # Main KVS WebRTC component
│   ├── libsrtp2/           # SRTP library (submodule)
│   ├── libwebsockets/      # WebSocket library (submodule)
│   ├── media_stream/       # Video/audio capture and playback
│   ├── network_coprocessor/ # Network coprocessor support
│   ├── patches/            # ESP-IDF patches need to be applied using git am
│   ├── signaling_serializer/ # Signaling message serialization
│   ├── slave_flasher/      # Co-processor firmware flasher
│   ├── state_machine/      # State machine implementation
│   └── webrtc_bridge/      # Split mode IPC bridge
├── docs/                   # Some puml diagrams demonstrating different WebRTC scenarios
├── examples/               # Example applications
├── patches/                # SDK patches for ESP-IDF compatibility (apply with git am)
└── README.md               # This README
```

## Operational Modes

The WebRTC SDK supports two operational modes for different hardware configurations:

### Classic Mode
In classic mode, signaling and streaming are performed on the same chip. This mode can be implemented as:
- **Single Chip Solution**: All WebRTC functionality runs on a single ESP chip (e.g., ESP32-WROVER-KIT, ESP32-S3-EYE)
- **Dual Chip Solution**:
 - Host processor (e.g., ESP32-P4) handles all signaling and streaming
 - Wi-Fi coprocessor (e.g., ESP32-C6) transparently forwards network traffic to the main processor

The `webrtc_classic` example demonstrates this functionality.

### Split Mode
Split mode is designed for dual chip solutions, dividing responsibilities between processors:
- **Signaling Processor**: Network coprocessor (ESP32-C6) handles signaling with KVS
- **Streaming Processor**: Main processor (ESP32-P4) handles media streaming

 - More about this in later sections

## Setup

### Clone the project

Please clone this project using the following git command:

```bash
git clone --recursive <git url>
```

If you've already cloned it without `--recursive` switch do submodule update.

```bash
cd </cloned/dir/path/>
git submodule update --init
```

### Apply SDK patches

The main WebRTC SDK submodule requires platform-specific patches for ESP-IDF compatibility. Apply them after cloning:

```bash
cd amazon-kinesis-video-streams-webrtc-sdk-c
git am ../patches/*.patch
cd ..
```

### Install the ESP-IDF

Please follow the [Espressif instructions](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html) to set up the IDF environment.

Clone an ESP-IDF release branch (`release/v5.4` or `release/v5.5`):

```bash
git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git esp-idf
```

### Install the tools and set the environment. For Linux/unix this looks like

```bash
export IDF_PATH=</path/to/esp-idf>
cd $IDF_PATH
./install.sh
. ./export.sh
```

### Install pkg-config

```bash
sudo apt-get install pkg-config
```

## Demonstration

You can find three examples present under `examples` directory.
The main logical steps involved in WebRTC are signalling and streaming. Signalling is something which deals with authentication and negotiation with signalling server. Streaming is only started once signalling is successful. Based on where the signalling and streaming are performed, there are two modes of operation.

### `classic_mode`
Typically, WebRTC can work on single chip solution or dual chip solution. In this mode, signalling and streaming are done without splitting the functionality, on the same chip.
- The example `webrtc_classic` can be built and flashed on the board to use this functionality.
- Single Chip
 - This can be used on any ESP board from mentioned: ESP32-WROVER-KIT, ESP32-S3-EYE. These dev boards have the Wi-Fi capabilities built in. With some modifications, it should be possible to extend this support to other dev boards and chipsets.
- Dual Chip
 - Host or main processor (e.g., ESP32-P4) runs the network stack and responsible for handling signalling and streaming.
 - Wi-Fi capability is provided by the co-processor chipset (on board ESP32-C6), which transperantly forwards all incoming frames to main processor for processing.
 - The [network_adapter](examples/network_adapter/README.md) example can be used to build and flash the co-processor.

### `split_mode`
split mode is generally referred on dual chip solution. In this mode, Signalling and Streaming roles are split on different chips. Main processor is responsible of streaming and the network co-processor is responsible for signalling.
- `signaling_only`: special application discussed [below](#signalling-and-streaming-split-on-esp32-p4-and-esp32-c6). <br>
 Signalling only binary flashed on network co-processor, i.e. ESP32-C6
- `streaming_only`: special app discussed [below](#signalling-and-streaming-split-on-esp32-p4-and-esp32-c6).<br>
 Streaming only binary flashed on main processor, i.e. ESP32-P4

### Signalling and Streaming Split on ESP32-P4 and ESP32-C6
- Espressif's innovative solution, `esp_hosted` allows us to run network stack on both the co-processor and host. Network stack running on both the processors, share same IP but different, non-overlapping port numbers.
- `ESP32-P4 Function_EV_Board` is equipped with on-board main processor, `ESP32-P4` and on-board network co-processor, `ESP32-C6`.
- Signalling functionality of WebRTC would run on network co-processor, i.e., `ESP32-C6`.
- Once the streaming request is received by `ESP32-C6`, `ESP32-P4` is notified to start the streaming.
- This gives us two advantages:
 - Significant power savings <br>
 ESP32-P4 is a powerful and fast MCU. But at the same time, consumes higher power.
 By default, Keeping `ESP32-P4` in `deep sleep` mode and only wake-up once streaming request is received at `ESP32-C6`, would save a lot of power.
 - Instant wake-up times <br>
 Since network stack is running on both the sides, ESP32-P4 can instantly get IP address after waking up.

### Using the `split_mode`
- Similar to `classic_mode` on ESP32-P4, this can be built and flashed.
 - Build the `streaming_only` application and flash it on ESP32-P4
 - Build the `signalling_only` application and flash it on ESP32-C6
 - That's it, you can now start viewer from the [WebRTC Test webpage](https://awslabs.github.io/amazon-kinesis-video-streams-webrtc-sdk-js/examples/index.html).
 - You can observe that, the ESP32-C6 is connected to the signalling server and relays only absolutely necessary messages to ESP32-P4. Also, it forwards messages from ESP32-P4 to the signalling server.
 - The final RTP and SCTP sessions are directly established from the ESP32-P4.
- Please follow [streaming_only](examples/streaming_only/README.md) and [signalling_only](examples/signalling_only/README.md) for more in-depth understanding of how this works.


## BUILD

### Configure the project
Go to the example directory and select the target using following command:

```bash
# The non-split legacy webrtc app
cd examples/webrtc_classic

# for esp32
idf.py set-target esp32

# for esp32s3
idf.py set-target esp32s3

# for esp32p4
idf.py set-target esp32p4
```

Use menuconfig of ESP-IDF to configure the project.

```bash
idf.py menuconfig
```

### Console Configuration

Different Dev boards have different options for CONSOLE and LOGs. You may want to configure the console output as per your board:

```bash
idf.py menuconfig
# Go to Component config -> ESP System Settings -> Channel for console output
# (X) USB Serial/JTAG Controller # For ESP32-P4 Function_EV_Board V1.2 OR V1.5
# (X) Default: UART0 # For ESP32-P4 Function_EV_Board V1.4
```

**Note**: If the console selection is wrong, you will only see the initial bootloader logs. Please change the console as instructed above and reflash the app to see the complete logs.

### ESP32-C6 Configuration (For Dual Chip Setup)

While using P4+C6 setup (such as ESP32-P4 Function EV Board), please build and flash the network_adapter example from `examples/network_adapter` on ESP32-C6.

**Important Notes for ESP32-C6:**
- ESP32-C6 does not have an onboard UART port. You will need to use ESP-Prog or any other JTAG.
- Use the following Pin configuration:

| ESP32-C6 (J2/Prog-C6) | ESP-Prog |
|-----------------------|----------|
| IO0 | IO9 |
| TX0 | TXD0 |
| RX0 | RXD0 |
| EN | EN |
| GND | GND |

- These parameters under Example Configuration Options must be set via `idf.py menuconfig`:

 - `ESP_WIFI_SSID` — Wi-Fi network name
 - `ESP_WIFI_PASSWORD` — Wi-Fi password
 - `AWS_KVS_CHANNEL_NAME` — KVS signaling channel name
 - `AWS_DEFAULT_REGION` — AWS region (e.g., `us-east-1`)

- By default, IoT Core credentials are enabled (`IOT_CORE_ENABLE_CREDENTIALS=y`). Configure the IoT Core settings under "AWS Security Credentials" menu.
- To use static AWS credentials instead, disable `IOT_CORE_ENABLE_CREDENTIALS` and set `AWS_ACCESS_KEY_ID` and `AWS_SECRET_ACCESS_KEY`.

### Using IoT Credentials
It is also possible to use AWS IoT credentials instead of Access token.
- For this, please find and set `CONFIG_IOT_CORE_ENABLE_CREDENTIALS` via menuconfig.
- Put the certificates under [examples/app_common/spiffs_image/certs](examples/app_common/spiffs_image/certs/) directory.
- To generate these certificates, please take a look at [this](amazon-kinesis-video-streams-webrtc-sdk-c/scripts/generate-iot-credential.sh) script.
- **Note**: The credential handling has been significantly streamlined with centralized AWS credentials management using simplified APIs.

- Find the detailed info on KVS specific setup [here](amazon-kinesis-video-streams-webrtc-sdk-c/README.md#setup-iot).

## Documentation

For detailed API usage and implementation guides:

- **[API_USAGE.md](API_USAGE.md)** - **Complete API documentation** featuring the new simplified API, configuration options, and usage examples for all deployment modes
- **[CUSTOM_SIGNALING.md](CUSTOM_SIGNALING.md)** - **Comprehensive guide** for implementing custom signaling protocols using the pluggable architecture

## Migrating from `beta-reference-esp-port` Branch

This repository replaces the `esp_port/` subdirectory that previously lived inside the upstream [`awslabs/amazon-kinesis-video-streams-webrtc-sdk-c`](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c) repository on the `beta-reference-esp-port` branch.

### What Changed

The `esp_port/` directory prefix has been removed. Components and examples now live at the repository root:

| Before (upstream `beta-reference-esp-port`) | After (this repo) |
|---|---|
| `${KVS_SDK_PATH}/esp_port/components/<name>` | `${KVS_SDK_PATH}/components/<name>` |
| `${KVS_SDK_PATH}/esp_port/examples/<name>` | `${KVS_SDK_PATH}/examples/<name>` |

### Updating `idf_component.yml` References

If your project uses `idf_component.yml` to reference KVS SDK components via `path:` or `override_path:`, remove the `esp_port/` segment from every path:

```diff
  app_webrtc:
-   path: ${KVS_SDK_PATH}/esp_port/components/app_webrtc
+   path: ${KVS_SDK_PATH}/components/app_webrtc
    version: "*"
```

This applies to all component references: `app_webrtc`, `esp_webrtc_utils`, `kvs_webrtc`, `kvs_signaling`, `media_stream`, `network_coprocessor`, `signaling_bridge_adapter`, etc.

### No API Changes

All component APIs remain the same. No changes are needed in application source code (`.c` / `.h` files).

## License

This project is licensed under the Apache-2.0 License.
