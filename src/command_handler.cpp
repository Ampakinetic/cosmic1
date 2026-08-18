#include "command_handler.h"
#include "auto_capture.h"
#include "image_tx_manager.h"

// Debug configuration
#ifndef DEBUG_COMMAND_HANDLER
#define DEBUG_COMMAND_HANDLER true
#endif

// ===========================
// Static Instance
// ===========================

static CommandHandler commandHandlerInstance;
CommandHandler& CmdHandler() {
    return commandHandlerInstance;
}

// ===========================
// Constructor/Destructor
// ===========================

CommandHandler::CommandHandler()
    : lora(nullptr)
    , camera(nullptr)
    , initialized(false)
    , receiveIndex(0)
    , inPacket(false)
    , hasCommand(false)
    , commandsReceived(0)
    , commandsExecuted(0)
    , commandsFailed(0)
{
}

CommandHandler::~CommandHandler() {
    end();
}

// ===========================
// Initialization
// ===========================

bool CommandHandler::begin(E32LoRa* lora, CameraManager* camera) {
    if (!lora || !camera) {
        return false;
    }

    this->lora = lora;
    this->camera = camera;

    resetReceiveState();

    initialized = true;

    if (DEBUG_COMMAND_HANDLER) {
        Serial.println("CommandHandler: Initialized");
    }

    return true;
}

void CommandHandler::end() {
    initialized = false;
    resetReceiveState();
}

// ===========================
// Main Processing
// ===========================

void CommandHandler::process() {
    if (!initialized || !lora) {
        return;
    }

    // Read all available bytes from LoRa
    while (lora->available() > 0) {
        uint8_t byte = lora->read();
        processIncomingByte(byte);
    }

    // Process complete command if received
    if (hasCommand && !pendingCommand.processing) {
        pendingCommand.processing = true;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Executing command %02X\n",
                         static_cast<uint8_t>(pendingCommand.packet.cmd));
        }

        CommandResult result = executeCommand(pendingCommand.packet);

        // Send response
        ResponsePacket response;
        if (result.success) {
            // Typed by the handler's result (WR-05): ACK for plain success,
            // STATUS for status queries — never flattened to ACK
            response = createResponsePacket(
                result.responseType,
                pendingCommand.packet.sequenceNumber,
                result.responseData,
                result.responseLength
            );
        } else {
            response = CommandProtocol::createNACK(
                pendingCommand.packet.sequenceNumber,
                result.responseType,
                result.message
            );
        }

        sendResponse(response);

        // Clear pending command
        hasCommand = false;
        pendingCommand.processing = false;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Command %s - %s\n",
                         CommandProtocol::commandToString(pendingCommand.packet.cmd),
                         result.success ? "SUCCESS" : "FAILED");
        }
    }
}

// ===========================
// Command Execution
// ===========================

