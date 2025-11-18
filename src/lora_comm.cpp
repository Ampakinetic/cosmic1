#include "lora_comm.h"

// ===========================
// Constructor/Destructor
// ===========================

LoRaManager::LoRaManager() {
    // Initialize LoRa configuration
    frequency = LORA_FREQUENCY;
    spreadingFactor = LORA_SPREADING_FACTOR;
    bandwidth = LORA_BANDWIDTH;
    codingRate = LORA_CODING_RATE;
    txPower = LORA_TX_POWER;
    preambleLength = LORA_PREAMBLE_LEN;
    syncWord = LORA_SYNC_WORD;
    
    // Initialize current settings
    currentSpreadingFactor = spreadingFactor;
    currentBandwidth = bandwidth;
    currentTxPower = txPower;
    
    // Initialize packet management
    nextSequenceNumber = random(0xFFFF);
    deviceId = DEVICE_TYPE;  // From balloon_config.h
    
    // Initialize queues
    memset(queueSizes, 0, sizeof(queueSizes));
    memset(priorityQueues, 0, sizeof(priorityQueues));
    
    // Initialize transmission state
    transmitting = false;
    receiving = false;
    transmitStartTime = 0;
    lastReceiveTime = 0;
    ackTimeout = 0;
    
    // Initialize signal quality monitoring
    memset(rssiHistory, 0, sizeof(rssiHistory));
    memset(snrHistory, 0, sizeof(snrHistory));
    rssiIndex = 0;
    snrIndex = 0;
    lastRssi = -128;
    lastSnr = -128;
    
    // Initialize error tracking
    transmitErrorCount = 0;
    receiveErrorCount = 0;
    crcErrorCount = 0;
    ackTimeoutCount = 0;
}

LoRaManager::~LoRaManager() {
    end();
}

// ===========================
// Initialization
// ===========================

bool LoRaManager::begin() {
    if (!initLoRaModule()) {
        return false;
    }
    
    configureLoRaSettings();
    
    if (DEBUG_LORA) {
        Serial.println("LoRa: Initialized successfully");
        printLoRaInfo();
    }
    
    return true;
}

void LoRaManager::end() {
    LoRa.end();
    transmitting = false;
    receiving = false;
}

bool LoRaManager::reinitialize() {
    end();
    delay(100);
    return begin();
}

// ===========================
// Private Initialization Methods
// ===========================

bool LoRaManager::initLoRaModule() {
    // Configure SPI pins for LoRa
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_CS_PIN);
    
    // Initialize LoRa module
    if (!LoRa.begin(frequency)) {
        if (DEBUG_LORA) {
            Serial.printf("LoRa: Failed to initialize at %.1f MHz\n", frequency);
        }
        return false;
    }
    
    return true;
}

void LoRaManager::configureLoRaSettings() {
    LoRa.setSpreadingFactor(spreadingFactor);
    LoRa.setSignalBandwidth(bandwidth);
    LoRa.setCodingRate4(codingRate);
    LoRa.setTxPower(txPower);
    LoRa.setPreambleLength(preambleLength);
    LoRa.setSyncWord(syncWord);
    // CRC is automatically enabled in most LoRa libraries
    
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Configured - SF:%d, BW:%ld, CR:%d, Power:%d\n",
                     spreadingFactor, bandwidth, codingRate, txPower);
    }
}

// ===========================
// Configuration Methods
// ===========================

bool LoRaManager::setFrequency(long freq) {
    frequency = freq;
    LoRa.setFrequency(freq);
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Frequency set to %.1f MHz\n", freq);
    }
    return true;

    
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Failed to set frequency to %.1f MHz\n", freq);
    }
    return false;
}

bool LoRaManager::setSpreadingFactor(int sf) {
    if (sf < 6 || sf > 12) {
        return false;
    }
    
    spreadingFactor = sf;
    LoRa.setSpreadingFactor(sf);
    currentSpreadingFactor = sf;
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Spreading factor set to %d\n", sf);
    }
    return true;
}

