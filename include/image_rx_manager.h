#ifndef IMAGE_RX_MANAGER_H
#define IMAGE_RX_MANAGER_H

#include <Arduino.h>
#include "command_protocol.h"
#include "image_protocol.h"

// ===========================
// Image RX Manager
// Base Station - Reassembles pushed image streams, pulls full images with
// windowed ARQ (D-21..D-24), persists via SDStorage, tracks telemetry beacons
// Phase 2: Image Transmission (02-01 push half; 02-03 pull/ARQ half)
// ===========================
// CommandSender::processIncomingByte validates every frame (type dispatch +
// CRC16 + end marker) and forwards complete 0x12/0x13/0x14 frames here.
// Manifests are validated BEFORE allocating (Pitfall 11). Every transfer —
// pushed thumbnail or pulled full — is ONE slot kind with the SAME
// reliability machinery: an exact chunk bitmap, a window context, a bounded
// pass counter, and finalize-with-verification (D-22: no parallel
// best-effort path exists anywhere).
//
// The base speaks on the link ONLY when (Pitfall 5, half-duplex discipline):
//   - a manifest arrived while no pull was active (first window request),
//   - a window's slice completed (advance to the next span), or
//   - a stall fired (IMG_WINDOW_STALL_MS with no chunk progress).
// There are NO free-running request timers. Window requests ride the Phase 1
// tracked-command machinery (CmdSender().sendCommand with the
// CMD_ACK_TIMEOUT_WINDOW_MS class); the terminal-state and duplicate guards
// apply unchanged.

// Locked transfer-state vocabulary (D-20) — the ONLY state vocabulary any
// consumer (/status JSON, the UI panel) ever sees; the single
// transferStateToString mapping below is shared by all of them. States are
// DERIVED at snapshot time from the chunk bitmap, pass counter, and terminal
// flags — never stored, never fabricated.
enum class TransferDisplayState : uint8_t {
    QUEUED = 0,   // full-image pull waiting for its FIFO turn (D-19)
    RECEIVING,    // chunks arriving (thumbnail push or active window stream)
    RETRYING,     // a stall re-request has fired (passCount > 0)
    COMPLETE,     // all chunks present AND end-to-end CRC32 verified (D-23)
    INCOMPLETE    // D-24 pass bound exceeded, CRC mismatch, or not stored
};

const char* transferStateToString(TransferDisplayState state);

// G-01-7 burst full-delivery (01-22, base RX half): bound on the 01-12
// serialization hold. A user-requested FULL pull must not wait behind burst
// thumbnail serialization longer than this before it activates (arms a FULL
// window on the balloon) — arming promotes the entry to the protected
// eviction class (windowEverArmed/lastWindowRequestMs). Image-transfer
// rework: the balloon re-announce clock this deadline once coordinated is
// gone (FULL is request-driven); the deadline survives as the heal-before-
// pull ordering bound. Outside this bound the 01-12 hold stands.
static constexpr uint32_t IMG_FULL_ARM_DEADLINE_MS = 20000;

// Duplicate-manifest COMPLETE reply rate (image-transfer rework): when a
// re-manifest names a row that is already terminal+complete, the base
// ignores it and answers one rate-limited KIND_COMPLETE_CRC_OK IMAGE_ACK so
// a rebooted balloon can learn the kind is delivered and skip re-pushing
// it. 30 s bounds the reply cadence against a pathological announce storm
// while staying far below any plausible re-boot interval.
static constexpr uint32_t IMG_COMPLETE_REACK_RATE_MS = 30000;

// User-requested FULL pull (image-transfer rework): an IMAGE_FULL_REQUEST
// (0x32) that has produced no FULL manifest within this window re-sends
// (bounded), because the request or its manifest may have air-lost.
static constexpr uint32_t IMG_FULL_REQUEST_RETRY_MS    = 15000;
// Abandon the request after this many sends — the UI shows the honest
// failure (PRI-03: never a silent dead button).
static constexpr uint8_t  IMG_FULL_REQUEST_MAX_ATTEMPTS = 3;

