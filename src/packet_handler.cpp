#include "packet_handler.h"

// Debug Options
#ifndef DEBUG_GLOBAL
#define DEBUG_GLOBAL true
#endif

// ===========================
// Static Instance
// ===========================

static PacketHandler packetHandlerInstance;

PacketHandler& PacketMgr() {
    return packetHandlerInstance;
}

// ===========================
// Constructor/Destructor
// ===========================

PacketHandler::PacketHandler() {
    // Initialize internal state
    currentSequenceNumber = 0;
    receiveIndex = 0;
    inPacket = false;
    expectedPayloadSize = 0;
    lastPacketTime = 0;

    // Initialize buffer management
    queueSize = 0;
    queueHead = 0;
    queueTail = 0;
    maxPacketSize = MAX_PACKET_SIZE;
    bufferSize = DEFAULT_BUFFER_SIZE;

    // Initialize statistics
    packetsSent = 0;
    packetsReceived = 0;
    packetsDropped = 0;
    crcErrors = 0;
    lastStatisticsReset = 0;

    // Initialize configuration
    ackEnabled = true;
    maxRetries = MAX_RETRIES;

    // Clear receive buffer
    memset(receiveBuffer, 0, sizeof(receiveBuffer));
    
    // Initialize packet queue
    for (int i = 0; i < 16; i++) {
        packetQueue[i].data = nullptr;
        packetQueue[i].size = 0;
        packetQueue[i].capacity = 0;
        packetQueue[i].timestamp = 0;
        packetQueue[i].priority = PacketPriority::PRIORITY_NORMAL;
        packetQueue[i].ready = false;
    }
}

PacketHandler::~PacketHandler() {
    end();
}

// ===========================
// Initialization
// ===========================

bool PacketHandler::begin() {
    if (DEBUG_PACKET_HANDLER) {
        Serial.println("Packet Handler: Initializing...");
    }

    // Reset internal state
    resetReceiveState();
    resetStatistics();
    clearBuffer();

    if (DEBUG_PACKET_HANDLER) {
        Serial.println("Packet Handler: Initialized successfully");
    }

    return true;
}

void PacketHandler::end() {
    // Clean up packet queue
    clearBuffer();
    
    if (DEBUG_PACKET_HANDLER) {
        Serial.println("Packet Handler: Shutdown complete");
    }
}

bool PacketHandler::reinitialize() {
    end();
    delay(10);
    return begin();
}

// ===========================
// Main Operations
// ===========================

bool PacketHandler::processIncomingData(uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return false;
    }

    bool packetProcessed = false;

    for (size_t i = 0; i < length; i++) {
        if (processReceivedByte(data[i])) {
            packetProcessed = true;
        }
    }

    if (packetProcessed) {
        lastPacketTime = millis();
    }

    return packetProcessed;
}

bool PacketHandler::createPacket(PacketType type, void* payload, size_t payloadSize) {
    if (payloadSize > MAX_PAYLOAD_SIZE) {
        logError("Payload too large");
        return false;
    }

    uint8_t* packetData = nullptr;
    size_t packetSize = 0;

    if (!assemblePacket(type, static_cast<uint8_t*>(payload), payloadSize, packetData, packetSize)) {
        return false;
    }

    bool success = addToBuffer(packetData, packetSize);
    
    if (packetData) {
        free(packetData);
    }

    return success;
}

bool PacketHandler::sendPacket() {
    uint8_t* packetData = nullptr;
    size_t packetSize = 0;

    if (!dequeuePacket(packetData, packetSize)) {
        return false;
    }

    // Send packet via LoRa (this would interface with LoRaComm)
    bool success = true; // Placeholder - would call LoRaComm.send()

    if (success) {
        packetsSent++;
        updateStatistics(PacketType::HEARTBEAT, true); // Update with actual type
        logPacket(packetData, packetSize, true);
    } else {
        packetsDropped++;
        logError("Failed to send packet");
    }

    if (packetData) {
        free(packetData);
    }

    return success;
}