bool LoRaManager::setBandwidth(long bw) {
    bandwidth = bw;
    LoRa.setSignalBandwidth(bw);
    currentBandwidth = bw;
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Bandwidth set to %ld Hz\n", bw);
    }
    return true;
}

bool LoRaManager::setTxPower(int power) {
    if (power < 2 || power > 20) {
        return false;
    }
    
    txPower = power;
    LoRa.setTxPower(power);
    currentTxPower = power;
    if (DEBUG_LORA) {
        Serial.printf("LoRa: TX power set to %d dBm\n", power);
    }
    return true;
}

bool LoRaManager::setCodingRate(int cr) {
    if (cr < 5 || cr > 8) {
        return false;
    }
    
    codingRate = cr;
    LoRa.setCodingRate4(cr);
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Coding rate set to %d\n", cr);
    }
    return true;
}

bool LoRaManager::setSyncWord(byte sw) {
    syncWord = sw;
    LoRa.setSyncWord(sw);
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Sync word set to 0x%02X\n", sw);
    }
    return true;
}

// ===========================
// Packet Operations
// ===========================

bool LoRaManager::sendPacket(const Packet& packet, Priority priority) {
    QueuedPacket queuedPacket;
    queuedPacket.packet = packet;
    queuedPacket.priority = priority;
    queuedPacket.enqueueTime = millis();
    queuedPacket.transmitAttempts = 0;
    queuedPacket.lastTransmitTime = 0;
    queuedPacket.waitingForAck = false;
    
    addToQueueInternal(queuedPacket);
    
    if (DEBUG_LORA) {
        Serial.printf("LoRa: Packet queued for transmission (Type: %s, Priority: %s)\n",
                     packetTypeToString(packet.type), priorityToString(priority));
    }
    
    return true;
}

bool LoRaManager::sendTelemetry(const uint8_t* data, size_t length) {
    Packet packet = createPacket(PacketType::TELEMETRY, data, length);
    return sendPacket(packet, Priority::TELEMETRY);
}

bool LoRaManager::sendGPSData(const uint8_t* data, size_t length) {
    Packet packet = createPacket(PacketType::GPS, data, length);
    return sendPacket(packet, Priority::GPS);
}

bool LoRaManager::sendCameraThumbnail(const uint8_t* data, size_t length) {
    Packet packet = createPacket(PacketType::CAMERA_THUMB, data, length);
    return sendPacket(packet, Priority::CAMERA);
}

bool LoRaManager::sendStatus(const uint8_t* data, size_t length) {
    Packet packet = createPacket(PacketType::STATUS, data, length);
    return sendPacket(packet, Priority::STATUS);
}

bool LoRaManager::sendEmergency(const uint8_t* data, size_t length) {
    Packet packet = createPacket(PacketType::EMERGENCY, data, length);
    return sendPacket(packet, Priority::EMERGENCY);
}

// ===========================
// Queue Management
// ===========================

void LoRaManager::addToQueue(const Packet& packet, Priority priority) {
    sendPacket(packet, priority);
}

void LoRaManager::addToQueueInternal(const QueuedPacket& queuedPacket) {
    int priorityIndex = static_cast<int>(queuedPacket.priority) - 1;
    
    if (queueSizes[priorityIndex] < MAX_QUEUE_SIZE) {
        priorityQueues[priorityIndex][queueSizes[priorityIndex]] = queuedPacket;
        queueSizes[priorityIndex]++;
    } else {
        // Queue is full, remove oldest packet
        for (int i = 0; i < MAX_QUEUE_SIZE - 1; i++) {
            priorityQueues[priorityIndex][i] = priorityQueues[priorityIndex][i + 1];
        }
        priorityQueues[priorityIndex][MAX_QUEUE_SIZE - 1] = queuedPacket;
        
        if (DEBUG_LORA) {
            Serial.printf("LoRa: Queue overflow, oldest packet removed\n");
        }
    }
}

