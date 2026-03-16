# Signaling-Only Example - **Always-On Communication Hub**

**Power-efficient signaling device** - This device stays connected to AWS KVS 24/7, handling all WebRTC signaling while a separate `streaming_only` device sleeps until needed. Perfect for battery-powered security cameras and IoT applications.

## What This Does

This is **the communication brain** of a two-device system:
- **This Device (ESP32-C6)**: Always-on AWS KVS signaling and connection management
- **Partner Device (ESP32-P4)**: Sleeps until signaled to stream video (see `streaming_only` example)

**Result**: Ultra-low power consumption while maintaining 24/7 connectivity and instant responsiveness!

## Features

- **AWS KVS Integration** - Full WebRTC signaling protocol support
- **SDP Negotiation** - Handles offers, answers, and ICE candidates
- **Bridge Protocol** - Seamless communication with streaming device
- **Always-On Operation** - Maintains connection while partner sleeps
- **Instant Wake-up** - Can wake streaming device in milliseconds
- **Reliable Connection** - Auto-reconnection and error recovery
- **Low Memory Footprint** - Optimized for C6's memory constraints
- **Streamlined Credentials** - Compatible with ESP RainMaker's simplified AWS credential management

## Hardware Requirements

### Primary Hardware
| Component | Requirement | Notes |
|-----------|-------------|-------|
| **Main Board** | ESP32-P4 Function EV Board | **Required** - Has both P4 + C6 onboard |
| **Network Processor** | ESP32-C6 (this device) | Handles signaling and bridge communication |
| **Streaming Processor** | ESP32-P4 (partner) | Runs `streaming_only` example |

### Network Requirements
- **Wi-Fi Connection** with internet access
- **Stable Connection** - Recommended for 24/7 operation
- **Low Latency** preferred for responsive wake-up

### Power Supply
- **3.3V/500mA** sufficient for C6 signaling-only operation
- **Battery Powered** - Can run for weeks on battery
- **USB Power** - Convenient for development

## Architecture Overview

### Two-Device System Architecture
```
 Internet ESP32-P4 Function EV Board
 │ ┌─────────────────────────────────┐
 │ │ │
 ▼ │ ┌─────────────┐ Bridge IPC ┌──▼──────┐
┌─────────────┐ │ │ ESP32-C6 │◄──────────►│ESP32-P4 │
│ AWS KVS │ │ │(This Device)│ │(Partner)│
│ Signaling │◄─────────────┼──┤ │ │ │
│ Server │ │ │ • Signaling │ │ • Video │
└─────────────┘ │ │ • Always-On │ │ • Audio │
 │ │ • Low Power │ │ • Sleep │
 │ └─────────────┘ └─────────┘
 │ │
 └─────────────────────────────────┘
```

### Signaling Flow
```
Viewer Browser ESP32-C6 ESP32-P4
 │ (signaling_only) (streaming_only)
 │ │ │
 ├─── SDP Offer ─────────────►│ │
 │ ├─── Wake + SDP ────────────►│
 │ │◄─── SDP Answer ───────────┤
 │◄─── SDP Answer ───────────┤ │
 ├─── ICE Candidates ────────►│ │
 │ ├─── ICE Candidates ────────►│
 │ │◄─── ICE Candidates ───────┤
 │◄─── ICE Candidates ───────┤ │
 │ │ │
 │◄══════════ Direct RTP/SCTP Connection ═══════════════►│
```

## Setup Instructions - Communication Hub

This is **Device #1** of a two-device system. Flash this first, then flash the partner `streaming_only` device.

### Prerequisites 
- ESP32-P4 Function EV Board (has both P4 and C6 chips)
- ESP-IDF v5.4 or v5.5 configured
- AWS account with KVS access
- Stable Wi-Fi network

### Step 1: Configure Target Device
```bash
cd examples/signaling_only

# Configure for C6 chip (signaling device)
idf.py set-target esp32c6
```

### Step 2: Configure AWS & Network
```bash
idf.py menuconfig
```

**Navigate to: Example Configuration Options**

#### Wi-Fi Provisioning (Default for ESP32-C6)

