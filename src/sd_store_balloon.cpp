#include "sd_store_balloon.h"
#include "image_tx_manager.h"   // ImageTxSettings (the META record's 7-byte trailer)

// Debug configuration
#ifndef DEBUG_SD_STORE
#define DEBUG_SD_STORE true
#endif

// ===========================
// Static Instance
// ===========================

static BalloonSdStore sdStoreBalloonInstance;
BalloonSdStore& BalloonSdStoreTx() {
    return sdStoreBalloonInstance;
}

// Balloon-side SD flight-archive store (Phase 2.5, STORE-01/STORE-03). The
// transport mirrors the base's proven SdStorage idiom (operator decision
// D-01) but the module is deliberately separate: src/sd_storage.cpp is the
// base-gallery store, base-env-owned (excluded from the balloon build), and
// its gallery/sidecar machinery has no meaning on the balloon. See the
// header for the pinned card format and the keep-everything policy.

// The META trailer mirrors ImageTxSettings byte-for-byte (header forward-
// declares the struct; the layout contract is pinned here).
static_assert(sizeof(ImageTxSettings) == 7,
              "BalloonCaptureRecord's 7 settings bytes assume the ImageTxSettings layout");

// ===========================
// Constructor
// ===========================

BalloonSdStore::BalloonSdStore()
    : available(false)
{
    status.available = false;
    status.initFailed = false;
    status.writeFailed = false;
    status.cardFull = false;
}

// ===========================
// Initialization
// ===========================

bool BalloonSdStore::begin() {
    // Built-in SDMMC slot, 1-bit mode: setPins must precede begin() on
    // GPIO-matrix targets (see the header transport note — without it,
    // begin() fails with "some SD pins are not set"). Frequency deliberately
    // SDMMC_FREQ_DEFAULT (20 MHz): chunk transmits are paced by the 9.6 kbps
    // air rate, so throughput is irrelevant and the lower clock is the
    // SD_MMC.h-documented stability lever for marginal slots/traces.
    bool pinsOk = SD_MMC.setPins(SD_STORE_CLK_PIN, SD_STORE_CMD_PIN, SD_STORE_DATA_PIN);
    if (!pinsOk || !SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
        // Degrade, never halt (1223f46 lesson / SdStorage idiom): the balloon
        // keeps flying; captures take the honestly-labeled volatile fallback
        // in ImageTxManager (D-03).
        Serial.printf("SdStore: SD_MMC.begin failed (1-bit SDMMC pins CLK=%d CMD=%d D0=%d); "
                      "running WITHOUT card store — captures take the volatile fallback\n",
                      SD_STORE_CLK_PIN, SD_STORE_CMD_PIN, SD_STORE_DATA_PIN);
        available = false;
        status.available = false;
        status.initFailed = true;
        status.writeFailed = false;
        status.cardFull = false;
        return true;
    }

    // Flat image directory — mirrors the base's D-32 convention; mkdir is
    // idempotent on FAT.
    if (!SD_MMC.mkdir(SD_STORE_DIR)) {
        Serial.println("SdStore: /images mkdir failed (existing dir?); continuing");
    }

    available = true;
    status.available = true;
    status.initFailed = false;
    status.writeFailed = false;
    status.cardFull = false;

    Serial.println("SdStore: card mounted, /images ready");
    return true;
}

// ===========================
// Paths
// ===========================

void BalloonSdStore::filePath(char* out, size_t cap, uint16_t imageId, ImageKind kind) {
    // D-29/D-31 convention mirrored from the base: ids zero-padded to 5
    // (wrap-safe ordering on the FAT directory), thumbnails carry the
    // _T.JPG suffix. Names are built ONLY from the %05u-formatted numeric
    // id — no network-supplied string ever enters a file path (T-02.5-04).
    if (kind == ImageKind::THUMBNAIL) {
        snprintf(out, cap, "%s/IMG_%05u_T.JPG", SD_STORE_DIR, static_cast<unsigned>(imageId));
    } else {
        snprintf(out, cap, "%s/IMG_%05u.JPG", SD_STORE_DIR, static_cast<unsigned>(imageId));
    }
}

void BalloonSdStore::metaPath(char* out, size_t cap, uint16_t imageId) {
    snprintf(out, cap, "%s/IMG_%05u.META", SD_STORE_DIR, static_cast<unsigned>(imageId));
}

// ===========================
// Persist (commit-ordered capture write)
// ===========================

