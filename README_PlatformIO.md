## Build Configuration

### PlatformIO Settings
- **Platform**: Espressif32
- **Framework**: Arduino
- **Board**: esp32-s3-devkitc-1
- **Monitor Speed**: 115200 baud
- **Upload Speed**: 921600 baud

### Build Flags
- ESP32-S3 specific optimizations
- PSRAM support enabled
- ESP32S3_EYE camera model selected
- USB-CDC enabled for serial monitoring
- Debug logging enabled

### Libraries
- espressif/esp32-camera@^2.0.4
- bblanchon/ArduinoJson@^6.21.3

### ESP32-S3 Specific Settings
- Memory type: QIO OPI
- Flash size: 16MB
- PSRAM type: OPI (if available)
- USB mode: CDC

## Enhanced Server Features

### IP Address Display
The camera server now displays detailed network information on startup:
- **WiFi Mode**: Station, Access Point, or AP+STA
- **IP Address**: Shows current IP address for each mode
- **Connection Status**: Real-time connection information

### Server Startup Messages
```
Starting web server on port: '80' (Mode: Station, IP: 192.168.1.100)
Starting stream server on port: '81' (Mode: Station, IP: 192.168.1.100)
```

### Network Modes Supported
- **Station Mode**: Connects to WiFi router
- **Access Point Mode**: Creates hotspot for configuration
- **AP+STA Mode**: Dual mode for advanced setups