BLE provisioning is enabled by default. On first boot, the device advertises as a BLE device (`PROV_XXXXXX`). Use the **ESP BLE Provisioning** phone app ([Android](https://play.google.com/store/apps/details?id=com.espressif.provble) / [iOS](https://apps.apple.com/app/esp-ble-provisioning/id1473590141)) to provide WiFi credentials. See [app_common README](../app_common/README.md#wifi-provisioning) for details.

You can also use the `wifi-set <ssid> <password>` CLI command at runtime to change credentials (use `wifi-set "" ""` to clear and re-trigger provisioning).

Alternatively, configure Wi-Fi credentials via menuconfig:
```
ESP_WIFI_SSID = "YourWiFiNetwork"
ESP_WIFI_PASSWORD = "YourPassword"
```

#### AWS KVS Settings (Required)
```
AWS_ACCESS_KEY_ID = "AKIA..."
AWS_SECRET_ACCESS_KEY = "your-secret-key"
AWS_DEFAULT_REGION = "us-east-1"
AWS_KVS_CHANNEL = "my-security-camera"
AWS_KVS_LOG_LEVEL = 2
```

### Step 3: Build & Flash Signaling Device

**Important for ESP32-C6:**
- ESP32-C6 does not have an onboard UART port. You will need to use ESP-Prog or any other JTAG.
- Use the following Pin configuration:

| ESP32-C6 (J2/Prog-C6) | ESP-Prog |
|-----------------------|----------|
| IO0 | IO9 |
| TX0 | TXD0 |
| RX0 | RXD0 |
| EN | EN |
| GND | GND |

```bash
# Build for C6
idf.py build

# Flash to C6 (usually second USB port)
idf.py -p /dev/ttyUSB1 flash monitor

# Expected output:
# I (2345) wifi: connected to YourWiFiNetwork
# I (3456) kvs_signaling: Connected to AWS KVS
# I (4567) signaling_only: Waiting for streaming device...
```

### Step 4: Flash Partner Streaming Device
**Now flash the partner `streaming_only` example to ESP32-P4**

See **[streaming_only README](../streaming_only/README.md)** for complete instructions.

### Step 5: Test the Complete System
**Expected Boot Sequence:**

**1. C6 Signaling Device (this device) Boots First:**
```
I (2345) wifi: connected to YourWiFiNetwork
I (3456) kvs_signaling: Connected to AWS KVS
I (4567) signaling_only: Ready for bridge connections
I (5678) signaling_only: ⏳ Waiting for streaming device...
```

**2. P4 Streaming Device Connects:**
```
I (8901) bridge: Bridge connected to P4
I (8902) signaling_only: Two-device system ready!
I (8903) signaling_only: Monitoring for viewer connections...
```

**3. Viewer Connects via Browser:**
```
I (15000) signaling_only: Incoming viewer: viewer-12345
I (15001) bridge: Signaling P4 to start streaming...
I (15002) signaling_only: WebRTC session established
```

## Advanced Configuration

### ICE Server Management
```c
// In main/app_main.c - Configure ICE servers
#define ICE_SERVER_REFRESH_INTERVAL_HOURS 12 // Refresh every 12 hours
#define ICE_SERVER_RETRY_COUNT 3 // Retry failed requests
#define ICE_SERVER_TIMEOUT_MS 30000 // 30 second timeout
```

### Bridge Communication Tuning
```c
// Bridge timing configuration
#define BRIDGE_HEARTBEAT_INTERVAL_MS 5000 // Keep-alive ping
#define BRIDGE_WAKE_TIMEOUT_MS 10000 // Wake-up timeout
#define BRIDGE_RETRY_ATTEMPTS 5 // Connection retries
```

### Power Optimization
```bash
# In menuconfig → Component Config → Power Management
CONFIG_PM_ENABLE=y # Enable power management
CONFIG_ESP32C6_DEFAULT_CPU_FREQ_160=y # Balanced performance
CONFIG_ESP_WIFI_IRAM_OPT=y # Optimize Wi-Fi for power
```

### Memory Optimization for C6
```bash
# In menuconfig → Component Config → ESP32C6-specific
CONFIG_ESP32C6_INSTRUCTION_CACHE_SIZE_32KB=y # Smaller cache
CONFIG_ESP32C6_DATA_CACHE_SIZE_32KB=y # Optimize for signaling
```

## Architecture Deep Dive

### API Design
This example uses the `app_webrtc` API with pluggable peer connection interfaces. It auto-detects signaling-only mode by checking if the `peer_connection_if` has a `create_session` callback.

```c
// Configure with minimal settings
app_webrtc_config_t app_webrtc_config = APP_WEBRTC_CONFIG_DEFAULT();
app_webrtc_config.signaling_client_if = kvs_signaling_client_if_get();
app_webrtc_config.signaling_cfg = &g_kvsSignalingConfig;
app_webrtc_config.peer_connection_if = bridge_peer_connection_if_get(); // Signaling-only
```

### Bridge Communication Flow
The signaling device communicates with the streaming device through the direct bridge connection. The peer interface intentionally lacks `create_session`, so the system bypasses session management and takes an efficient signaling-relay path.

## Troubleshooting

### Common Issues & Solutions

| Problem | Symptoms | Solution |
|---------|----------|----------|
| **AWS Connection Failed** | `KVS connection error` | Verify credentials, region, channel name |
| **Bridge Connection Failed** | `No streaming device` | Check P4 is running `streaming_only` |
| **ICE Server Errors** | `ICE refresh failed` | Check network connectivity, AWS quotas |
| **Memory Issues** | `Allocation failed` | Enable memory optimizations in menuconfig |
| **Wi-Fi Disconnections** | `Connection lost` | Check Wi-Fi signal strength, power supply |

### Debug Information

**Monitor WebRTC Events:**
The application logs all major events - watch the serial monitor for:
```
I (12345) signaling_only: WebRTC Event: 1, Status: 0x00000000, Peer: NULL, Message: NULL
I (12346) signaling_only: Signaling connected successfully
I (12347) signaling_only: ICE servers fetched from AWS and ready for requests
```

**Bridge Communication Logs:**
```
I (15000) signaling_bridge_adapter: Received bridged message from streaming device (123 bytes)
I (15001) signaling_bridge_adapter: Successfully sent message to signaling server
```

### Expected Connection Logs

**Successful Initialization:**
```
I (12345) signaling_only: ESP32 WebRTC Signaling-Only Example (Using App WebRTC State Machine)
I (12346) signaling_only: Setting up WebRTC application with KVS signaling and app_webrtc state machine
I (12347) signaling_only: Initializing WebRTC application
I (12348) signaling_only: Starting WebRTC application with split mode support
I (12349) signaling_only: Signaling device ready - will forward messages to/from streaming device via bridge
```

**WebRTC Events:**
```
I (15000) signaling_only: WebRTC Event: 1, Status: 0x00000000, Peer: NULL, Message: NULL
I (15001) signaling_only: Signaling connected successfully
I (15002) signaling_only: ICE servers fetched from AWS and ready for requests
```

## Power & Performance

### Power Efficiency
This signaling-only device enables power optimization by allowing the streaming device (P4) to sleep until needed. The C6 maintains the AWS KVS connection continuously while consuming significantly less power than running full WebRTC on the P4.

## Next Steps

### After Successful Setup 
1. **Monitor Performance** - Track power consumption and reliability
2. **Custom Bridge Protocol** - Add your own bridge commands
3. **Production Deployment** - Add error recovery and monitoring

### Related Examples
- **[streaming_only](../streaming_only/README.md)** - Partner device (required!)
- **[webrtc_classic](../webrtc_classic/README.md)** - Single-device alternative
- **[esp_camera](../esp_camera/README.md)** - AppRTC compatible mode

### Advanced Topics
- **[ICE_SERVER_BRIDGE_README.md](../ICE_SERVER_BRIDGE_README.md)** - Bridge architecture details
- **[API_USAGE.md](../../API_USAGE.md)** - Complete API reference
- **[CUSTOM_SIGNALING.md](../../CUSTOM_SIGNALING.md)** - Custom signaling protocols

## License

Apache License 2.0