bool LoRaManager::processQueue() {
    // Check for incoming packets
    processIncomingPacket();
    
    // Get next packet to transmit
    QueuedPacket* nextPacket = getNextPacket();
    if (!nextPacket) {
        return false;  // No packets to send
    }
    
    // Check if we need to wait for ACK
    if (nextPacket->waitingForAck) {
        if (millis() - nextPacket->lastTransmitTime > ACK_TIMEOUT_MS) {
            nextPacket->waitingForAck = false;
            ackTimeoutCount++;
            
            if (DEBUG_LORA) {
                Serial.printf("LoRa: ACK timeout for packet %d\n", nextPacket->packet.sequenceNumber);
            }
        } else {
            return false;  // Still waiting for ACK
        }
    }
    
    // Check retry limit
    if (nextPacket->transmitAttempts >= MAX_RETRIES) {
        // Remove packet from queue
        Priority priority = nextPacket->priority;
        int priorityIndex = static_cast<int>(priority) - 1;
        int index = nextPacket - &priorityQueues[priorityIndex][0];
        removePacketFromQueue(priority, index);
        
        if (DEBUG_LORA) {
            Serial.printf("LoRa: Max retries exceeded for packet %d\n", nextPacket->packet.sequenceNumber);
        }
        
        transmitErrorCount++;
        return false;
    }
    
    // Transmit packet
    if (transmitPacket(nextPacket->packet)) {
        nextPacket->transmitAttempts++;
        nextPacket->lastTransmitTime = millis();
        nextPacket->waitingForAck = true;
        transmitStartTime = millis();
        transmitting = true;
        
        if (DEBUG_LORA) {
            Serial.printf("LoRa: Transmitted packet %d (Attempt %d/%d)\n",
                         nextPacket->packet.sequenceNumber, nextPacket->transmitAttempts, MAX_RETRIES);
        }
        
        return true;
    } else {
        transmitErrorCount++;
        return false;
    }
}

QueuedPacket* LoRaManager::getNextPacket() {
    // Check queues in priority order (1=Emergency, 5=Status)
    for (int priority = 0; priority < 5; priority++) {
        if (queueSizes[priority] > 0) {
            return &priorityQueues[priority][0];  // FIFO within priority
        }
    }
    
    return nullptr;  // No packets available
}

void LoRaManager::removePacketFromQueue(Priority priority, int index) {
    int priorityIndex = static_cast<int>(priority) - 1;
    
    // Shift remaining packets
    for (int i = index; i < queueSizes[priorityIndex] - 1; i++) {
        priorityQueues[priorityIndex][i] = priorityQueues[priorityIndex][i + 1];
    }
    
    queueSizes[priorityIndex]--;
}

void LoRaManager::clearQueue() {
    memset(queueSizes, 0, sizeof(queueSizes));
    memset(priorityQueues, 0, sizeof(priorityQueues));
}

int LoRaManager::getQueueSize(Priority priority) const {
    int priorityIndex = static_cast<int>(priority) - 1;
    return queueSizes[priorityIndex];
}

int LoRaManager::getTotalQueueSize() const {
    int total = 0;
    for (int i = 0; i < 5; i++) {
        total += queueSizes[i];
    }
    return total;
}

// ===========================
// Transmission Methods
// ===========================

bool LoRaManager::transmitPacket(const Packet& packet) {
    // Serialize packet
    static uint8_t buffer[MAX_PACKET_SIZE];
    size_t bufferLength = 0;
    
    if (!serializePacket(packet, buffer, bufferLength)) {
        if (DEBUG_LORA) {
            Serial.println("LoRa: Failed to serialize packet");
        }
        return false;
    }
    
    // Transmit packet
    LoRa.beginPacket();
    LoRa.write(buffer, bufferLength);
    bool success = LoRa.endPacket();
    
    if (success) {
        transmitting = false;
        transmitStartTime = 0;
        
        // Update signal quality (transmit side)
        updateSignalQuality(LoRa.packetRssi(), LoRa.packetSnr());
    }
    
    return success;
}

