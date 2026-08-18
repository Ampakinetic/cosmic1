#ifndef IMAGE_PROTOCOL_H
#define IMAGE_PROTOCOL_H

#include <Arduino.h>
#include <stdint.h>
#include "common_types.h"

// ===========================
// Image Transfer Protocol
// Phase 2: Image Transmission (plan 02-01)
// ===========================
// The single wire contract for image/telemetry traffic on the E32 link.
// Laid out section-for-section after include/command_protocol.h; it rides the
// same 240-byte framed transport (7-byte header + body + CRC16 + 0x0D 0x0A)
// and reuses the Phase 1 CRC16/validateCRC/header conventions unchanged.
//
// ALL multi-byte fields in the bodies below are encoded big-endian on the
// wire via CommandProtocol::writeUint16/writeUint32 (Pitfall 9 / IN-08 —
// never memcpy native structs onto the wire).

// Image/telemetry packet types (extend the Phase 1 0x10/0x11 pair declared
// in command_protocol.h; the PacketType enum itself lives in common_types.h)
static constexpr PacketType PACKET_TYPE_IMAGE_MANIFEST   = static_cast<PacketType>(0x12);
static constexpr PacketType PACKET_TYPE_IMAGE_CHUNK      = static_cast<PacketType>(0x13);
static constexpr PacketType PACKET_TYPE_TELEMETRY_BEACON = static_cast<PacketType>(0x14);

// ===========================
// Enums
// ===========================

// What a manifest/chunk stream carries. D-22: one reliability mechanism for
// both kinds — a thumbnail with holes falls back to the same windowed pull.
enum class ImageKind : uint8_t {
    THUMBNAIL = 0,
    FULL_IMAGE = 1
};

// Which capture source produced an image (stamped via
// CameraManager::setLastCaptureSource, rides the manifest)
enum class CaptureSource : uint8_t {
    MANUAL = 0,
    INTERVAL = 1,
    EVENT_ALTITUDE = 2,
    EVENT_DISTANCE = 3,
    EVENT_PHASE = 4
};

// ===========================
// Transfer Constants
// ===========================

// Chunk framing budget: 7 header + 5 chunk overhead + 200 payload + 4 trailer
// = 216 <= CMD_MAX_PACKET_SIZE (240)
static constexpr uint8_t  IMG_CHUNK_PAYLOAD_SIZE     = 200;

// D-21 suggested window (chunks requested per pull round)
static constexpr uint8_t  IMG_WINDOW_MAX_CHUNKS      = 16;

// Balloon transfer-queue depth (PSRAM budget ~57 KB of 8 MB); overflow drops
// the OLDEST entry with a Serial warning — never silently
static constexpr uint8_t  IMG_TX_QUEUE_DEPTH         = 3;

// D-24: bounded retransmit passes over missing chunks before finalizing
// an image incomplete
static constexpr uint8_t  IMG_RETRANSMIT_MAX_PASSES  = 3;

// Full-image transfer cap (research Q4 resolution): fulls larger than this
// never arm a pull — the balloon logs a warning naming the image ID and size
// while the thumbnail still pushes, and the base never sees a FULL_IMAGE
// manifest for them (no airtime is wasted on a doomed transfer). PAIRED with
// MAX_IMAGE_SIZE (50000) in include/base_station_config.h — the base-side
// manifest-validation bound; the two must stay equal or the balloon would
// announce manifests the base rejects.
static constexpr uint32_t IMG_MAX_IMAGE_SIZE         = 50000;

static constexpr uint32_t IMG_WINDOW_STALL_MS        = 8000;
static constexpr uint32_t IMG_ENTRY_TTL_MS           = 900000;

// Minimal telemetry-over-E32 beacon cadence (PRI-01 / SC-5 observability;
// transmit side lands in 02-02 behind a blocking decision checkpoint)
static constexpr uint32_t TELEMETRY_BEACON_INTERVAL_MS = 5000;

// Fixed manifest body size on the wire (see ImageManifestBody)
static constexpr size_t   IMG_MANIFEST_BODY_SIZE     = 27;

// Fixed telemetry beacon body size on the wire (see TelemetryBeaconBody)
static constexpr size_t   IMG_TELEMETRY_BEACON_BODY_SIZE = 17;

// ===========================
// Wire Bodies
// ===========================

