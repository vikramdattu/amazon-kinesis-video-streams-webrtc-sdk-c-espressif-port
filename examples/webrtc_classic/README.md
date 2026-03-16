# WebRTC Classic Example - **AWS KVS Streaming**

**Simple AWS KVS Integration** - Complete WebRTC implementation using Amazon Kinesis Video Streams for signaling. This example provides a straightforward way to stream video to browsers via AWS infrastructure.

## What You'll Build

A WebRTC camera that streams video and audio to web browsers using AWS KVS (Kinesis Video Streams) for signaling.

**Expected Result**: Device auto-connects to AWS KVS and streams video to browsers through the KVS WebRTC test page!

## Features

- **Video Streaming** - H.264 encoding for live video transmission
- **Audio Streaming** - Opus encoding for audio transmission
- **AWS KVS Integration** - Uses Amazon Kinesis Video Streams for signaling
- **Authentication Options** - Direct AWS credentials or IoT Core certificates
- **Auto-Connect** - Automatically connects to AWS KVS on startup
- **Single Device** - Everything runs on one ESP32 (signaling + streaming)

## Hardware Requirements

### Supported Development Boards
| Board | Video | Audio | Notes |
|-------|-------|-------|-------|
| **ESP32-S3-EYE** | Built-in camera | Built-in mic | **Recommended** - Ready to use |
| **ESP32-WROVER-KIT** | Add camera module | Add I2S mic | Requires additional hardware |
| **ESP32-P4-Function-EV-Board** | High-quality camera | Built-in mic | **Premium** - Best performance |

### Additional Hardware (Optional)
- **Microphone**: I2S microphone for audio streaming
- **Speaker**: I2S speaker for audio playback (bidirectional audio)
- **External Antenna**: For better Wi-Fi range in challenging environments

## Quick Start - AWS KVS Streaming

### Prerequisites 
- ESP-IDF v5.4 or v5.5 installed and configured
- ESP32 development board with camera (ESP32-S3-EYE recommended)
- AWS account with KVS access
- Wi-Fi network

### Step 1: Configure Target Device
```bash
cd examples/webrtc_classic

# Choose your ESP32 variant
idf.py set-target esp32s3 # For ESP32-S3-EYE (recommended)
# idf.py set-target esp32 # For ESP32-CAM
# idf.py set-target esp32p4 # For ESP32-P4 Function EV Board
```

### Step 2: Configure Wi-Fi & AWS Credentials

#### Wi-Fi Provisioning (Default for ESP32-S3 and ESP32-P4)

