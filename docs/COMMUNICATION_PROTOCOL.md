# LoRa Communication Protocol for ESP32-S3 Balloon Project

## Overview
This document defines the communication protocol between the balloon transmitter and base station receiver using LoRa radio modules. The protocol is optimized for low bandwidth, long-range communication with error detection, acknowledgment, and adaptive transmission features.

## Protocol Characteristics

### Physical Layer
- **Frequency**: 915 MHz (US/Canada band)
- **Modulation**: LoRa (Chirp Spread Spectrum)
- **Spreading Factor**: Adaptive (SF7-SF12)
- **Bandwidth**: 125 kHz
- **Coding Rate**: 4/5 (configurable 4/6, 4/7, 4/8)
- **TX Power**: 20 dBm (maximum)
- **Payload Size**: Maximum 240 bytes per packet

### Data Link Layer
- **Medium Access**: CSMA/CA (optional)
- **Error Detection**: CRC-16
- **Flow Control**: ACK/NACK system
- **Packet Sequencing**: 16-bit sequence numbers
- **Priority System**: 5-level priority queue

## Packet Structure

### Basic Packet Format

```
+--------+--------+--------+--------+--------+--------+--------+--------+
| Preamble| Header | Type   | Seq#   | Payload| CRC-16 | RSSI   | SNR    |
| 8 bytes | 8 bytes| 1 byte | 2 bytes| N bytes| 2 bytes | 1 byte | 1 byte |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

### Field Definitions

#### Preamble (8 bytes)
- **Purpose**: Synchronization and detection
- **Format**: Fixed pattern `0xAA 0x55 0xAA 0x55 0xAA 0x55 0xAA 0x55`
- **Note**: Helps receiver detect packet start

#### Header (8 bytes)
```
+--------+--------+--------+--------+--------+--------+--------+--------+
| Version| Device | Flags  | Retry  | Timestamp|Battery| RSSI   | SNR    |
| 1 byte | ID     | 1 byte | Count  | 4 bytes | Level  | Avg     | Avg    |
|         | 1 byte |        | 1 byte |         | 1 byte | 1 byte | 1 byte |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

- **Version**: Protocol version (currently 0x01)
- **Device ID**: Unique device identifier
- **Flags**: Bit field for various flags
- **Retry Count**: Number of transmission attempts
- **Timestamp**: Unix timestamp (seconds since epoch)
- **Battery Level**: Battery voltage (scaled, 0.01V resolution)
- **RSSI Avg**: Average received signal strength (balloon only)
- **SNR Avg**: Average signal-to-noise ratio (balloon only)

#### Packet Type (1 byte)
- `0x01`: Telemetry Data
- `0x02`: GPS Position
- `0x03`: Camera Thumbnail
- `0x04`: Camera Full Image
- `0x05`: Status Message
- `0x06`: ACK (Acknowledgment)
- `0x07`: NACK (Negative Acknowledgment)
- `0x08`: Ping
- `0x09`: Pong
- `0xFF`: Emergency

#### Sequence Number (2 bytes)
- **Purpose**: Packet ordering and duplication detection
- **Range**: 0-65535 (wraps around)
- **Initialization**: Random value at startup

#### Payload (Variable length)
- **Maximum**: 220 bytes (after overhead)
- **Format**: Depends on packet type
- **Compression**: Optional LZSS compression for large payloads

#### CRC-16 (2 bytes)
- **Algorithm**: CRC-16-CCITT (x^16 + x^12 + x^5 + 1)
- **Coverage**: Header + Type + Seq# + Payload
- **Purpose**: Error detection

#### RSSI/SNR (2 bytes)
- **Added by LoRa module**: Not part of transmitted payload
- **Purpose**: Signal quality monitoring
- **Usage**: Adaptive transmission and diagnostics

## Packet Types

### 0x01: Telemetry Data
```
+--------+--------+--------+--------+--------+--------+--------+
| Pressure| Temp   | Humidity| Altitude|  Status| Reserv | CRC-16 |
| 4 bytes | 4 bytes| 4 bytes| 4 bytes | 4 bytes| 4 bytes| 2 bytes|
+--------+--------+--------+--------+--------+--------+--------+
```