// Truncate-create + write + close. FILE_WRITE on arduino-esp32 is "w" —
// an existing file (a torn capture from a previous IO_ERROR, never deleted
// per keep-everything) is honestly overwritten by the fresh, complete
// capture of the same id — the write IS the new commit candidate; only a
// validating META makes it trusted.
static bool writeFileTruncateCreate(const char* path, const uint8_t* data, size_t len) {
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) {
        return false;
    }
    bool ok = true;
    if (len > 0) {
        ok = (f.write(data, len) == len);
    }
    f.close();
    return ok;
}

PersistOutcome BalloonSdStore::persistCapture(uint16_t imageId,
                                              uint8_t captureSource,
                                              uint32_t captureTimeMs,
                                              const ImageTxSettings& settings,
                                              const uint8_t* fullBytes, size_t fullLen, uint32_t fullCrc32,
                                              const uint8_t* thumbBytes, size_t thumbLen, uint32_t thumbCrc32) {
    if (!available) {
        // No card mounted: the caller's D-03 fallback owns the consequence;
        // this line names the module-side verdict for the bench log.
        Serial.printf("SdStore: persistCapture image %u skipped - no card mounted\n",
                      static_cast<unsigned>(imageId));
        return PersistOutcome::IO_ERROR;
    }

    // Step 1 — free-space pre-check BEFORE any write (T-02.5-03): a CARD_FULL
    // refusal leaves zero partial files. Required = the capture's own bytes
    // + both commit records + SD_STORE_MIN_FREE_BYTES headroom (the headroom
    // keeps card-full a stable verdict instead of a one-byte-from-the-edge
    // flapper). totalBytes/usedBytes are 64-bit — the comparison stays 64-bit.
    const uint64_t required = static_cast<uint64_t>(fullLen) + static_cast<uint64_t>(thumbLen)
                              + (2ULL * sizeof(BalloonCaptureRecord))
                              + static_cast<uint64_t>(SD_STORE_MIN_FREE_BYTES);
    const uint64_t freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
    if (freeBytes < required) {
        status.cardFull = true;
        Serial.printf("SdStore: card full - image %u persist refused (%llu B free < %llu B required)\n",
                      static_cast<unsigned>(imageId),
                      (unsigned long long)freeBytes, (unsigned long long)required);
        return PersistOutcome::CARD_FULL;
    }

    // Step 2 — image files. Any failure: close handles (writeFileTruncateCreate
    // closes internally), log the failed path, set status.writeFailed, return
    // IO_ERROR. Partial files are NEVER deleted (keep-everything archive —
    // the boot rescan classifies JPGs-without-META as torn and skips them).
    char path[32];
    filePath(path, sizeof(path), imageId, ImageKind::FULL_IMAGE);
    if (!writeFileTruncateCreate(path, fullBytes, fullLen)) {
        Serial.printf("SdStore: full write FAILED for %s (image %u) - IO error\n",
                      path, static_cast<unsigned>(imageId));
        status.writeFailed = true;
        return PersistOutcome::IO_ERROR;
    }

    // Thumbnail file: skipped honestly when the capture had no thumbnail
    // (thumbLen == 0) — the META record carries the zeroed thumb fields.
    if (thumbBytes != nullptr && thumbLen > 0) {
        filePath(path, sizeof(path), imageId, ImageKind::THUMBNAIL);
        if (!writeFileTruncateCreate(path, thumbBytes, thumbLen)) {
            Serial.printf("SdStore: thumb write FAILED for %s (image %u) - IO error\n",
                          path, static_cast<unsigned>(imageId));
            status.writeFailed = true;
            return PersistOutcome::IO_ERROR;
        }
    }

    // Step 3 — META record LAST as the commit marker (STORE-01 ordering is
    // the point: a boot rescan trusts an image only when this record
    // validates, so a torn write sequence is honestly skipped, never
    // fabricated — T-02.5-01).
    metaPath(path, sizeof(path), imageId);
    BalloonCaptureRecord rec{};
    rec.magic = SD_STORE_META_MAGIC;
    rec.imageId = imageId;
    rec.flags = 0;   // delivery bits zero at capture — set later in place by markDelivered
    rec.captureSource = captureSource;
    rec.captureTimeMs = captureTimeMs;
    rec.fullLength = static_cast<uint32_t>(fullLen);
    rec.fullCrc32 = fullCrc32;
    rec.thumbLength = (thumbBytes != nullptr) ? static_cast<uint32_t>(thumbLen) : 0;
    rec.thumbCrc32 = (thumbBytes != nullptr) ? thumbCrc32 : 0;
    // The 7 settings bytes ride the pinned ImageTxSettings layout verbatim
    // (static_assert above).
    static_assert(sizeof(rec.resolution) == 1, "settings trailer is byte-addressed");
    memcpy(&rec.resolution, &settings, 7);

    if (!writeFileTruncateCreate(path, reinterpret_cast<const uint8_t*>(&rec), sizeof(rec))) {
        Serial.printf("SdStore: META write FAILED for %s (image %u) - IO error\n",
                      path, static_cast<unsigned>(imageId));
        status.writeFailed = true;
        return PersistOutcome::IO_ERROR;
    }

    if (DEBUG_SD_STORE) {
        Serial.printf("SdStore: persisted image %u (full %u B, thumb %u B, commit META %s %u B)\n",
                      static_cast<unsigned>(imageId),
                      static_cast<unsigned>(fullLen),
                      static_cast<unsigned>(thumbLen),
                      path,
                      static_cast<unsigned>(sizeof(rec)));
    }
    status.cardFull = false;
    return PersistOutcome::PERSISTED;
}