// Outstanding user FULL request (image-transfer rework): set by
// requestFullImage, consumed by startTransfer when the matching FULL
// manifest arrives (that slot becomes the userRequested pull).
struct PendingFullRequest {
    bool     valid;
    uint16_t imageId;
    uint32_t requestedMs;   // millis() of the last send
    uint8_t  attempts;      // sends so far
    uint16_t seq;           // tracked-command seq of the last send (0 = none)
};

// D-20 progress row — every field derives from real chunk-bitmap accounting
struct TransferRow {
    uint16_t imageId;
    uint8_t  kind;            // ImageKind value (THUMBNAIL / FULL_IMAGE)
    uint16_t receivedChunks;
    uint16_t totalChunks;
    uint8_t  percent;         // receivedChunks * 100 / totalChunks (bitmap truth)
    TransferDisplayState state;
    bool     requested;       // image-transfer rework: FULL slot marked
                              // userRequested (drives the UI's Fetch-full
                              // button state)
};

// In-flight or terminal transfer: one manifest + its exact accounting.
// Terminal slots keep their counters for the D-20 rows until slot pressure
// recycles them; the bitmap/buffer work memory is freed at finalize.
struct ImageRxTransfer {
    bool used;
    uint32_t arrivalSeq;      // manifest arrival order — FIFO pull order (D-19)
    uint32_t manifestArrivedMs;   // millis() at FULL-manifest acceptance — the
                                  // arm-deadline clock (IMG_FULL_ARM_DEADLINE_MS,
                                  // 01-22); zero on slot reset. Stamped for every
                                  // manifest; consumed only on the FULL path.
    uint16_t imageId;
    uint8_t  imageKind;       // ImageKind value from the manifest
    uint8_t  captureSource;   // CaptureSource value from the manifest
    uint32_t totalSize;       // validated <= MAX_IMAGE_SIZE (Pitfall 11)
    uint16_t chunkSize;       // validated <= IMG_CHUNK_PAYLOAD_SIZE
    uint16_t totalChunks;     // validated == ceil(totalSize / chunkSize)
    uint32_t crc32;           // D-23 end-to-end CRC over this kind's bytes
    uint32_t captureTimeMs;
    // Camera-settings trailer from the manifest
    uint8_t  resolution;
    uint8_t  quality;
    int8_t   brightness;
    int8_t   contrast;
    int8_t   saturation;
    int8_t   exposure;
    uint8_t  wbMode;

    uint8_t* buffer;          // RAM reassembly — THUMBNAIL slots only (the
                              // 02-01 retention path; fulls stream to SD per
                              // Pattern 5, no whole-image RAM buffer)
    uint8_t* chunkPresent;    // one flag byte per chunk — THE bitmap
    uint16_t receivedCount;   // distinct chunks accepted
    uint32_t bytesReceived;   // accepted payload bytes (sidecar statistic)
    uint8_t  passCount;       // stall-triggered re-request passes (D-24 bound)
    uint32_t lastProgressMs;  // last accepted chunk, or last issued request

    // D-21 window context: the span of the last successfully QUEUED
    // IMAGE_WINDOW_REQUEST for this transfer
    bool     windowActive;
    uint16_t windowBase;
    uint16_t windowCount;
    // Defer-aware D-24 (G-01-7 residual, 01-15): the tracked-command
    // sequence of the last successfully QUEUED IMAGE_WINDOW_REQUEST for
    // this transfer; 0 means none. A recycled terminal command slot reads
    // as IDLE, which the in-flight check treats as terminal — correct,
    // because only terminal commands are recycled (findFreeSlot).
    uint16_t windowRequestSeq;

