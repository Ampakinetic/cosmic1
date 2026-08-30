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
// Free-Space Gate (plan 02.5-03, STORE-04)
// ===========================

// The SHARED free-space arithmetic — one implementation for the pre-capture
// gates (CommandHandler/AutoCapture via hasHeadroomFor) and persistCapture's
// Step-1 pre-check, so the two layers can never drift. Required = the
// capture's own bytes + both commit records + SD_STORE_MIN_FREE_BYTES
// headroom (the headroom keeps card-full a stable verdict instead of a
// one-byte-from-the-edge flapper).
static uint64_t requiredSpaceFor(uint32_t fullLen, uint32_t thumbLen) {
    return static_cast<uint64_t>(fullLen) + static_cast<uint64_t>(thumbLen)
           + (2ULL * sizeof(BalloonCaptureRecord))
           + static_cast<uint64_t>(SD_STORE_MIN_FREE_BYTES);
}

// One volume query per capture attempt (T-02.5-09): totalBytes/usedBytes are
// 64-bit; the comparison stays 64-bit. Never called inside the per-chunk
// loop (PRI-03 pacing untouched).
static uint64_t cardFreeBytes() {
    return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

bool BalloonSdStore::hasHeadroomFor(uint32_t fullLen, uint32_t thumbLen) const {
    // Not mounted = the honest card-full-adjacent refusal for the gates
    // (the true cause stays visible in the boot log's mount verdict and
    // getStatus()); the gate never mutates state — it is a pure query.
    if (!available || status.initFailed) {
        return false;
    }
    return cardFreeBytes() >= requiredSpaceFor(fullLen, thumbLen);
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

    // Crash-fix cache interlock (session 8): a fresh capture is about to
    // create files — drop any cached read handles so the cache never spans a
    // write sequence (also covers any card-removal weirdness between mounts).
    invalidateChunkReadCache();

    // Step 1 — free-space pre-check BEFORE any write (T-02.5-03), via the
    // SHARED gate: hasHeadroomFor is the SAME implementation the
    // CommandHandler and AutoCapture pre-capture gates call, so the gate
    // layer and this persist-time race backstop cannot drift. A CARD_FULL
    // refusal leaves zero partial files. (Reaching here implies mounted —
    // the !available IO_ERROR branch above already returned — so a gate
    // false is genuinely the space verdict.)
    if (!hasHeadroomFor(fullLen, thumbLen)) {
        status.cardFull = true;
        Serial.printf("SdStore: card full - image %u persist refused (%llu B free < %llu B required)\n",
                      static_cast<unsigned>(imageId),
                      (unsigned long long)cardFreeBytes(),
                      (unsigned long long)requiredSpaceFor(fullLen, thumbLen));
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

void BalloonSdStore::invalidateChunkReadCache() {
    for (uint8_t k = 0; k < 2; k++) {
        if (chunkReadCache[k]) {
            chunkReadCache[k].close();
            chunkReadCache[k] = File();
        }
    }
}

bool BalloonSdStore::readChunk(uint16_t imageId, ImageKind kind, uint16_t chunkIndex,
                               uint8_t* out, size_t cap, size_t* outLen) {
    if (out == nullptr || outLen == nullptr || cap == 0) {
        return false;
    }
    if (!available || status.initFailed) {
        return false;
    }
    const uint8_t slot = static_cast<uint8_t>(kind);
    if (slot >= 2) {
        return false;
    }

    // Cached read handle (crash fix, session 8 — see the header comment):
    // the per-chunk open was "deliberate" when airtime dominated (250-400 ms
    // per chunk transmit); the watchdog-reset correlation with SD/ISR churn
    // reversed that trade. Miss or stale id → one open here; the handle then
    // persists across the image's whole push/pull. ANY failure closes +
    // invalidates, so the next call starts fresh — no stale-cursor or
    // stale-handle class survives an error.
    if (!chunkReadCache[slot] || chunkReadCacheId[slot] != imageId) {
        if (chunkReadCache[slot]) {
            chunkReadCache[slot].close();
            chunkReadCache[slot] = File();
        }
        char path[32];
        filePath(path, sizeof(path), imageId, kind);
        chunkReadCache[slot] = SD_MMC.open(path, FILE_READ);
        if (!chunkReadCache[slot]) {
            return false;
        }
        chunkReadCacheId[slot] = imageId;
    }
    File& f = chunkReadCache[slot];

    // Offset-past-EOF guard (T-02.5-02): the file size mirrors the length
    // this image's META record stores for the kind (both were written in the
    // same persist sequence; a mismatch is a torn card the boot rescan
    // owns). A chunkIndex beyond the stored length returns false — the
    // caller treats it exactly like a transmit failure (same-index retry,
    // bounded, named skip). No fabricated chunk, no zero fill.
    const size_t offset = static_cast<size_t>(chunkIndex) * IMG_CHUNK_PAYLOAD_SIZE;
    if (offset >= f.size()) {
        return false;
    }
    if (!f.seek(offset)) {
        invalidateChunkReadCache();
        return false;
    }
    const size_t got = f.read(out, cap);
    if (got == 0) {
        invalidateChunkReadCache();
        return false;   // IO failure at the offset — never an empty chunk
    }
    *outLen = got;   // the tail chunk's genuine partial length
    return true;
}

// ===========================
// Delivery Bookkeeping
// ===========================

// Forward declaration — defined just below the Boot Rescan section header,
// shared by bootRescan's pass-1 and loadResumedRecord.
static void fillResumedRecord(const BalloonCaptureRecord& rec, BalloonResumedRecord& rr);

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

bool BalloonSdStore::persistThumbAcked(uint16_t imageId, uint64_t bitmap) {
    if (!available || status.initFailed) {
        return false;
    }

    char path[32];
    metaPath(path, sizeof(path), imageId);
    // "r+" — in-place bitmap update, never a truncate (keep-everything).
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

    // One-field update in place: seek to the bitmap and rewrite it
    if (!f.seek(offsetof(BalloonCaptureRecord, thumbAcked))) {
        f.close();
        return false;
    }
    const bool ok = (f.write(reinterpret_cast<const uint8_t*>(&bitmap),
                             sizeof(bitmap)) == sizeof(bitmap));
    f.close();
    if (!ok) {
        status.writeFailed = true;
        Serial.printf("SdStore: thumbAcked write FAILED for %s (image %u)\n",
                      path, static_cast<unsigned>(imageId));
    }
    return ok;
}

bool BalloonSdStore::loadResumedRecord(uint16_t imageId, BalloonResumedRecord& out) {
    if (!available || status.initFailed) {
        return false;
    }

    // The full bootRescanValidateMeta check (META magic + id, referenced JPGs
    // exist with matching sizes) is the trust boundary — a record that fails
    // it is a torn capture, never a transferable one.
    BalloonCaptureRecord rec{};
    if (!bootRescanValidateMeta(imageId, rec)) {
        return false;
    }
    out = BalloonResumedRecord{};
    fillResumedRecord(rec, out);
    return true;
}

// Walk-local record→resume-candidate fill, shared by bootRescan's pass-1
// and loadResumedRecord (image-transfer rework): one place copies the
// pinned field mapping so the two readers can never drift.
static void fillResumedRecord(const BalloonCaptureRecord& rec, BalloonResumedRecord& rr) {
    rr.imageId = rec.imageId;
    rr.captureSource = rec.captureSource;
    rr.captureTimeMs = rec.captureTimeMs;
    rr.fullLength = rec.fullLength;
    rr.fullCrc32 = rec.fullCrc32;
    rr.thumbLength = rec.thumbLength;
    rr.thumbCrc32 = rec.thumbCrc32;
    memcpy(rr.settings, &rec.resolution, sizeof(rr.settings));
    rr.thumbDelivered = (rec.flags & SD_ST_DELIV_THUMB) != 0;
    rr.fullDelivered = (rec.flags & SD_ST_DELIV_FULL) != 0;
    rr.thumbAcked = rec.thumbAcked;
}

// ===========================
// Boot Rescan (02.5-02, STORE-02)
// ===========================

// Per-record validator for bootRescan (T-02.5-05): the META record must
// validate (magic + id), and the referenced JPG files must EXIST with sizes
// matching the recorded lengths — thumbLength 0 legitimately means no
// thumbnail was captured, so the _T.JPG is checked only when thumbLength > 0.
// A record that fails any check is a torn capture: the caller skips it with
// the named line and the files stay on card — never fabricated metadata,
// never deletion.
bool BalloonSdStore::bootRescanValidateMeta(uint16_t id, BalloonCaptureRecord& rec) {
    char path[32];
    metaPath(path, sizeof(path), id);
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        return false;
    }
    const bool recOk =
        f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) == sizeof(rec) &&
        rec.magic == SD_STORE_META_MAGIC &&
        rec.imageId == id;
    f.close();
    if (!recOk) {
        return false;
    }

    // Full JPG: must exist and match the recorded length (a zero-length full
    // is a broken capture, not a resumable one).
    if (rec.fullLength == 0) {
        return false;
    }
    filePath(path, sizeof(path), id, ImageKind::FULL_IMAGE);
    File g = SD_MMC.open(path, FILE_READ);
    if (!g) {
        return false;
    }
    const bool fullOk = (g.size() == rec.fullLength);
    g.close();
    if (!fullOk) {
        return false;
    }

    // Thumbnail JPG: checked only when the record says one exists.
    if (rec.thumbLength > 0) {
        filePath(path, sizeof(path), id, ImageKind::THUMBNAIL);
        File t = SD_MMC.open(path, FILE_READ);
        if (!t) {
            return false;
        }
        const bool thumbOk = (t.size() == rec.thumbLength);
        t.close();
        if (!thumbOk) {
            return false;
        }
    }
    return true;
}

// Walk-local name parser: recognizes IMG_<digits>.META (pass 1) and
// IMG_<digits>.JPG / IMG_<digits>_T.JPG (pass 2), extracting the id.
// Names are built ONLY from %05u ids at persist time (T-02.5-04); the parser
// mirrors the base's buildIndex discipline (f.name() may carry a leading
// path depending on the core version — parse the LAST component only).
static bool bootRescanParseName(const char* base, const char* suffix, uint16_t& outId) {
    const size_t blen = strlen(base);
    const size_t slen = strlen(suffix);
    if (blen <= 4 + slen + 1 || strncmp(base, "IMG_", 4) != 0 ||
        strcmp(base + blen - slen, suffix) != 0) {
        return false;
    }
    const char* p = base + 4;
    const char* end = base + blen - slen;
    if (end <= p) {
        return false;
    }
    for (const char* q = p; q < end; ++q) {
        if (*q < '0' || *q > '9') {
            return false;
        }
    }
    const unsigned long parsed = strtoul(p, nullptr, 10);
    if (parsed < 1 || parsed > 65535) {
        return false;
    }
    outId = static_cast<uint16_t>(parsed);
    return true;
}

uint8_t BalloonSdStore::bootRescan(BalloonResumedRecord* out, uint8_t cap) {
    if (out == nullptr || cap == 0) {
        return 0;
    }
    if (!available || status.initFailed) {
        return 0;   // no card — nothing to resume from (the caller logs the regime)
    }

    File dir = SD_MMC.open(SD_STORE_DIR);
    if (!dir || !dir.isDirectory()) {
        Serial.println("SdStore: bootRescan - /images open failed; nothing to resume");
        return 0;
    }

    uint8_t  count = 0;             // resumed records held in out[] (ascending by imageId)
    uint16_t deliveredCount = 0;    // both delivery bits set — history, stays on card
    uint16_t tornCount = 0;         // META-invalid or size-mismatched captures
    uint16_t overCapDropped = 0;    // undelivered beyond the tracked cap (oldest ids)

    // Pass 1 — the openNextFile() walk (the base's buildIndex idiom, mirrored):
    // every .META candidate is read, validated, and classified. Walk order is
    // FAT directory order — records are inserted ascending-by-id so the
    // output is FIFO resume order regardless.
    File f;
    while ((f = dir.openNextFile())) {
        if (!f.isDirectory()) {
            const char* base = f.name();
            const char* slash = strrchr(base, '/');
            if (slash) {
                base = slash + 1;
            }
            uint16_t id = 0;
            if (bootRescanParseName(base, ".META", id)) {
                BalloonCaptureRecord rec{};
                if (!bootRescanValidateMeta(id, rec)) {
                    tornCount++;
                    Serial.printf("SdStore: torn capture at image %u skipped (no validating META/size mismatch) — file kept on card\n",
                                  static_cast<unsigned>(id));
                } else if ((rec.flags & (SD_ST_DELIV_THUMB | SD_ST_DELIV_FULL)) ==
                           (SD_ST_DELIV_THUMB | SD_ST_DELIV_FULL)) {
                    // Delivered history: counted here, one summary line below —
                    // delivered images are never re-announced.
                    deliveredCount++;
                } else {
                    // Validated + undelivered → resume candidate. The settings
                    // trailer copies verbatim (pinned 7-byte ImageTxSettings
                    // layout, static_assert'd at the top of this file).
                    BalloonResumedRecord rr{};
                    fillResumedRecord(rec, rr);

                    // Ascending-by-id insertion; over cap the NEWEST ids are
                    // kept (the oldest shift out — honest, named degradation).
                    uint8_t pos = 0;
                    while (pos < count && out[pos].imageId < rr.imageId) {
                        pos++;
                    }
                    if (pos < count && out[pos].imageId == rr.imageId) {
                        // duplicate id (impossible on FAT) — keep the first
                    } else if (count < cap) {
                        for (uint8_t i = count; i > pos; --i) {
                            out[i] = out[i - 1];
                        }
                        out[pos] = rr;
                        count++;
                    } else if (rr.imageId > out[0].imageId) {
                        overCapDropped++;
                        for (uint8_t i = 1; i < cap; ++i) {
                            out[i - 1] = out[i];
                        }
                        // the array shifted left by one — insert at pos-1
                        for (uint8_t i = cap - 1; i > pos - 1; --i) {
                            out[i] = out[i - 1];
                        }
                        out[pos - 1] = rr;
                    } else {
                        overCapDropped++;   // older than everything held
                    }
                }
            }
        }
        f.close();
    }
    dir.close();

    // Pass 2 — JPGs whose id has NO META file at all (a persist that died
    // between the JPG writes and the META commit): the same named torn line,
    // so no card artifact is ever silently ignored. Ids whose META file
    // EXISTS were already classified in pass 1 (validated, delivered, or
    // torn) — never double-printed. Memory-free: an SD_MMC.exists() check
    // per JPG, boot-time only.
    dir = SD_MMC.open(SD_STORE_DIR);
    if (dir) {
        while ((f = dir.openNextFile())) {
            if (!f.isDirectory()) {
                const char* base = f.name();
                const char* slash = strrchr(base, '/');
                if (slash) {
                    base = slash + 1;
                }
                uint16_t id = 0;
                if (bootRescanParseName(base, ".JPG", id) ||
                    bootRescanParseName(base, "_T.JPG", id)) {
                    char mpath[32];
                    metaPath(mpath, sizeof(mpath), id);
                    if (!SD_MMC.exists(mpath)) {
                        tornCount++;
                        Serial.printf("SdStore: torn capture at image %u skipped (no validating META/size mismatch) — file kept on card\n",
                                      static_cast<unsigned>(id));
                    }
                }
            }
            f.close();
        }
        dir.close();
    }

    if (deliveredCount > 0) {
        Serial.printf("SdStore: rescan skipped %u already-delivered record(s) - history kept on card\n",
                      static_cast<unsigned>(deliveredCount));
    }
    if (overCapDropped > 0) {
        Serial.printf("SdStore: %u undelivered exceeds tracked cap %u - oldest ids left un-announced (files kept on card)\n",
                      static_cast<unsigned>(count + overCapDropped),
                      static_cast<unsigned>(cap));
    }
    return count;
}

// ===========================
// Manual Archive Clear (plan 02.5-03, T-02.5-08)
// ===========================

// The T-02.5-08 confirmation vocabulary as a named predicate — the single
// point where the manual-clear token is judged: any argument other than the
// literal SD_STORE_CONFIRM_TOKEN deletes nothing (a stray line or typo is
// inert; the token IS the confirmation — no timeout machinery, no NVS
// state).
static bool clearAllImagesTokenMatches(const char* confirmToken) {
    return confirmToken != nullptr && strcmp(confirmToken, SD_STORE_CONFIRM_TOKEN) == 0;
}

uint16_t BalloonSdStore::clearAllImages(const char* confirmToken) {
    if (!clearAllImagesTokenMatches(confirmToken)) {
        Serial.println("SdStore: manual clear requires: SDCLEAR CONFIRM");
        return 0;
    }
    if (!available || status.initFailed) {
        Serial.println("SdStore: manual clear skipped - no card mounted");
        return 0;
    }

    // Crash-fix cache interlock (session 8): never hold a cached read handle
    // across the archive's ONLY deletion surface.
    invalidateChunkReadCache();

    File dir = SD_MMC.open(SD_STORE_DIR);
    if (!dir || !dir.isDirectory()) {
        Serial.println("SdStore: manual clear - /images open failed; nothing removed");
        return 0;
    }

    // The ONLY card-deletion call in the balloon firmware (keep-everything
    // retention invariant, pinned by the plan's counted grep): every file
    // inside /images is removed, the directory itself is KEPT. Names are
    // copied out before close (f.name() is valid only while open); a name
    // that cannot fit the bounded path buffer is skipped with a named line
    // rather than silently truncated into a wrong-path remove.
    uint16_t removed = 0;
    File f;
    while ((f = dir.openNextFile())) {
        if (f.isDirectory()) {
            f.close();
            continue;
        }
        const char* base = f.name();
        const char* slash = strrchr(base, '/');
        if (slash) {
            base = slash + 1;
        }
        char path[48];
        if (strlen(base) + strlen(SD_STORE_DIR) + 2 > sizeof(path)) {
            Serial.printf("SdStore: clear skipped for over-long name (kept on card)\n");
            f.close();
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", SD_STORE_DIR, base);
        f.close();
        if (SD_MMC.remove(path)) {
            removed++;
            Serial.printf("SdStore: cleared %s\n", path);
        } else {
            Serial.printf("SdStore: clear FAILED for %s (kept on card)\n", path);
        }
    }
    dir.close();

    Serial.printf("SdStore: manual clear removed %u file(s)\n",
                  static_cast<unsigned>(removed));
    return removed;
}
