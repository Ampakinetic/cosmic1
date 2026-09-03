#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
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

    // Mission provenance (feature: missions) — the base-stamps active
    // mission id at finalize; 0 = no mission was active
    uint16_t missionId;

    // Chunk accounting (bitmap truth from ImageRxManager)
    uint16_t chunksReceived;
    uint16_t chunksTotal;
    uint32_t bytesReceived;   // sum of accepted chunk payload bytes
    bool     complete;        // D-23: end-to-end CRC32 verified
    bool     crcMismatch;     // complete == false BECAUSE the CRC disagreed
    bool     storedToSd;      // SET BY SdStorage at finalize from its own
                              // persistence tracking — never caller-fabricated

    // Gallery detail (03-04): sidecar-parse presence mask — one
    // SD_SC_PRESENT_* bit per field the sidecar ACTUALLY carried, set only
    // by readSidecarMeta. The /gallery/{id} route serializes ONLY present
    // fields — absent fields are omitted, never zero-filled (D-47). Stays 0
    // when the struct is built for WRITING; finalizeImage ignores it.
    uint16_t present;
};

// Sidecar field-presence bits (03-04). The writer's telemetry triple
// (altitudeM/lat/lon) is all-numeric or all-null — one bit covers the
// triple, and telemetryValid doubles as the detail view's gpsValid (the
// sidecar records no separate GPS-fix flag; null telemetry IS the
// "GPS no fix" case).
static constexpr uint16_t SD_SC_PRESENT_CAPTURETIME = 0x0001;  // captureTimeMs
static constexpr uint16_t SD_SC_PRESENT_TRIGGER     = 0x0002;  // triggerSource
static constexpr uint16_t SD_SC_PRESENT_TELEMETRY   = 0x0004;  // altitude/lat/lon
static constexpr uint16_t SD_SC_PRESENT_CAMERA      = 0x0008;  // cameraSettings{}
static constexpr uint16_t SD_SC_PRESENT_CHUNKS      = 0x0010;  // chunks n/m
static constexpr uint16_t SD_SC_PRESENT_COMPLETE    = 0x0020;  // complete
static constexpr uint16_t SD_SC_PRESENT_STORED      = 0x0040;
static constexpr uint16_t SD_SC_PRESENT_MISSION     = 0x0080;  // missionId  // storedToSd

// ===========================
// Gallery Index (IMG-06, 03-04)
// ===========================

// One row of the boot-built RAM index over /images. Every flag is directory
// existence truth from the openNextFile() walk — nothing fabricated. The
// index is kept sorted DESCENDING by id (ids are unique and monotonically
// increasing in flight, so descending id = newest-first, a total stable
// order).
struct SdGalleryEntry {
    uint16_t id;
    bool     hasThumb;         // IMG_{id}_T.JPG exists
    bool     hasFull;          // IMG_{id}.JPG exists
    bool     hasThumbSidecar;  // IMG_{id}_T.JSON exists
    bool     hasFullSidecar;   // IMG_{id}.JSON exists
    uint32_t fullSize;         // IMG_{id}.JPG size in bytes (0 when absent)

    // Capture position (feature: capture markers) — filled by the
    // post-sort index pass reading one sidecar per image. hasGps is false
    // unless the sidecar carried a valid telemetry triple; the base
    // stamped lat/lon/alt at finalize from the then-latest beacon.
    bool     hasGps;
    int32_t  latE6;            // degrees * 1e6
    int32_t  lonE6;
    int16_t  altM;

    // Mission provenance (feature: missions) — 0 = captured outside any
    // mission or the sidecar predates the field
    uint16_t missionId;
};

// Index capacity — an EDITABLE constant. ~1000 images ≈ 5.5 h at the 20 s
// capture cadence; beyond it the NEWEST 1000 images show and older ids
// honestly leave the gallery (HONEST DEGRADATION, documented here: files
// are never deleted or recycled to widen the window — full history stays
// on the card, reachable by mounting it elsewhere).
static constexpr uint16_t SD_GALLERY_MAX_ENTRIES = 1000;