    bool     pullActive;      // FULL only: this slot is THE active pull
    bool     terminal;        // finalized (COMPLETE or INCOMPLETE)
    bool     complete;        // terminal AND D-23 CRC verified
    bool     crcMismatch;     // terminal, incomplete because CRC disagreed
    bool     notStored;       // terminal, incomplete because SD was degraded
                              // (chunks received, but no stored bytes exist
                              // to verify — never reported complete)

    // Image-transfer rework: a FULL slot only ever activates its pull when
    // the user asked for it (requestFullImage stamps this; "Fetch full" in
    // the UI). Non-requested FULL manifests stay QUEUED discovery rows.
    bool     userRequested;
    // millis() of the last rate-limited KIND_COMPLETE reply sent for a
    // duplicate manifest on this row (IMG_COMPLETE_REACK_RATE_MS)
    uint32_t lastCompleteAckMs;
};

// Snapshot of the newest verified 0x14 telemetry beacon. Absent telemetry is
// reported as absent (valid == false) — never fabricated.
struct TelemetrySnapshot {
    bool     valid;        // at least one beacon ever received
    uint32_t receivedMs;   // millis() at the last beacon
    uint16_t seq;
    float    altitudeM;    // altitudeCm / 100
    float    tempC;        // tempCentiC / 100
    float    lat;          // latE6 / 1e6
    float    lon;          // lonE6 / 1e6
    bool     gpsValid;     // flags bit0
    uint16_t batteryMv;    // batteryMilliV — truth only while batteryValid
    bool     batteryValid; // flags bit1
};

class ImageRxManager {
public:
    ImageRxManager();
    ~ImageRxManager();

    // Initialization
    bool begin();
    void end();

    // Frame entry points — CommandSender hands over the COMPLETE framed
    // packet after type dispatch, length framing, and CRC validation. The
    // frame is borrowed for the call only; these never touch the
    // tracked-command table directly (0x12/0x13/0x14 are unsolicited data
    // frames — window REQUESTS are issued separately through CmdSender).
    void onManifestFrame(const uint8_t* frame, size_t length);
    void onChunkFrame(const uint8_t* frame, size_t length);
    void onTelemetryBeaconFrame(const uint8_t* frame, size_t length);

    // Main processing — call from the base loop after CmdSender().process();
    // drives the active pull's window state machine (advance / stall /
    // D-24 bound) and the thumbnail stall fallback (D-22)
    void process();

    // Newest CRC-verified thumbnail. Id 0 means none has been verified yet;
    // the buffer stays owned here and is valid until the next verified
    // thumbnail replaces it.
    uint16_t getLatestThumbId() const { return latestThumbId; }
    const uint8_t* getLatestThumbData() const { return latestThumbBuffer; }
    size_t getLatestThumbLength() const { return latestThumbLength; }

    // User-requested FULL pull (image-transfer rework — the "Fetch full"
    // button). Returns false ONLY when the full is already terminal+complete
    // (the UI answers 409): an existing non-terminal slot is marked
    // userRequested in place (no wire traffic — its manifest is in hand),
    // anything else sends IMAGE_FULL_REQUEST (0x32) to the balloon.
    bool requestFullImage(uint16_t imageId);

    // Bounded retry/abandon driver for the pending request — call from
    // process(). startTransfer consumes the pending request when the
    // matching FULL manifest arrives.
    void processFullRequest();

    // Live pending-request view for /api/state (invalid → the UI shows no
    // outstanding request).
    const PendingFullRequest& getPendingFullRequest() const { return pendingFullRequest; }

    // Telemetry beacon state (absent telemetry reported as absent)
    const TelemetrySnapshot& getTelemetrySnapshot() const { return telemetry; }

    // D-20: live transfer snapshot — fills out with one row per used slot in
    // manifest-arrival order (stable FIFO view); returns the row count.
    // Every value derives from the bitmap/pass/terminal state at call time.
    static constexpr uint8_t RX_TRANSFER_SLOTS = 8;
    uint8_t getTransferSnapshot(TransferRow* rows, uint8_t maxRows) const;

