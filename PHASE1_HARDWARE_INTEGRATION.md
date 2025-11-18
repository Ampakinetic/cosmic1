# Phase 1: Hardware Integration Tasks

## Objective
Integrate all new sensors and LoRa module with proper pin configuration for the ESP32-S3 balloon project.

## Duration
2-3 days

## Prerequisites
- ESP32-S3 DevKitC-1 board with camera module working
- All hardware components acquired:
  - BMP280 pressure/temperature sensor
  - MAX-M10S GPS module with antenna
  - LoRa 900T30D radio module with antenna
  - Status LEDs (3x) and resistors
  - Power supply components
  - Prototyping board and wiring

## Detailed Tasks

### Task 1.1: Prepare Workspace and Components
**Time Estimate**: 2 hours
**Status**: ❌ Not Started

**Subtasks**:
- [ ] Set up clean workspace with proper lighting
- [ ] Organize all components and tools
- [ ] Verify component specifications and datasheets
- [ ] Check for any damaged components
- [ ] Prepare necessary tools (soldering iron, multimeter, etc.)

**Materials Needed**:
- Antistatic mat
- Multimeter
- Soldering iron and solder
- Wire strippers/cutters
- Breadboard or prototyping board
- Jumper wires
- Resistors (4.7kΩ, 220Ω)

### Task 1.2: Mount ESP32-S3 and Power System
**Time Estimate**: 1 hour
**Status**: ❌ Not Started

**Subtasks**:
- [ ] Mount ESP32-S3 DevKitC-1 on prototyping board
- [ ] Connect 3.3V power supply with adequate current capacity
- [ ] Add decoupling capacitors (100nF near power pins)
- [ ] Add bulk capacitor (10µF) for stability
- [ ] Verify power supply output (3.3V ±5%)
- [ ] Test ESP32-S3 power-up with existing camera

**Verification**:
- [ ] ESP32-S3 boots successfully
- [ ] Camera module initializes properly
- [ ] Power draw within specifications
- [ ] No voltage drops under load

### Task 1.3: Connect BMP280 Sensor (I2C)
**Time Estimate**: 45 minutes
**Status**: ❌ Not Started

**Connections**:
```
BMP280 → ESP32-S3
VCC → 3.3V
GND → GND
SDA → GPIO 1 (with 4.7kΩ pull-up)
SCL → GPIO 2 (with 4.7kΩ pull-up)
```

**Subtasks**:
- [ ] Connect BMP280 VCC and GND
- [ ] Add 4.7kΩ pull-up resistors to SDA and SCL lines
- [ ] Connect SDA to GPIO 1 and SCL to GPIO 2
- [ ] Verify connections with multimeter
- [ ] Test I2C communication with scanner

**Test Code Required**:
- I2C scanner to detect BMP280 at address 0x76
- Basic temperature/pressure reading test

**Verification**:
- [ ] BMP280 detected at I2C address 0x76
- [ ] Temperature readings are reasonable (±5°C)
- [ ] Pressure readings are reasonable (±10 hPa)
- [ ] No I2C bus errors

### Task 1.4: Connect MAX-M10S GPS Module (UART)
**Time Estimate**: 1 hour
**Status**: ❌ Not Started

**Connections**:
```
MAX-M10S → ESP32-S3
VCC → 3.3V
GND → GND
TX → GPIO 43 (ESP32 RX)
RX → GPIO 44 (ESP32 TX)
PPS → GPIO 42 (optional)
```

**Subtasks**:
- [ ] Connect GPS VCC and GND
- [ ] Connect GPS TX to GPIO 43
- [ ] Connect GPS RX to GPIO 44
- [ ] Optional: Connect PPS to GPIO 42
- [ ] Position GPS antenna for good sky view
- [ ] Test UART communication

**Test Code Required**:
- Basic NMEA sentence parsing test
- GPS status and satellite count verification

