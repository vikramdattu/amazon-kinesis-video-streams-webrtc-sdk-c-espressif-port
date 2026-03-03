# App Common

Shared component used by all examples. Provides Wi-Fi initialization, SPIFFS/SD card storage setup, and common configuration options via `idf.py menuconfig`.

## Configuration Options

Available under `idf.py menuconfig`:

**App Common Configs (Wi-Fi, SD-Card, Camera)**:
- `ESP_WIFI_SSID` - Wi-Fi network name
- `ESP_WIFI_PASSWORD` - Wi-Fi password
- `ESP_MAXIMUM_RETRY` - Max Wi-Fi reconnection attempts
- `APP_COMMON_USE_SPIFFS` - Read certificates from SPIFFS partition
- SD card bus width and pin configuration

**AWS Security Credentials**:
- `AWS_KVS_CHANNEL_NAME` - KVS signaling channel name
- `AWS_DEFAULT_REGION` - AWS region
- `IOT_CORE_ENABLE_CREDENTIALS` - Use IoT Core credentials (default: enabled)
- IoT Core endpoint, certificate paths, role alias, and thing name
- Direct AWS credentials (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`) when IoT Core is disabled

## Contents

- `set_kvs_sdk_path.cmake` - CMake helper to locate the KVS SDK submodule
- `spiffs_image/` - SPIFFS partition contents (CA certificate, sample H.264 frames)
- `Kconfig.projbuild` - Menuconfig definitions for shared options
