# ESP32-S3 Balloon Project Pin Mapping Guide

## Overview
This document provides complete pin mapping and wiring instructions for the ESP32-S3 balloon project, including camera, BMP280 sensor, GPS module, LoRa radio, and status LEDs.

## Pin Assignment Summary

| GPIO | Function | Component | Direction | Voltage |
|------|----------|-----------|-----------|---------|
| **Camera Pins (Already Defined)** |
| 4 | SIOC | Camera SCL | Output | 3.3V |
| 5 | SIOD | Camera SDA | I2C | 3.3V |
| 6 | VSYNC | Camera | Input | 3.3V |
| 7 | HREF | Camera | Input | 3.3V |
| 8 | Y4 | Camera | Input | 3.3V |
| 9 | Y3 | Camera | Input | 3.3V |
| 10 | Y5 | Camera | Input | 3.3V |
| 11 | Y2 | Camera | Input | 3.3V |
| 12 | Y6 | Camera | Input | 3.3V |
| 13 | PCLK | Camera | Input | 3.3V |
| 15 | XCLK | Camera | Output | 3.3V |
| 16 | Y9 | Camera | Input | 3.3V |
| 17 | Y8 | Camera | Input | 3.3V |
| 18 | Y7 | Camera | Input | 3.3V |
| **New Sensor Pins** |
| 1 | SDA | BMP280 | I2C | 3.3V |
| 2 | SCL | BMP280 | I2C | 3.3V |
| 14 | CS | LoRa NSS | Output | 3.3V |
| 19 | RST | LoRa Reset | Output | 3.3V |
| 20 | DIO0 | LoRa IRQ | Input | 3.3V |
| 21 | SCK | LoRa SPI | Output | 3.3V |
| 23 | DIO1 | LoRa DIO1 | Input | 3.3V |
| 38 | GPS Lock LED | Status | Output | 3.3V |
| 39 | LoRa TX LED | Status | Output | 3.3V |
| 40 | Error LED | Status | Output | 3.3V |
| 42 | GPS PPS | GPS | Input | 3.3V |
| 43 | RX | GPS TX → ESP32 RX | Input | 3.3V |
| 44 | TX | GPS RX ← ESP32 TX | Output | 3.3V |
| 47 | MOSI | LoRa SPI | Output | 3.3V |
| 48 | MISO | LoRa SPI | Input | 3.3V |

## Wiring Diagrams

### BMP280 Pressure/Temperature Sensor (I2C)

```
BMP280 Module    →    ESP32-S3
─────────────────────────────────────
VCC               →    3.3V
GND               →    GND
SDA/SDI           →    GPIO 1
SCL/SCK           →    GPIO 2
```

**Important Notes**:
- Add 4.7kΩ pull-up resistors on both SDA and SCL lines
- Default I2C address: 0x76 (alternative: 0x77)
- Maximum voltage: 3.6V
- Operating range: -40°C to +85°C

### MAX-M10S GPS Module (UART)

```
MAX-M10S Module   →    ESP32-S3
─────────────────────────────────────
VCC               →    3.3V
GND               →    GND
TX                →    GPIO 43 (ESP32 RX)
RX                →    GPIO 44 (ESP32 TX)
PPS (Optional)     →    GPIO 42
```

**Important Notes**:
- Baud rate: 9600 (configurable)
- PPS provides precise timing (1Hz pulse)
- Active GPS antenna recommended
- Cold start time: ~26 seconds

### LoRa 900T30D Module (SPI)

```
LoRa 900T30D      →    ESP32-S3
─────────────────────────────────────
VCC               →    3.3V
GND               →    GND
NSS/CS            →    GPIO 14
SCK               →    GPIO 21
MOSI              →    GPIO 47
MISO              →    GPIO 48
RST               →    GPIO 19
DIO0              →    GPIO 20
DIO1 (Optional)   →    GPIO 23
ANT               →    915MHz Antenna
```

**Important Notes**:
- Use proper 915MHz antenna for US/Canada
- SPI mode: CPOL=0, CPHA=0
- Maximum transmit power: 20dBm
- Range: up to 15km line-of-sight

### Status LEDs

```
LED Configuration  →    ESP32-S3
─────────────────────────────────────
GPS Lock LED       →    GPIO 38 (with 220Ω resistor)
LoRa TX LED        →    GPIO 39 (with 220Ω resistor)
Error LED          →    GPIO 40 (with 220Ω resistor)
```

**LED Functions**:
- **GPS Lock**: Steady on when GPS has valid fix
- **LoRa TX**: Brief flash when transmitting data
- **Error**: Rapid blinking for system errors

### Power Management (Optional)

```
Power Monitor     →    ESP32-S3
─────────────────────────────────────
Battery Voltage   →    GPIO 4 (ADC1_CH3, with voltage divider)
Power Enable       →    GPIO 41 (to control sensor power)
```

## Complete Wiring Schematic

