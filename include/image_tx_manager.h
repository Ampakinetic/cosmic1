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
// armable full transfer emits the FULL_IMAGE manifest, consumed only on a
// SUCCESSFUL transmit (ANNOUNCE_FULL -> ANNOUNCED; a failed transmit retries
// on later process() passes, bounded by IMG_MANIFEST_MAX_ATTEMPTS —
// CR-02/WR-01, 01-14) and from then on serves chunks ONLY through a window
// context armed by handleWindowRequest — never free-runs (Pitfall 5: the
// half-duplex link is serialized by the base asking).
enum class ImageTxEntryState : uint8_t {
    IDLE = 0,                 // slot free
    PUSH_THUMB_MANIFEST,      // next transmit: the 0x12 thumbnail manifest (retries while transmits fail, bounded by IMG_MANIFEST_MAX_ATTEMPTS)
    PUSH_THUMB_CHUNKS,        // one 0x13 chunk per process() pass
    ANNOUNCE_FULL,            // thumbnail done; next transmit: the 0x12 FULL_IMAGE manifest (retries while transmits fail, bounded by IMG_MANIFEST_MAX_ATTEMPTS)
    ANNOUNCED,                // full manifest transmit SUCCEEDED; serves chunks via window context only
    THUMB_PUSHED,             // parked: thumbnail done but full not armable (oversize / no buffer); eviction only
    // SERVED (02-05 / CR-03 fix c): the FULL window whose clamped span reached
    // fullTotalChunks — the base has been offered every full chunk at least
    // once. The entry KEEPS both buffers (a tail-chunk loss must still be
    // healable via a re-request, which re-opens ANNOUNCED), but is a
    // PREFERRED eviction candidate under queue pressure.
    SERVED,
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
    // WR-08 (01-17): consecutive same-index transmit failures in the push
    // path. The cursor advances only on a successful transmit — or at
    // IMG_CHUNK_TX_RETRY_MAX with a named skip log; reset on any successful
    // transmit and in freeEntry.
    uint8_t thumbChunkFailStreak;

    // Window context (D-21 pull half) — armed ONLY by handleWindowRequest.
    // KIND-PARAMETERIZED (02-05/CR-01): windowKind names which of the entry's
    // two owned buffers the armed window serves (THUMBNAIL slices
    // thumbBuffer, FULL_IMAGE slices fullBuffer), validated against that
    // kind's totalChunks at arming. Idempotent by construction (Pitfall 10):
    // arming resets windowNextIndex to windowStart, so a duplicate or
    // re-requested window simply re-sends the same indices; no ID allocation,
    // no queue mutation.
    bool windowArmed;
    uint8_t  windowKind;      // ImageKind the armed window serves
    bool windowEverArmed;     // any window (either kind) has armed on this entry — marks the active-pull context, the LAST-resort overflow-eviction class (02-05 / CR-03 fix c)
    // G-01-9 defect C (01-17): a FULL window has armed on this entry — the
    // receipt signal that permanently stops FULL-manifest re-announces.
    // Distinct from windowEverArmed (which THUMBNAIL heals also set): a
    // thumb-healed entry whose FULL manifest was air-lost (image-15 class)
    // must KEEP re-announcing, so only a non-thumb arm sets this.
    bool fullWindowEverArmed;
    uint16_t windowStart;     // first chunk index of the armed window
    uint16_t windowCount;     // chunks in the armed window
    uint16_t windowNextIndex; // next chunk index to transmit
    // WR-08 (01-17): consecutive same-index transmit failures in the window
    // service path — same semantics as thumbChunkFailStreak (same-index
    // retry bounded by IMG_CHUNK_TX_RETRY_MAX, then a named skip; the SERVED
    // transition additionally requires a transmit-successful final chunk).
    uint8_t windowChunkFailStreak;
    // G-01-7 lever 3 (01-12): when this window context was (re-)armed — the
    // balloon-side RX-settle clock. Only the FIRST chunk of a freshly (re-)
    // armed window waits out IMG_WINDOW_RX_SETTLE_MS (a re-armed span
    // re-settles — exactly the retransmit-collision case); mid-window
    // continuation and the preempt clock are unaffected.
    uint32_t windowArmedAtMs;

    uint32_t lastActivityMs;

    // CR-02/WR-01 (01-14): bounded manifest-transmit attempts, shared by the
    // two mutually exclusive manifest phases (PUSH_THUMB_MANIFEST and
    // ANNOUNCE_FULL are sequential per entry — never simultaneous); reset to
    // 0 on each success and in freeEntry so a phase never inherits the
    // other's count. At IMG_MANIFEST_MAX_ATTEMPTS the kind is dropped
    // honestly (buffer freed, named log).
    uint8_t manifestAttempts;

    // G-01-9 defect C (01-17): FULL-manifest re-announces issued while the
    // entry is ANNOUNCED with no FULL window ever armed; reset in freeEntry.
    // Bounded by IMG_FULL_REANNOUNCE_MAX — each transmit attempt counts
    // regardless of TX verdict (air loss is the class being treated); at the
    // bound the full is dropped with a named log (park at THUMB_PUSHED
    // keeping thumbBuffer for heals).
    uint8_t reannounceAttempts;

