# Cosmic1 ESP32 Camera Project - PlatformIO Version

This project has been successfully converted from Arduino IDE to PlatformIO. It provides an ESP32-based camera web server with streaming capabilities.

## Project Structure

```
├── platformio.ini          # PlatformIO configuration
├── partitions.csv          # Partition table for ESP32
├── src/                  # Source files
│   ├── main.cpp          # Main application (converted from .ino)
│   └── app_httpd.cpp     # HTTP server implementation
├── include/              # Header files
│   ├── board_config.h     # Camera model configuration
│   ├── camera_pins.h     # GPIO pin definitions
│   ├── camera_index.h    # Web interface data
│   └── wifi_config.h     # WiFi credentials configuration
└── lib/                 # Custom libraries (empty)
```

## Hardware Configuration

### Current Configuration
- **Board**: ESP32 Dev Module
- **Camera Model**: ESP_EYE (configurable via `board_config.h`)
- **PSRAM**: Required for high-resolution camera support
- **Flash**: 4MB with 3MB APP partition

### Supported Camera Models
The project supports multiple ESP32 camera boards:
- ESP_EYE (default)
- AI_THINKER
- M5STACK_PSRAM
- M5STACK_WIDE
- ESP32S3_EYE
- And many more...

To change camera model, edit `include/board_config.h` and uncomment the desired model.

## Setup Instructions

### 1. Configure WiFi
Edit `include/wifi_config.h` and update your WiFi credentials:

```cpp
const char *WIFI_SSID = "your_wifi_ssid";
const char *WIFI_PASSWORD = "your_wifi_password";
```

### 2. Build and Upload
```bash
# Build the project
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

### 3. Access the Camera
After successful connection, open your browser and navigate to:
```
http://[ESP32_IP_ADDRESS]
```

The IP address will be displayed in the serial monitor.

## Features

- **Live Video Streaming**: MJPEG stream accessible via `/stream`
- **Image Capture**: Single image capture via `/capture`
- **Camera Control**: Adjustable parameters via web interface
- **Multiple Formats**: JPEG and BMP support
- **LED Flash Control**: For supported boards
- **Camera Settings**: Brightness, contrast, saturation, etc.

## Web Interface Endpoints

- `/` - Main web interface
- `/stream` - Live video stream
- `/capture` - Single image capture
- `/status` - JSON camera status
- `/control` - Camera parameter control
- `/bmp` - BMP format capture

## Build Configuration

### PlatformIO Settings
- **Platform**: Espressif32
- **Framework**: Arduino
- **Board**: esp32dev
- **Monitor Speed**: 115200 baud

### Build Flags
- PSRAM support enabled
- ESP-EYE camera model selected
- Debug logging enabled

### Libraries
- espressif/esp32-camera@^2.0.4
- bblanchon/ArduinoJson@^6.21.3

## Development

### Adding New Camera Models
1. Add pin definitions to `include/camera_pins.h`
2. Add model macro to `include/board_config.h`
3. Update build flags in `platformio.ini` if needed

### Customization
- Modify `src/main.cpp` for application logic changes
- Edit `src/app_httpd.cpp` for web server modifications
- Adjust camera parameters via web interface or code

## Troubleshooting

### Build Issues
- Ensure PlatformIO is updated: `pio upgrade`
- Clean build: `pio run --target clean`

### Runtime Issues
- Check WiFi credentials in `wifi_config.h`
- Verify camera model matches hardware
- Ensure sufficient PSRAM is available

### Camera Initialization Failures
- Check pin configurations for your camera model
- Verify proper wiring
- Ensure power supply is adequate

## Migration from Arduino IDE

This PlatformIO version maintains full compatibility with the original Arduino project. Key changes:
- `.ino` file converted to `main.cpp`
- WiFi credentials externalized to `wifi_config.h`
- Proper PlatformIO directory structure
- Automated library management
- Cross-platform build system

## License

This project maintains the same license as the original Arduino version.
