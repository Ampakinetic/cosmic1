#ifndef LORA_COMM_H
#define LORA_COMM_H

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "balloon_config.h"
#include "sensor_pins.h"
#include "common_types.h"

// ===========================
// LoRa Data Structures
// ===========================

// PacketType - defined in common_types.h

enum class Priority : uint8_t {
    EMERGENCY = 1,
    GPS = 2,
    TELEMETRY = 3,
    CAMERA = 4,
    STATUS = 5
};

struct LoRaPacketHeader {
    uint8_t version;         // Protocol version
    uint8_t deviceId;        // Device identifier
    uint8_t flags;           // Status flags
    uint8_t retryCount;      // Retry attempt count
    uint32_t timestamp;      // Unix timestamp
    uint16_t batteryLevel;   // Battery voltage (scaled by 100)
    int8_t rssiAvg;         // Average RSSI
    int8_t snrAvg;          // Average SNR
};

struct Packet {
    LoRaPacketHeader header;
    PacketType type;
    uint16_t sequenceNumber;
    uint8_t* payload;
    size_t payloadLength;
    uint16_t crc16;
    int8_t rssi;            // RSSI of received packet
    int8_t snr;             // SNR of received packet
    bool valid;              // Packet validity
};

struct QueuedPacket {
    Packet packet;
    Priority priority;
    uint32_t enqueueTime;
    uint8_t transmitAttempts;
    uint32_t lastTransmitTime;
    bool waitingForAck;
};

// ===========================
// LoRa Manager Class
// ===========================

class LoRaManager {
private:
    // LoRa configuration
    float frequency;
    int spreadingFactor;
    long bandwidth;
    int codingRate;
    int txPower;
    int preambleLength;
    byte syncWord;
    
    // Current settings
    int currentSpreadingFactor;
    long currentBandwidth;
    int currentTxPower;
    
    // Packet management
    uint16_t nextSequenceNumber;
    uint8_t deviceId;
    
    // Priority queues
    static const int MAX_QUEUE_SIZE = 10;
    QueuedPacket priorityQueues[5][MAX_QUEUE_SIZE];  // 5 priority levels
    int queueSizes[5];  // Current size of each queue
    
    // Transmission state
    bool transmitting;
    bool receiving;
    uint32_t transmitStartTime;
    uint32_t lastReceiveTime;
    
    // ACK/NACK handling
    uint32_t ackTimeout;
    static const int MAX_RETRIES = 3;
    static const uint32_t ACK_TIMEOUT_MS = 2000U;
    
    // Signal quality monitoring
    static const int RSSI_HISTORY_SIZE = 10;
    int8_t rssiHistory[RSSI_HISTORY_SIZE];
    int8_t snrHistory[RSSI_HISTORY_SIZE];
    int rssiIndex;
    int snrIndex;
    int8_t lastRssi;
    int8_t lastSnr;
    
    // Error tracking
    uint32_t transmitErrorCount;
    uint32_t receiveErrorCount;
    uint32_t crcErrorCount;
    uint32_t ackTimeoutCount;
    
    // Private methods
    bool initLoRaModule();
    void configureLoRaSettings();
    bool transmitPacket(const Packet& packet);
    bool receivePacket(Packet& packet);
    void processIncomingPacket();
    void handleAck(const Packet& ack);
    void handleNack(const Packet& nack);
    void updateSignalQuality(int8_t rssi, int8_t snr);
    void adaptTransmissionSettings();
    uint16_t calculateCRC16(const uint8_t* data, size_t length);
    bool validatePacket(const Packet& packet);
    void addToQueueInternal(const QueuedPacket& queuedPacket);
    QueuedPacket* getNextPacket();
    void removePacketFromQueue(Priority priority, int index);
    
public:
    LoRaManager();
    ~LoRaManager();
    
    // Initialization
    bool begin();
    void end();
    bool reinitialize();
    
    // Configuration
    bool setFrequency(long freq);
    bool setSpreadingFactor(int sf);
    bool setBandwidth(long bw);
    bool setTxPower(int power);
    bool setCodingRate(int cr);
    bool setSyncWord(byte sw);
    
    float getFrequency() const { return frequency; }
    int getSpreadingFactor() const { return spreadingFactor; }
    long getBandwidth() const { return bandwidth; }
    int getTxPower() const { return txPower; }
    
    // Packet operations
    bool sendPacket(const Packet& packet, Priority priority);
    bool sendTelemetry(const uint8_t* data, size_t length);
    bool sendGPSData(const uint8_t* data, size_t length);
    bool sendCameraThumbnail(const uint8_t* data, size_t length);
    bool sendStatus(const uint8_t* data, size_t length);
    bool sendEmergency(const uint8_t* data, size_t length);
    
    // Queue management
    void addToQueue(const Packet& packet, Priority priority);
    bool processQueue();
    void clearQueue();
    int getQueueSize(Priority priority) const;
    int getTotalQueueSize() const;
    
    // ACK/NACK handling
    void handleAcknowledgment(const Packet& ack);
    void sendAck(uint16_t sequenceNumber, uint8_t ackType, int8_t rssi, int8_t snr);
    void sendNack(uint16_t sequenceNumber, uint8_t nackType);
    
    // Adaptive transmission
    void adaptTransmissionSettings(int8_t rssi, int8_t snr);
    void enableAdaptiveMode(bool enable);
    bool isAdaptiveModeEnabled() const;
    
    // Signal quality
    int8_t getLastRSSI() const { return lastRssi; }
    int8_t getLastSNR() const { return lastSnr; }
    int8_t getAverageRSSI() const;
    int8_t getAverageSNR() const;
    float getPacketErrorRate() const;
    
    // Status methods
    bool isReady() const;
    bool isTransmitting() const { return transmitting; }
    bool isReceiving() const { return receiving; }
    uint32_t getLastTransmitTime() const;
    uint32_t getLastReceiveTime() const { return lastReceiveTime; }
    
    // Statistics
    uint32_t getTransmitErrorCount() const { return transmitErrorCount; }
    uint32_t getReceiveErrorCount() const { return receiveErrorCount; }
    uint32_t getCrcErrorCount() const { return crcErrorCount; }
    uint32_t getAckTimeoutCount() const { return ackTimeoutCount; }
    void resetStatistics();
    
    // Power management
    void enterLowPowerMode();
    void exitLowPowerMode();
    void sleep();
    void wakeup();
    
    // Debug methods
    void printLoRaInfo() const;
    void printQueueStatus() const;
    void printSignalQuality() const;
    void printStatistics() const;
    void printPacket(const Packet& packet) const;
};

// ===========================
// Global Instance
// ===========================

extern LoRaManager& LoRaComm();

// ===========================
// Utility Functions
// ===========================

Packet createPacket(PacketType type, const uint8_t* payload, size_t length);
uint16_t calculatePacketCRC(const Packet& packet);
bool serializePacket(const Packet& packet, uint8_t* buffer, size_t& length);
bool deserializePacket(const uint8_t* buffer, size_t length, Packet& packet);
const char* packetTypeToString(PacketType type);
const char* priorityToString(Priority priority);

#endif // LORA_COMM_H