CommandResult CommandHandler::executeCommand(const CommandPacket& cmd) {
    CommandResult result{};
    result.success = false;
    result.responseType = ResponseType::NACK;
    result.responseLength = 0;

    commandsReceived++;

    // Check if camera is ready
    if (!camera->isReady()) {
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Camera not ready", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    // Dispatch to handler based on command type
    switch (cmd.cmd) {
        case CameraCommand::CAPTURE_NOW:
            return handleCaptureNow(cmd);

        case CameraCommand::SET_RESOLUTION:
            return handleSetResolution(cmd);

        case CameraCommand::SET_QUALITY:
            return handleSetQuality(cmd);

        case CameraCommand::SET_BRIGHTNESS:
            return handleSetBrightness(cmd);

        case CameraCommand::SET_CONTRAST:
            return handleSetContrast(cmd);

        case CameraCommand::SET_SATURATION:
            return handleSetSaturation(cmd);

        case CameraCommand::SET_EXPOSURE:
            return handleSetExposure(cmd);

        case CameraCommand::SET_WB_MODE:
            return handleSetWBMode(cmd);

        case CameraCommand::AUTO_CAPTURE_ENABLE:
            return handleAutoCaptureEnable(cmd);

        case CameraCommand::AUTO_CAPTURE_DISABLE:
            return handleAutoCaptureDisable(cmd);

        case CameraCommand::GET_STATUS:
            return handleGetStatus(cmd);

        case CameraCommand::IMAGE_WINDOW_REQUEST:
            return handleImageWindowRequest(cmd);

        default:
            result.responseType = ResponseType::NACK_INVALID;
            strncpy(result.message, "Unknown command", sizeof(result.message) - 1);
            commandsFailed++;
            return result;
    }
}

// ===========================
// Command Handlers
// ===========================

CommandResult CommandHandler::handleCaptureNow(const CommandPacket& cmd) {
    CommandResult result{};

    if (DEBUG_COMMAND_HANDLER) {
        Serial.println("CommandHandler: CAPTURE_NOW");
    }

    // Stamp the manual capture source BEFORE capturing so the enqueued image
    // carries it (interval captures keep the CaptureSource::INTERVAL default)
    camera->setLastCaptureSource(static_cast<uint8_t>(CaptureSource::MANUAL));

    // Capture image
    if (camera->captureImage()) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        // Return image ID as response data - shared ID sequence with
        // automatic captures (single authority in AutoCapture)
        uint16_t imageId = AutoCap().allocateImageId();

        result.responseData[0] = (imageId >> 8) & 0xFF;
        result.responseData[1] = imageId & 0xFF;
        result.responseLength = 2;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Captured image ID %d\n", imageId);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Capture failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleSetResolution(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 1) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing parameter", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    FrameSize fs = static_cast<FrameSize>(cmd.payload[0]);
    framesize_t espFs;

    if (!framesizeFromInt(fs, espFs)) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid resolution", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (camera->setFrameSize(espFs)) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        result.responseData[0] = static_cast<uint8_t>(fs);
        result.responseLength = 1;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Set resolution to %d\n", static_cast<int>(fs));
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Set resolution failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleSetQuality(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 1) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing parameter", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    uint8_t quality = cmd.payload[0];

    if (quality > 63) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid quality", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (camera->setQuality(quality)) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        result.responseData[0] = quality;
        result.responseLength = 1;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Set quality to %d\n", quality);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Set quality failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleSetBrightness(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 1) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing parameter", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    int8_t brightness = static_cast<int8_t>(cmd.payload[0]);

    if (brightness < -2 || brightness > 2) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid brightness", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (camera->setBrightness(brightness)) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        result.responseData[0] = static_cast<uint8_t>(brightness);
        result.responseLength = 1;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Set brightness to %d\n", brightness);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Set brightness failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleSetContrast(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 1) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing parameter", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    int8_t contrast = static_cast<int8_t>(cmd.payload[0]);

    if (contrast < -2 || contrast > 2) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid contrast", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (camera->setContrast(contrast)) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        result.responseData[0] = static_cast<uint8_t>(contrast);
        result.responseLength = 1;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Set contrast to %d\n", contrast);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Set contrast failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleSetSaturation(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 1) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing parameter", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    int8_t saturation = static_cast<int8_t>(cmd.payload[0]);

    if (saturation < -2 || saturation > 2) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid saturation", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (camera->setSaturation(saturation)) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        result.responseData[0] = static_cast<uint8_t>(saturation);
        result.responseLength = 1;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Set saturation to %d\n", saturation);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Set saturation failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleSetExposure(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 1) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing parameter", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    int8_t exposure = static_cast<int8_t>(cmd.payload[0]);

    if (exposure < -2 || exposure > 2) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid exposure", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (camera->setExposure(exposure)) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        result.responseData[0] = static_cast<uint8_t>(exposure);
        result.responseLength = 1;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Set exposure to %d\n", exposure);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Set exposure failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleSetWBMode(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 1) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing parameter", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    WhiteBalanceMode wbMode = static_cast<WhiteBalanceMode>(cmd.payload[0]);

    int wbModeValue = static_cast<int>(wbMode);
    if (wbModeValue < 0 || wbModeValue > 4) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid WB mode", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (camera->setWBMode(wbModeValue)) {
        result.success = true;
        result.responseType = ResponseType::ACK;

        result.responseData[0] = static_cast<uint8_t>(wbMode);
        result.responseLength = 1;

        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Set WB mode to %d\n", wbModeValue);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Set WB mode failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleAutoCaptureEnable(const CommandPacket& cmd) {
    CommandResult result{};

    if (cmd.payloadLength < 4) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing interval", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    uint32_t intervalMs = CommandProtocol::readUint32(cmd.payload);

    if (intervalMs < 1000 || intervalMs > 3600000) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Invalid interval", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    if (AutoCap().enable(intervalMs)) {
        result.success = true;
        result.responseType = ResponseType::ACK;
        CommandProtocol::writeUint32(result.responseData, intervalMs);
        result.responseLength = 4;
        commandsExecuted++;

        if (DEBUG_COMMAND_HANDLER) {
            Serial.printf("CommandHandler: Auto-capture enabled at %lu ms\n", intervalMs);
        }
    } else {
        result.success = false;
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Auto-capture enable failed", sizeof(result.message) - 1);
        commandsFailed++;
    }

    return result;
}

CommandResult CommandHandler::handleAutoCaptureDisable(const CommandPacket& cmd) {
    CommandResult result{};

    AutoCap().disable();

    result.success = true;
    result.responseType = ResponseType::ACK;
    commandsExecuted++;

    if (DEBUG_COMMAND_HANDLER) {
        Serial.println("CommandHandler: Auto-capture disabled");
    }

    return result;
}