bool PacketHandler::sendUrgentPacket(PacketType type, void* payload, size_t payloadSize) {
    if (payloadSize > MAX_PAYLOAD_SIZE) {
        logError("Urgent payload too large");
        return false;
    }

    uint8_t* packetData = nullptr;
    size_t packetSize = 0;

    if (!assemblePacket(type, static_cast<uint8_t*>(payload), payloadSize, packetData, packetSize)) {
        return false;
    }

    // Send urgent packet with highest priority
    bool success = true; // Placeholder - would call LoRaComm.sendUrgent()

    if (success) {
        packetsSent++;
        updateStatistics(type, true);
        logPacket(packetData, packetSize, true);
    } else {
        packetsDropped++;
        logError("Failed to send urgent packet");
    }

    if (packetData) {
        free(packetData);
    }

    return success;
}

// ===========================
// Packet Creation Methods
// ===========================

bool PacketHandler::createHeartbeatPacket() {
    currentSequenceNumber++;
    uint8_t payload[] = { currentSequenceNumber };
    return createPacket(PacketType::HEARTBEAT, payload, sizeof(payload));
}

bool PacketHandler::createTelemetryPacket(const TelemetryData& data) {
    uint8_t payload[sizeof(TelemetryData)];
    size_t offset = 0;

    // Pack telemetry data into payload
    floatToBytes(data.temperature, &payload[offset]); offset += 4;
    floatToBytes(data.pressure, &payload[offset]); offset += 4;
    floatToBytes(data.humidity, &payload[offset]); offset += 4;
    floatToBytes(data.batteryVoltage, &payload[offset]); offset += 4;
    floatToBytes(data.batteryCurrent, &payload[offset]); offset += 4;
    payload[offset++] = data.batteryPercentage;
    uint32ToBytes(data.uptime, &payload[offset]); offset += 4;
    payload[offset++] = static_cast<uint8_t>(data.rssi);
    uint16ToBytes(data.freeHeap, &payload[offset]); offset += 2;
    floatToBytes(data.cpuTemperature, &payload[offset]); offset += 4;
    payload[offset++] = data.powerState;

    return createPacket(PacketType::TELEMETRY, payload, offset);
}

bool PacketHandler::createGPSPacket(const GPSData& data) {
    uint8_t payload[sizeof(GPSData)];
    size_t offset = 0;

    // Pack GPS data into payload
    floatToBytes(data.latitude, &payload[offset]); offset += 4;
    floatToBytes(data.longitude, &payload[offset]); offset += 4;
    floatToBytes(data.altitude, &payload[offset]); offset += 4;
    payload[offset++] = data.satellites;
    floatToBytes(data.speed, &payload[offset]); offset += 4;
    floatToBytes(data.course, &payload[offset]); offset += 4;
    uint32ToBytes(data.fixTime, &payload[offset]); offset += 4;
    payload[offset++] = data.hdop;
    payload[offset++] = data.quality;

    return createPacket(PacketType::GPS_DATA, payload, offset);
}

bool PacketHandler::createCameraPacket(const CameraData& data) {
    uint8_t payload[sizeof(CameraData)];
    size_t offset = 0;

    // Pack camera data into payload
    uint16ToBytes(data.imageId, &payload[offset]); offset += 2;
    uint32ToBytes(data.timestamp, &payload[offset]); offset += 4;
    uint16ToBytes(data.imageSize, &payload[offset]); offset += 2;
    payload[offset++] = data.compression;
    floatToBytes(data.brightness, &payload[offset]); offset += 4;
    floatToBytes(data.contrast, &payload[offset]); offset += 4;
    payload[offset++] = data.faceCount;
    payload[offset++] = data.objectCount;

    return createPacket(PacketType::CAMERA_DATA, payload, offset);
}

