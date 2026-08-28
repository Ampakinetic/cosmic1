#ifndef SD_STORE_BALLOON_H
#define SD_STORE_BALLOON_H

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include "image_protocol.h"   // ImageKind, IMG_CHUNK_PAYLOAD_SIZE

// Forward declaration — persistCapture takes a const reference only; the
// 7-byte layout is consumed in the .cpp (which includes image_tx_manager.h).
struct ImageTxSettings;

// ===========================
// Balloon SD Card Store
// Balloon Unit - FAT32 flight-archive store for captured images (Phase 2.5)
// ===========================
// STORE-01/D-01: every capture persists IMG_{id}.JPG (full) +
// IMG_{id}_T.JPG (thumbnail) + IMG_{id}.META (commit record) to the balloon
// card BEFORE any 0x12 manifest transmits — the manifest can never precede
// the file. STORE-03: window/thumbnail chunk service reads its bytes from
// those card files via readChunk; the normal path holds NO PSRAM image copy
// (the RAM queue is a small index over card bytes).
//
// Operator decisions locked 2026-08-29 (CONTEXT.md):
//   D-01  transport mirrors the base's proven idiom — built-in slot,
//         SD_MMC 1-bit mode, CLK=39 / CMD=38 / D0=40, 20 MHz — defined here
//         as balloon-side constants (base_station_config.h stays base-only).
//   D-03  the volatile PSRAM queue survives ONLY as the honestly-labeled
//         SD-write-failure fallback in ImageTxManager; this module is the
//         sole normal path.
//   Keep-everything: the card is a flight archive. NOTHING here ever
//   deletes, truncates, or overwrites an existing image to reclaim space —
//   only the manual serial clear (explicit CONFIRM token, later plan) may
//   delete. A torn capture (JPGs without a validating META) is honestly
//   skipped by the boot rescan (plan 02.5-02), never fabricated.
//
// Transport note (mirrors src/sd_storage.cpp): custom pins REQUIRE
// SD_MMC.setPins() before begin() on GPIO-matrix targets (arduino-esp32
// 3.x) — the SDMMCFS constructor only loads pin defaults when the variant
// defines BOARD_HAS_SDMMC, which esp32-s3-devkitc-1 does not. 1-bit mode
// (the slot wires only D0) at SDMMC_FREQ_DEFAULT (20 MHz): transfers are
// paced by the 9.6 kbps air rate, so the lower documented-stable clock is
// the right lever for marginal slots.
//
// begin() degrades, never halts (1223f46 lesson / SdStorage idiom): on a
// failed mount the balloon keeps flying and captures take the volatile
// fallback in ImageTxManager; the verdict is exposed honestly through
// isAvailable()/getStatus().

// Balloon-side SD slot pins (D-01, operator decision locked 2026-08-29 —
// same board family as the base's datasheet-verified built-in slot;
// provenance: include/base_station_config.h SD_*_PIN 39/38/40). Deliberately
// NOT included from base_station_config.h — that header is base-env-only.
static constexpr uint8_t SD_STORE_CLK_PIN  = 39;
static constexpr uint8_t SD_STORE_CMD_PIN  = 38;
static constexpr uint8_t SD_STORE_DATA_PIN = 40;

// Flat image directory on the balloon card (mirrors the base's D-32 layout
// convention; mkdir is idempotent on FAT)
static constexpr const char* SD_STORE_DIR = "/images";

// META commit-record magic ("M1"). Validated on EVERY record read — a
// torn/hostile record is skipped, never zero-filled into a trusted entry
// (T-02.5-01).
static constexpr uint16_t SD_STORE_META_MAGIC = 0x4D31;

// Free-space headroom required ABOVE the capture's own bytes at persist
// time: the refusal verdict must leave room for the next capture's
// thumbnail-scale writes too, not merely this one (card-full must be a
// stable, honest state, not a one-byte-from-the-edge flapper).
static constexpr uint32_t SD_STORE_MIN_FREE_BYTES = 65536;

// Worst-case capture reservation for the pre-capture card-full gates
// (plan 02.5-03, STORE-04): the full image's wire cap (IMG_MAX_IMAGE_SIZE,
// image_protocol.h) plus the thumbnail cap (camera_manager.cpp
// THUMB_MAX_BYTES 8192 — the createThumbnail bound, mirrored here so both
// gate call sites share one value; this header is the only shared surface
// the balloon env's gate callers need). A refusal on estimate is honest —
// the gate spends no camera/air resources on a capture that cannot persist;
// persistCapture's identical gate remains the binding check.
static constexpr uint32_t SD_STORE_CAPTURE_ESTIMATE_FULL =
    IMG_MAX_IMAGE_SIZE;
