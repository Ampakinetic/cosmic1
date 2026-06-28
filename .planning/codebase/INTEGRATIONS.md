# External Integrations

**Analysis Date:** 2026-06-28

## APIs & External Services

**Sensor Libraries:**
- Adafruit BMP280 Library - Pressure/temperature sensing
  - Location: `.pio/libdeps/esp32-s3-balloon/Adafruit BMP280 Library/`
  - Protocol: I2C (GPIO 1=SDA, GPIO 2=SCL)
  - Usage: Altitude calculation, weather telemetry

- TinyGPSPlus - GPS data parsing
  - Location: `.pio/libdeps/esp32-s3-balloon/TinyGPSPlus/`
  - Protocol: UART (GPIO 45=RX, 46=TX)
  - Usage: Position, speed, altitude, satellite tracking

**Radio Communication:**
- LoRa Library - Long-range radio
  - Location: `.pio/libdeps/esp32-s3-balloon/LoRa/`
  - Protocol: SPI (GPIO 3=SCK, 41=CS, 47=MOSI, 48=MISO)
  - Frequency: 915MHz (US/Canada band)
  - Usage: Balloon-to-ground station communication

## Data Storage

**Non-Volatile Storage (NVS):**
- ESP32 NVS partition (20KB in flash)
  - Purpose: WiFi credentials, configuration persistence
  - Location: `include/wifi_config.h` (runtime configuration)
  - Preferences: `Preferences.h` library

**Flash Storage:**
- Custom partition scheme
  - APP: 3MB (firmware)
  - FR: 128KB (file storage)
  - Coredump: 64KB (crash dumps)

**File Storage:**
- SPIFFS/FFAT (optional for camera images)
  - Location: Not currently implemented

**Caching:**
- PSRAM (8MB OPI) - Camera frame buffer
  - Purpose: Image capture and buffering
  - Used by: esp32-camera library

## Authentication & Identity

**Device Identification:**
- Device Type Selection (compile-time)
  - Balloon Unit (`DEVICE_TYPE=DEVICE_BALLOON`)
  - Base Station (`DEVICE_TYPE=DEVICE_BASE_STATION`)
  - Location: `include/balloon_config.h`

**WiFi Security:**
- WPA2/WPA3 PSK
  - Config: `include/wifi_config.h`
  - SSID/Password stored as constants

**Access Point Mode (Fallback):**
- Network: "ESP32-Camera-Setup"
- Password: "12345678"
- IP: 192.168.4.1

## Monitoring & Observability

**Error Tracking:**
- ESP Exception Decoder
  - Serial monitor filter: `esp32_exception_decoder`
  - Location: Serial output (115200 baud)

**Logs:**
- Serial debug output
  - Framework: Arduino Serial
  - Baud rate: 115200
  - Levels: CORE_DEBUG_LEVEL=3
  - Custom debug: `src/debug_utils.cpp`

**Crash Dumping:**
- ESP32 Core Dump
  - Partition: 64KB coredump region
  - Trigger: System crashes/faults

## CI/CD & Deployment

**Hosting:**
- None (embedded firmware, no cloud hosting)

**Build System:**
- PlatformIO
  - Build command: `pio run`
  - Upload: `pio run --target upload`
  - Monitor: `pio device monitor`
  - Clean: `pio run --target clean`

**Deployment Method:**
- USB-Serial upload (esptool)
  - Protocol: esptool
  - Speed: 921600 baud
  - Target: ESP32-S3 via USB-C

## Environment Configuration

**Required env vars:**
- PlatformIO-managed (no shell env vars)
- WiFi credentials in `include/wifi_config.h`
  - `WIFI_SSID` - WiFi network name
  - `WIFI_PASSWORD` - WiFi network password

**Secrets location:**
- `include/wifi_config.h` (source code - NOT committed to git)
- `.gitignore` configured to exclude credentials

**Build-time Configuration:**
- `platformio.ini` - Build flags and library versions
- `partitions.csv` - Flash partition layout
- `include/balloon_config.h` - Device-specific settings

## Webhooks & Callbacks

**Incoming:**
- HTTP Server endpoints (`src/app_httpd.cpp`)
  - `/` - Main web interface
  - `/stream` - MJPEG video stream
  - `/capture` - Single image capture
  - `/status` - JSON status
  - `/control` - Camera parameters

**Outgoing:**
- LoRa packet transmission (custom protocol)
  - Frequency: Every 10 seconds (telemetry)
  - Packet types defined in `include/balloon_config.h`
  - Protocol: See `docs/COMMUNICATION_PROTOCOL.md`

## Hardware Interfaces

**I2C Bus (BMP280):**
- SDA: GPIO 1
- SCL: GPIO 2
- Pull-up resistors: 4.7kΩ on both lines

**SPI Bus (LoRa):**
- CS: GPIO 41
- SCK: GPIO 3
- MOSI: GPIO 47
- MISO: GPIO 48
- RST: GPIO 19
- DIO0: GPIO 20
- DIO1: GPIO 21

**UART (GPS):**
- RX: GPIO 45 (connected to GPS TX)
- TX: GPIO 46 (connected to GPS RX)
- Baud: 9600 (default)

**Camera Interface (Parallel):**
- Data pins: GPIO 4-18 (except 14)
- I2C (SCCB): GPIO 4 (SIOC), 5 (SIOD)
- Control: GPIO 15 (XCLK), 16 (PCLK)
- See: `include/camera_pins.h`

## Communication Protocols

**LoRa Protocol:**
- Custom packet format
  - Preamble: 8 bytes
  - Header: 8 bytes (version, device ID, flags)
  - Type: 1 byte (0x01-0xFF)
  - Sequence: 2 bytes
  - Payload: Variable (max 220 bytes)
  - CRC-16: 2 bytes
- Adaptive spreading: SF7-SF12
- Bandwidth: 125kHz
- Coding Rate: 4/5
- Full spec: `docs/COMMUNICATION_PROTOCOL.md`

**HTTP/WebSocket:**
- HTTP 1.1 server for camera interface
- MJPEG streaming for video
- REST API for camera control

---

*Integration audit: 2026-06-28*
