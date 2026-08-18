#ifndef IMAGE_TX_MANAGER_H
#define IMAGE_TX_MANAGER_H

#include <Arduino.h>
#include "e32_lora.h"
#include "camera_manager.h"
#include "command_protocol.h"
#include "image_protocol.h"

// ===========================
// Image TX Manager
// Balloon Unit - Pushes captured-image thumbnails over the E32 link
// Phase 2: Image Transmission (plan 02-01, IMG-01 push half)
// ===========================
// Push-only in this plan: after any capture (manual CAPTURE_NOW or interval),
// the module takes PSRAM ownership of the full + thumbnail buffers and pushes
// the thumbnail — manifest(kind=THUMBNAIL) followed by chunks paced ONE per
// process() pass (Pattern 4: the E32 transmit is synchronous and costs
// ~250-400 ms). Full-image announcement (kind=FULL_IMAGE manifest) and window
// servicing arrive in 02-02 as an EXTENSION of this entry struct, not a
// rework.

// Per-entry transfer state. The vocabulary is final architecture for the
// phase; states beyond THUMB_PUSHED are wired by 02-02.
enum class ImageTxEntryState : uint8_t {
    IDLE = 0,                 // slot free
    PUSH_THUMB_MANIFEST,      // next transmit: the 0x12 thumbnail manifest
    PUSH_THUMB_CHUNKS,        // one 0x13 chunk per process() pass
    THUMB_PUSHED,             // thumbnail stream complete; awaiting 02-02 window servicing
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

    uint8_t* fullBuffer;      // PSRAM-owned copy of the full image
    size_t fullLength;
    uint32_t fullCrc32;       // esp_rom_crc32_le over fullBuffer

    uint8_t* thumbBuffer;     // PSRAM-owned copy of the thumbnail (may be null)
    size_t thumbLength;
    uint32_t thumbCrc32;      // esp_rom_crc32_le over thumbBuffer
    uint16_t thumbTotalChunks;

    ImageTxEntryState state;
    uint16_t nextThumbChunk;  // 0-based index of the next chunk to push
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

private:
    E32LoRa* lora;
    bool initialized;

    ImageTxEntry entries[QUEUE_DEPTH];
    uint32_t nextEnqueueSeq;
    uint16_t lastEnqueuedImageId;

    // Poll side
    void enqueueCapture(uint16_t imageId);

    // Push side — at most ONE transmit per call
    void pushPending();
    ImageTxEntry* findActiveEntry();

    // Helpers
    ImageTxSettings snapshotSettings() const;
    bool pushThumbManifest(ImageTxEntry& entry);
    bool pushThumbChunk(ImageTxEntry& entry);
    void freeEntry(ImageTxEntry& entry);
};

// ===========================
// Global Instance Access
// ===========================

extern ImageTxManager& ImageTx();

#endif // IMAGE_TX_MANAGER_H