bool LoRaManager::receivePacket(Packet& packet) {
    int packetSize = LoRa.parsePacket();
    if (packetSize == 0) {
        return false;
    }
    
    // Read packet data
    static uint8_t buffer[MAX_PACKET_SIZE];
    int bytesRead = 0;
    
    while (LoRa.available() && bytesRead < packetSize && bytesRead < MAX_PACKET_SIZE) {
        buffer[bytesRead] = LoRa.read();
        bytesRead++;
    }
    
    // Update signal quality (receive side)
    lastRssi = LoRa.packetRssi();
    lastSnr = LoRa.packetSnr();
    updateSignalQuality(lastRssi, lastSnr);
    lastReceiveTime = millis();
    
    // Deserialize packet
    if (deserializePacket(buffer, bytesRead, packet)) {
        if (validatePacket(packet)) {
            packet.rssi = lastRssi;
            packet.snr = lastSnr;
            packet.valid = true;
            
            if (DEBUG_LORA) {
                Serial.printf("LoRa: Received packet (Type: %s, RSSI: %d dBm, SNR: %d dB)\n",
                             packetTypeToString(packet.type), lastRssi, lastSnr);
            }
            
            return true;
        } else {
            crcErrorCount++;
            
            if (DEBUG_LORA) {
                Serial.println("LoRa: CRC validation failed");
            }
        }
    } else {
        receiveErrorCount++;
        
        if (DEBUG_LORA) {
            Serial.println("LoRa: Failed to deserialize packet");
        }
    }
    
    return false;
}

void LoRaManager::processIncomingPacket() {
    Packet packet;
    if (receivePacket(packet)) {
        // Handle special packet types
        switch (packet.type) {
            case PacketType::ACK:
                handleAck(packet);
                break;
                
            case PacketType::NACK:
                handleNack(packet);
                break;
                
            default:
                // Regular data packets could be handled by application layer
                break;
        }
    }
}

// ===========================
// ACK/NACK Handling
// ===========================

void LoRaManager::handleAck(const Packet& ack) {
    if (ack.payloadLength < 4) {
        return;  // Invalid ACK packet
    }
    
    uint16_t ackSequence = (ack.payload[0] << 8) | ack.payload[1];
    uint8_t ackType = ack.payload[2];
    int8_t rssi = static_cast<int8_t>(ack.payload[3]);
    
    // Find and remove the corresponding packet from queue
    for (int priority = 0; priority < 5; priority++) {
        for (int i = 0; i < queueSizes[priority]; i++) {
            QueuedPacket* qp = &priorityQueues[priority][i];
            if (qp->packet.sequenceNumber == ackSequence && qp->waitingForAck) {
                // Packet acknowledged, remove from queue
                removePacketFromQueue(static_cast<Priority>(priority + 1), i);
                
                if (DEBUG_LORA) {
                    Serial.printf("LoRa: Packet %d acknowledged (Type: %d)\n", ackSequence, ackType);
                }
                
                // Adapt transmission settings based on signal quality
                adaptTransmissionSettings(rssi, ack.snr);
                
                return;
            }
        }
    }
    
    if (DEBUG_LORA) {
        Serial.printf("LoRa: ACK received for unknown packet %d\n", ackSequence);
    }
}

void LoRaManager::handleNack(const Packet& nack) {
    if (nack.payloadLength < 3) {
        return;  // Invalid NACK packet
    }
    
    uint16_t nackSequence = (nack.payload[0] << 8) | nack.payload[1];
    uint8_t nackType = nack.payload[2];
    
    // Find the corresponding packet and reset waiting for ACK
    for (int priority = 0; priority < 5; priority++) {
        for (int i = 0; i < queueSizes[priority]; i++) {
            QueuedPacket* qp = &priorityQueues[priority][i];
            if (qp->packet.sequenceNumber == nackSequence && qp->waitingForAck) {
                qp->waitingForAck = false;
                
                if (DEBUG_LORA) {
                    Serial.printf("LoRa: Packet %d NACK received (Type: %d)\n", nackSequence, nackType);
                }
                
                return;
            }
        }
    }
}