static constexpr uint32_t SD_STORE_CAPTURE_ESTIMATE_THUMB = 8192;

// Per-image delivery bits in BalloonCaptureRecord::flags (written in place
// by markDelivered — plan 02.5-02's boot rescan consumes them). Zero at
// capture time. SD_ST_DELIV_THUMB is set either by a completed thumbnail
// push OR at persist time when the record carries no thumbnail at all
// (thumbLength 0 — the delivery obligation is vacuous, retired immediately),
// so the boot rescan judges such a record purely on the full's bit.
static constexpr uint8_t SD_ST_DELIV_THUMB = 0x01;   // thumbnail push provably completed (or no thumbnail owed)
static constexpr uint8_t SD_ST_DELIV_FULL  = 0x02;   // full window service provably completed

// Outcome of persistCapture — branched on by ImageTxManager::enqueueCapture:
//   PERSISTED  -> file-backed entry (null buffers, card is the byte source)
//   CARD_FULL  -> honest refusal, NO entry, never the volatile fallback
//                 (the fallback must not become the normal path for a full
//                 card — locked keep-everything decision)
//   IO_ERROR   -> SD write failure; the caller engages the legacy volatile
//                 PSRAM queue behind the honest degradation log (D-03)
enum class PersistOutcome : uint8_t {
    PERSISTED = 0,
    CARD_FULL = 1,
    IO_ERROR  = 2
};

// Per-image META commit record — fixed-size binary struct, written LAST in
// the persist sequence as the commit marker: a boot rescan (plan 02.5-02)
// trusts an image only when its META record validates (magic + id), so torn
// JPGs-without-META are honestly skipped.
//
// CARD FORMAT NOTE: the card is read back ONLY by this firmware (the base
// never mounts the balloon card in v1), so the layout is packed
// little-endian native. The plan spec's "32 bytes" was arithmetically
// impossible — the named fields alone total 33 bytes (2+2+1+1 + 5x4 + 7) —
// so the record pins at 36 bytes: all specified fields plus the specified
// 3 pad bytes (02.5-01 execution deviation, documented in the plan SUMMARY).
struct __attribute__((packed)) BalloonCaptureRecord {
    uint16_t magic;          // SD_STORE_META_MAGIC — validated on read
    uint16_t imageId;        // AutoCap-assigned id (matches the file names)
    uint8_t  flags;          // bit0 SD_ST_DELIV_THUMB, bit1 SD_ST_DELIV_FULL
    uint8_t  captureSource;  // CaptureSource value at capture
    uint32_t captureTimeMs;  // balloon millis at capture (rides the manifest)
    uint32_t fullLength;     // IMG_{id}.JPG byte length
    uint32_t fullCrc32;      // esp_rom_crc32_le over the full bytes (caller-computed)
    uint32_t thumbLength;    // IMG_{id}_T.JPG byte length (0 = no thumbnail)
    uint32_t thumbCrc32;     // esp_rom_crc32_le over the thumb bytes (0 when absent)
    // 7 camera-settings bytes — ImageTxSettings layout verbatim (the
    // manifest's 7-byte trailer), so a boot-rescanned entry replays a
    // byte-identical manifest
    uint8_t  resolution;     // FrameSize wire code
    uint8_t  quality;
    int8_t   brightness;
    int8_t   contrast;
    int8_t   saturation;
    int8_t   exposure;
    uint8_t  wbMode;
    uint8_t  pad[3];         // reserved (0) — keeps the record word-multiple
};
static_assert(sizeof(BalloonCaptureRecord) == 36,
              "BalloonCaptureRecord must stay 36 bytes (33 field bytes + 3 pad) — "
              "see the card-format note above");

// RAM index bound for the boot rescan (02.5-02, T-02.5-06): at most this many
// undelivered records are surfaced per boot. OVER-CAP CONSEQUENCE (honest
// degradation, named in bootRescan's log): ids beyond the cap are unreachable
// until the manual serial clear (explicit CONFIRM token, later plan) or a
// future boot's rescan after delivery drains below the cap — their files
// remain safely on card (keep-everything archive).
static constexpr uint8_t SD_STORE_MAX_TRACKED = 64;