// ===========================
// Chunk Serving (STORE-03)
// ===========================

bool BalloonSdStore::readChunk(uint16_t imageId, ImageKind kind, uint16_t chunkIndex,
                               uint8_t* out, size_t cap, size_t* outLen) {
    if (out == nullptr || outLen == nullptr || cap == 0) {
        return false;
    }
    if (!available || status.initFailed) {
        return false;
    }

    char path[32];
    filePath(path, sizeof(path), imageId, kind);

    // Per-call open is deliberate: one window services at a time, and a
    // chunk transmit costs 250-400 ms at the 9.6 kbps air rate, so a 20 MHz
    // open/seek/read is noise next to it. No cached handle to invalidate,
    // no cross-image stale-cursor class.
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        return false;
    }

    // Offset-past-EOF guard (T-02.5-02): the file size mirrors the length
    // this image's META record stores for the kind (both were written in the
    // same persist sequence; a mismatch is a torn card the boot rescan
    // owns). A chunkIndex beyond the stored length returns false — the
    // caller treats it exactly like a transmit failure (same-index retry,
    // bounded, named skip). No fabricated chunk, no zero fill.
    const size_t offset = static_cast<size_t>(chunkIndex) * IMG_CHUNK_PAYLOAD_SIZE;
    if (offset >= f.size()) {
        f.close();
        return false;
    }
    if (!f.seek(offset)) {
        f.close();
        return false;
    }
    const size_t got = f.read(out, cap);
    f.close();
    if (got == 0) {
        return false;   // IO failure at the offset — never an empty chunk
    }
    *outLen = got;   // the tail chunk's genuine partial length
    return true;
}

// ===========================
// Delivery Bookkeeping
// ===========================

bool BalloonSdStore::markDelivered(uint16_t imageId, ImageKind kind) {
    if (!available || status.initFailed) {
        return false;
    }

    char path[32];
    metaPath(path, sizeof(path), imageId);
    // "r+" — in-place flags update, never a truncate (keep-everything).
    File f = SD_MMC.open(path, "r+");
    if (!f) {
        return false;
    }

    // Untrusted removable media (T-02.5-01): validate magic + id before any
    // write back — a torn/hostile record is left untouched.
    BalloonCaptureRecord rec{};
    if (f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec) ||
        rec.magic != SD_STORE_META_MAGIC || rec.imageId != imageId) {
        f.close();
        return false;
    }

    const uint8_t bit = (kind == ImageKind::THUMBNAIL) ? SD_ST_DELIV_THUMB : SD_ST_DELIV_FULL;
    if (rec.flags & bit) {
        f.close();
        return true;   // idempotent — already marked
    }
    rec.flags |= bit;

    // One-byte update in place: seek back to the flags byte and rewrite it
    // (no record rewrite).
    if (!f.seek(offsetof(BalloonCaptureRecord, flags))) {
        f.close();
        return false;
    }
    const bool ok = (f.write(&rec.flags, 1) == 1);
    f.close();
    if (!ok) {
        status.writeFailed = true;
        Serial.printf("SdStore: delivery-flag write FAILED for %s (image %u)\n",
                      path, static_cast<unsigned>(imageId));
    }
    return ok;
}