void LoRaManager::sendAck(uint16_t sequenceNumber, uint8_t ackType, int8_t rssi, int8_t snr) {
    uint8_t payload[4];
    payload[0] = (sequenceNumber >> 8) & 0xFF;
    payload[1] = sequenceNumber & 0xFF;
    payload[2] = ackType;
    payload[3] = static_cast<uint8_t>(rssi);
    
    Packet ackPacket = createPacket(PacketType::ACK, payload, 4);
    ackPacket.header.rssiAvg = getAverageRSSI();
    ackPacket.header.snrAvg = getAverageSNR();
    
    transmitPacket(ackPacket);
}

void LoRaManager::sendNack(uint16_t sequenceNumber, uint8_t nackType) {
    uint8_t payload[3];
    payload[0] = (sequenceNumber >> 8) & 0xFF;
    payload[1] = sequenceNumber & 0xFF;
    payload[2] = nackType;
    
    Packet nackPacket = createPacket(PacketType::NACK, payload, 3);
    nackPacket.header.rssiAvg = getAverageRSSI();
    nackPacket.header.snrAvg = getAverageSNR();
    
    transmitPacket(nackPacket);
}

// ===========================
// Adaptive Transmission
// ===========================

void LoRaManager::adaptTransmissionSettings(int8_t rssi, int8_t snr) {
    if (!ENABLE_ADAPTIVE_SF) {
        return;
    }
    
    bool settingsChanged = false;
    
    // Adapt spreading factor based on signal quality
    if (rssi > ADAPTIVE_SF_HIGH_THRESHOLD && currentSpreadingFactor > 7) {
        // Good signal, use faster spreading factor
        setSpreadingFactor(currentSpreadingFactor - 1);
        settingsChanged = true;
    } else if (rssi < ADAPTIVE_SF_LOW_THRESHOLD && currentSpreadingFactor < 12) {
        // Poor signal, use more robust spreading factor
        setSpreadingFactor(currentSpreadingFactor + 1);
        settingsChanged = true;
    }
    
    if (settingsChanged && DEBUG_LORA) {
        Serial.printf("LoRa: Adapted settings - SF:%d (RSSI:%d, SNR:%d)\n",
                     currentSpreadingFactor, rssi, snr);
    }
}

void LoRaManager::enableAdaptiveMode(bool enable) {
    // This would enable/disable adaptive transmission
    // Implementation depends on specific requirements
}

bool LoRaManager::isAdaptiveModeEnabled() const {
    return ENABLE_ADAPTIVE_SF;
}

// ===========================
// Signal Quality Monitoring
// ===========================

void LoRaManager::updateSignalQuality(int8_t rssi, int8_t snr) {
    // Update RSSI history
    rssiHistory[rssiIndex] = rssi;
    rssiIndex = (rssiIndex + 1) % RSSI_HISTORY_SIZE;
    
    // Update SNR history
    snrHistory[snrIndex] = snr;
    snrIndex = (snrIndex + 1) % RSSI_HISTORY_SIZE;
    
    lastRssi = rssi;
    lastSnr = snr;
}

int8_t LoRaManager::getAverageRSSI() const {
    int32_t sum = 0;
    int count = 0;
    
    for (int i = 0; i < RSSI_HISTORY_SIZE; i++) {
        if (rssiHistory[i] != 0) {
            sum += rssiHistory[i];
            count++;
        }
    }
    
    return (count > 0) ? (sum / count) : -128;
}

