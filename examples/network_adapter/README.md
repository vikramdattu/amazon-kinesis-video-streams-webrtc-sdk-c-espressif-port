# Network Adapter

This example provides Wi-Fi network connectivity from a co-processor (ESP32-C6 or ESP32-C5) to the main processor (ESP32-P4) on dual-chip boards. It uses the [esp_hosted](https://github.com/espressif/esp-hosted) component to transparently forward network traffic between the two chips.

## When to Use

Use this example when running `webrtc_classic` on ESP32-P4 in a dual-chip setup. The ESP32-P4 does not have built-in Wi-Fi, so the co-processor handles all Wi-Fi connectivity and forwards packets to the P4 over SDIO.

This is different from `signaling_only`, which runs signaling logic on the co-processor. Here, the co-processor acts purely as a network adapter with no application logic.

## Hardware Requirements

- ESP32-P4 Function EV Board (has onboard ESP32-C6 co-processor)
- ESP-Prog or JTAG adapter for flashing the co-processor

## Setup

### Step 1: Configure Target Device
```bash
cd examples/network_adapter

# Configure for C6 chip
idf.py set-target esp32c6
```

### Step 2: Build & Flash

**Important for ESP32-C6:**
- ESP32-C6 does not have an onboard UART port. You will need to use ESP-Prog or another JTAG adapter.
- Use the following pin configuration:

| ESP32-C6 (J2/Prog-C6) | ESP-Prog |
|-----------------------|----------|
| IO0 | IO9 |
| TX0 | TXD0 |
| RX0 | RXD0 |
| EN | EN |
| GND | GND |

```bash
# Build
idf.py build

# Flash (usually second USB port)
idf.py -p /dev/ttyUSB1 flash monitor
```

### Step 3: Flash the Main Processor

Build and flash the `webrtc_classic` example on ESP32-P4:
```bash
cd examples/webrtc_classic
idf.py set-target esp32p4
idf.py build flash monitor
```

## Troubleshooting

- **No IP address on P4**: Ensure the network_adapter firmware is running on C6 before booting P4. Check SDIO connection between the chips.
- **Build errors**: Run `idf.py set-target esp32c6` if the target is not already configured.
- **Cannot flash C6**: Verify ESP-Prog pin connections and that the correct serial port is used.

## Related Examples

- [webrtc_classic](../webrtc_classic/) - Main WebRTC example that uses this network adapter on P4
- [signaling_only](../signaling_only/) - Alternative co-processor firmware that handles signaling logic (for split mode)
