#ifndef PACKET_HANDLER_H
#define PACKET_HANDLER_H

#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include "sensor_pins.h"
#include "balloon_config.h"
#include "common_types.h"

// ===========================
// Packet Handler Module
// ESP32-S3 Balloon Project
// ===========================

// Packet Types - defined in common_types.h

// Alert Types
enum class AlertType : uint8_t {
    LOW_BATTERY = 0x01,
    CRITICAL_BATTERY = 0x02,
    SYSTEM_ERROR = 0x03,
    SENSOR_FAILURE = 0x04,
    COMMUNICATION_LOST = 0x05,
    MEMORY_FULL = 0x06,
    OVERHEATING = 0x07
};

// Packet Priority - defined in common_types.h
// PacketHeader - defined in common_types.h

struct PacketFooter {
    uint16_t crc16;
    uint8_t endByte1;           // 0x0D
    uint8_t endByte2;           // 0x0A
};

struct TelemetryData {
    float temperature;
    float pressure;
    float humidity;
    float batteryVoltage;
    float batteryCurrent;
    uint8_t batteryPercentage;
    uint32_t uptime;
    int8_t rssi;
    uint16_t freeHeap;
    float cpuTemperature;
    uint8_t powerState;
};

// GPSData - defined in common_types.h

struct CameraData {
    uint16_t imageId;
    uint32_t timestamp;
    uint16_t imageSize;
    uint8_t compression;
    float brightness;
    float contrast;
    uint8_t faceCount;
    uint8_t objectCount;
};

struct AlertData {
    AlertType alertType;
    uint32_t timestamp;
    uint8_t severity;
    char message[64];
    float sensorValue;
    uint8_t sensorId;
};

// Packet Buffer
struct PacketBuffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
    uint32_t timestamp;
    PacketPriority priority;
    bool ready;
};

// ===========================
// Packet Handler Class
// ===========================

class PacketHandler {
public:
    // Constructor/Destructor
    PacketHandler();
    ~PacketHandler();

    // Initialization
    bool begin();
    void end();
    bool reinitialize();

    // Main Operations
    bool processIncomingData(uint8_t* data, size_t length);
    bool createPacket(PacketType type, void* payload, size_t payloadSize);
    bool sendPacket();
    bool sendUrgentPacket(PacketType type, void* payload, size_t payloadSize);

    // Packet Creation Methods
    bool createHeartbeatPacket();
    bool createTelemetryPacket(const TelemetryData& data);
    bool createGPSPacket(const GPSData& data);
    bool createCameraPacket(const CameraData& data);
    bool createAlertPacket(const AlertData& data);
    bool createStatusPacket(const char* status);
    bool createDebugPacket(const char* message);

    // Data Extraction
    bool extractTelemetry(TelemetryData& data);
    bool extractGPS(GPSData& data);
    bool extractCamera(CameraData& data);
    bool extractAlert(AlertData& data);
    bool extractCommand(uint8_t& commandId, uint8_t* params, size_t& paramLength);

    // Buffer Management
    bool addToBuffer(uint8_t* data, size_t length, PacketPriority priority = PacketPriority::PRIORITY_NORMAL);
    bool getBufferedPacket(uint8_t*& packetData, size_t& packetSize);
    void clearBuffer();
    size_t getBufferUsage() const;
    size_t getBufferCapacity() const;

    // Statistics
    void resetStatistics();
    uint32_t getPacketsSent() const { return packetsSent; }
    uint32_t getPacketsReceived() const { return packetsReceived; }
    uint32_t getPacketsDropped() const { return packetsDropped; }
    uint32_t getCRCErrors() const { return crcErrors; }
    float getPacketLossRate() const;
    uint32_t getLastPacketTime() const { return lastPacketTime; }

    // Configuration
    void setSequenceNumber(uint8_t seq) { currentSequenceNumber = seq; }
    uint8_t getSequenceNumber() const { return currentSequenceNumber; }
    void setMaxPacketSize(size_t maxSize) { maxPacketSize = maxSize; }
    void setBufferSize(size_t bufferSize);
    void enableAck(bool enable) { ackEnabled = enable; }
    void setRetryCount(uint8_t retries) { maxRetries = retries; }