// 0x12 body — fixed 27 bytes:
//   imageId u16, imageKind u8, captureSource u8, totalSize u32, chunkSize u16,
//   totalChunks u16, crc32 u32, captureTimeMs u32, then the 7-byte
//   camera-settings trailer.
struct ImageManifestBody {
    uint16_t imageId;        // BE16 — balloon-assigned image ID (AutoCapture sequence)
    uint8_t  imageKind;      // ImageKind: THUMBNAIL or FULL_IMAGE
    uint8_t  captureSource;  // CaptureSource: what triggered the capture
    uint32_t totalSize;      // BE32 — byte length of THIS kind's payload
    uint16_t chunkSize;      // BE16 — payload bytes per chunk (IMG_CHUNK_PAYLOAD_SIZE)
    uint16_t totalChunks;    // BE16 — ceil(totalSize / chunkSize)
    uint32_t crc32;          // BE32 — end-to-end CRC32 over THIS kind's original
                             // bytes (D-23): thumbnail CRC over thumbnail bytes,
                             // full-image CRC over full bytes
    uint32_t captureTimeMs;  // BE32 — balloon millis at capture
    // 7-byte camera-settings trailer (values at capture time)
    uint8_t  resolution;     // FrameSize wire code (command_protocol.h)
    uint8_t  quality;
    int8_t   brightness;
    int8_t   contrast;
    int8_t   saturation;
    int8_t   exposure;
    uint8_t  wbMode;
};

// 0x13 body — 5-byte overhead + dataLen data bytes. The framed header's
// bodyLen field carries dataLen, so chunk framing uses the same arithmetic
// shape as commands: expectedTotal = 7 + 5 + bodyLen + 4.
struct ImageChunkBody {
    uint16_t imageId;     // BE16 — must match the in-flight manifest
    uint16_t chunkIndex;  // BE16, 0-based
    uint8_t  dataLen;     // <= IMG_CHUNK_PAYLOAD_SIZE
    uint8_t  data[IMG_CHUNK_PAYLOAD_SIZE]; // first dataLen bytes valid
};

// 0x14 body — fixed 17 bytes: seq u16, altitudeCm i32, tempCentiC i16,
// latE6 i32, lonE6 i32, flags u8 (bit0 gpsValid). Minimal telemetry-over-E32
// surface; richer telemetry stays Phase 3.
struct TelemetryBeaconBody {
    uint16_t seq;          // BE16 — rolling beacon sequence
    int32_t  altitudeCm;   // BE32 — altitude in centimeters
    int16_t  tempCentiC;   // BE16 — temperature in centi-degrees C
    int32_t  latE6;        // BE32 — latitude degrees * 1e6
    int32_t  lonE6;        // BE32 — longitude degrees * 1e6
    uint8_t  flags;        // bit0 gpsValid
};

// ===========================
// Command Payloads (ride PACKET_TYPE_COMMAND frames)
// ===========================

// IMAGE_WINDOW_REQUEST (0x30) payload — 5 bytes:
//   imageId u16, startChunk u16, count u8
struct PayloadImageWindowRequest {
    uint16_t imageId;     // BE16
    uint16_t startChunk;  // BE16 — first chunk index of the window
    uint8_t  count;       // chunks in this window (<= IMG_WINDOW_MAX_CHUNKS)
};

// SET_EVENT_THRESHOLDS (0x31) payload — 7 bytes:
//   altDeltaM u16, distDeltaM u16, minSpacingSec u16, flags u8
struct PayloadSetEventThresholds {
    uint16_t altDeltaM;      // BE16 — altitude-delta trigger threshold (meters)
    uint16_t distDeltaM;     // BE16 — horizontal-distance delta threshold (meters)
    uint16_t minSpacingSec;  // BE16 — D-28 global minimum spacing (seconds)
    uint8_t  flags;          // bit0 eventsEnabled
};

// ===========================
// Packet Structs
// ===========================
// In-memory representations whose `type` field is owned by the factories in
// command_protocol.cpp (createManifestPacket / createChunkPacket assign it
// as the FIRST field — the Phase 1 CR-01 lesson: the factory owns the wire
// type byte). The serializers emit pkt.type verbatim at byte 2.

struct ImageManifestPacket {
    PacketType type;        // PACKET_TYPE_IMAGE_MANIFEST (0x12) — factory-assigned
    ImageManifestBody body; // fixed 27 bytes on the wire
};

struct ImageChunkPacket {
    PacketType type;        // PACKET_TYPE_IMAGE_CHUNK (0x13) — factory-assigned
    ImageChunkBody body;    // 5-byte overhead + dataLen data bytes
};

struct TelemetryBeaconPacket {
    PacketType type;        // PACKET_TYPE_TELEMETRY_BEACON (0x14) — factory-assigned
    TelemetryBeaconBody body; // fixed 17 bytes on the wire
};

#endif // IMAGE_PROTOCOL_H
