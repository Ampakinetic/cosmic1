#ifndef IMAGE_TX_MANAGER_H
#define IMAGE_TX_MANAGER_H

#include <Arduino.h>
#include "e32_lora.h"
#include "camera_manager.h"
#include "command_protocol.h"
#include "image_protocol.h"

// ===========================
// Image TX Manager
// Balloon Unit - Pushes thumbnails, announces fulls, services window pulls,
// beacons telemetry
// Phase 2: Image Transmission (plan 02-01 push half + 02-02 pull half/beacon)
// ===========================
// Hybrid push/pull (D-17): after any capture (manual CAPTURE_NOW or interval),
// the module takes PSRAM ownership of the full + thumbnail buffers and pushes
// the thumbnail — manifest(kind=THUMBNAIL) followed by chunks paced ONE per
// process() pass (Pattern 4: the E32 transmit is synchronous and costs
// ~250-400 ms). When the thumbnail push completes, an armable full transfer
// emits its manifest(kind=FULL_IMAGE) ONCE and then serves chunks ONLY in
// response to base window requests (IMAGE_WINDOW_REQUEST), FIFO in capture
// order (D-19). Fulls larger than IMG_MAX_IMAGE_SIZE never arm — logged
// skip, thumbnail still pushes (research Q4 / PRI-03).
//
// TX arbitration (Pattern 6, PRI-01): at each process() call the fixed
// priority is (1) command responses — ahead by LOOP ORDER, since
// CmdHandler().process() runs before ImageTx().process() in
// processPacketHandling; (2) the 0x14 telemetry beacon when due (every
// TELEMETRY_BEACON_INTERVAL_MS); (3) one chunk transmit (push or window
// service). A beacon or response is never delayed by more than one chunk
// transmit. The beacon branch and the chunk branch are mutually exclusive
// within a pass — exactly one transmit per process() call.

// Per-entry transfer state (02-02 extends the 02-01 vocabulary; the push
// states are unchanged). After the thumbnail push completes, an entry with an
// armable full transfer emits the FULL_IMAGE manifest exactly ONCE
// (ANNOUNCE_FULL -> ANNOUNCED) and from then on serves chunks ONLY through a
// window context armed by handleWindowRequest — never free-runs (Pitfall 5:
// the half-duplex link is serialized by the base asking).
enum class ImageTxEntryState : uint8_t {
    IDLE = 0,                 // slot free
    PUSH_THUMB_MANIFEST,      // next transmit: the 0x12 thumbnail manifest
    PUSH_THUMB_CHUNKS,        // one 0x13 chunk per process() pass
    ANNOUNCE_FULL,            // thumbnail done; next transmit: the 0x12 FULL_IMAGE manifest (once)
    ANNOUNCED,                // full manifest emitted; serves chunks via window context only
    THUMB_PUSHED,             // parked: thumbnail done but full not armable (oversize / no buffer); eviction only
};

// Outcome of arming a window — mapped by CommandHandler to the existing
// ACK/NACK response machinery (ACK on ARMED; NACK_INVALID for an
// unknown/evicted image or out-of-bounds range; NACK_BUSY when another
// entry's window is mid-service — the base retries with its existing
// timeout/retry machinery).
enum class WindowRequestResult : uint8_t {
    ARMED = 0,
    UNKNOWN_IMAGE,            // no queued ANNOUNCED entry carries that image ID
    INVALID_RANGE,            // count == 0, count > IMG_WINDOW_MAX_CHUNKS, or startChunk >= totalChunks
    BUSY                      // another entry's window is mid-service
};

// Camera-settings snapshot copied from CameraManager cached getters at
// enqueue time (the manifest's 7-byte trailer)
struct ImageTxSettings {
    uint8_t resolution;   // FrameSize wire code (by-name mapping)
    uint8_t quality;
    int8_t  brightness;
    int8_t  contrast;
    int8_t  saturation;
    int8_t  exposure;
    uint8_t wbMode;
};

// Transfer-queue entry. Full + thumbnail buffers are PSRAM-owned COPIES taken
// at enqueue (Pitfall 7: the next capture's freeCurrentImage() must not pull
// bytes out from under a transfer).
struct ImageTxEntry {
    bool used;
    uint32_t enqueueSeq;      // monotonically increasing enqueue order (drop-oldest)
    uint16_t imageId;
    uint8_t captureSource;    // CaptureSource value
    uint32_t captureTimeMs;   // balloon millis at capture (from ImageData timestamp)
    ImageTxSettings settings;