**Verification**:
- [ ] GPS module powers on
- [ ] NMEA sentences received correctly
- [ ] GPS gets satellite lock (may take time)
- [ ] PPS signal detected (if connected)
- [ ] Baud rate communication successful (9600)

### Task 1.5: Connect LoRa Module (SPI)
**Time Estimate**: 1.5 hours
**Status**: ❌ Not Started

**Connections**:
```
LoRa 900T30D → ESP32-S3
VCC → 3.3V
GND → GND
NSS/CS → GPIO 14
SCK → GPIO 21
MOSI → GPIO 47
MISO → GPIO 48
RST → GPIO 19
DIO0 → GPIO 20
DIO1 → GPIO 23 (optional)
ANT → 915MHz antenna
```

**Subtasks**:
- [ ] Connect LoRa VCC and GND
- [ ] Connect SPI interface pins
- [ ] Connect control pins (CS, RST, DIO0)
- [ ] Optional: Connect DIO1
- [ ] Attach 915MHz antenna securely
- [ ] Verify antenna connection
- [ ] Test SPI communication

**Test Code Required**:
- SPI initialization test
- LoRa module register read/write test
- Basic transmission test (loopback if possible)

**Verification**:
- [ ] LoRa module responds to SPI commands
- [ ] Module registers accessible
- [ ] Antenna detected (if supported)
- [ ] Basic transmission/reception works

### Task 1.6: Add Status LEDs
**Time Estimate**: 30 minutes
**Status**: ❌ Not Started

**Connections**:
```
LEDs → ESP32-S3
GPS Lock LED → GPIO 38 (with 220Ω resistor)
LoRa TX LED → GPIO 39 (with 220Ω resistor)
Error LED → GPIO 40 (with 220Ω resistor)
All LED cathodes → GND
```

**Subtasks**:
- [ ] Connect LEDs with 220Ω current-limiting resistors
- [ ] Connect LED anodes to GPIO pins 38, 39, 40
- [ ] Connect all cathodes to GND
- [ ] Test LED functionality
- [ ] Verify current draw (~10mA per LED)

**Test Code Required**:
- LED blink test for each LED
- PWM dimming test (optional)

**Verification**:
- [ ] All LEDs turn on/off correctly
- [ ] LED brightness is appropriate
- [ ] No GPIO pin conflicts
- [ ] Current draw within specifications

### Task 1.7: Optional Power Management Circuit
**Time Estimate**: 1 hour
**Status**: ❌ Not Started

**Connections**:
```
Power Monitor → ESP32-S3
Battery Voltage → GPIO 4 (ADC1_CH3, with voltage divider)
Power Enable → GPIO 41 (to control sensor power)
```

**Subtasks**:
- [ ] Design voltage divider for battery monitoring
- [ ] Connect battery voltage sense to GPIO 4
- [ ] Add power control transistor/MOSFET
- [ ] Connect power enable to GPIO 41
- [ ] Test battery voltage reading
- [ ] Test sensor power switching

**Test Code Required**:
- ADC voltage reading test
- Power switching test

**Verification**:
- [ ] Battery voltage reads accurately
- [ ] Power switching works reliably
- [ ] No excessive power consumption
- [ ] Voltage divider calculations correct

### Task 1.8: System Integration Testing
**Time Estimate**: 2 hours
**Status**: ❌ Not Started

**Subtasks**:
- [ ] Power up complete system
- [ ] Verify no pin conflicts
- [ ] Test all sensors simultaneously
- [ ] Measure total power consumption
- [ ] Check for electrical noise/interference
- [ ] Verify thermal performance

**Comprehensive Tests**:
- [ ] All sensors initialize together
- [ ] No I2C/SPI/UART conflicts
- [ ] Camera still works with new components
- [ ] LoRa transmission doesn't interfere with GPS
- [ ] Power supply maintains voltage under load
- [ ] System stable for extended period

### Task 1.9: Pin Validation and Documentation
**Time Estimate**: 30 minutes
**Status**: ❌ Not Started

