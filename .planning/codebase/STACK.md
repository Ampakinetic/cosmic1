# Technology Stack

**Analysis Date:** 2026-06-08

## Languages

**Primary:**
- C++ [C++17/C++11] - ESP32 embedded firmware (Arduino Framework)
- C - ESP-IDF components (camera driver, HTTP server)

**Secondary:**
- C/C++ - PlatformIO build system and libraries
- HTML/CSS/JavaScript - Web interface (embedded in firmware)

## Runtime

**Environment:**
- ESP-IDF [ESP32-S3] - Espressif IoT Development Framework
- Arduino Core [ESP32-S3] - Arduino compatibility layer
- FreeRTOS [Built-in] - Real-time operating system (via ESP-IDF)

**Package Manager:**
- PlatformIO [Core] - Dependency and build management
- Library Registry: PlatformIO Library Registry
- Lockfile: `.pio/libdeps/<env>/` (auto-generated)

## Frameworks

**Core:**
- Arduino Framework [ESP32-S3] - Core HAL and API
- ESP-IDF [5.x] - Low-level hardware abstraction

**Camera & Imaging:**
- esp32-camera [2.0.4] - Camera driver and sensor support
- ESP32 JPEG Decoder/Encoder - Image conversion

**Sensors:**
- Adafruit BMP280 Library [2.1.0] - Pressure/temperature sensor
- TinyGPSPlus [1.0.3] - GPS NMEA parsing

**Communication:**
- LoRa [0.8.0] - LoRa radio communication (Sandeeo Mistry)
- WiFi [ESP32] - WiFi connectivity (built-in)
- HTTP Server [ESP32] - Web server for camera interface

**Data Processing:**
- ArduinoJson [6.21.3] - JSON serialization/deserialization

## Key Dependencies

**Critical:**
- espressif/esp32-camera@^2.0.4 - Camera capture and image processing
- sandeepmistry/LoRa@^0.8.0 - Long-range radio communication
- adafruit/Adafruit BMP280 Library@^2.1.0 - Atmospheric pressure/temperature
- mikalhart/TinyGPSPlus@^1.0.3 - GPS data parsing

**Infrastructure:**
- bblanchon/ArduinoJson@^6.21.3 - Configuration and data exchange
- Wire (I2C) - Sensor communication
- SPI - LoRa module communication
- HardwareSerial - GPS UART communication

## Configuration

**Environment:**
- PlatformIO project (platformio.ini)
- Multi-environment build (balloon vs base station)
- Source filter for different firmware targets
- Partition table: Custom CSV (3MB APP + 128KB FR + 64KB coredump)

**Build:**
- Build flags: PSRAM, camera model, debug level
- Board: ESP32-S3-DevKitC-1
- Flash: 16MB with OPI PSRAM
- Memory type: qio_opi

## Platform Requirements

**Development:**
- PlatformIO Core (VSCode extension or CLI)
- Python 3.x (PlatformIO dependency)
- ESP32 toolchain (managed by PlatformIO)
- ESP-Prog or USB-serial for debugging

**Production:**
- ESP32-S3 DevKitC-1 with 16MB flash + PSRAM
- 3.3V power supply (1.5A+ capability)
- Compatible camera module (ESP32-S3-EYE)
- External sensors: BMP280, GPS, LoRa module
- USB-C for programming/monitoring

---

*Stack analysis: 2026-06-08*
