#include "command_sender.h"

// Debug configuration
#ifndef DEBUG_COMMAND_SENDER
#define DEBUG_COMMAND_SENDER true
#endif

// ===========================
// Static Instance
// ===========================

static CommandSender commandSenderInstance;
CommandSender& CmdSender() {
    return commandSenderInstance;
}

// ===========================
// Constructor/Destructor
// ===========================

CommandSender::CommandSender()
    : lora(nullptr)
    , initialized(false)
    , nextSequenceNumber(1)
    , pendingCommandCount(0)
    , maxRetries(3)
    , ackTimeoutMs(2000)
    , retryDelayMs(100)
    , commandsSent(0)
    , commandsAcked(0)
    , commandsFailed(0)
    , commandsTimeout(0)
    , receiveIndex(0)
    , inPacket(false)
{
    memset(pendingCommands, 0, sizeof(pendingCommands));
    memset(receiveBuffer, 0, sizeof(receiveBuffer));
}

CommandSender::~CommandSender() {
    end();
}

// ===========================
// Initialization
// ===========================

bool CommandSender::begin(E32LoRa* lora) {
    if (!lora) {
        return false;
    }

    this->lora = lora;

    resetReceiveState();

    initialized = true;

    if (DEBUG_COMMAND_SENDER) {
        Serial.println("CommandSender: Initialized");
    }

    return true;
}

void CommandSender::end() {
    initialized = false;
    resetReceiveState();

    // Clear all pending commands
    memset(pendingCommands, 0, sizeof(pendingCommands));
    pendingCommandCount = 0;
}

// ===========================
// Command Transmission
// ===========================

uint16_t CommandSender::sendCommand(CameraCommand cmd, const void* payload, size_t payloadSize) {
    if (!initialized || !lora) {
        return 0;
    }

    // Find free slot
    TrackedCommand* slot = findFreeSlot();
    if (!slot) {
        if (DEBUG_COMMAND_SENDER) {
            Serial.println("CommandSender: No free command slots");
        }
        return 0;
    }

    // Create command packet
    slot->packet = createCommandPacket(cmd, nextSequenceNumber, payload, payloadSize);
    slot->sequenceNumber = nextSequenceNumber;
    slot->state = CommandState::PENDING;
    slot->retryCount = 0;
    slot->sendTime = 0;
    slot->lastRetryTime = 0;
    slot->hasResponse = false;

    // Increment sequence number
    nextSequenceNumber++;
    if (nextSequenceNumber == 0) {
        nextSequenceNumber = 1; // Skip 0
    }

    pendingCommandCount++;

    if (DEBUG_COMMAND_SENDER) {
        Serial.printf("CommandSender: Queued command %s (seq=%d)\n",
                     CommandProtocol::commandToString(cmd),
                     slot->sequenceNumber);
    }

    return slot->sequenceNumber;
}

bool CommandSender::cancelCommand(uint16_t sequenceNumber) {
    TrackedCommand* cmd = findTrackedCommand(sequenceNumber);
    if (!cmd) {
        return false;
    }

    // Remove from tracking
    cmd->state = CommandState::FAILED;
    pendingCommandCount--;

    if (DEBUG_COMMAND_SENDER) {
        Serial.printf("CommandSender: Cancelled command seq=%d\n", sequenceNumber);
    }

    return true;
}

// ===========================
// Main Processing
// ===========================