CommandResult CommandHandler::handleGetStatus(const CommandPacket& cmd) {
    CommandResult result{};

    ResponseStatusData status{};
    status.imageId = AutoCap().getLastImageId();
    status.autoCaptureEnabled = AutoCap().isEnabled() ? 1 : 0;
    status.autoCaptureInterval = AutoCap().getInterval();
    status.currentResolution = frameSizeFromEsp(camera->getFrameSize());
    status.currentQuality = static_cast<uint8_t>(camera->getQuality());
    status.currentBrightness = static_cast<int8_t>(camera->getBrightness());
    status.currentContrast = static_cast<int8_t>(camera->getContrast());

    result.success = true;
    result.responseType = ResponseType::STATUS;
    memcpy(result.responseData, &status, sizeof(ResponseStatusData));
    result.responseLength = sizeof(ResponseStatusData);
    commandsExecuted++;

    if (DEBUG_COMMAND_HANDLER) {
        Serial.println("CommandHandler: Status sent");
    }

    return result;
}

CommandResult CommandHandler::handleImageWindowRequest(const CommandPacket& cmd) {
    CommandResult result{};

    // PayloadImageWindowRequest: imageId BE16, startChunk BE16, count u8
    if (cmd.payloadLength < 5) {
        result.responseType = ResponseType::NACK_PARAM;
        strncpy(result.message, "Missing window params", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }

    // All validation and arming lives in ImageTx (T-02-04); this handler
    // only maps the outcome onto the existing response machinery so the
    // base's tracked-command table, terminal-state guard, and duplicate-ACK
    // guard apply unchanged (Phase 1 CR-03 lesson applied to chunk ACKs)
    WindowRequestResult outcome = ImageTx().handleWindowRequest(cmd.payload, cmd.payloadLength);

    switch (outcome) {
        case WindowRequestResult::ARMED:
            result.success = true;
            result.responseType = ResponseType::ACK;
            // Echo the armed window (imageId BE16, startChunk BE16, count u8)
            // so the base can confirm exactly what was armed
            memcpy(result.responseData, cmd.payload, 5);
            result.responseLength = 5;
            commandsExecuted++;

            if (DEBUG_COMMAND_HANDLER) {
                Serial.println("CommandHandler: IMAGE_WINDOW_REQUEST armed");
            }
            break;

        case WindowRequestResult::UNKNOWN_IMAGE:
            result.responseType = ResponseType::NACK_INVALID;
            strncpy(result.message, "Unknown image", sizeof(result.message) - 1);
            commandsFailed++;
            break;

        case WindowRequestResult::INVALID_RANGE:
            result.responseType = ResponseType::NACK_INVALID;
            strncpy(result.message, "Invalid window", sizeof(result.message) - 1);
            commandsFailed++;
            break;

        case WindowRequestResult::BUSY:
            result.responseType = ResponseType::NACK_BUSY;
            strncpy(result.message, "Window busy", sizeof(result.message) - 1);
            commandsFailed++;
            break;
    }

    return result;
}

// ===========================
// Response Sending
// ===========================

bool CommandHandler::sendResponse(const ResponsePacket& response) {
    if (!initialized || !lora) {
        return false;
    }

    uint8_t buffer[128];
    size_t length = 0;

    if (!CommandProtocol::serializeResponse(response, buffer, length)) {
        if (DEBUG_COMMAND_HANDLER) {
            Serial.println("CommandHandler: Failed to serialize response");
        }
        return false;
    }

    bool sent = lora->transmit(buffer, length);

    if (DEBUG_COMMAND_HANDLER) {
        Serial.printf("CommandHandler: Response %s - %d bytes\n",
                     sent ? "sent" : "failed", length);
    }

    return sent;
}

// ===========================
// Reception Helpers
// ===========================

void CommandHandler::processIncomingByte(uint8_t byte) {
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
    // the payload must not terminate the packet, so the end marker is only
    // ever tested at the length the header announces. Mirrors the framing
    // rule in CommandSender::processIncomingByte.
    receiveBuffer[receiveIndex++] = byte;

    if (receiveIndex < CMD_HEADER_SIZE) {
        return;
    }

    // WR-12 fix (Phase 2): dispatch on the packet-type byte BEFORE any body
    // arithmetic. The balloon accepts COMMAND frames (0x10) only — a
    // CRC-valid RESPONSE, manifest, chunk, or beacon heard by the balloon
    // must never execute as a command, nor be parsed with command arithmetic.
    switch (receiveBuffer[2]) {
        case static_cast<uint8_t>(PACKET_TYPE_COMMAND):
            break; // the only frame type this receiver handles
        default:
            resetReceiveState(); // foreign or unknown type — discard the frame
            return;
    }

    // Big-endian body length from header offsets 4-5 (payloadLength for commands)
    size_t bodyLen = (static_cast<size_t>(receiveBuffer[4]) << 8) | receiveBuffer[5];

    // Commands: header + cmd/sequence/payloadLength block (5) + payload + CRC/end (4)
    size_t expectedTotal = CMD_HEADER_SIZE + 5 + bodyLen + 4;

    if (expectedTotal > sizeof(receiveBuffer) || bodyLen > CMD_MAX_PAYLOAD_SIZE) {
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
            CommandPacket cmd;
            if (CommandProtocol::deserializeCommand(receiveBuffer, expectedTotal, cmd)) {
                pendingCommand.packet = cmd;
                pendingCommand.receivedTime = millis();
                hasCommand = true;
            }
        }
    }
    resetReceiveState();
}