**Fields**:
- **Pressure**: Atmospheric pressure (Pa, float32)
- **Temperature**: Temperature (°C, float32)
- **Humidity**: Relative humidity (%, float32, 0 if sensor not available)
- **Altitude**: Calculated altitude (m, float32)
- **Status**: Bit field for system status
  - Bit 0: GPS Lock
  - Bit 1: Camera Active
  - Bit 2: Low Battery
  - Bit 3: Error Condition
  - Bits 4-7: Reserved
- **Reserved**: Future use
- **CRC-16**: Telemetry data checksum

### 0x02: GPS Position
```
+--------+--------+--------+--------+--------+--------+--------+--------+
| Latitude|Longitude| Altitude| Speed  | Course | Satellites| HDOP  | CRC-16 |
| 8 bytes | 8 bytes | 4 bytes| 4 bytes| 4 bytes| 1 byte   | 2 bytes| 2 bytes|
+--------+--------+--------+--------+--------+--------+--------+--------+
```

**Fields**:
- **Latitude**: Latitude in degrees (double64)
- **Longitude**: Longitude in degrees (double64)
- **Altitude**: GPS altitude (m, float32)
- **Speed**: Ground speed (m/s, float32)
- **Course**: Course over ground (degrees, float32)
- **Satellites**: Number of satellites in use (uint8)
- **HDOP**: Horizontal dilution of precision (uint16, scaled by 100)
- **CRC-16**: GPS data checksum

### 0x03: Camera Thumbnail
```
+--------+--------+--------+--------+--------+--------+--------+
| Width   | Height | Quality | Size   | Chunk# | Total  | Image  | CRC-16 |
| 2 bytes | 2 bytes| 1 byte  | 2 bytes| 2 bytes| 2 bytes| N bytes| 2 bytes|
+--------+--------+--------+--------+--------+--------+--------+--------+
```

**Fields**:
- **Width**: Image width in pixels (uint16)
- **Height**: Image height in pixels (uint16)
- **Quality**: JPEG quality (0-100, uint8)
- **Size**: Total image size (uint16)
- **Chunk#**: Current chunk number (uint16, starts at 0)
- **Total**: Total chunks (uint16)
- **Image**: JPEG image chunk data (variable)
- **CRC-16**: Image data checksum

### 0x04: Camera Full Image
Same format as thumbnail but with higher resolution and quality.

### 0x05: Status Message
```
+--------+--------+--------+--------+--------+--------+
| Code    | Level   | Message| Reserv | Reserv | CRC-16 |
| 2 bytes | 1 byte  | N bytes| 4 bytes| 4 bytes| 2 bytes|
+--------+--------+--------+--------+--------+--------+
```

**Fields**:
- **Code**: Status code (uint16)
- **Level**: Severity level (uint8: 0=Info, 1=Warning, 2=Error, 3=Critical)
- **Message**: Human-readable message (null-terminated string)
- **Reserved**: Future use
- **CRC-16**: Status data checksum

### 0x06: ACK (Acknowledgment)
```
+--------+--------+--------+--------+--------+
| Ack Seq| Ack Type| RSSI   | SNR    | CRC-16 |
| 2 bytes | 1 byte | 1 byte | 1 byte | 2 bytes|
+--------+--------+--------+--------+--------+
```

**Fields**:
- **Ack Seq**: Sequence number being acknowledged (uint16)
- **Ack Type**: Type of acknowledgment (uint8)
  - 0x00: ACK (packet received successfully)
  - 0x01: NACK (packet not received, please resend)
  - 0x02: ACK with request to slow down
  - 0x03: ACK with request to speed up
- **RSSI**: Received signal strength (int8, dBm)
- **SNR**: Signal-to-noise ratio (int8, dB)
- **CRC-16**: ACK data checksum