void CommandSender::process() {
    if (!initialized || !lora) {
        return;
    }

    // Process incoming responses from LoRa
    while (lora->available() > 0) {
        uint8_t byte = lora->read();
        processIncomingByte(byte);
    }

    // Check and update pending commands
    uint32_t currentTime = millis();

    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        TrackedCommand* cmd = &pendingCommands[i];

        if (cmd->state == CommandState::IDLE || cmd->state == CommandState::ACKED ||
            cmd->state == CommandState::FAILED || cmd->state == CommandState::TIMEOUT) {
            continue;
        }

        // Check if command needs to be sent
        if (cmd->state == CommandState::PENDING) {
            if (transmitCommand(cmd)) {
                cmd->state = CommandState::SENT;
                cmd->sendTime = currentTime;
                commandsSent++;

                if (DEBUG_COMMAND_SENDER) {
                    Serial.printf("CommandSender: Sent command seq=%d\n", cmd->sequenceNumber);
                }
            }
        }

        // Check for ACK timeout
        if (cmd->state == CommandState::SENT) {
            if (currentTime - cmd->sendTime > ackTimeoutMs) {
                // Check if we should retry
                if (cmd->retryCount < maxRetries) {
                    retryCommand(cmd);
                } else {
                    // Max retries reached
                    cmd->state = CommandState::TIMEOUT;
                    pendingCommandCount--;
                    commandsTimeout++;

                    if (DEBUG_COMMAND_SENDER) {
                        Serial.printf("CommandSender: Command seq=%d timeout after %d retries\n",
                                     cmd->sequenceNumber, maxRetries);
                    }
                }
            }
        }

        // Check for retry delay
        if (cmd->state == CommandState::SENT && cmd->retryCount > 0) {
            if (currentTime - cmd->lastRetryTime < retryDelayMs) {
                continue; // Still waiting for retry delay
            }
        }
    }
}

// ===========================
// Response Reception
// ===========================

void CommandSender::processIncomingByte(uint8_t byte) {
    if (!inPacket) {
        // Looking for start sequence
        if (receiveIndex == 0 && byte == CMD_START_BYTE1) {
            receiveBuffer[receiveIndex++] = byte;
        } else if (receiveIndex == 1 && byte == CMD_START_BYTE2) {
            receiveBuffer[receiveIndex++] = byte;
            inPacket = true;
        } else {
            receiveIndex = 0; // Reset
        }
    } else {
        // In packet
        receiveBuffer[receiveIndex++] = byte;

        // Check for end sequence
        if (receiveIndex >= 2) {
            if (receiveBuffer[receiveIndex - 2] == CMD_END_BYTE1 &&
                receiveBuffer[receiveIndex - 1] == CMD_END_BYTE2) {
                // Complete packet received
                if (validatePacket(receiveBuffer, receiveIndex)) {
                    ResponsePacket response;
                    if (CommandProtocol::deserializeResponse(receiveBuffer, receiveIndex, response)) {
                        handleResponse(response);
                    }
                }
                resetReceiveState();
            } else if (receiveIndex >= sizeof(receiveBuffer)) {
                // Buffer overflow
                resetReceiveState();
            }
        }
    }
}

void CommandSender::handleResponse(const ResponsePacket& response) {
    // Find matching command
    TrackedCommand* cmd = findTrackedCommand(response.refSequence);
    if (!cmd) {
        if (DEBUG_COMMAND_SENDER) {
            Serial.printf("CommandSender: Received response for unknown seq=%d\n",
                         response.refSequence);
        }
        return;
    }

    // Store response
    cmd->response = response;
    cmd->hasResponse = true;

    // Update state based on response type
    if (response.responseType == ResponseType::ACK) {
        cmd->state = CommandState::ACKED;
        pendingCommandCount--;
        commandsAcked++;

        if (DEBUG_COMMAND_SENDER) {
            Serial.printf("CommandSender: Command seq=%d ACKED\n", response.refSequence);
        }
    } else {
        // NACK or error
        cmd->state = CommandState::FAILED;
        pendingCommandCount--;
        commandsFailed++;

        if (DEBUG_COMMAND_SENDER) {
            Serial.printf("CommandSender: Command seq=%d %s\n",
                         response.refSequence,
                         CommandProtocol::responseToString(response.responseType));
        }
    }
}

// ===========================
// Private Methods
// ===========================

TrackedCommand* CommandSender::findTrackedCommand(uint16_t sequenceNumber) {
    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        TrackedCommand* cmd = &pendingCommands[i];
        if (cmd->state != CommandState::IDLE && cmd->sequenceNumber == sequenceNumber) {
            return cmd;
        }
    }
    return nullptr;
}