    // Validation and Diagnostics
    bool validatePacket(const uint8_t* packet, size_t length);
    bool isBufferFull() const;
    void printStatistics() const;
    void printBufferStatus() const;

    // Utility Methods
    const char* packetTypeToString(PacketType type) const;
    const char* alertTypeToString(AlertType type) const;
    const char* priorityToString(PacketPriority priority) const;

private:
    // Internal State
    uint8_t currentSequenceNumber;
    uint8_t receiveBuffer[512];
    size_t receiveIndex;
    bool inPacket;
    PacketHeader currentHeader;
    size_t expectedPayloadSize;
    uint32_t lastPacketTime;

    // Buffer Management
    PacketBuffer packetQueue[16];
    size_t queueSize;
    size_t queueHead;
    size_t queueTail;
    size_t maxPacketSize;

    // Statistics
    uint32_t packetsSent;
    uint32_t packetsReceived;
    uint32_t packetsDropped;
    uint32_t crcErrors;
    uint32_t lastStatisticsReset;

    // Configuration
    bool ackEnabled;
    uint8_t maxRetries;
    size_t bufferSize;

    // CRC Calculation
    uint8_t calculateCRC8(const uint8_t* data, size_t length);
    uint16_t calculateCRC16(const uint8_t* data, size_t length);
    bool verifyCRC(const uint8_t* packet, size_t length);

    // Packet Assembly
    bool assemblePacket(PacketType type, const uint8_t* payload, size_t payloadSize, 
                     uint8_t*& packetData, size_t& packetSize);
    bool disassemblePacket(const uint8_t* packet, size_t length, 
                        PacketType& type, uint8_t*& payload, size_t& payloadSize);

    // Buffer Operations
    bool enqueuePacket(const uint8_t* packetData, size_t packetSize, PacketPriority priority);
    bool dequeuePacket(uint8_t*& packetData, size_t& packetSize);
    void sortQueueByPriority();
    size_t findOldestLowPriorityPacket() const;

    // Data Conversion
    void floatToBytes(float value, uint8_t* bytes);
    float bytesToFloat(const uint8_t* bytes);
    void uint16ToBytes(uint16_t value, uint8_t* bytes);
    uint16_t bytesToUint16(const uint8_t* bytes);
    void uint32ToBytes(uint32_t value, uint8_t* bytes);
    uint32_t bytesToUint32(const uint8_t* bytes);

    // Internal Helpers
    void resetReceiveState();
    bool processReceivedByte(uint8_t byte);
    bool validateHeader(const PacketHeader& header);
    bool processCompletePacket();
    void updateStatistics(PacketType type, bool sent = true);
    bool shouldRetransmit(PacketType type, uint8_t attempts);

    // Debug and Logging
    void logPacket(const uint8_t* packet, size_t length, bool outgoing = true) const;
    void logError(const char* message) const;
};

// ===========================
// Global Instance Access
// ===========================

extern PacketHandler& PacketMgr();

// ===========================
// Constants and Configuration
// ===========================

#define PACKET_START_BYTE1     0xAA
#define PACKET_START_BYTE2     0x55
#define PACKET_END_BYTE1       0x0D
#define PACKET_END_BYTE2       0x0A
// MAX_PACKET_SIZE defined in balloon_config.h
#define MAX_PAYLOAD_SIZE       200
#define DEFAULT_BUFFER_SIZE     1024
#define PACKET_TIMEOUT_MS      5000
#define MAX_RETRIES           3
#define CRC8_POLYNOMIAL       0x07
#define CRC16_POLYNOMIAL      0x1021

// Debug Options
#define DEBUG_PACKET_HANDLER   DEBUG_GLOBAL
#define PACKET_STATS_ENABLED   true
#define LOG_PACKET_CONTENTS    false

#endif // PACKET_HANDLER_H