int8_t LoRaManager::getAverageSNR() const {
    int32_t sum = 0;
    int count = 0;
    
    for (int i = 0; i < RSSI_HISTORY_SIZE; i++) {
        if (snrHistory[i] != 0) {
            sum += snrHistory[i];
            count++;
        }
    }
    
    return (count > 0) ? (sum / count) : -128;
}

float LoRaManager::getPacketErrorRate() const {
    uint32_t totalPackets = transmitErrorCount + receiveErrorCount + crcErrorCount;
    if (totalPackets == 0) {
        return 0.0f;
    }
    
    return (float)(transmitErrorCount + receiveErrorCount + crcErrorCount) / totalPackets;
}

// ===========================
// Status Methods
// ===========================

bool LoRaManager::isReady() const {
    return LoRa.begin(frequency);  // Quick check if LoRa is responsive
}

uint32_t LoRaManager::getLastTransmitTime() const {
    return transmitStartTime;
}

// ===========================
// Power Management
// ===========================

void LoRaManager::enterLowPowerMode() {
    setTxPower(10);  // Reduce transmit power
    setSpreadingFactor(12);  // Use most robust setting
    
    if (DEBUG_LORA) {
        Serial.println("LoRa: Entered low power mode");
    }
}

void LoRaManager::exitLowPowerMode() {
    // Restore normal settings
    setTxPower(LORA_TX_POWER);
    setSpreadingFactor(LORA_SPREADING_FACTOR);
    
    if (DEBUG_LORA) {
        Serial.println("LoRa: Exited low power mode");
    }
}

void LoRaManager::sleep() {
    LoRa.sleep();
}

void LoRaManager::wakeup() {
    LoRa.begin(frequency);
    configureLoRaSettings();
}

// ===========================
// Statistics
// ===========================

void LoRaManager::resetStatistics() {
    transmitErrorCount = 0;
    receiveErrorCount = 0;
    crcErrorCount = 0;
    ackTimeoutCount = 0;
    
    memset(rssiHistory, 0, sizeof(rssiHistory));
    memset(snrHistory, 0, sizeof(snrHistory));
    rssiIndex = 0;
    snrIndex = 0;
}

// ===========================
// CRC and Validation
// ===========================

uint16_t LoRaManager::calculateCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

bool LoRaManager::validatePacket(const Packet& packet) {
    // Calculate CRC for header + type + sequence + payload
    size_t headerSize = sizeof(PacketHeader);
    size_t dataSize = headerSize + 1 + 2 + packet.payloadLength;
    
    static uint8_t buffer[MAX_PACKET_SIZE];
    memcpy(buffer, &packet.header, headerSize);
    buffer[headerSize] = static_cast<uint8_t>(packet.type);
    buffer[headerSize + 1] = (packet.sequenceNumber >> 8) & 0xFF;
    buffer[headerSize + 2] = packet.sequenceNumber & 0xFF;
    memcpy(buffer + headerSize + 3, packet.payload, packet.payloadLength);
    
    uint16_t calculatedCRC = calculateCRC16(buffer, dataSize);
    return (calculatedCRC == packet.crc16);
}

// ===========================
// Debug Methods
// ===========================

void LoRaManager::printLoRaInfo() const {
    Serial.println("=== LoRa Information ===");
    Serial.printf("Frequency: %.1f MHz\n", frequency);
    Serial.printf("Spreading Factor: %d\n", spreadingFactor);
    Serial.printf("Bandwidth: %ld Hz\n", bandwidth);
    Serial.printf("Coding Rate: %d\n", codingRate);
    Serial.printf("TX Power: %d dBm\n", txPower);
    Serial.printf("Preamble Length: %d\n", preambleLength);
    Serial.printf("Sync Word: 0x%02X\n", syncWord);
    Serial.printf("Device ID: %d\n", deviceId);
}