bool PacketHandler::createAlertPacket(const AlertData& data) {
    uint8_t payload[sizeof(AlertData)];
    size_t offset = 0;

    // Pack alert data into payload
    payload[offset++] = static_cast<uint8_t>(data.alertType);
    uint32ToBytes(data.timestamp, &payload[offset]); offset += 4;
    payload[offset++] = data.severity;
    memcpy(&payload[offset], data.message, sizeof(data.message)); offset += sizeof(data.message);
    floatToBytes(data.sensorValue, &payload[offset]); offset += 4;
    payload[offset++] = data.sensorId;

    return createPacket(PacketType::ALERT, payload, offset);
}

bool PacketHandler::createStatusPacket(const char* status) {
    if (!status) {
        return false;
    }

    size_t statusLen = strlen(status);
    if (statusLen > 100) {
        statusLen = 100; // Truncate if too long
    }

    return createPacket(PacketType::STATUS, const_cast<char*>(status), statusLen);
}

bool PacketHandler::createDebugPacket(const char* message) {
    if (!message) {
        return false;
    }

    size_t messageLen = strlen(message);
    if (messageLen > 150) {
        messageLen = 150; // Truncate if too long
    }

    return createPacket(PacketType::DEBUG, const_cast<char*>(message), messageLen);
}

// ===========================
// Data Extraction
// ===========================

bool PacketHandler::extractTelemetry(TelemetryData& data) {
    // Implementation would extract telemetry from received packet
    // This is a placeholder - would be called when telemetry packet is received
    return false;
}

bool PacketHandler::extractGPS(GPSData& data) {
    // Implementation would extract GPS data from received packet
    // This is a placeholder - would be called when GPS packet is received
    return false;
}

bool PacketHandler::extractCamera(CameraData& data) {
    // Implementation would extract camera data from received packet
    // This is a placeholder - would be called when camera packet is received
    return false;
}

bool PacketHandler::extractAlert(AlertData& data) {
    // Implementation would extract alert from received packet
    // This is a placeholder - would be called when alert packet is received
    return false;
}

bool PacketHandler::extractCommand(uint8_t& commandId, uint8_t* params, size_t& paramLength) {
    // Implementation would extract command from received packet
    // This is a placeholder - would be called when command packet is received
    return false;
}

// ===========================
// Buffer Management
// ===========================

bool PacketHandler::addToBuffer(uint8_t* data, size_t length, PacketPriority priority) {
    if (queueSize >= 16) {
        // Queue full, remove oldest low priority packet if needed
        size_t oldestIndex = findOldestLowPriorityPacket();
        if (oldestIndex < 16) {
            if (packetQueue[oldestIndex].data) {
                free(packetQueue[oldestIndex].data);
            }
            queueSize--;
            
            // Shift queue
            for (size_t i = oldestIndex; i < queueSize; i++) {
                packetQueue[i] = packetQueue[i + 1];
            }
        } else {
            packetsDropped++;
            return false;
        }
    }

    // Find insertion point based on priority
    size_t insertPos = queueSize;
    for (size_t i = 0; i < queueSize; i++) {
        if (static_cast<uint8_t>(priority) > static_cast<uint8_t>(packetQueue[i].priority)) {
            insertPos = i;
            break;
        }
    }

    // Shift elements to make room
    for (size_t i = queueSize; i > insertPos; i--) {
        packetQueue[i] = packetQueue[i - 1];
    }

    // Insert new packet
    packetQueue[insertPos].data = static_cast<uint8_t*>(malloc(length));
    if (!packetQueue[insertPos].data) {
        logError("Failed to allocate memory for packet buffer");
        return false;
    }

    memcpy(packetQueue[insertPos].data, data, length);
    packetQueue[insertPos].size = length;
    packetQueue[insertPos].capacity = length;
    packetQueue[insertPos].timestamp = millis();
    packetQueue[insertPos].priority = priority;
    packetQueue[insertPos].ready = true;

    queueSize++;
    return true;
}

bool PacketHandler::getBufferedPacket(uint8_t*& packetData, size_t& packetSize) {
    return dequeuePacket(packetData, packetSize);
}

