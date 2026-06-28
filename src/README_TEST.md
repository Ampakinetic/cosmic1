# OLED + LoRa Test Sketch

## Overview
`test_oled_lora.cpp` is a comprehensive test sketch for validating the OLED display and LoRa E32 module connections.

## How to Use This Test

### Option 1: Temporarily Replace main.cpp
1. Rename your current `src/main.cpp` to `src/main_balloon.cpp` (backup)
2. Rename `test_oled_lora.cpp` to `main.cpp`
3. Build and upload: `pio run --target upload`
4. After testing, rename files back

### Option 2: Use Separate Test Environment
Add this to your `platformio.ini`:
```ini
[env:esp32-s3-test]
extends = env:esp32-s3-balloon
build_type = test
```

Then build with: `pio run -e esp32-s3-test`

## What It Tests

### I2C Bus Test
- Scans for all I2C devices
- Verifies OLED at 0x3C (or 0x3D)
- Verifies BMP280 at 0x76 (if connected)
- Shows device addresses on OLED

### OLED Display Test
- Graphics test (rectangles, circles, lines, triangles)
- Text test (different sizes 1-4)
- Pixel rendering verification

### LoRa E32 Module Test
- Pin configuration test (M0, M1, AUX)
- Mode switching test (Normal, Wake-up, Power-save, Sleep)
- UART communication test
- AUX status monitoring

## Controls

- **BOOT Button**: Cycle through tests
- **Serial Monitor**: 115200 baud for detailed output
- **OLED Display**: Visual test results

## Test Sequence

1. **I2C Scan** - Shows all I2C devices found
2. **OLED Graphics** - Draws various shapes
3. **OLED Text** - Displays different text sizes
4. **LoRa Pins** - Tests M0, M1, AUX pins
5. **LoRa Modes** - Tests all 4 mode combinations
6. **LoRa UART** - Tests Serial2 communication
7. **All Tests** - Runs complete test suite

## Expected Results

### I2C Scan
Should show:
- `0x3C (OLED)` or `0x3D (OLED?)`
- `0x76 (BMP280)` if sensor connected

### LoRa Pin Test
- M0, M1: OK
- AUX: HIGH or LOW (module dependent)
- TX(14)/RX(48): Configured

### LoRa Modes Test
- Mode 00 (Normal): AUX should be HIGH
- Mode 01 (Wake-up): AUX may vary
- Mode 10 (Power-save): AUX may vary
- Mode 11 (Sleep/Config): AUX may vary

### LoRa UART Test
- Serial2 initialized at 9600 baud
- RX data: May show bytes if module is transmitting
- TX test: Requires paired receiver module to verify

## Troubleshooting

### "No I2C devices found"
- Check SDA/SCL connections (GPIO 1/2)
- Verify pull-up resistors (4.7kΩ)
- Check power supply (3.3V)
- Try both OLED addresses (0x3C and 0x3D)

### "OLED not found at 0x3C or 0x3D"
- OLED might be 5V - check module markings
- Verify VCC connection
- Check SDA/SCL wiring
- Try a different OLED module

### "LoRa Pins: ERR"
- Verify M0/M1 connections to GPIO 19/20
- Check for loose wires
- Use multimeter to test GPIO outputs

### "AUX always LOW"
- AUX may be pulled low by module
- Check AUX connection to GPIO 21
- Module might be in sleep mode

### "No LoRa response on UART"
- Set M0/M1 to LOW (Normal mode)
- Check TX/RX connections (GPIO 14/48)
- Verify module is powered
- Try 9600 baud (default for E32)

## Hardware Configuration

```
OLED Display (SSD1306):
├── VCC → 3.3V (or 5V)
├── GND → GND
├── SDA → GPIO 1
└── SCL → GPIO 2

LoRa E32 Module:
├── VCC → 3.3V
├── GND → GND
├── M0  → GPIO 19
├── M1  → GPIO 20
├── RXD → GPIO 14
├── TXD → GPIO 48
└── AUX → GPIO 21 (optional)
```

## Required Libraries

Ensure these are in your `platformio.ini` lib_deps:
```ini
lib_deps =
    adafruit/Adafruit SSD1306
    adafruit/Adafruit GFX Library
    wire
```

## Next Steps After Testing

1. ✅ If all tests pass: Your hardware is correctly connected
2. ⚠️ If warnings: Check the specific issue mentioned
3. ❌ If failures: Review wiring and connections

After successful testing, you can integrate the OLED and LoRa code into your main balloon firmware.