void LoRaManager::printQueueStatus() const {
    Serial.println("=== Queue Status ===");
    Serial.printf("Emergency: %d/%d\n", getQueueSize(Priority::EMERGENCY), MAX_QUEUE_SIZE);
    Serial.printf("GPS: %d/%d\n", getQueueSize(Priority::GPS), MAX_QUEUE_SIZE);
    Serial.printf("Telemetry: %d/%d\n", getQueueSize(Priority::TELEMETRY), MAX_QUEUE_SIZE);
    Serial.printf("Camera: %d/%d\n", getQueueSize(Priority::CAMERA), MAX_QUEUE_SIZE);
    Serial.printf("Status: %d/%d\n", getQueueSize(Priority::STATUS), MAX_QUEUE_SIZE);
    Serial.printf("Total: %d packets\n", getTotalQueueSize());
}

void LoRaManager::printSignalQuality() const {
    Serial.println("=== Signal Quality ===");
    Serial.printf("Last RSSI: %d dBm\n", lastRssi);
    Serial.printf("Last SNR: %d dB\n", lastSnr);
    Serial.printf("Average RSSI: %d dBm\n", getAverageRSSI());
    Serial.printf("Average SNR: %d dB\n", getAverageSNR());
    Serial.printf("Packet Error Rate: %.2f%%\n", getPacketErrorRate() * 100);
}

void LoRaManager::printStatistics() const {
    Serial.println("=== LoRa Statistics ===");
    Serial.printf("Transmit Errors: %lu\n", transmitErrorCount);
    Serial.printf("Receive Errors: %lu\n", receiveErrorCount);
    Serial.printf("CRC Errors: %lu\n", crcErrorCount);
    Serial.printf("ACK Timeouts: %lu\n", ackTimeoutCount);
    Serial.printf("Last Transmit: %lu ms ago\n", millis() - getLastTransmitTime());
    Serial.printf("Last Receive: %lu ms ago\n", millis() - lastReceiveTime);
}

void LoRaManager::printPacket(const Packet& packet) const {
    Serial.println("=== Packet Information ===");
    Serial.printf("Type: %s (0x%02X)\n", packetTypeToString(packet.type), static_cast<uint8_t>(packet.type));
    Serial.printf("Sequence: %d\n", packet.sequenceNumber);
    Serial.printf("Device ID: %d\n", packet.header.deviceId);
    Serial.printf("Timestamp: %lu\n", packet.header.timestamp);
    Serial.printf("Battery: %d.%02d V\n", packet.header.batteryLevel / 100, packet.header.batteryLevel % 100);
    Serial.printf("Payload Length: %d bytes\n", packet.payloadLength);
    Serial.printf("CRC16: 0x%04X\n", packet.crc16);
    Serial.printf("RSSI: %d dBm\n", packet.rssi);
    Serial.printf("SNR: %d dB\n", packet.snr);
    Serial.printf("Valid: %s\n", packet.valid ? "Yes" : "No");
}

// ===========================
// Utility Functions
// ===========================

Packet createPacket(PacketType type, const uint8_t* payload, size_t length) {
    Packet packet;
    
    // Initialize header
    packet.header.version = 0x01;  // Protocol version 1
    packet.header.deviceId = DEVICE_TYPE;
    packet.header.flags = 0;  // No special flags
    packet.header.retryCount = 0;
    packet.header.timestamp = millis() / 1000;  // Convert to seconds
    packet.header.batteryLevel = 330;  // TODO: Get actual battery voltage
    packet.header.rssiAvg = LoRaComm().getAverageRSSI();
    packet.header.snrAvg = LoRaComm().getAverageSNR();
    
    // Initialize packet
    packet.type = type;
    packet.sequenceNumber = 0;  // Will be set by LoRaManager
    packet.payload = const_cast<uint8_t*>(payload);
    packet.payloadLength = length;
    packet.crc16 = 0;  // Will be calculated by LoRaManager
    packet.rssi = -128;
    packet.snr = -128;
    packet.valid = false;
    
    return packet;
}

