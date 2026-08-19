#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include "image_protocol.h"   // ImageKind (THUMBNAIL / FULL_IMAGE)

// ===========================
// SD Storage
// Base Station - Persists received images + sidecar metadata (IMG-05)
// Phase 2: Image Transmission (plan 02-03)
// ===========================
// Flat /images layout (D-32): IMG_{id}.JPG fulls, IMG_{id}_T.JPG thumbnails
// (D-29/D-31, ids zero-padded to 5), IMG_{id}.JSON sidecar written ONCE at
// finalization (D-30, Pitfall 8 — never per chunk). Chunks are written as
// they arrive via seek+offset (Pattern 5) — no whole-image RAM buffer, no
// pre-allocation from an untrusted manifest totalSize.
//
// Disk-full policy (Q5 — stop-storing-and-warn): an open() or write()
// failure degrades the module (isAvailable() false, reason surfaced via
// getStatus()); transfers keep completing in chunk accounting but are not
// persisted. NO file is ever deleted or overwritten to make space — flight
// history is never silently recycled. begin() failure leaves the station
// running without storage (degrade, never halt).

// Everything the D-30 sidecar records. Chunk statistics and completeness
// come from the ImageRxManager bitmap accounting — this module never
// fabricates any of them.
struct SdImageMetadata {
    uint16_t imageId;
    uint8_t  kind;            // ImageKind: THUMBNAIL or FULL_IMAGE
    uint8_t  captureSource;   // CaptureSource: what triggered the capture
    uint32_t captureTimeMs;   // balloon millis at capture (from the manifest)
    uint32_t receiptTimeMs;   // base millis at finalize (caller stamps)

    // Latest telemetry beacon at finalize (D-30: altitude + GPS position;
    // invalid-flagged when no beacon was ever received — never fabricated)
    bool     telemetryValid;
    float    altitudeM;
    float    lat;
    float    lon;

    // Camera settings at capture (the manifest's 7-byte trailer)
    uint8_t  resolution;      // FrameSize wire code
    uint8_t  quality;
    int8_t   brightness;
    int8_t   contrast;
    int8_t   saturation;
    int8_t   exposure;
    uint8_t  wbMode;

    // Chunk accounting (bitmap truth from ImageRxManager)
    uint16_t chunksReceived;
    uint16_t chunksTotal;
    uint32_t bytesReceived;   // sum of accepted chunk payload bytes
    bool     complete;        // D-23: end-to-end CRC32 verified
    bool     crcMismatch;     // complete == false BECAUSE the CRC disagreed
    bool     storedToSd;      // SET BY SdStorage at finalize from its own
                              // persistence tracking — never caller-fabricated
};

// Honest availability for the UI / /status (IN-03 discipline: computed
// truth, never a hardcoded OK). Three distinct, reachable states:
// OK (available), UNAVAILABLE (initFailed — no card / mount failed),
// FULL-or-write-failed (writeFailed — open/write failed mid-flight).
struct SdStorageStatus {
    bool available;    // mounted and no write failure since
    bool initFailed;   // begin() could not mount the card
    bool writeFailed;  // an open/write failed mid-flight (disk full / IO error)
};

class SdStorage {
public:
    SdStorage();

    // Mount the card on the dedicated SPI instance. Returns true even on
    // failure — the station runs degraded without storage (never halts);
    // query isAvailable()/getStatus() for the honest outcome.
    bool begin();

    // Honest availability: false until a successful begin(), false after a
    // mid-flight write failure (stop-storing-and-warn)
    bool isAvailable() const { return available; }
    SdStorageStatus getStatus() const { return status; }

    // Truncate-create /images/IMG_{id}.JPG (or IMG_{id}_T.JPG for
    // THUMBNAIL) and keep the handle open for seek writes. A re-open for
    // the same (id, kind) restarts the file; opening a different id closes
    // the previous partial file (kept on disk, never deleted). No-ops
    // (returns false) while degraded — accounting continues without storage.
    bool openTransfer(uint16_t imageId, uint8_t kind, uint32_t totalSize);

    // seek(chunkIndex * chunkSize) + write — out-of-order arrival lands at
    // the right offset (Pattern 5). The manifest's totalSize is never used
    // to pre-allocate. When the kind's handle holds a different id (a
    // thumbnail heal window competing with a newer push), the chunk's file
    // is re-opened NON-truncating instead of dropped — a drop made the
    // transfer finalize INCOMPLETE on SD despite 100% chunk reception
    // (CR-01). Returns false (and degrades) when the write fails.
    bool writeChunk(uint16_t imageId, uint8_t kind, uint16_t chunkIndex,
                    uint16_t chunkSize, const uint8_t* data, size_t len);

    // Close the open file and write the sidecar IMG_{id}.JSON (or
    // IMG_{id}_T.JSON for THUMBNAIL — kind-suffixed so thumbnail and full
    // D-30 records persist independently, 02-05/WR-05) exactly ONCE at
    // finalization (Pitfall 8) as hand-built String JSON (the base env
    // deliberately has no ArduinoJson — research Supporting table).
    bool finalizeImage(const SdImageMetadata& meta);

    // Read-only open of a stored file for the HTTP route (server.streamFile).
    // The returned File tests false when absent or storage is degraded —
    // absence is reported honestly, never fabricated.
    File serveFile(uint16_t imageId, uint8_t kind);

private:
    bool available;
    SdStorageStatus status;

    // One open write handle per kind: at most one active full pull (FIFO)
    // and one in-flight thumbnail push can receive chunks at a time
    File     fullFile;
    uint16_t fullFileId;
    uint32_t fullPersistedBytes;
    File     thumbFile;
    uint16_t thumbFileId;
    uint32_t thumbPersistedBytes;

    void degrade(const char* reason);
    void fileFor(uint8_t kind, File** out, uint16_t** outId, uint32_t** outPersisted);
    static void imagePath(char* out, size_t cap, uint16_t imageId, uint8_t kind);
    static void sidecarPath(char* out, size_t cap, uint16_t imageId, uint8_t kind);
    static const char* triggerSourceName(uint8_t captureSource);
    bool writeSidecar(const SdImageMetadata& meta);
};

// ===========================
// Global Instance Access
// ===========================

extern SdStorage& SDStorage();

#endif // SD_STORAGE_H