**Subtasks**:
- [ ] Verify all pin assignments match documentation
- [ ] Check for GPIO conflicts
- [ ] Update pin mapping if necessary
- [ ] Photograph final wiring
- [ ] Create wiring diagram
- [ ] Document any deviations from plan

**Validation Checklist**:
- [ ] Camera pins: 4,5,6,7,8,9,10,11,12,13,15,16,17,18
- [ ] BMP280 pins: 1,2
- [ ] GPS pins: 42,43,44
- [ ] LoRa pins: 14,19,20,21,23,47,48
- [ ] LED pins: 38,39,40
- [ ] No conflicts detected
- [ ] All pins within ESP32-S3 capabilities

### Task 1.10: Power Consumption Measurement
**Time Estimate**: 45 minutes
**Status**: ❌ Not Started

**Measurements Required**:
- [ ] Idle current (all sensors on, no transmission)
- [ ] Camera operation current
- [ ] GPS acquisition current
- [ ] LoRa transmission current (different power levels)
- [ ] Peak current draw
- [ ] Average current during normal operation

**Expected Values**:
- Idle: ~361mA
- Camera active: +100mA
- GPS acquisition: +50mA peak
- LoRa TX: +100mA peak
- Peak: ~931mA

**Subtasks**:
- [ ] Measure current at each operating state
- [ ] Compare with expected values
- [ ] Identify any excessive power consumption
- [ ] Verify power supply adequacy
- [ ] Document power profile

## Deliverables

### Hardware Documentation
- [ ] Complete wiring diagram
- [ ] Pin assignment table
- [ ] Power consumption report
- [ ] Component placement photos

### Test Results
- [ ] Sensor functionality test reports
- [ ] Integration test results
- [ ] Power consumption measurements
- [ ] Pin validation report

### Code Examples
- [ ] I2C scanner for BMP280
- [ ] GPS NMEA parser test
- [ ] LoRa SPI communication test
- [ ] LED control test
- [ ] Power management test

## Troubleshooting Guide

### Common Issues and Solutions

**BMP280 Not Detected**:
- Check I2C pull-up resistors (4.7kΩ)
- Verify SDA/SCL connections
- Check I2C address (0x76 vs 0x77)
- Ensure 3.3V power supply

**GPS No Lock**:
- Ensure antenna has clear sky view
- Check baud rate (9600 default)
- Allow sufficient time for cold start
- Verify UART connections (TX/RX not crossed)

**LoRa Not Responding**:
- Check SPI connections (CS, SCK, MOSI, MISO)
- Verify antenna is connected
- Check CS pin logic level
- Ensure correct SPI mode (CPOL=0, CPHA=0)

**Power Issues**:
- Measure actual current draw
- Check for short circuits
- Verify power supply capacity
- Check voltage drops under load

**Camera Conflicts**:
- Ensure new pins don't conflict with camera pins
- Check for I2C address conflicts
- Verify sufficient power for camera + sensors
- Check for electrical noise

## Success Criteria

### Functional Requirements
- [ ] All sensors detected and functional
- [ ] LoRa communication established
- [ ] Camera continues to work properly
- [ ] Status LEDs operational
- [ ] Power supply adequate

### Performance Requirements
- [ ] Power consumption within specifications
- [ ] No pin conflicts detected
- [ ] System stable for extended operation
- [ ] All components work together without interference

### Documentation Requirements
- [ ] Complete wiring documentation
- [ ] Test results documented
- [ ] Power profile established
- [ ] Pin assignments validated

## Next Phase Preparation

### Code Development Setup
- [ ] Install required libraries in Arduino IDE
- [ ] Create basic sensor test sketches
- [ ] Set up version control for firmware
- [ ] Prepare development environment

### Phase 2 Readiness
- [ ] All hardware components integrated and tested
- [ ] Power consumption profile documented
- [ ] Pin assignments finalized
- [ ] Basic sensor functionality verified

This phase provides the foundation for all subsequent development. Completing these tasks thoroughly will ensure smooth progress through the remaining implementation phases.