uint16_t calculatePacketCRC(const Packet& packet) {
    // Simple CRC calculation for utility function
    uint16_t crc = 0xFFFF;
    size_t headerSize = sizeof(PacketHeader);
    uint8_t tempBuffer[MAX_PACKET_SIZE];
    
    // Build temporary buffer for CRC calculation
    memcpy(tempBuffer, &packet.header, headerSize);
    tempBuffer[headerSize] = static_cast<uint8_t>(packet.type);
    tempBuffer[headerSize + 1] = (packet.sequenceNumber >> 8) & 0xFF;
    tempBuffer[headerSize + 2] = packet.sequenceNumber & 0xFF;
    memcpy(tempBuffer + headerSize + 3, packet.payload, packet.payloadLength);
    
    // Calculate CRC
    for (size_t i = 0; i < headerSize + 3 + packet.payloadLength; i++) {
        crc ^= tempBuffer[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

bool serializePacket(const Packet& packet, uint8_t* buffer, size_t& length) {
    size_t headerSize = sizeof(PacketHeader);
    size_t totalSize = headerSize + 1 + 2 + packet.payloadLength + 2;  // +2 for CRC
    
    if (totalSize > MAX_PACKET_SIZE) {
        return false;
    }
    
    // Copy header
    memcpy(buffer, &packet.header, headerSize);
    
    // Copy type and sequence
    buffer[headerSize] = static_cast<uint8_t>(packet.type);
    buffer[headerSize + 1] = (packet.sequenceNumber >> 8) & 0xFF;
    buffer[headerSize + 2] = packet.sequenceNumber & 0xFF;
    
    // Copy payload
    memcpy(buffer + headerSize + 3, packet.payload, packet.payloadLength);
    
    // Calculate and add CRC
    uint16_t crc = calculatePacketCRC(packet);
    buffer[totalSize - 2] = (crc >> 8) & 0xFF;
    buffer[totalSize - 1] = crc & 0xFF;
    
    length = totalSize;
    return true;
}

bool deserializePacket(const uint8_t* buffer, size_t length, Packet& packet) {
    size_t headerSize = sizeof(PacketHeader);
    
    if (length < headerSize + 5) {  // Minimum packet size
        return false;
    }
    
    // Copy header
    memcpy(&packet.header, buffer, headerSize);
    
    // Extract type and sequence
    packet.type = static_cast<PacketType>(buffer[headerSize]);
    packet.sequenceNumber = (buffer[headerSize + 1] << 8) | buffer[headerSize + 2];
    
    // Extract payload
    packet.payloadLength = length - headerSize - 3 - 2;  // -3 for type+seq, -2 for CRC
    packet.payload = const_cast<uint8_t*>(buffer + headerSize + 3);
    
    // Extract CRC
    packet.crc16 = (buffer[length - 2] << 8) | buffer[length - 1];
    
    packet.rssi = -128;
    packet.snr = -128;
    packet.valid = false;
    
    return true;
}

const char* packetTypeToString(PacketType type) {
    switch (type) {
        case PacketType::TELEMETRY: return "Telemetry";
       // case PacketType::GPS: return "GPS";
        case PacketType::CAMERA_THUMB: return "Camera Thumbnail";
        case PacketType::CAMERA_FULL: return "Camera Full";
        case PacketType::STATUS: return "Status";
        case PacketType::ACK: return "ACK";
     //   case PacketType::NACK: return "NACK";
        case PacketType::PING: return "Ping";
        case PacketType::PONG: return "Pong";
        case PacketType::EMERGENCY: return "Emergency";
        default: return "Unknown";
    }
}

const char* priorityToString(Priority priority) {
    switch (priority) {
        case Priority::EMERGENCY: return "Emergency";
        case Priority::GPS: return "GPS";
        case Priority::TELEMETRY: return "Telemetry";
        case Priority::CAMERA: return "Camera";
        case Priority::STATUS: return "Status";
        default: return "Unknown";
    }
}