### 0xFF: Emergency
```
+--------+--------+--------+--------+--------+--------+--------+
| Code    | Severity| Battery| Altitude| LastPos | Message| CRC-16 |
| 2 bytes | 1 byte  | 2 bytes | 4 bytes | 16 bytes| N bytes| 2 bytes|
+--------+--------+--------+--------+--------+--------+--------+
```

**Fields**:
- **Code**: Emergency code (uint16)
  - 0x0001: Battery critical
  - 0x0002: Rapid descent
  - 0x0003: GPS loss
  - 0x0004: System failure
  - 0x0005: Max altitude exceeded
- **Severity**: Emergency severity (1-5, uint8)
- **Battery**: Last known battery voltage (uint16, scaled by 100)
- **Altitude**: Last known altitude (uint32, meters)
- **LastPos**: Last known GPS position (lat+lon, 8 bytes each)
- **Message**: Emergency message (variable length)
- **CRC-16**: Emergency data checksum

## Priority System

### Priority Levels
1. **Priority 1 (Emergency)**: Emergency packets (0xFF)
2. **Priority 2 (Critical)**: GPS Position (0x02)
3. **Priority 3 (High)**: Telemetry Data (0x01)
4. **Priority 4 (Normal)**: Camera Thumbnails (0x03)
5. **Priority 5 (Low)**: Camera Full Images (0x04)

### Queue Management
- Each priority level has separate queue
- Higher priority packets transmitted first
- Within same priority, FIFO ordering
- Queue size limits prevent memory exhaustion

## Transmission Logic

### Balloon Transmitter

#### Transmission Schedule
```
Every 10 seconds:
  - Transmit GPS data (if position changed)
  - Transmit telemetry data
  - Transmit status (if changed)

Every 30 seconds:
  - Transmit camera thumbnail
  - Check for acknowledgments

Every 5 minutes:
  - Transmit full camera image (if bandwidth allows)

Emergency conditions:
  - Transmit immediately, override normal schedule
```

#### Adaptive Transmission
```
Signal Quality Based:
  - RSSI > -80 dBm: Use SF7 (fastest)
  - RSSI -80 to -95 dBm: Use SF9 (medium)
  - RSSI -95 to -110 dBm: Use SF11 (slow, reliable)
  - RSSI < -110 dBm: Use SF12 (slowest, max range)

Distance Estimation:
  - Use packet loss rate to estimate distance
  - Increase spreading factor if loss > 20%
  - Decrease spreading factor if loss < 5%

Battery-Based Adaptation:
  - Low battery: Reduce transmission power
  - Critical battery: Only transmit emergency packets
```

#### Retry Logic
```
Max Retries: 3 per packet
Backoff Strategy:
  - Attempt 1: Immediate
  - Attempt 2: Wait 1 second
  - Attempt 3: Wait 5 seconds
  - Attempt 4: Wait 15 seconds

Abort Conditions:
  - Max retries exceeded
  - Battery critical
  - Emergency mode active
```

### Base Station Receiver

#### Continuous Listening
```
Default Mode:
  - Continuous receive mode
  - Process all incoming packets
  - Send ACK/NACK as required
  - Monitor signal quality

Silence Detection:
  - No packets for 60 seconds: Enter search mode
  - Scan alternative frequencies
  - Adjust receiver parameters
  - Return to normal mode on packet receipt
```

#### ACK/NACK Logic
```
ACK Generation:
  - Valid CRC and sequence: Send ACK
  - Include current RSSI/SNR in ACK
  - Request speed adjustment if needed

NACK Generation:
  - Invalid CRC: Send NACK
  - Missing sequence number: Request retransmission
  - Buffer overflow: Request slower transmission
```

## Error Handling

### Error Detection
1. **CRC-16 Validation**: All packets must pass CRC check
2. **Sequence Validation**: Detect missing or duplicate packets
3. **Length Validation**: Verify packet length matches expected format
4. **Timeout Detection**: Detect lost packets and stale data

