#include "command_sender.h"
#include "image_rx_manager.h"

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
// Per-Command ACK Timeout (D-05)
// ===========================

// ACK-timeout window for a tracked command's stored CameraCommand value:
// TRIGGER 2000ms (CAPTURE_NOW), SETTINGS 5000ms (SET_* + auto-capture
// config), COMPLEX 10000ms (GET_STATUS).
static uint32_t ackTimeoutFor(uint8_t commandType) {
    switch (static_cast<CameraCommand>(commandType)) {
        case CameraCommand::CAPTURE_NOW:
            return CMD_ACK_TIMEOUT_TRIGGER_MS;

        case CameraCommand::SET_RESOLUTION:
        case CameraCommand::SET_QUALITY:
        case CameraCommand::SET_BRIGHTNESS:
        case CameraCommand::SET_CONTRAST:
        case CameraCommand::SET_SATURATION:
        case CameraCommand::SET_EXPOSURE:
        case CameraCommand::SET_WB_MODE:
        case CameraCommand::AUTO_CAPTURE_ENABLE:
        case CameraCommand::AUTO_CAPTURE_DISABLE:
            return CMD_ACK_TIMEOUT_SETTINGS_MS;

        case CameraCommand::GET_STATUS:
            return CMD_ACK_TIMEOUT_COMPLEX_MS;

        // WINDOW 15000ms (IMAGE_WINDOW_REQUEST) — the ACK bounds only the
        // arm handshake; the 16-chunk stream itself is watched by the
        // ImageRxManager stall clock (IMG_WINDOW_STALL_MS), so the class
        // stays generous (Pitfall 4: a whole-window timeout on the
        // command's arm ACK would abandon healthy streams)
        case CameraCommand::IMAGE_WINDOW_REQUEST:
            return CMD_ACK_TIMEOUT_WINDOW_MS;

        default:
            return CMD_ACK_TIMEOUT_SETTINGS_MS;
    }
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
    , commandsSent(0)
    , commandsAcked(0)
    , commandsFailed(0)
    , commandsTimeout(0)
    , receiveIndex(0)
    , inPacket(false)
    , hasStatusData(false)
{
    memset(pendingCommands, 0, sizeof(pendingCommands));
    memset(receiveBuffer, 0, sizeof(receiveBuffer));
    memset(&latestStatus, 0, sizeof(latestStatus));
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

    // Decrement only when leaving a counted state — cancelling an
    // already-terminal command (ACKED/FAILED/TIMEOUT) must not decrement
    // the pending count a second time (WR-03)
    if (cmd->state == CommandState::PENDING || cmd->state == CommandState::SENT) {
        pendingCommandCount--;
    }

    cmd->state = CommandState::FAILED;

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

        // Pace retries with exponential backoff (D-07: 2000/4000/8000ms before
        // retries 1/2/3). This check guards the PENDING and SENT branches below
        // so transmit attempts can never stack or fire back-to-back (WR-04).
        if ((cmd->state == CommandState::PENDING || cmd->state == CommandState::SENT) &&
            cmd->retryCount > 0) {
            // retryCount was incremented by the previous (failed or un-ACKed)
            // attempt, so it is the retry attempt about to run (1..maxRetries)
            uint8_t attempt = cmd->retryCount;
            uint8_t shift = (attempt - 1 > 2) ? 2 : static_cast<uint8_t>(attempt - 1); // clamp the shift at 2
            uint32_t backoffMs = CMD_RETRY_BACKOFF_BASE_MS << shift;
            if (currentTime - cmd->lastRetryTime < backoffMs) {
                continue; // Still pacing the upcoming retry attempt
            }
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
            } else {
                // Transmit failed: count the attempt, then either terminate
                // or stay PENDING so the backoff check above paces the next
                // attempt (CR-04) — never retry back-to-back forever
                cmd->retryCount++;
                cmd->lastRetryTime = currentTime;

                if (cmd->retryCount >= maxRetries) {
                    cmd->state = CommandState::FAILED;
                    pendingCommandCount--;
                    commandsFailed++;

                    if (DEBUG_COMMAND_SENDER) {
                        Serial.printf("CommandSender: Command seq=%d FAILED after %d transmit attempts\n",
                                     cmd->sequenceNumber, maxRetries);
                    }
                }
            }
        }

        // Check for ACK timeout (D-05: window selected per command type)
        if (cmd->state == CommandState::SENT) {
            if (currentTime - cmd->sendTime > ackTimeoutFor(static_cast<uint8_t>(cmd->packet.cmd))) {
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
        return;
    }

    // In packet — length-driven framing (CR-03): an embedded 0x0D 0x0A pair in
    // the frame data must not terminate the packet, so the end marker is
    // only ever tested at the length the header announces.
    receiveBuffer[receiveIndex++] = byte;

    if (receiveIndex < CMD_HEADER_SIZE) {
        return;
    }

    // WR-12 (base half): dispatch on the wire type byte at buffer[2] BEFORE
    // any body arithmetic — each frame type has its own body shape, and a
    // foreign type byte must never be interpreted with response arithmetic.
    // The base accepts 0x11 RESPONSE, 0x12 MANIFEST, 0x13 CHUNK, 0x14 BEACON.
    uint8_t frameType = receiveBuffer[2];
    size_t bodyLen;
    size_t bodyOverhead;      // typed bytes between the header and the body
    bool isResponse = false;  // only 0x11 touches the tracked-command table
    switch (frameType) {
        case static_cast<uint8_t>(PACKET_TYPE_RESPONSE):
            // Big-endian dataLength from header offsets 4-5
            bodyLen = (static_cast<size_t>(receiveBuffer[4]) << 8) | receiveBuffer[5];
            bodyOverhead = 4; // responseType/refSequence/dataLength block
            isResponse = true;
            break;

        case static_cast<uint8_t>(PACKET_TYPE_IMAGE_MANIFEST):
            bodyLen = IMG_MANIFEST_BODY_SIZE;           // fixed 27-byte body
            bodyOverhead = 0;
            break;

        case static_cast<uint8_t>(PACKET_TYPE_IMAGE_CHUNK):
            // The chunk header's bodyLen field carries dataLen
            bodyLen = (static_cast<size_t>(receiveBuffer[4]) << 8) | receiveBuffer[5];
            bodyOverhead = 5; // imageId/chunkIndex/dataLen block
            break;

        case static_cast<uint8_t>(PACKET_TYPE_TELEMETRY_BEACON):
            bodyLen = IMG_TELEMETRY_BEACON_BODY_SIZE;   // fixed 17-byte body
            bodyOverhead = 0;
            break;

        default:
            resetReceiveState(); // foreign or unknown type — discard the frame
            return;
    }

    size_t expectedTotal = CMD_HEADER_SIZE + bodyOverhead + bodyLen + 4;

    if (expectedTotal > sizeof(receiveBuffer) ||
        (isResponse && bodyLen > CMD_MAX_RESPONSE_DATA) ||
        (frameType == static_cast<uint8_t>(PACKET_TYPE_IMAGE_CHUNK)
             && bodyLen > IMG_CHUNK_PAYLOAD_SIZE)) {
        resetReceiveState(); // Bogus header — lengths exceed protocol bounds
        return;
    }

    if (receiveIndex < expectedTotal) {
        return; // Still accumulating toward the announced length
    }

    // Full expected length received: verify the end marker at the framed
    // position, then validate and dispatch
    if (receiveBuffer[expectedTotal - 2] == CMD_END_BYTE1 &&
        receiveBuffer[expectedTotal - 1] == CMD_END_BYTE2) {
        if (validatePacket(receiveBuffer, expectedTotal)) {
            if (isResponse) {
                ResponsePacket response;
                if (CommandProtocol::deserializeResponse(receiveBuffer, expectedTotal, response)) {
                    handleResponse(response);
                }
            } else {
                // Unsolicited image/telemetry frames — forwarded to the
                // reassembly module; they NEVER touch the tracked-command
                // table (0x12/0x13/0x14 are not responses to any command)
                switch (frameType) {
                    case static_cast<uint8_t>(PACKET_TYPE_IMAGE_MANIFEST):
                        ImageRx().onManifestFrame(receiveBuffer, expectedTotal);
                        break;
                    case static_cast<uint8_t>(PACKET_TYPE_IMAGE_CHUNK):
                        ImageRx().onChunkFrame(receiveBuffer, expectedTotal);
                        break;
                    case static_cast<uint8_t>(PACKET_TYPE_TELEMETRY_BEACON):
                        ImageRx().onTelemetryBeaconFrame(receiveBuffer, expectedTotal);
                        break;
                    default:
                        break; // unreachable — dispatched above
                }
            }
        }
    }
    resetReceiveState();
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

    // Duplicate or late response — retryCommand retransmits with the SAME
    // sequenceNumber, so at the D-05 window edge a late first ACK and the
    // re-execution's second ACK can both arrive; the first one already
    // performed the counted transition and this one must be discarded (a
    // second decrement underflows the uint8 pendingCommandCount from 0 to
    // 255 and latches hasPendingCommands() true forever)
    if (cmd->state == CommandState::ACKED || cmd->state == CommandState::FAILED || cmd->state == CommandState::TIMEOUT) {
        return;
    }

    // Store response
    cmd->response = response;
    cmd->hasResponse = true;

    // Update state based on response type
    if (response.responseType == ResponseType::ACK || response.responseType == ResponseType::STATUS) {
        // ACK confirms execution; STATUS is a successful typed response
        // (GET_STATUS) — both count as success (WR-05)
        cmd->state = CommandState::ACKED;
        pendingCommandCount--;
        commandsAcked++;

        // D-26: latch the STATUS payload so the balloon-reported config
        // survives tracked-slot reuse (same latch discipline as the base's
        // auto-capture chip)
        if (response.responseType == ResponseType::STATUS &&
            response.dataLength >= sizeof(ResponseStatusData)) {
            memcpy(&latestStatus, response.data, sizeof(ResponseStatusData));
            hasStatusData = true;
        }

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

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
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

    // Restart the ACK-timeout window from this attempt on BOTH outcomes, so
    // a failed retransmit does not leave an instantly-expired window
    cmd->sendTime = cmd->lastRetryTime;

    if (transmitCommand(cmd)) {
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

uint8_t CommandSender::getCommandRetryCount(uint16_t sequenceNumber) const {
    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        const TrackedCommand* cmd = &pendingCommands[i];
        if (cmd->state != CommandState::IDLE && cmd->sequenceNumber == sequenceNumber) {
            return cmd->retryCount;
        }
    }
    return 0;
}

uint8_t CommandSender::getCommandQueue(CommandQueueEntry* out, uint8_t maxEntries) const {
    if (!out || maxEntries == 0) {
        return 0;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS && count < maxEntries; i++) {
        const TrackedCommand* cmd = &pendingCommands[i];
        if (cmd->state == CommandState::IDLE) {
            continue;
        }

        out[count].sequenceNumber = cmd->sequenceNumber;
        out[count].commandType = static_cast<uint8_t>(cmd->packet.cmd);
        out[count].state = cmd->state;
        out[count].retryCount = cmd->retryCount;
        count++;
    }

    return count;
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
    Serial.printf("ACK Timeout (D-05): TRIGGER %lu ms / SETTINGS %lu ms / COMPLEX %lu ms\n",
                 CMD_ACK_TIMEOUT_TRIGGER_MS, CMD_ACK_TIMEOUT_SETTINGS_MS, CMD_ACK_TIMEOUT_COMPLEX_MS);
    Serial.printf("Retry Backoff (D-07): %lu/%lu/%lu ms\n",
                 CMD_RETRY_BACKOFF_BASE_MS, CMD_RETRY_BACKOFF_BASE_MS * 2, CMD_RETRY_BACKOFF_BASE_MS * 4);
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