    // G-01-7 burst full-delivery, balloon lever 1 (01-21): receipt-evidence
    // stamp — an inbound window request that MATCHED this entry (either kind:
    // proof the base demonstrably holds at least one of its manifests and is
    // asking). Nonzero re-arms the bounded re-announce budget and ranks the
    // entry in the protected last-resort eviction class alongside
    // windowEverArmed; 0 = never requested. Reset in freeEntry.
    uint32_t lastWindowRequestMs;
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

    // Beacon health for the OLED status screen (status_display) — read-only
    // views of the transmit-side truth the serial log already prints
    uint16_t getBeaconSeq() const { return beaconSeq; }
    uint32_t getBeaconsSent() const { return beaconsSent; }
    bool getLastBeaconOk() const { return lastBeaconOk; }
    uint32_t getBeaconAgeMs() const { return millis() - lastBeaconMs; }

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
    uint32_t beaconsSent;    // successful transmits (OLED diagnostics)
    bool lastBeaconOk;       // last attempt's transmit result

    // G-01-7 burst full-delivery, balloon lever 1 (01-21): receipt-informed
    // re-announce gating. lastInboundWindowRequestMs is the channel-liveness
    // stamp — EVERY inbound window request refreshes it, any kind, any verdict
    // including the unknown/evicted rejects (session-6's rejects were exactly
    // the evidence that the base was still working the burst while the
    // drop-clock expired). Write-only timing data, never used for content
    // decisions (T-01-21-01). While it is younger than
    // IMG_FULL_REANNOUNCE_BUSY_MS the idle-slot re-announce is HELD (nothing
    // consumed); reannounceHoldLogged is the one-shot latch printing one hold
    // line per episode, cleared whenever a re-announce actually transmits.
    uint32_t lastInboundWindowRequestMs;
    bool reannounceHoldLogged;

    // G-01-11 / WINDOWS 16 fix (01-25): receipt-ever flag — true only after a
    // GENUINE inbound window request has been observed since boot. The zero
    // stamp alone let the busy-hold gate fire once per boot window
    // (millis() - 0 < IMG_FULL_REANNOUNCE_BUSY_MS, balloon.log:504), polluting
    // the G-01-7 discriminator with a phantom "inbound window traffic active"
    // episode before any traffic existed. The flag makes the zero stamp inert;
    // the hold line itself is NOT muted — it keeps its evidentiary meaning.
    bool inboundWindowRequestSeen;

    // G-01-10 D1 debug instrumentation (01-25), lever 2 per
    // .planning/debug/d1-crash-regression-push-start.md §3: bounded memory
    // state. logMemDiagnostic() prints at each enqueueCapture (the exact
    // session-7 crash phase — one line per capture) and, from process() on a
    // 1 s throttle, only on a NEW monotone low of min-ever-internal-heap or
    // loopTask stack high-water. REMOVAL CONDITION: strip both call sites and
    // this block after the 01-27 bench closes G-01-10.
    uint32_t memDiagLastMs;
    uint32_t memDiagMinHeap;
    UBaseType_t memDiagMinStackHw;

    // Poll side
    void enqueueCapture(uint16_t imageId);

    // G-01-10 D1 instrumentation (01-25): one bounded [MEM] line — internal
    // heap, lifetime-min internal heap, PSRAM free, loopTask stack high-water
    // (words). Called per capture enqueue and on new monotone lows only.
    void logMemDiagnostic(const char* phase);

    // Beacon side — at most ONE transmit per call; returns transmit success
    bool sendTelemetryBeacon();

    // Push/service side — at most ONE transmit per call
    void pushPending();
    ImageTxEntry* findActiveEntry();          // earliest entry with push work (thumb or full announcement)
    ImageTxEntry* findWindowServiceEntry();   // earliest entry with an armed, incomplete window (ANNOUNCED, or THUMB_PUSHED with a heal window armed)
    // G-01-9 defect C (01-17): earliest ANNOUNCED full with no FULL window
    // ever armed (receipt never observed) idle past IMG_FULL_REANNOUNCE_IDLE_MS
    ImageTxEntry* findReannounceCandidate();

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
    // G-01-9 defect C (01-17): bounded idle FULL-manifest re-announce with a
    // named drop at the bound (park-and-free mirroring the 01-14 announce
    // bound); fires only in pushPending's idle slot
    void reannounceFullManifest(ImageTxEntry& entry);
    // Shared 0x12 FULL body construction — the one-shot announce and the
    // re-announce call the same helper (one construction site, no wire change)
    static ImageManifestBody fillFullManifestBody(const ImageTxEntry& entry);
    bool serviceWindowChunk(ImageTxEntry& entry);
    void freeEntry(ImageTxEntry& entry);
};

// ===========================
// Global Instance Access
// ===========================

extern ImageTxManager& ImageTx();

#endif // IMAGE_TX_MANAGER_H