### Error Recovery
```
Single Packet Errors:
  - Request retransmission via NACK
  - Log error for diagnostics
  - Continue processing other packets

Multiple Packet Errors:
  - Increase spreading factor
  - Reduce transmission rate
  - Enter diagnostic mode

Persistent Errors:
  - Reset communication parameters
  - Emergency mode activation
  - System restart if necessary
```

### Emergency Procedures
```
Balloon Emergency:
  - Transmit emergency packet (Priority 1)
  - Repeat emergency packet every 30 seconds
  - Include last known position and status
  - Reduce power to conserve battery

Base Station Emergency:
  - Display emergency alerts
  - Log all emergency data
  - Notify operators
  - Attempt to maintain communication
```

## Performance Optimization

### Bandwidth Optimization
1. **Data Compression**: LZSS compression for large payloads
2. **Delta Encoding**: Only transmit changed values
3. **Bundling**: Combine multiple small packets
4. **Adaptive Rates**: Adjust transmission rate based on conditions

### Power Optimization
1. **Sleep Cycles**: Deep sleep between transmissions
2. **Transmission Timing**: Optimize for battery life
3. **Power Scaling**: Adjust TX power based on distance
4. **Sensor Scheduling**: Coordinate sensor readings

### Range Optimization
1. **Antenna Positioning**: Optimal antenna placement
2. **Frequency Selection**: Best frequency for conditions
3. **Timing Optimization**: Avoid interference periods
4. **Adaptive Modulation**: Adjust parameters for range

## Security Considerations

### Basic Security
1. **Device Authentication**: Unique device IDs
2. **Packet Validation**: CRC and sequence checking
3. **Replay Protection**: Sequence number tracking
4. **Access Control**: Limited packet types per device

### Enhanced Security (Optional)
1. **Encryption**: AES-128 encryption for sensitive data
2. **Authentication**: HMAC for packet authentication
3. **Frequency Hopping**: Spread spectrum techniques
4. **Jamming Detection**: Monitor for interference

## Testing and Validation

### Protocol Testing
```
Unit Tests:
  - Packet serialization/deserialization
  - CRC calculation and validation
  - Sequence number handling
  - Priority queue operation

Integration Tests:
  - End-to-end communication
  - Error handling and recovery
  - Adaptive transmission
  - Emergency procedures

Performance Tests:
  - Throughput measurement
  - Range testing
  - Battery life validation
  - Signal quality monitoring
```

### Test Scenarios
1. **Normal Operation**: Standard transmission/reception
2. **Interference**: Simulated RF interference
3. **Distance Testing**: Various distance conditions
4. **Power Scenarios**: Low battery conditions
5. **Emergency Testing**: Various emergency conditions

## Implementation Notes

### Memory Requirements
```
Balloon Transmitter:
  - Packet buffers: 2KB
  - Sensor data: 1KB
  - Image buffer: 50KB
  - Stack/Heap: 100KB
  - Total: ~153KB

Base Station Receiver:
  - Packet queues: 10KB
  - Image storage: 500KB
  - Historical data: 100KB
  - Web interface: 200KB
  - Total: ~810KB
```

### Timing Requirements
```
Transmission Times (per packet):
  - SF7: ~50ms
  - SF9: ~200ms
  - SF11: ~800ms
  - SF12: ~1600ms

Processing Times:
  - Sensor reading: ~10ms
  - Image capture: ~500ms
  - Packet creation: ~5ms
  - CRC calculation: ~1ms
```

### Configuration Parameters
```c
// Key configuration values
#define DEFAULT_SPREADING_FACTOR   9
#define DEFAULT_BANDWIDTH         125000
#define DEFAULT_CODING_RATE       5
#define DEFAULT_TX_POWER          20
#define MAX_RETRIES              3
#define ACK_TIMEOUT_MS           2000
#define PACKET_QUEUE_SIZE        10
#define EMERGENCY_REPEAT_MS      30000
```

This communication protocol provides a robust foundation for reliable balloon-to-ground communication using LoRa technology. The protocol is designed to handle challenging conditions while maintaining efficient use of limited bandwidth and power resources.