// One validated, undelivered record produced by bootRescan() — everything
// ImageTxManager::admitRescanned needs to rebuild the file-backed entry
// shape the PERSISTED branch of enqueueCapture produces (identical fields,
// same state machine entry point; the settings bytes carry the pinned
// ImageTxSettings layout verbatim — static_assert'd in the .cpp). The
// delivery bits ride along as classification truth (a record is only
// returned when NOT both-set).
struct BalloonResumedRecord {
    uint16_t imageId;
    uint8_t  captureSource;
    uint32_t captureTimeMs;
    uint32_t fullLength;
    uint32_t fullCrc32;
    uint32_t thumbLength;    // 0 = no thumbnail was captured
    uint32_t thumbCrc32;
    uint8_t  settings[7];    // ImageTxSettings layout verbatim (the manifest's 7-byte trailer)
    bool     thumbDelivered;
    bool     fullDelivered;
};

// Honest module state (IN-03 discipline: computed truth, never a hardcoded
// OK). Four distinct, reachable states: OK; unavailable-at-boot (initFailed
// — volatile fallback territory); write-failed-since (writeFailed — the
// card mounted but a persist failed mid-flight); card-full (cardFull — the
// last persist verdict hit the free-space gate).
struct BalloonSdStoreStatus {
    bool available;    // mounted and accepting persists
    bool initFailed;   // begin() could not mount the card
    bool writeFailed;  // an open/write failed mid-flight since mount
    bool cardFull;     // the last persist verdict was CARD_FULL
};

class BalloonSdStore {
public:
    BalloonSdStore();

    // Mount the card on the board's built-in SDMMC slot (1-bit mode, pins
    // SD_STORE_*_PIN via SD_MMC.setPins — see the transport note above).
    // Returns true even on failure — the balloon runs degraded without the
    // card store (captures take the volatile fallback, D-03; boot never
    // halts on peripheral failure, 1223f46). Query isAvailable()/getStatus()
    // for the honest outcome.
    bool begin();

    // Honest availability: false until a successful begin(). Stays true
    // through mid-flight write failures (the card is still mounted; the
    // failure is surfaced via getStatus().writeFailed and each failed
    // capture takes the volatile fallback individually) — read availability
    // is independent of write health, mirroring the base's WR-05 lesson.
    bool isAvailable() const { return available; }
    BalloonSdStoreStatus getStatus() const { return status; }

    // Pre-capture free-space gate (plan 02.5-03, STORE-04): true when the
    // card can hold a capture of the given full/thumb byte sizes PLUS both
    // META commit records PLUS the SD_STORE_MIN_FREE_BYTES headroom — the
    // same arithmetic persistCapture's Step-1 pre-check uses (one shared
    // implementation, so the pre-capture gates and the persist-time race
    // backstop can never drift). A pure volume query — ONE totalBytes/
    // usedBytes read per capture attempt (T-02.5-09), never inside the
    // per-chunk loop (PRI-03 pacing untouched), and the loop never blocks
    // on the card.
    // Returns FALSE also when the store is not mounted: an absent card is
    // the honest card-full-adjacent refusal for the capture gates (the true
    // cause stays visible in the boot log's mount verdict and status).
    // Consumers: CommandHandler::handleCaptureNow (refuses CAPTURE_NOW via
    // the EXISTING NACK_BUSY class, before any camera work) and
    // AutoCapture::fire (skips with a named log; the baseline-before-
    // attempt idiom paces the retry). GET_STATUS, IMAGE_WINDOW_REQUEST,
    // SET_* and the beacon path never call this — window service of
    // already-stored images continues on a full card (that is the point of
    // the archive).
    bool hasHeadroomFor(uint32_t fullLen, uint32_t thumbLen) const;