void PacketHandler::clearBuffer() {
    for (size_t i = 0; i < 16; i++) {
        if (packetQueue[i].data) {
            free(packetQueue[i].data);
            packetQueue[i].data = nullptr;
        }
        packetQueue[i].size = 0;
        packetQueue[i].capacity = 0;
        packetQueue[i].timestamp = 0;
        packetQueue[i].priority = PacketPriority::PRIORITY_NORMAL;
        packetQueue[i].ready = false;
    }
    queueSize = 0;
    queueHead = 0;
    queueTail = 0;
}

size_t PacketHandler::getBufferUsage() const {
    return queueSize;
}

size_t PacketHandler::getBufferCapacity() const {
    return 16; // Maximum queue size
}

// ===========================
// Statistics
// ===========================

void PacketHandler::resetStatistics() {
    packetsSent = 0;
    packetsReceived = 0;
    packetsDropped = 0;
    crcErrors = 0;
    lastStatisticsReset = millis();
}

float PacketHandler::getPacketLossRate() const {
    uint32_t total = packetsSent + packetsDropped;
    if (total == 0) {
        return 0.0f;
    }
    return (static_cast<float>(packetsDropped) / static_cast<float>(total)) * 100.0f;
}

// ===========================
// Validation and Diagnostics
// ===========================

bool PacketHandler::validatePacket(const uint8_t* packet, size_t length) {
    if (!packet || length < sizeof(PacketHeader) + sizeof(PacketFooter)) {
        return false;
    }

    // Check start bytes
    if (packet[0] != PACKET_START_BYTE1 || packet[1] != PACKET_START_BYTE2) {
        return false;
    }

    // Check end bytes
    if (packet[length - 2] != PACKET_END_BYTE1 || packet[length - 1] != PACKET_END_BYTE2) {
        return false;
    }

    // Verify CRC
    return verifyCRC(packet, length);
}

bool PacketHandler::isBufferFull() const {
    return queueSize >= 16;
}

void PacketHandler::printStatistics() const {
    if (!PACKET_STATS_ENABLED) {
        return;
    }

    Serial.println("=== Packet Handler Statistics ===");
    Serial.printf("Packets Sent: %lu\n", packetsSent);
    Serial.printf("Packets Received: %lu\n", packetsReceived);
    Serial.printf("Packets Dropped: %lu\n", packetsDropped);
    Serial.printf("CRC Errors: %lu\n", crcErrors);
    Serial.printf("Packet Loss Rate: %.2f%%\n", getPacketLossRate());
    Serial.printf("Buffer Usage: %zu/%zu\n", queueSize, (size_t)16);
    Serial.printf("Last Packet Time: %lu ms\n", lastPacketTime);
    
    uint32_t uptime = millis() - lastStatisticsReset;
    Serial.printf("Statistics Uptime: %lu ms\n", uptime);
}

void PacketHandler::printBufferStatus() const {
    Serial.println("=== Packet Buffer Status ===");
    Serial.printf("Queue Size: %zu/16\n", queueSize);
    Serial.printf("Buffer Usage: %.1f%%\n", (static_cast<float>(queueSize) / 16.0f) * 100.0f);
    
    for (size_t i = 0; i < queueSize; i++) {
        Serial.printf("Queue[%zu]: Type=%s, Priority=%s, Size=%zu, Age=%lu ms\n",
                     i,
                     packetTypeToString(static_cast<PacketType>(packetQueue[i].data[3])),
                     priorityToString(packetQueue[i].priority),
                     packetQueue[i].size,
                     millis() - packetQueue[i].timestamp);
    }
}

// ===========================
// Utility Methods
// ===========================

const char* PacketHandler::packetTypeToString(PacketType type) const {
    switch (type) {
        case PacketType::HEARTBEAT: return "Heartbeat";
        case PacketType::TELEMETRY: return "Telemetry";
        case PacketType::GPS_DATA: return "GPS";
        case PacketType::CAMERA_DATA: return "Camera";
        case PacketType::ALERT: return "Alert";
        case PacketType::COMMAND_ACK: return "Command ACK";
        case PacketType::STATUS: return "Status";
        case PacketType::DEBUG: return "Debug";
        default: return "Unknown";
    }
}