TrackedCommand* CommandSender::findOldestCommand() {
    TrackedCommand* oldest = nullptr;
    uint32_t oldestTime = UINT32_MAX;

    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        TrackedCommand* cmd = &pendingCommands[i];
        if (cmd->state != CommandState::IDLE && cmd->sendTime < oldestTime) {
            oldest = cmd;
            oldestTime = cmd->sendTime;
        }
    }

    return oldest;
}

TrackedCommand* CommandSender::findFreeSlot() {
    // First try to find an IDLE slot
    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (pendingCommands[i].state == CommandState::IDLE) {
            return &pendingCommands[i];
        }
    }

    // If no IDLE slot, try to evict oldest completed command
    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        TrackedCommand* cmd = &pendingCommands[i];
        if (cmd->state == CommandState::ACKED || cmd->state == CommandState::FAILED ||
            cmd->state == CommandState::TIMEOUT) {
            // Clear and reuse this slot
            memset(cmd, 0, sizeof(TrackedCommand));
            return cmd;
        }
    }

    return nullptr;
}

bool CommandSender::transmitCommand(TrackedCommand* cmd) {
    if (!lora) {
        return false;
    }

    uint8_t buffer[128];
    size_t length = 0;

    if (!CommandProtocol::serializeCommand(cmd->packet, buffer, length)) {
        if (DEBUG_COMMAND_SENDER) {
            Serial.println("CommandSender: Failed to serialize command");
        }
        return false;
    }

    return lora->transmit(buffer, length);
}

void CommandSender::retryCommand(TrackedCommand* cmd) {
    cmd->retryCount++;
    cmd->lastRetryTime = millis();

    if (transmitCommand(cmd)) {
        cmd->sendTime = cmd->lastRetryTime;

        if (DEBUG_COMMAND_SENDER) {
            Serial.printf("CommandSender: Retrying command seq=%d (attempt %d/%d)\n",
                         cmd->sequenceNumber, cmd->retryCount + 1, maxRetries);
        }
    }
}

void CommandSender::resetReceiveState() {
    receiveIndex = 0;
    inPacket = false;
    memset(receiveBuffer, 0, sizeof(receiveBuffer));
}

bool CommandSender::validatePacket(const uint8_t* buffer, size_t length) {
    if (length < CMD_HEADER_SIZE + 4) {
        return false;
    }

    return CommandProtocol::validateCRC(buffer, length);
}

// ===========================
// State Query
// ===========================

CommandState CommandSender::getCommandState(uint16_t sequenceNumber) const {
    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        const TrackedCommand* cmd = &pendingCommands[i];
        if (cmd->sequenceNumber == sequenceNumber && cmd->state != CommandState::IDLE) {
            return cmd->state;
        }
    }
    return CommandState::IDLE;
}

// ===========================
// Statistics
// ===========================

void CommandSender::resetStatistics() {
    commandsSent = 0;
    commandsAcked = 0;
    commandsFailed = 0;
    commandsTimeout = 0;
}

// ===========================
// Debug
// ===========================

void CommandSender::printStatus() const {
    Serial.println("=== Command Sender Status ===");
    Serial.printf("Initialized: %s\n", initialized ? "Yes" : "No");
    Serial.printf("Pending Commands: %d/%d\n", pendingCommandCount, MAX_PENDING_COMMANDS);
    Serial.printf("Commands Sent: %lu\n", commandsSent);
    Serial.printf("Commands ACKed: %lu\n", commandsAcked);
    Serial.printf("Commands Failed: %lu\n", commandsFailed);
    Serial.printf("Commands Timeout: %lu\n", commandsTimeout);
    Serial.printf("Max Retries: %d\n", maxRetries);
    Serial.printf("ACK Timeout: %lu ms\n", ackTimeoutMs);
}

void CommandSender::printPendingCommands() const {
    Serial.println("=== Pending Commands ===");

    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        const TrackedCommand* cmd = &pendingCommands[i];
        if (cmd->state == CommandState::IDLE) {
            continue;
        }

        Serial.printf("Slot %d: Seq=%d, State=%d, Retries=%d, Age=%lu ms\n",
                     i, cmd->sequenceNumber, static_cast<int>(cmd->state),
                     cmd->retryCount, millis() - cmd->sendTime);
    }
}