    // Persist one capture to the flight archive, in commit order:
    //   1. free-space pre-check (BEFORE any write — a CARD_FULL refusal
    //      leaves zero partial files): fullLen + thumbLen + 2 records +
    //      SD_STORE_MIN_FREE_BYTES headroom against totalBytes()-usedBytes()
    //   2. IMG_{id}.JPG (full bytes), then IMG_{id}_T.JPG (thumbnail bytes,
    //      skipped when thumbLen == 0 — a thumbnail-less capture persists
    //      the full only, with zeroed thumb fields in the record)
    //   3. IMG_{id}.META LAST — the commit marker (ordering is the point:
    //      a torn capture lacks a validating META and is never trusted)
    // CRCs are the CALLER's (esp_rom_crc32_le over the original bytes) —
    // this module only stores them. Any open/write failure mid-sequence:
    // handles closed, the failed path logged, status.writeFailed set,
    // IO_ERROR returned. Partial files are NEVER deleted (keep-everything
    // archive — the boot rescan classifies them).
    PersistOutcome persistCapture(uint16_t imageId,
                                  uint8_t captureSource,
                                  uint32_t captureTimeMs,
                                  const ImageTxSettings& settings,
                                  const uint8_t* fullBytes, size_t fullLen, uint32_t fullCrc32,
                                  const uint8_t* thumbBytes, size_t thumbLen, uint32_t thumbCrc32);

    // Read one chunk's bytes from the kind's card file at
    // chunkIndex * IMG_CHUNK_PAYLOAD_SIZE. Guards chunkIndex against the
    // file's stored length (offset past EOF -> false — T-02.5-02: the caller
    // rides the existing same-index retry/skip path; no fabricated chunk,
    // no cursor advance on unread bytes). The tail chunk returns its genuine
    // partial length via *outLen. Read failure or zero bytes -> false.
    bool readChunk(uint16_t imageId, ImageKind kind, uint16_t chunkIndex,
                   uint8_t* out, size_t cap, size_t* outLen);

    // Set the kind's SD_ST_DELIV_* bit in the META record — open, validate
    // magic + id, set the bit, seek back, write the single flags byte in
    // place (one-byte update, no record rewrite). Bookkeeping for plan
    // 02.5-02's boot rescan.
    bool markDelivered(uint16_t imageId, ImageKind kind);

    // Boot-time rescan (02.5-02, STORE-02): ONE bounded openNextFile() walk
    // of SD_STORE_DIR collecting IMG_%05u.META commit records (the base's
    // buildIndex walk idiom), plus one companion pass naming JPGs whose id
    // has no META file at all (a persist that died before the commit write).
    // Per-record validation before any admission (T-02.5-05): META magic +
    // id, then the referenced JPG files must EXIST with sizes matching the
    // recorded lengths (the _T.JPG is checked only when thumbLength > 0 —
    // thumbLength 0 legitimately means no thumbnail was captured).
    // Classification — each observable in the boot log:
    //   resumed   → a BalloonResumedRecord in out[] (caller re-admits it)
    //   delivered → both SD_ST_DELIV_* bits set: counted, one summary line,
    //               history stays on card, never re-announced
    //   torn      → named skip line, file KEPT (never deleted, never
    //               fabricated — keep-everything + the no-fabrication rule)
    // Output is sorted ascending by imageId (FIFO resume order mirrors
    // capture order) and capped at `cap` (SD_STORE_MAX_TRACKED) keeping the
    // NEWEST ids when over cap — the honest over-cap degradation line names
    // what was left un-announced. Untrusted removable media: a corrupt,
    // foreign, or hand-mangled card can only ever produce skipped records,
    // never transferable state. Boot-time only — never called per loop pass.
    // Returns the number of records written to out[].
    uint8_t bootRescan(BalloonResumedRecord* out, uint8_t cap);

private:
    bool available;
    BalloonSdStoreStatus status;

    // Paths built ONLY from the %05u-formatted numeric id (T-02.5-04, the
    // T-02-08 discipline carried from the base's SdStorage::imagePath — no
    // network-supplied string ever enters a file path)
    static void filePath(char* out, size_t cap, uint16_t imageId, ImageKind kind);
    static void metaPath(char* out, size_t cap, uint16_t imageId);

    // bootRescan's per-record validator (T-02.5-05): reads the 36-byte META
    // for id, validates magic + id, then requires the referenced JPGs to
    // exist with sizes matching the recorded lengths. Any failure -> false
    // (the caller classifies the capture torn and skips it).
    static bool bootRescanValidateMeta(uint16_t id, BalloonCaptureRecord& rec);
};

// ===========================
// Global Instance Access
// ===========================

extern BalloonSdStore& BalloonSdStoreTx();

#endif // SD_STORE_BALLOON_H
