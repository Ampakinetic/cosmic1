#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include "command_protocol.h"
#include "e32_lora.h"
#include "camera_manager.h"

// ===========================
// Command Handler
// Balloon Unit - Receives and executes camera commands
// ===========================

// Command execution result
struct CommandResult {
    bool success;
    ResponseType responseType;
    char message[32];
    uint8_t responseData[50];
    uint16_t responseLength;
};

// Pending command state
struct PendingCommand {
    CommandPacket packet;
    uint32_t receivedTime;
    bool processing;
};

// ===========================
// Command Handler Class
// ===========================

class CommandHandler {
public:
    CommandHandler();
    ~CommandHandler();

    // Initialization
    bool begin(E32LoRa* lora, CameraManager* camera);
    void end();

    // Processing
    void process(); // Call from main loop
    bool hasPendingCommand() const { return hasCommand; }

    // Command execution
    CommandResult executeCommand(const CommandPacket& cmd);

    // Status
    uint32_t getCommandsReceived() const { return commandsReceived; }
    uint32_t getCommandsExecuted() const { return commandsExecuted; }
    uint32_t getCommandsFailed() const { return commandsFailed; }
    void resetStatistics();

    // Debug
    void printStatus() const;

private:
    E32LoRa* lora;
    CameraManager* camera;
    bool initialized;

    // Command reception
    uint8_t receiveBuffer[256];
    size_t receiveIndex;
    bool inPacket;
    bool hasCommand;
    PendingCommand pendingCommand;

    // Statistics
    uint32_t commandsReceived;
    uint32_t commandsExecuted;
    uint32_t commandsFailed;

    // Command handlers
    CommandResult handleCaptureNow(const CommandPacket& cmd);
    CommandResult handleSetResolution(const CommandPacket& cmd);
    CommandResult handleSetQuality(const CommandPacket& cmd);
    CommandResult handleSetBrightness(const CommandPacket& cmd);
    CommandResult handleSetContrast(const CommandPacket& cmd);
    CommandResult handleSetSaturation(const CommandPacket& cmd);
    CommandResult handleSetExposure(const CommandPacket& cmd);
    CommandResult handleSetWBMode(const CommandPacket& cmd);
    CommandResult handleAutoCaptureEnable(const CommandPacket& cmd);
    CommandResult handleAutoCaptureDisable(const CommandPacket& cmd);
    CommandResult handleGetStatus(const CommandPacket& cmd);

    // Phase 2 (02-02): base pulls a window of full-image chunks (D-21);
    // validates via ImageTx().handleWindowRequest and answers through the
    // existing createResponsePacket path (ACK/NACK_INVALID/NACK_BUSY)
    CommandResult handleImageWindowRequest(const CommandPacket& cmd);

    // Response sending
    bool sendResponse(const ResponsePacket& response);

    // Reception helpers
    void processIncomingByte(uint8_t byte);
    void resetReceiveState();
    bool validatePacket(const uint8_t* buffer, size_t length);

    // Camera helpers
    bool framesizeFromInt(FrameSize fs, framesize_t& espFramesize);
    FrameSize frameSizeFromEsp(framesize_t espFrameSize) const;
};

// ===========================
// Global Instance Access
// ===========================

extern CommandHandler& CmdHandler();

#endif // COMMAND_HANDLER_H
