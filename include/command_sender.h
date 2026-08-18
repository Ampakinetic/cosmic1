#ifndef COMMAND_SENDER_H
#define COMMAND_SENDER_H

#include <Arduino.h>
#include "command_protocol.h"
#include "e32_lora.h"

// ===========================
// Command Sender
// Base Station - Sends camera commands and handles ACK/NACK
// ===========================

// Maximum pending commands
static constexpr uint8_t MAX_PENDING_COMMANDS = 5;

// Command transmission state
enum class CommandState : uint8_t {
    IDLE = 0,
    PENDING = 1,
    SENT = 2,
    ACKED = 3,
    FAILED = 4,
    TIMEOUT = 5
};

// Command transmission tracking
struct TrackedCommand {
    CommandPacket packet;
    uint16_t sequenceNumber;
    CommandState state;
    uint8_t retryCount;
    uint32_t sendTime;
    uint32_t lastRetryTime;
    ResponsePacket response;
    bool hasResponse;
};

// ===========================
// Command Sender Class
// ===========================

class CommandSender {
public:
    CommandSender();
    ~CommandSender();

    // Initialization
    bool begin(E32LoRa* lora);
    void end();

    // Command transmission
    uint16_t sendCommand(CameraCommand cmd, const void* payload = nullptr, size_t payloadSize = 0);
    bool cancelCommand(uint16_t sequenceNumber);

    // Processing (call from main loop)
    void process();
    void processIncomingByte(uint8_t byte);

    // State query
    CommandState getCommandState(uint16_t sequenceNumber) const;
    bool hasPendingCommands() const { return pendingCommandCount > 0; }

    // Statistics
    uint32_t getCommandsSent() const { return commandsSent; }
    uint32_t getCommandsAcked() const { return commandsAcked; }
    uint32_t getCommandsFailed() const { return commandsFailed; }
    uint32_t getCommandsTimeout() const { return commandsTimeout; }
    void resetStatistics();

    // Configuration
    void setMaxRetries(uint8_t maxRetries) { this->maxRetries = maxRetries; }
    void setAckTimeout(uint32_t timeoutMs) { this->ackTimeoutMs = timeoutMs; }

    // Debug
    void printStatus() const;
    void printPendingCommands() const;

private:
    E32LoRa* lora;
    bool initialized;

    // Sequence management
    uint16_t nextSequenceNumber;

    // Command tracking
    TrackedCommand pendingCommands[MAX_PENDING_COMMANDS];
    uint8_t pendingCommandCount;

    // Retry configuration
    uint8_t maxRetries;
    uint32_t ackTimeoutMs;
    uint32_t retryDelayMs;

    // Statistics
    uint32_t commandsSent;
    uint32_t commandsAcked;
    uint32_t commandsFailed;
    uint32_t commandsTimeout;

    // Response reception
    uint8_t receiveBuffer[CMD_MAX_PACKET_SIZE];
    size_t receiveIndex;
    bool inPacket;

    // Private methods
    TrackedCommand* findTrackedCommand(uint16_t sequenceNumber);
    TrackedCommand* findOldestCommand();
    TrackedCommand* findFreeSlot();
    bool transmitCommand(TrackedCommand* cmd);
    void retryCommand(TrackedCommand* cmd);
    void handleResponse(const ResponsePacket& response);
    void resetReceiveState();
    bool validatePacket(const uint8_t* buffer, size_t length);
};

// ===========================
// Global Instance Access
// ===========================

extern CommandSender& CmdSender();

#endif // COMMAND_SENDER_H