    uint8_t* fullBuffer;      // PSRAM-owned copy of the full image (null when oversize/not armable)
    size_t fullLength;
    uint32_t fullCrc32;       // esp_rom_crc32_le over fullBuffer
    uint16_t fullTotalChunks; // ceil(fullLength / IMG_CHUNK_PAYLOAD_SIZE); 0 when not armable

    uint8_t* thumbBuffer;     // PSRAM-owned copy of the thumbnail (may be null)
    size_t thumbLength;
    uint32_t thumbCrc32;      // esp_rom_crc32_le over thumbBuffer
    uint16_t thumbTotalChunks;

    ImageTxEntryState state;
    uint16_t nextThumbChunk;  // 0-based index of the next chunk to push

    // Window context (D-21 pull half) — armed ONLY by handleWindowRequest.
    // Idempotent by construction (Pitfall 10): arming resets windowNextIndex
    // to windowStart, so a duplicate or re-requested window simply re-sends
    // the same indices; no ID allocation, no queue mutation.
    bool windowArmed;
    uint16_t windowStart;     // first chunk index of the armed window
    uint16_t windowCount;     // chunks in the armed window
    uint16_t windowNextIndex; // next chunk index to transmit

    uint32_t lastActivityMs;
};

class ImageTxManager {
public:
    ImageTxManager();
    ~ImageTxManager();

    // Initialization
    bool begin(E32LoRa* lora);
    void end();

    // Main processing - call from main loop AFTER CmdHandler().process() and
    // AutoCap().process() (PRI-01 arbitration half: command responses always
    // get the transmit opportunity before image traffic)
    void process();

    // Queue snapshot (logging + later plans' progress UI)
    static constexpr uint8_t QUEUE_DEPTH = IMG_TX_QUEUE_DEPTH;
    uint8_t getQueueCount() const;
    const ImageTxEntry* getEntry(uint8_t index) const { return (index < QUEUE_DEPTH) ? &entries[index] : nullptr; }

    // D-21 pull half (02-02): arm a window context on the queued ANNOUNCED
    // entry named by the payload. Decodes PayloadImageWindowRequest
    // (big-endian), validates imageId/range/count BEFORE arming (T-02-04),
    // implicitly evicts older entries (FIFO pull order — the base has moved
    // on), and returns a result the command handler maps to ACK/NACK.
    // Re-arming the same window is idempotent (Pitfall 10).
    WindowRequestResult handleWindowRequest(const uint8_t* payload, size_t len);

private:
    E32LoRa* lora;
    bool initialized;

    ImageTxEntry entries[QUEUE_DEPTH];
    uint32_t nextEnqueueSeq;
    uint16_t lastEnqueuedImageId;

    // Telemetry beacon state (PRI-01 / SC-5 — the 0x14 transmit side, 02-02
    // Task 3). lastBeaconMs uses the wraparound-safe subtraction idiom and is
    // advanced BEFORE each attempt (AutoCapture millis idiom) so a failed
    // transmit cannot drive a tight retry loop; the next due cycle retries.
    uint32_t lastBeaconMs;
    uint16_t beaconSeq;      // monotonically increasing, wraps at 65535
    bool firstBeaconLogged;  // transition-only logging: first beacon after boot

    // Poll side
    void enqueueCapture(uint16_t imageId);

    // Beacon side — at most ONE transmit per call; returns transmit success
    bool sendTelemetryBeacon();

    // Push/service side — at most ONE transmit per call
    void pushPending();
    ImageTxEntry* findActiveEntry();          // earliest entry with push work (thumb or full announcement)
    ImageTxEntry* findWindowServiceEntry();   // earliest ANNOUNCED entry with an armed, incomplete window

    // Eviction policy (bounded memory)
    void evictEntriesOlderThan(const ImageTxEntry& reference); // newer-ID window request (D-19)
    void sweepExpiredEntries();                                // IMG_ENTRY_TTL_MS idle timeout

    // Helpers
    bool fullTransferArmable(const ImageTxEntry& entry) const;
    static uint16_t chunksForSize(size_t lengthBytes);
    ImageTxEntryState completedThumbState(const ImageTxEntry& entry) const;
    ImageTxSettings snapshotSettings() const;
    bool pushThumbManifest(ImageTxEntry& entry);
    bool pushThumbChunk(ImageTxEntry& entry);
    bool announceFullManifest(ImageTxEntry& entry);
    bool serviceWindowChunk(ImageTxEntry& entry);
    void freeEntry(ImageTxEntry& entry);
};

// ===========================
// Global Instance Access
// ===========================

extern ImageTxManager& ImageTx();

#endif // IMAGE_TX_MANAGER_H
