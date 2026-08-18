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

// Live command-queue snapshot entry (D-16: full-queue view for the
// base-station UI — pending/in-progress state for every command, not just
// the last one)
struct CommandQueueEntry {
    uint16_t sequenceNumber;
    uint8_t commandType;   // CameraCommand value of the slot's stored packet
    CommandState state;
    uint8_t retryCount;
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
    uint8_t getCommandRetryCount(uint16_t sequenceNumber) const;
    bool hasPendingCommands() const { return pendingCommandCount > 0; }

    // Live command-queue snapshot (D-16): fills out with one entry per
    // non-IDLE slot of the command table in slot order; returns the entry count
    uint8_t getCommandQueue(CommandQueueEntry* out, uint8_t maxEntries) const;

    // Statistics
    uint32_t getCommandsSent() const { return commandsSent; }
    uint32_t getCommandsAcked() const { return commandsAcked; }
    uint32_t getCommandsFailed() const { return commandsFailed; }
    uint32_t getCommandsTimeout() const { return commandsTimeout; }
    void resetStatistics();

    // Configuration
    void setMaxRetries(uint8_t maxRetries) { this->maxRetries = maxRetries; }

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