```
                    ESP32-S3 DevKitC-1
                    ┌─────────────────┐
                    │                 │
                    │      USB       │
                    │                 │
                    │    3.3V  GND   │
                    └─────────────────┘
                          │ │ │
                         ─┴─┴─┴─
                         │ │ │
        ┌─────────────────┘ │ └─────────────────┐
        │                   │                   │
   ┌─────────┐         ┌─────────┐         ┌─────────┐
   │ Camera  │         │ BMP280  │         │ LoRa    │
   │ Module  │         │ Sensor  │         │ 900T30D │
   └─────────┘         └─────────┘         └─────────┘
        │                   │                   │
        └───────────────────┴───────────────────┘
                          │
                    ┌─────────┐
                    │ MAX-M10S│
                    │   GPS   │
                    └─────────┘
                          │
                    ┌─────────┐
                    │ Status  │
                    │  LEDs   │
                    └─────────┘
```

## Power Considerations

### Power Requirements

| Component | Current (Typical) | Current (Peak) | Voltage |
|-----------|-------------------|----------------|---------|
| ESP32-S3  | 200mA             | 500mA          | 3.3V    |
| Camera    | 100mA             | 200mA          | 3.3V    |
| BMP280    | 1mA               | 1mA            | 3.3V    |
| MAX-M10S  | 30mA              | 80mA           | 3.3V    |
| LoRa      | 20mA              | 120mA          | 3.3V    |
| LEDs      | 10mA              | 30mA           | 3.3V    |
| **Total** | **361mA**         | **931mA**      | **3.3V** |

### Power Supply Recommendations

**For Balloon Unit**:
- **Primary**: 3.7V LiPo battery (2000mAh+ recommended)
- **Regulator**: 3.3V LDO with 1.5A+ capability
- **Backup**: Supercapacitor for transmission peaks

**For Base Station**:
- **Primary**: USB power supply or wall adapter
- **Regulator**: On-board 3.3V sufficient
- **Backup**: Optional battery for portability

## Assembly Instructions

### Step 1: Prepare ESP32-S3 Board
1. Mount ESP32-S3 on prototyping board
2. Connect power supply (3.3V, GND)
3. Add decoupling capacitors (100nF near power pins)
4. Add bulk capacitor (10µF) for stability

### Step 2: Connect Camera Module
1. Connect camera ribbon cable to ESP32-S3
2. Verify all camera pins are connected
3. Power on and test camera functionality

### Step 3: Connect BMP280 Sensor
1. Connect VCC to 3.3V and GND to GND
2. Connect SDA to GPIO 1 with 4.7kΩ pull-up
3. Connect SCL to GPIO 2 with 4.7kΩ pull-up
4. Test I2C communication

### Step 4: Connect GPS Module
1. Connect VCC to 3.3V and GND to GND
2. Connect GPS TX to GPIO 43
3. Connect GPS RX to GPIO 44
4. Optional: Connect PPS to GPIO 42
5. Test UART communication

### Step 5: Connect LoRa Module
1. Connect VCC to 3.3V and GND to GND
2. Connect SPI pins (CS, SCK, MOSI, MISO)
3. Connect RST and DIO0 pins
4. Optional: Connect DIO1
5. Attach 915MHz antenna
6. Test SPI communication

### Step 6: Add Status LEDs
1. Connect LEDs with 220Ω current-limiting resistors
2. Connect LED anodes to GPIO pins 38, 39, 40
3. Connect all cathodes to GND
4. Test LED functionality

### Step 7: Final Integration
1. Verify all connections
2. Test power consumption
3. Upload test firmware
4. Validate all sensor functionality
5. Perform range testing

## Troubleshooting Guide

### Common Issues

**Camera Not Working**:
- Check ribbon cable connection
- Verify camera pin configuration
- Ensure sufficient power supply

**BMP280 Not Detected**:
- Check I2C pull-up resistors
- Verify SDA/SCL connections
- Test with I2C scanner

**GPS No Fix**:
- Ensure antenna has clear sky view
- Check baud rate configuration
- Verify UART connections
- Allow time for cold start

**LoRa Not Transmitting**:
- Check SPI connections
- Verify antenna is connected
- Ensure correct frequency band
- Check CS pin configuration

**Power Issues**:
- Measure current draw
- Check for short circuits
- Verify power supply capacity
- Check voltage drops

### Testing Procedures

1. **Power-On Test**: Verify LEDs blink, system boots
2. **Sensor Test**: Check each sensor individually
3. **Communication Test**: Test LoRa transmission
4. **Integration Test**: Test complete system
5. **Range Test**: Verify communication range
6. **Power Test**: Measure battery life

## Safety Considerations

### Electrical Safety
- Use proper voltage regulators
- Add reverse polarity protection
- Include overcurrent protection
- Ensure proper grounding

### RF Safety
- Use appropriate antennas
- Follow local regulations
- Maintain safe distance during high power transmission
- Monitor for interference

### Physical Safety
- Secure all connections
- Use strain relief for cables
- Protect against vibration
- Weatherproof outdoor components

This pin mapping guide provides complete wiring instructions for the ESP32-S3 balloon project. Follow these instructions carefully to ensure reliable operation and avoid conflicts between components.