const char* PacketHandler::alertTypeToString(AlertType type) const {
    switch (type) {
        case AlertType::LOW_BATTERY: return "Low Battery";
        case AlertType::CRITICAL_BATTERY: return "Critical Battery";
        case AlertType::SYSTEM_ERROR: return "System Error";
        case AlertType::SENSOR_FAILURE: return "Sensor Failure";
        case AlertType::COMMUNICATION_LOST: return "Communication Lost";
        case AlertType::MEMORY_FULL: return "Memory Full";
        case AlertType::OVERHEATING: return "Overheating";
        default: return "Unknown";
    }
}

const char* PacketHandler::priorityToString(PacketPriority priority) const {
    switch (priority) {
        case PacketPriority::PRIORITY_LOW: return "Low";
        case PacketPriority::PRIORITY_NORMAL: return "Normal";
        case PacketPriority::PRIORITY_HIGH: return "High";
        case PacketPriority::PRIORITY_CRITICAL: return "Critical";
        default: return "Unknown";
    }
}

// ===========================
// Private Methods - CRC Calculation
// ===========================

uint8_t PacketHandler::calculateCRC8(const uint8_t* data, size_t length) {
    uint8_t crc = 0x00;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

uint16_t PacketHandler::calculateCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0x0000;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

bool PacketHandler::verifyCRC(const uint8_t* packet, size_t length) {
    if (length < sizeof(PacketHeader) + sizeof(PacketFooter)) {
        return false;
    }

    // Verify header CRC8
    uint8_t headerCRC = calculateCRC8(packet, sizeof(PacketHeader) - 1); // Exclude CRC8 field
    PacketHeader* header = reinterpret_cast<PacketHeader*>(const_cast<uint8_t*>(packet));
    
    if (headerCRC != header->crc8) {
        crcErrors++;
        return false;
    }

    // Verify payload CRC16
    size_t payloadSize = header->payloadLength;
    const uint8_t* payloadStart = packet + sizeof(PacketHeader);
    uint16_t payloadCRC = calculateCRC16(payloadStart, payloadSize);
    
    PacketFooter* footer = reinterpret_cast<PacketFooter*>(
        const_cast<uint8_t*>(packet + sizeof(PacketHeader) + payloadSize));
    
    if (payloadCRC != footer->crc16) {
        crcErrors++;
        return false;
    }

    return true;
}

// ===========================
// Private Methods - Packet Assembly
// ===========================

bool PacketHandler::assemblePacket(PacketType type, const uint8_t* payload, size_t payloadSize,
                                uint8_t*& packetData, size_t& packetSize) {
    packetSize = sizeof(PacketHeader) + payloadSize + sizeof(PacketFooter);
    
    if (packetSize > maxPacketSize) {
        logError("Assembled packet too large");
        return false;
    }

    packetData = static_cast<uint8_t*>(malloc(packetSize));
    if (!packetData) {
        logError("Failed to allocate memory for packet assembly");
        return false;
    }

    size_t offset = 0;

    // Assemble header
    packetData[offset++] = PACKET_START_BYTE1;
    packetData[offset++] = PACKET_START_BYTE2;
    packetData[offset++] = static_cast<uint8_t>(type);
    packetData[offset++] = currentSequenceNumber;
    uint16ToBytes(static_cast<uint16_t>(payloadSize), &packetData[offset]); offset += 2;
    
    // Calculate and add header CRC8
    uint8_t headerCRC = calculateCRC8(packetData, sizeof(PacketHeader) - 1);
    packetData[offset++] = headerCRC;

    // Copy payload
    if (payload && payloadSize > 0) {
        memcpy(&packetData[offset], payload, payloadSize);
        offset += payloadSize;
    }

    // Assemble footer
    uint16_t payloadCRC = calculateCRC16(payload, payloadSize);
    uint16ToBytes(payloadCRC, &packetData[offset]); offset += 2;
    packetData[offset++] = PACKET_END_BYTE1;
    packetData[offset++] = PACKET_END_BYTE2;

    return true;
}

bool PacketHandler::disassemblePacket(const uint8_t* packet, size_t length,
                                   PacketType& type, uint8_t*& payload, size_t& payloadSize) {
    if (!validatePacket(packet, length)) {
        return false;
    }

    PacketHeader* header = reinterpret_cast<PacketHeader*>(const_cast<uint8_t*>(packet));
    type = header->packetType;
    payloadSize = header->payloadLength;

    if (payloadSize == 0) {
        payload = nullptr;
        return true;
    }

    payload = static_cast<uint8_t*>(malloc(payloadSize));
    if (!payload) {
        logError("Failed to allocate memory for payload extraction");
        return false;
    }

    const uint8_t* payloadStart = packet + sizeof(PacketHeader);
    memcpy(payload, payloadStart, payloadSize);

    return true;
}

// ===========================
// Private Methods - Buffer Operations
// ===========================

bool PacketHandler::enqueuePacket(const uint8_t* packetData, size_t packetSize, PacketPriority priority) {
    return addToBuffer(const_cast<uint8_t*>(packetData), packetSize, priority);
}

bool PacketHandler::dequeuePacket(uint8_t*& packetData, size_t& packetSize) {
    if (queueSize == 0) {
        return false;
    }

    // Get highest priority packet (front of queue)
    PacketBuffer& buffer = packetQueue[0];
    
    packetData = buffer.data;
    packetSize = buffer.size;
    buffer.data = nullptr; // Transfer ownership
    buffer.size = 0;
    buffer.ready = false;

    // Shift queue
    for (size_t i = 0; i < queueSize - 1; i++) {
        packetQueue[i] = packetQueue[i + 1];
    }

    queueSize--;
    return true;
}

void PacketHandler::sortQueueByPriority() {
    // Simple bubble sort for small queue
    for (size_t i = 0; i < queueSize - 1; i++) {
        for (size_t j = 0; j < queueSize - i - 1; j++) {
            if (static_cast<uint8_t>(packetQueue[j].priority) < 
                static_cast<uint8_t>(packetQueue[j + 1].priority)) {
                // Swap
                PacketBuffer temp = packetQueue[j];
                packetQueue[j] = packetQueue[j + 1];
                packetQueue[j + 1] = temp;
            }
        }
    }
}

size_t PacketHandler::findOldestLowPriorityPacket() const {
    size_t oldestIndex = 16;
    uint32_t oldestTime = UINT32_MAX;
    
    for (size_t i = 0; i < queueSize; i++) {
        if (static_cast<uint8_t>(packetQueue[i].priority) == static_cast<uint8_t>(PacketPriority::PRIORITY_LOW) || 
            static_cast<uint8_t>(packetQueue[i].priority) == static_cast<uint8_t>(PacketPriority::PRIORITY_NORMAL)) {
            if (packetQueue[i].timestamp < oldestTime) {
                oldestTime = packetQueue[i].timestamp;
                oldestIndex = i;
            }
        }
    }
    
    return oldestIndex;
}

// ===========================
// Private Methods - Data Conversion
// ===========================

void PacketHandler::floatToBytes(float value, uint8_t* bytes) {
    memcpy(bytes, &value, sizeof(float));
}

float PacketHandler::bytesToFloat(const uint8_t* bytes) {
    float value;
    memcpy(&value, bytes, sizeof(float));
    return value;
}

void PacketHandler::uint16ToBytes(uint16_t value, uint8_t* bytes) {
    bytes[0] = static_cast<uint8_t>(value >> 8);
    bytes[1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t PacketHandler::bytesToUint16(const uint8_t* bytes) {
    return (static_cast<uint16_t>(bytes[0]) << 8) | static_cast<uint16_t>(bytes[1]);
}

void PacketHandler::uint32ToBytes(uint32_t value, uint8_t* bytes) {
    bytes[0] = static_cast<uint8_t>(value >> 24);
    bytes[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(value & 0xFF);
}

uint32_t PacketHandler::bytesToUint32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

// ===========================
// Private Methods - Internal Helpers
// ===========================

void PacketHandler::resetReceiveState() {
    receiveIndex = 0;
    inPacket = false;
    memset(&currentHeader, 0, sizeof(currentHeader));
    expectedPayloadSize = 0;
}

bool PacketHandler::processReceivedByte(uint8_t byte) {
    if (!inPacket) {
        // Looking for start sequence
        if (receiveIndex == 0 && byte == PACKET_START_BYTE1) {
            receiveBuffer[receiveIndex++] = byte;
        } else if (receiveIndex == 1 && byte == PACKET_START_BYTE2) {
            receiveBuffer[receiveIndex++] = byte;
            inPacket = true;
        } else {
            receiveIndex = 0; // Reset if not start sequence
        }
    } else {
        // In packet processing
        receiveBuffer[receiveIndex++] = byte;

        // Check if we have enough for header
        if (receiveIndex == sizeof(PacketHeader)) {
            memcpy(&currentHeader, receiveBuffer, sizeof(PacketHeader));
            
            if (!validateHeader(currentHeader)) {
                resetReceiveState();
                crcErrors++;
                return false;
            }
            
            expectedPayloadSize = currentHeader.payloadLength;
            
            // Check if total packet size is reasonable
            size_t totalSize = sizeof(PacketHeader) + expectedPayloadSize + sizeof(PacketFooter);
            if (totalSize > sizeof(receiveBuffer)) {
                resetReceiveState();
                logError("Packet size exceeds buffer");
                return false;
            }
        }

        // Check if complete packet received
        size_t totalExpectedSize = sizeof(PacketHeader) + expectedPayloadSize + sizeof(PacketFooter);
        if (receiveIndex >= totalExpectedSize) {
            bool success = processCompletePacket();
            resetReceiveState();
            return success;
        }
    }

    return false;
}

bool PacketHandler::validateHeader(const PacketHeader& header) {
    // Validate packet type
    if (static_cast<uint8_t>(header.packetType) < static_cast<uint8_t>(PacketType::HEARTBEAT) ||
        static_cast<uint8_t>(header.packetType) > static_cast<uint8_t>(PacketType::DEBUG)) {
        return false;
    }

    // Validate payload length
    if (header.payloadLength > MAX_PAYLOAD_SIZE) {
        return false;
    }

    return true;
}

bool PacketHandler::processCompletePacket() {
    if (!verifyCRC(receiveBuffer, receiveIndex)) {
        crcErrors++;
        return false;
    }

    packetsReceived++;
    updateStatistics(currentHeader.packetType, false);
    
    if (DEBUG_PACKET_HANDLER && LOG_PACKET_CONTENTS) {
        logPacket(receiveBuffer, receiveIndex, false);
    }

    // Process packet based on type
    // This would interface with other system components
    // For now, just count it as received
    
    return true;
}

void PacketHandler::updateStatistics(PacketType type, bool sent) {
    if (sent) {
        packetsSent++;
    } else {
        packetsReceived++;
    }
}

bool PacketHandler::shouldRetransmit(PacketType type, uint8_t attempts) {
    if (attempts >= maxRetries) {
        return false;
    }

    // Critical and alert packets should be retried
    return (type == PacketType::ALERT || 
            type == PacketType::HEARTBEAT ||
            type == PacketType::STATUS);
}

// ===========================
// Private Methods - Debug and Logging
// ===========================

void PacketHandler::logPacket(const uint8_t* packet, size_t length, bool outgoing) const {
    if (!LOG_PACKET_CONTENTS) {
        return;
    }

    Serial.printf("[%s] Packet: ", outgoing ? "TX" : "RX");
    for (size_t i = 0; i < length && i < 32; i++) { // Limit to first 32 bytes
        Serial.printf("%02X ", packet[i]);
    }
    if (length > 32) {
        Serial.printf("... (%zu bytes)", length);
    }
    Serial.println();
}

void PacketHandler::logError(const char* message) const {
    if (DEBUG_PACKET_HANDLER) {
        Serial.printf("Packet Handler Error: %s\n", message);
    }
}
