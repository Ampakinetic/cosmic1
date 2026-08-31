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
//
// 09-01 bench recalibration (base40.log): 750 ms sat BELOW the stream's real
// cadence — a chunk costs ~600-700 ms end to end (240 B = 250 ms of E32 UART
// feed at 9600, plus the card read and the interleaved telemetry/beacon work),
// so ordinary inter-chunk jitter breached the gate every few chunks. The web
// UI's GET_STATUS (seq=3, base40.log:427-431) logged "transmit held" and then
// transmitted on the next jitter gap — the two chunks on air (13, 14) died,
// the 25-chunk thumbnail push ended 23/25, and the stall machinery burned a
// heal pass on them. 2000 ms clears the worst inter-chunk gap with margin
// while the inter-window pause (settle 500 + quiet 2000 + flight + balloon
// settle 500 ≈ 3-4 s) still opens it for requests, receipts, and polls.
static constexpr uint32_t CMD_TX_CHANNEL_QUIET_MS = 2000;

// Bound on the quiet hold: a sustained chunk stream can never strand a
// command indefinitely — at this bound the command transmits best-effort
// with a named log, and the honest D-05/D-07 terminal semantics apply to
// that attempt like any other.
//
// 09-01: raised 30000 -> 120000. A thumbnail push free-runs ~25 chunks
// (~18-20 s of chunk frames with no 2 s quiet gap), and a push interleaved
// with preempted window service stretches further — a 30 s bound GUARANTEED
// a best-effort transmit into a live stream, the exact collision class this
// gate exists to prevent (the 750 ms gate was already leaking; the bound
// would have made it unconditional). 120 s still bounds stranding well past
// any observed continuous stream.
static constexpr uint32_t CMD_TX_CHANNEL_HOLD_MAX_MS = 120000;

// IMAGE_ACK (0x15) receipt queue (image-transfer rework). Receipts are
// UNTRACKED: no ACK of the ACK, no retry — a lost receipt is recovered by
// the later receipts that supersede it (window-complete → kind-complete)
// and, worst case, by the balloon's duplicate-manifest COMPLETE-reply loop
// on the base side. Depth 8 covers a 225-chunk full (15 window ACKs +
// verdicts) comfortably across the settle gaps they drain through.
static constexpr uint8_t  IMG_ACK_TX_QUEUE_DEPTH = 8;

// A receipt older than this is dropped, never transmitted — the stream it
// described has moved on and a fresher receipt either sits newer in the
// queue or will be enqueued by the next window/finalize event.
static constexpr uint32_t IMG_ACK_TX_MAX_AGE_MS  = 10000;

// Queued receipt slot (image-transfer rework)
struct QueuedImageAck {
    bool used;
    uint32_t enqueuedMs;
    ImageAckBody body;
};

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

    // IMAGE_ACK receipt enqueue (image-transfer rework): dedupes by
    // (imageId, imageKind, windowBase, kind-scoped?) — a newer bitmap for
    // the same window replaces the queued one, because the newest receipt
    // is the truthful one. Returns false only when the arguments are
    // invalid (never blocks; a full queue drops the OLDEST receipt with a
    // named log — receipts supersede).
    bool enqueueImageAck(const ImageAckBody& body);

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
    // review-WR-03 framer inter-byte resync: millis() stamp of the last
    // received byte — an in-packet gap beyond CMD_FRAME_INTERBYTE_MS resets
    // the framer instead of wedging it mid-frame
    uint32_t lastFrameByteMs;

    // Channel-activity latch (G-01-7): millis() of the last inbound 0x13
    // chunk frame — the storm class that ate seq=6's 4 transmissions.
    // 0 = no chunk frame ever seen.
    uint32_t lastChunkFrameMs;

    // Latched GET_STATUS payload (D-26) — survives tracked-slot reuse
    ResponseStatusData latestStatus;
    bool hasStatusData;

    // IMAGE_ACK receipt queue (image-transfer rework)
    QueuedImageAck imageAckQueue[IMG_ACK_TX_QUEUE_DEPTH];

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
    // IMAGE_ACK receipt drain (image-transfer rework): at most ONE receipt
    // transmit per process() pass, gated on channelQuietForTx() — a receipt
    // must never be transmitted into an inbound chunk stream the balloon is
    // occupying (same G-01-7 discipline as tracked commands).
    void processImageAckQueue();
    void handleResponse(const ResponsePacket& response);
    void resetReceiveState();
    bool validatePacket(const uint8_t* buffer, size_t length);
};

// ===========================
// Global Instance Access
// ===========================

extern CommandSender& CmdSender();

#endif // COMMAND_SENDER_H