BLE provisioning is enabled by default. On first boot, the device advertises as a BLE device (`PROV_XXXXXX`). Use the **ESP BLE Provisioning** phone app ([Android](https://play.google.com/store/apps/details?id=com.espressif.provble) / [iOS](https://apps.apple.com/app/esp-ble-provisioning/id1473590141)) to provide WiFi credentials. On ESP32-P4, BLE runs on the C6 coprocessor. See [app_common README](../app_common/README.md#wifi-provisioning) for details.

You can also use the `wifi-set <ssid> <password>` CLI command at runtime to change credentials (use `wifi-set "" ""` to clear and re-trigger provisioning).

Alternatively, configure Wi-Fi credentials via menuconfig:
```bash
idf.py menuconfig
```
```
ESP_WIFI_SSID = "YourWiFiNetwork"
ESP_WIFI_PASSWORD = "YourPassword"
```

#### AWS Authentication (Choose Option A or B)

**Option A: Direct AWS Credentials**
```bash
idf.py menuconfig
# Navigate to: Component config → Amazon Web Services IoT →
# Disable: CONFIG_IOT_CORE_ENABLE_CREDENTIALS
```
Then edit `main/webrtc_main.c` (lines 277-281):
```c
kvs_signaling_cfg.awsAccessKey = CONFIG_AWS_ACCESS_KEY_ID; // Set via menuconfig
kvs_signaling_cfg.awsSecretKey = CONFIG_AWS_SECRET_ACCESS_KEY; // Set via menuconfig
kvs_signaling_cfg.awsSessionToken = CONFIG_AWS_SESSION_TOKEN; // Set via menuconfig
```

**Option B: IoT Core Certificates (Recommended)**
```bash
idf.py menuconfig
# Navigate to: Component config → Amazon Web Services IoT →
# Enable: CONFIG_IOT_CORE_ENABLE_CREDENTIALS
```
Then may also modify values in `main/webrtc_main.c` directly:
```c
kvs_signaling_cfg.iotCoreCredentialEndpoint = "your-endpoint.credentials.iot.us-east-1.amazonaws.com";
kvs_signaling_cfg.iotCoreCert = "/spiffs/certs/certificate.pem";
kvs_signaling_cfg.iotCorePrivateKey = "/spiffs/certs/private.key";
kvs_signaling_cfg.iotCoreRoleAlias = "your_role_alias";
kvs_signaling_cfg.iotCoreThingName = "your_thing_name";
```

**Option C: ESP RainMaker Integration (Advanced)**

For seamless credential management with ESP RainMaker, use the credential callback approach:

```c
// Credential callback implementation
int rmaker_fetch_aws_credentials(uint64_t user_data, /* ... other params ... */) {
 esp_rmaker_aws_credentials_t *credentials = esp_rmaker_get_aws_security_token("esp-videostream-v1-NodeRole");
 // Set output parameters and return 0 on success
}

// Configure KVS signaling with callback
kvs_signaling_cfg.fetch_credentials_cb = rmaker_fetch_aws_credentials;
kvs_signaling_cfg.fetch_credentials_user_data = 0;
```

This approach provides **dynamic credential renewal** and **optimized memory management** with ESP RainMaker's streamlined AWS integration.

### Step 3: Configure Console Output

Different Dev boards have different options for CONSOLE and LOGs. You may want to configure the console output as per your board:

```bash
idf.py menuconfig
# Go to Component config -> ESP System Settings -> Channel for console output
# (X) USB Serial/JTAG Controller # For ESP32-P4 Function_EV_Board V1.2 OR V1.5
# (X) Default: UART0 # For ESP32-P4 Function_EV_Board V1.4
```

**Note**: If the console selection is wrong, you will only see the initial bootloader logs. Please change the console as instructed above and reflash the app to see the complete logs.

### Step 4: Build & Flash
```bash
# Build the project
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Expected output:
# I (12345) webrtc_main: ESP32 WebRTC Example
# I (12346) webrtc_main: got ip:192.168.1.100
# I (12347) webrtc_main: Connected to WiFi
# I (12348) webrtc_main: [KVS Event] WebRTC Initialized.
# I (12349) webrtc_main: Initializing WebRTC application
# I (12350) webrtc_main: Running WebRTC application
```

**For Dual Chip Setup (P4+C6 Configuration):**

While using P4+C6 setup, please build and flash the network_adapter example from `examples/network_adapter` on ESP32-C6.

- ESP32-C6 does not have an onboard UART port. You will need to use ESP-Prog or any other JTAG.
- Use the following Pin configuration:

| ESP32-C6 (J2/Prog-C6) | ESP-Prog |
|-----------------------|----------|
| IO0 | IO9 |
| TX0 | TXD0 |
| RX0 | RXD0 |
| EN | EN |
| GND | GND |

### Step 5: View Your Stream! 
1. **Open [AWS KVS WebRTC Test Page](https://awslabs.github.io/amazon-kinesis-video-streams-webrtc-sdk-js/examples/index.html)**
2. **Enter your AWS credentials** (same as in Step 2)
3. **Enter channel name** (use the channel name configured in menuconfig)
4. **Select "Join as viewer"**
5. **Click "Start Viewer"** - you should see your ESP32 camera feed!
## Architecture Overview

This example demonstrates the new simplified WebRTC API:

```c
// Simplified configuration with reasonable defaults
app_webrtc_config_t app_webrtc_config = APP_WEBRTC_CONFIG_DEFAULT();
app_webrtc_config.signaling_client_if = kvs_signaling_client_if_get();
app_webrtc_config.signaling_cfg = &kvs_signaling_cfg;
app_webrtc_config.peer_connection_if = kvs_peer_connection_if_get();

// Media interfaces for bi-directional streaming
app_webrtc_config.video_capture = video_capture;
app_webrtc_config.audio_capture = audio_capture;
app_webrtc_config.audio_player = audio_player;
```

**Key Features:**
- **Simplified API**: Reasonable defaults (MASTER role, H.264/OPUS codecs, trickle ICE)
- **AWS KVS Integration**: Full signaling and peer connection through AWS
- **Auto-Detection**: Streaming mode auto-detected from provided media interfaces
- **Advanced APIs**: Override defaults using dedicated configuration functions
- **Single Device**: Everything runs on one ESP32 (no split mode)

## Troubleshooting

### Common Issues & Solutions

| Problem | Symptoms | Solution |
|---------|----------|----------|
| **Build Errors** | `STATUS.h not found` | Apply ESP-IDF patches (see main README) |
| **Wi-Fi Connection Failed** | `Failed to connect to WiFi` | Update hardcoded SSID/password in `webrtc_main.c` |
| **AWS Authentication Failed** | `AWS credentials invalid` | Verify AWS keys in menuconfig or IoT Core certificates |
| **No Video Stream** | Viewer connects but no video | Check camera connections, verify power supply |
| **Poor Video Quality** | Choppy/pixelated video | Check Wi-Fi signal strength, reduce interference |
| **Memory Issues** | `Allocation failed` | Check available RAM, restart device |

### Debug Information

**Monitor WebRTC Events:**
```
I (12345) webrtc_main: ESP32 WebRTC Example
I (12346) webrtc_main: got ip:192.168.1.100
I (12347) webrtc_main: Connected to WiFi
I (12348) webrtc_main: [KVS Event] WebRTC Initialized.
I (12349) webrtc_main: [KVS Event] Signaling Connecting.
I (12350) webrtc_main: [KVS Event] Signaling Connected.
I (12351) webrtc_main: [KVS Event] Peer Connected: viewer-12345
I (12352) webrtc_main: [KVS Event] Streaming Started for Peer: viewer-12345
```

**Test AWS Connectivity First:**
```bash
# Verify AWS credentials work
aws kinesisvideo list-signaling-channels --region us-east-1
```

### Expected Behavior
1. **Device boots** → Connects to hardcoded Wi-Fi network
2. **AWS KVS connection** → Automatically connects to AWS KVS signaling
3. **Waiting for viewers** → Ready to accept connections from browsers
4. **Viewer connects** → Starts streaming video/audio immediately

## Next Steps

### After Success 
1. **Try Split Mode** - Explore `streaming_only` + `signaling_only` for power optimization
2. **Custom Signaling** - Implement your own signaling protocol
3. **Production Deployment** - Use IoT Core certificates for security

### Related Examples
- **[streaming_only](../streaming_only/README.md)** - Split mode streaming device
- **[signaling_only](../signaling_only/README.md)** - Split mode signaling device
- **[esp_camera](../esp_camera/README.md)** - AppRTC compatible (no AWS needed)

### Documentation
- **[API_USAGE.md](../../API_USAGE.md)** - Complete API reference
- **[CUSTOM_SIGNALING.md](../../CUSTOM_SIGNALING.md)** - Custom signaling guide

## License

Apache License 2.0
