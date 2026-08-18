#ifndef IMAGE_RX_MANAGER_H
#define IMAGE_RX_MANAGER_H

#include <Arduino.h>
#include "command_protocol.h"
#include "image_protocol.h"

// ===========================
// Image RX Manager
// Base Station - Reassembles pushed image streams and tracks telemetry beacons
// Phase 2: Image Transmission (plan 02-01, IMG-01 receive half)
// ===========================
// Receive-only in this plan: CommandSender::processIncomingByte validates the
// frame (type dispatch + CRC16 + end marker) and forwards complete 0x12/0x13
// frames here; this module validates the manifest BEFORE allocating (Pitfall
// 11), reassembles chunks into a heap buffer, and verifies the end-to-end
// CRC32 (D-23) before exposing the result. The newest CRC-verified thumbnail
// is retained and served over HTTP by main_basestation. Windowed-pull
// re-request for holes (D-22) arrives in 02-03 as an EXTENSION of this state,
// not a rework.

// In-flight transfer being reassembled from one manifest + its chunks
struct ImageRxTransfer {
    bool active;
    uint16_t imageId;
    uint8_t  imageKind;       // ImageKind value from the manifest
    uint8_t  captureSource;   // CaptureSource value from the manifest
    uint32_t totalSize;       // validated <= MAX_IMAGE_SIZE (base_station_config.h)
    uint16_t chunkSize;       // validated <= IMG_CHUNK_PAYLOAD_SIZE
    uint16_t totalChunks;     // validated == ceil(totalSize / chunkSize)
    uint32_t crc32;           // D-23 end-to-end CRC over this kind's original bytes
    uint32_t captureTimeMs;
    // Camera-settings trailer from the manifest (informational for 02-01)
    uint8_t  resolution;
    uint8_t  quality;
    int8_t   brightness;
    int8_t   contrast;
    int8_t   saturation;
    int8_t   exposure;
    uint8_t  wbMode;

    uint8_t* buffer;          // heap buffer of totalSize bytes (plain malloc —
                              // the base build does not enable PSRAM)
    uint8_t* chunkPresent;    // one flag byte per chunk (duplicate detection)
    uint16_t receivedCount;   // distinct chunks accepted so far
    uint32_t lastActivityMs;  // stall watchdog stamp
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
    // tracked-command table (0x12/0x13/0x14 are unsolicited data frames).
    void onManifestFrame(const uint8_t* frame, size_t length);
    void onChunkFrame(const uint8_t* frame, size_t length);
    void onTelemetryBeaconFrame(const uint8_t* frame, size_t length);

    // Main processing — call from the base loop after CmdSender().process();
    // ages out stalled in-flight transfers (IMG_WINDOW_STALL_MS)
    void process();

    // Newest CRC-verified thumbnail. Id 0 means none has been verified yet;
    // the buffer stays owned here and is valid until the next verified
    // thumbnail replaces it.
    uint16_t getLatestThumbId() const { return latestThumbId; }
    const uint8_t* getLatestThumbData() const { return latestThumbBuffer; }
    size_t getLatestThumbLength() const { return latestThumbLength; }

    // Telemetry beacon state (absent telemetry reported as absent)
    const TelemetrySnapshot& getTelemetrySnapshot() const { return telemetry; }

private:
    bool initialized;

    ImageRxTransfer transfer;

    // Retained newest verified thumbnail (plain heap — served by the web
    // server from the single-threaded loop, so no locking is needed)
    uint16_t latestThumbId;
    uint8_t* latestThumbBuffer;
    size_t   latestThumbLength;

    TelemetrySnapshot telemetry;

    // Helpers
    void startTransfer(const ImageManifestBody& m);
    void finalizeTransfer();          // all chunks present: CRC32 verify + retain
    void clearTransfer();             // free and reset the in-flight slot
    void freeLatest();                // free the retained thumbnail
};

// ===========================
// Global Instance Access
// ===========================

extern ImageRxManager& ImageRx();

#endif // IMAGE_RX_MANAGER_H
