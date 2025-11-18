#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <Arduino.h>
#include <cstdint>

// ===========================
// Common Data Structures
// ESP32-S3 Balloon Project
// ===========================

// GPS Data Structure - shared between sensor_manager and packet_handler
struct GPSData {
    float latitude;        // Latitude in degrees
    float longitude;       // Longitude in degrees
    float altitude;        // GPS altitude in meters
    uint8_t satellites;    // Number of satellites
    float speed;          // Ground speed in m/s
    float course;          // Course over ground in degrees
    uint32_t fixTime;      // GPS fix timestamp
    uint8_t hdop;         // Horizontal dilution of precision
    uint8_t quality;       // Fix quality
};

// Packet Types - unified definition
enum class PacketType : uint8_t {
    HEARTBEAT = 0x01,
    TELEMETRY = 0x02,
    GPS_DATA = 0x03,
    CAMERA_DATA = 0x04,
    ALERT = 0x05,
    COMMAND_ACK = 0x06,
    STATUS = 0x07,
    DEBUG = 0x08,
    
    // LoRa-specific types (for compatibility)
    GPS = 0x02,
    CAMERA_THUMB = 0x03,
    CAMERA_FULL = 0x04,
    ACK = 0x06,
    NACK = 0x07,
    PING = 0x08,
    PONG = 0x09,
    EMERGENCY = 0xFF
};

// Packet Priority - unified definition
enum class PacketPriority : uint8_t {
    PRIORITY_LOW = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_HIGH = 2,
    PRIORITY_CRITICAL = 3,
    
    // LoRa-specific priorities (for compatibility)
    EMERGENCY = 1,
    GPS = 2,
    TELEMETRY = 3,
    CAMERA = 4,
    STATUS = 5
};

// Common Packet Header - unified structure
struct PacketHeader {
    uint8_t startByte1;         // 0xAA
    uint8_t startByte2;         // 0x55
    PacketType packetType;
    uint8_t sequenceNumber;
    uint16_t payloadLength;
    uint8_t crc8;
};

#endif // COMMON_TYPES_H