    // The /sd-clear busy gate (quick-260831): true while ANY slot holds a
    // non-terminal transfer. QUEUED/RECEIVING/RETRYING rows may still write
    // chunks or sidecars to the card; terminal rows write nothing more.
    // Read on the single-threaded loop by the HTTP handler BEFORE any
    // deletion — the wipe refuses while this is true.
    bool anyTransferActive() const {
        for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
            if (transfers[i].used && !transfers[i].terminal) {
                return true;
            }
        }
        return false;
    }

private:
    bool initialized;

    ImageRxTransfer transfers[RX_TRANSFER_SLOTS];
    uint32_t nextArrivalSeq;

    // G-01-7 lever 1 (01-12): log-once latch for the activation-hold message
    // — the hold logs once per held image id, not once per process() pass
    uint16_t healHoldLoggedId;

    // Defer-aware D-24 (01-15): log-once latch for the stall-deferred
    // message — the defer logs once per window-request sequence, not once
    // per process() pass
    uint16_t deferSkipLoggedSeq;

    // Retained newest verified thumbnail (plain heap — served by the web
    // server from the single-threaded loop, so no locking is needed)
    uint16_t latestThumbId;
    uint8_t* latestThumbBuffer;
    size_t   latestThumbLength;

    // Outstanding user FULL request (image-transfer rework)
    PendingFullRequest pendingFullRequest{};

    TelemetrySnapshot telemetry;

    // Slot handling
    ImageRxTransfer* findTransfer(uint16_t imageId, uint8_t kind);
    ImageRxTransfer* findActivePull();
    // Defer-aware D-24 (G-01-7 residual, 01-15): true while the tracked
    // command behind t.windowRequestSeq is still non-terminal (PENDING or
    // SENT) — the request has had no transfer opportunity yet, so a stall
    // must extend the clock, never charge a D-24 pass
    bool windowRequestInFlight(const ImageRxTransfer& t) const;
    // G-01-7 lever 1 (01-12): the earliest non-terminal THUMBNAIL slot with
    // holes whose same-id FULL manifest already arrived (the gate-2 proof the
    // thumbnail push finished) — while one exists, full-pull activation is
    // HELD so the thumbnail's heal completes first (serialization). Returns
    // nullptr when no such slot exists.
    ImageRxTransfer* pendingHealThumbnail();
    ImageRxTransfer* allocateSlot(uint8_t kind);
    void releaseSlotWork(ImageRxTransfer& t);   // free bitmap + working buffer

    // Transfer handling
    void startTransfer(const ImageManifestBody& m);
    bool acceptChunk(ImageRxTransfer& t, const ImageChunkBody& c);
    void finalizeTransfer(ImageRxTransfer& t);  // all chunks present: verify
    // D-24. crcRejected (image-transfer rework): true ONLY when the
    // termination was a CRC rejection — the one case where a KIND_FAILED_CRC
    // receipt must invalidate the balloon's persisted ACK state. Stall /
    // pass-exhaustion terminations leave held chunks as true receipts.
    void finalizeIncomplete(ImageRxTransfer& t, const char* reason,
                            bool crcRejected = false);
    void writeSidecarFor(const ImageRxTransfer& t, bool complete,
                         bool crcMismatch, bool notStored);

    // Window driver (D-21)
    void activateNextPull();
    void issueWindowRequest(ImageRxTransfer& t, uint16_t startChunk, uint16_t count);
    bool windowSliceComplete(const ImageRxTransfer& t) const;
    uint16_t firstMissingChunk(const ImageRxTransfer& t, uint16_t from) const;
    uint16_t lastMissingChunk(const ImageRxTransfer& t, uint16_t from, uint16_t to) const;
    bool verifyStoredCrc32(const ImageRxTransfer& t, uint32_t& crcOut);

    void freeLatest();
};

// ===========================
// Global Instance Access
// ===========================

extern ImageRxManager& ImageRx();

#endif // IMAGE_RX_MANAGER_H