void CommandHandler::resetReceiveState() {
    receiveIndex = 0;
    inPacket = false;
    memset(receiveBuffer, 0, sizeof(receiveBuffer));
}

bool CommandHandler::validatePacket(const uint8_t* buffer, size_t length) {
    if (length < CMD_HEADER_SIZE + 4) {
        return false;
    }

    return CommandProtocol::validateCRC(buffer, length);
}

// ===========================
// Camera Helpers
// ===========================

bool CommandHandler::framesizeFromInt(FrameSize fs, framesize_t& espFramesize) {
    switch (fs) {
        case FrameSize::FRAMESIZE_QQVGA:
            espFramesize = FRAMESIZE_QQVGA;
            return true;
        case FrameSize::FRAMESIZE_QVGA:
            espFramesize = FRAMESIZE_QVGA;
            return true;
        case FrameSize::FRAMESIZE_HQVGA:
            espFramesize = FRAMESIZE_HQVGA;
            return true;
        case FrameSize::FRAMESIZE_CIF:
            espFramesize = FRAMESIZE_CIF;
            return true;
        case FrameSize::FRAMESIZE_VGA:
            espFramesize = FRAMESIZE_VGA;
            return true;
        case FrameSize::FRAMESIZE_SVGA:
            espFramesize = FRAMESIZE_SVGA;
            return true;
        case FrameSize::FRAMESIZE_XGA:
            espFramesize = FRAMESIZE_XGA;
            return true;
        case FrameSize::FRAMESIZE_SXGA:
            espFramesize = FRAMESIZE_SXGA;
            return true;
        case FrameSize::FRAMESIZE_UXGA:
            espFramesize = FRAMESIZE_UXGA;
            return true;
        default:
            return false;
    }
}

// Reverse mapping: real esp32-camera framesize_t -> project FrameSize wire code,
// matched BY NAME (the two enums number identical names differently — e.g. real
// QVGA is 5 while the project QVGA code is 6, so a numeric reinterpretation
// reports the wrong resolution).
FrameSize CommandHandler::frameSizeFromEsp(framesize_t espFrameSize) const {
    switch (espFrameSize) {
        case FRAMESIZE_QQVGA:
            return FrameSize::FRAMESIZE_QQVGA;
        case FRAMESIZE_QVGA:
            return FrameSize::FRAMESIZE_QVGA;
        case FRAMESIZE_HQVGA:
            return FrameSize::FRAMESIZE_HQVGA;
        case FRAMESIZE_CIF:
            return FrameSize::FRAMESIZE_CIF;
        case FRAMESIZE_VGA:
            return FrameSize::FRAMESIZE_VGA;
        case FRAMESIZE_SVGA:
            return FrameSize::FRAMESIZE_SVGA;
        case FRAMESIZE_XGA:
            return FrameSize::FRAMESIZE_XGA;
        case FRAMESIZE_SXGA:
            return FrameSize::FRAMESIZE_SXGA;
        case FRAMESIZE_UXGA:
            return FrameSize::FRAMESIZE_UXGA;
        default:
            // Real sizes with no protocol code (96x96, QCIF, 240x240, HVGA, HD)
            // report as the boot default QVGA instead of a bogus numeric cast
            return FrameSize::FRAMESIZE_QVGA;
    }
}

// ===========================
// Statistics and Debug
// ===========================

void CommandHandler::resetStatistics() {
    commandsReceived = 0;
    commandsExecuted = 0;
    commandsFailed = 0;
}

void CommandHandler::printStatus() const {
    Serial.println("=== Command Handler Status ===");
    Serial.printf("Initialized: %s\n", initialized ? "Yes" : "No");
    Serial.printf("Commands Received: %lu\n", commandsReceived);
    Serial.printf("Commands Executed: %lu\n", commandsExecuted);
    Serial.printf("Commands Failed: %lu\n", commandsFailed);
    Serial.printf("Has Pending Command: %s\n", hasCommand ? "Yes" : "No");
}