// Gallery page size (D-46: 12 per page — 3 columns at the 752px content width)
static constexpr uint8_t SD_GALLERY_PAGE_SIZE = 12;

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

    // Mount the card on the board's built-in SDMMC slot (1-bit mode, pins
    // from base_station_config.h via SD_MMC.setPins — see sd_storage.cpp).
    // Returns true even on failure — the station runs degraded without
    // storage (never halts); query isAvailable()/getStatus() for the honest
    // outcome.
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

    // Flush the kind's open write handle for (imageId, kind) if held. CR-02:
    // Arduino-ESP32 File I/O rides stdio with buffering, so a read-back
    // through a SECOND handle (stored-CRC verification opens its own FILE*)
    // cannot see bytes still sitting in the write handle's buffer — flush
    // before any such read-back or a fully-received image reads short and
    // fails verification. No-op when this id's handle is not the open one.
    void flushTransfer(uint16_t imageId, uint8_t kind);

    // Close the open file and write the sidecar IMG_{id}.JSON (or
    // IMG_{id}_T.JSON for THUMBNAIL — kind-suffixed so thumbnail and full
    // D-30 records persist independently, 02-05/WR-05) exactly ONCE at
    // finalization (Pitfall 8) as hand-built String JSON (the base env
    // deliberately has no ArduinoJson — research Supporting table).
    bool finalizeImage(const SdImageMetadata& meta);

    // Read-only open of a stored file for the HTTP route (server.streamFile).
    // Readable whenever the card mounted successfully — INCLUDING after a
    // mid-flight write degrade (existing files stay servable, WR-05); only a
    // failed mount (initFailed) or an absent file yields a false File —
    // absence is reported honestly, never fabricated.
    File serveFile(uint16_t imageId, uint8_t kind);

    // ---- Gallery enumeration (IMG-06, 03-04) ----

    // Walk /images once (openNextFile) into the RAM index, then sort
    // descending by id. Called once from begin() after the mount succeeds;
    // afterwards ONLY lazily via ensureIndexCurrent() when finalizeImage
    // advanced the version — pagination slices the index, never the
    // directory (Pitfall 5, T-03-11).
    void buildIndex();

    // Version counter bumped by EVERY finalizeImage (both kinds); the RAM
    // index records the version it was built at — differing values mean a
    // rebuild is due.
    uint32_t getIndexVersion() const { return indexVersion; }
    uint32_t getIndexedVersion() const { return indexedVersion; }

    // Rebuild the index when the finalize version advanced. Bounded work
    // (one directory walk), boot-warm — the gallery handlers call it at the
    // top of each request, NOT per poll tick.
    void ensureIndexCurrent();

    uint16_t getTotalCount() const;             // index size (≤ SD_GALLERY_MAX_ENTRIES)
    uint16_t getPageCount() const;              // ceil(total / 12); ≥ 1 (empty listing = page 1)

    // Copy entries [(pageIdx-1)*12 .. pageIdx*12-1] of the descending index
    // into dst (1-based pageIdx). Returns how many entries were copied —
    // exactly the page size except on the last page, so a seam id is never
    // duplicated or dropped.
    uint8_t getIndexPage(uint16_t pageIdx, SdGalleryEntry* dst, uint8_t max);

    // Linear lookup by id (index is RAM — no I/O). False = nothing on disk.
    bool findIndexEntry(uint16_t imageId, SdGalleryEntry* out) const;

    // Read IMG_{id}.JSON (or IMG_{id}_T.JSON when thumb) and parse the
    // hand-built JSON the writer emits. EVERY value is untrusted input (the
    // card is removable, T-03-09): bounded scans into fixed buffers, numeric
    // clamps on every parsed number, and meta.present bits recording which
    // fields the sidecar actually carried (absent fields omitted, never
    // zero-filled). Returns false only when the file is absent/unreadable —
    // a readable-but-garbage sidecar returns true with present == 0.
    bool readSidecarMeta(uint16_t imageId, bool thumb, SdImageMetadata* out);

    // Locked trigger vocabulary — shared by the sidecar writer and the
    // /gallery/{id} detail serializer so the two can never diverge.
    static const char* triggerSourceName(uint8_t captureSource);

    // ---- Explicit operator wipe (quick-260831) ----

    // The ONLY deletion surface on the base card: deletes every file inside
    // /images (JPGs and sidecars, walked flat — never recursed), resets the
    // RAM gallery index in place, and returns the removed count. Invoked
    // solely by the /sd-clear HTTP handler in main_basestation.cpp (behind
    // the dashboard's confirm dialog and the ImageRx busy gate).
    //
    // The disk-full policy above (NO file is ever deleted to make space)
    // is untouched: that rule bans SILENT/automatic deletion; this is a
    // deliberate, operator-confirmed wipe. The wipe never latches the
    // boot-permanent writeFailed degradation — the clearing guard on
    // openTransfer/writeChunk/writeSidecar returns false WITHOUT calling
    // degrade(), so transfers persist normally after the wipe.
    // Synchronous by design (single-threaded loop, mirrors the balloon
    // serial SDCLEAR precedent): a full card is a few hundred ms of deletes.
    uint16_t clearAllImages();

private:
    bool available;
    SdStorageStatus status;

    // Wipe-in-progress latch (quick-260831): true only inside the
    // synchronous clearAllImages() walk. openTransfer/writeChunk/
    // writeSidecar check it and fail fast WITHOUT degrade() — a transfer
    // racing the wipe must not set the boot-permanent status.writeFailed.
    bool clearing;

    // One open write handle per kind: at most one active full pull (FIFO)
    // and one in-flight thumbnail push can receive chunks at a time
    File     fullFile;
    uint16_t fullFileId;
    uint32_t fullPersistedBytes;
    File     thumbFile;
    uint16_t thumbFileId;
    uint32_t thumbPersistedBytes;

    // RAM gallery index (03-04): fixed static array sorted descending by id
    // (~24 B × 1000 with GPS fields = ~24 KB — static storage, no heap)
    SdGalleryEntry galleryIndex[SD_GALLERY_MAX_ENTRIES];
    uint16_t       galleryCount;     // live entries in galleryIndex
    uint32_t       indexVersion;     // bumped by finalizeImage (both kinds)
    uint32_t       indexedVersion;   // version the RAM index reflects

    // Merge one walked directory file into its id's index entry (creating
    // or evicting-at-cap as needed — the cap keeps the NEWEST entries).
    void mergeIntoIndex(uint16_t id, uint8_t galleryFlags, uint32_t fullSize);
    void sortIndexDescending();

    // Post-sort pass: one sidecar read per indexed image fills the capture
    // position (lat/lon/alt stamped at finalize). Paid once at boot so the
    // map's marker endpoint never touches sidecar IO per request.
    void fillIndexGps();

    void degrade(const char* reason);
    void fileFor(uint8_t kind, File** out, uint16_t** outId, uint32_t** outPersisted);
    static void imagePath(char* out, size_t cap, uint16_t imageId, uint8_t kind);
    static void sidecarPath(char* out, size_t cap, uint16_t imageId, uint8_t kind);
    bool writeSidecar(const SdImageMetadata& meta);
};

// ===========================
// Global Instance Access
// ===========================

extern SdStorage& SDStorage();

#endif // SD_STORAGE_H
