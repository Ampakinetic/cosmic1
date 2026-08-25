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

// G-01-7 residual (session 5, base5.log:55/:73/:81/:92): a 16-byte command
// frame transmitted while inbound 217-byte 0x13 chunk frames flowed lost all
// 4 transmissions (original + retries 1/3, 2/3, 3/3) — a half-duplex channel
// the balloon is occupying gives a command transmitted into it near-zero
// landing probability, and the honest 3-retry budget burned in ~16 s of
// storm. Command transmits (first attempt and timeout retry) therefore wait
// for the quiet gap that necessarily follows every window/push completion
// (windows complete, pushes drain; the RX settle + request/ACK round trip
// at window boundaries is the landing slot — far above this threshold).
static constexpr uint32_t CMD_TX_CHANNEL_QUIET_MS = 750;

// Bound on the quiet hold: a sustained chunk stream can never strand a
// command indefinitely — at this bound the command transmits best-effort
// with a named log, and the honest D-05/D-07 terminal semantics apply to
// that attempt like any other.
static constexpr uint32_t CMD_TX_CHANNEL_HOLD_MAX_MS = 30000;

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
    uint32_t channelHoldStartMs;   // G-01-7 quiet gate: hold-episode start (0 = not currently held)
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

    // Latest GET_STATUS snapshot (D-26): latched from the newest STATUS
    // response — the balloon-reported truth the event-threshold card
    // displays. Returns false when no STATUS response ever arrived.
    bool getStatusData(ResponseStatusData& out) const {
        if (!hasStatusData) {
            return false;
        }
        out = latestStatus;
        return true;
    }

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

    // Channel-activity latch (G-01-7): millis() of the last inbound 0x13
    // chunk frame — the storm class that ate seq=6's 4 transmissions.
    // 0 = no chunk frame ever seen.
    uint32_t lastChunkFrameMs;

    // Latched GET_STATUS payload (D-26) — survives tracked-slot reuse
    ResponseStatusData latestStatus;
    bool hasStatusData;

    // Private methods
    TrackedCommand* findTrackedCommand(uint16_t sequenceNumber);
    TrackedCommand* findOldestCommand();
    TrackedCommand* findFreeSlot();
    // Channel-quiet transmit gate (G-01-7): decides whether a command frame
    // may occupy the half-duplex channel right now
    bool channelQuietForTx() const;
    bool canTransmitNow(TrackedCommand* cmd, uint32_t now);
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
