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

// Chunk framing budget: 7 header + 6 chunk overhead + 200 payload + 4 trailer
// = 217 <= CMD_MAX_PACKET_SIZE (240)
static constexpr uint8_t  IMG_CHUNK_PAYLOAD_SIZE     = 200;

// D-21 suggested window (chunks requested per pull round)
static constexpr uint8_t  IMG_WINDOW_MAX_CHUNKS      = 16;

// Balloon transfer-queue depth (PSRAM budget ~57 KB of 8 MB); overflow drops
// the OLDEST entry with a Serial warning — never silently
static constexpr uint8_t  IMG_TX_QUEUE_DEPTH         = 3;

// D-24: bounded retransmit passes over missing chunks before finalizing
// an image incomplete
static constexpr uint8_t  IMG_RETRANSMIT_MAX_PASSES  = 3;

// CR-02 / WR-01 (01-14): a manifest transmit failure must not consume its
// one-shot state — the ANNOUNCED / PUSH_THUMB_CHUNKS advance happens only on
// a successful transmit, and failures retry on later process() passes (one
// transmit per pass is the existing pacing; a manifest frame is 34 B — the
// cheapest frame class) bounded by this count so a dead link can never spin
// the announce. At the bound the payload buffer is freed and the kind
// degrades honestly (full dropped to thumbnail-only / thumbnail dropped to
// full-only) with a named log — never silent (PRI-03).
static constexpr uint8_t  IMG_MANIFEST_MAX_ATTEMPTS = 3;

// Full-image transfer cap (research Q4 resolution): fulls larger than this
// never arm a pull — the balloon logs a warning naming the image ID and size
// while the thumbnail still pushes, and the base never sees a FULL_IMAGE
// manifest for them (no airtime is wasted on a doomed transfer). PAIRED with
// MAX_IMAGE_SIZE (50000) in include/base_station_config.h — the base-side
// manifest-validation bound; the two must stay equal or the balloon would
// announce manifests the base rejects.
static constexpr uint32_t IMG_MAX_IMAGE_SIZE         = 50000;

static constexpr uint32_t IMG_WINDOW_STALL_MS        = 8000;

// Bounded push/window interleaving (02-05 / CR-03 fix a): when an armed
// window's entry has waited longer than this, the balloon PREEMPTS its own
// push work to service one window chunk. Must stay STRICTLY below
// IMG_WINDOW_STALL_MS (8000) — the base's stall clock resets on every
// accepted chunk, so a window serviced within this bound can never trip the
// base's 8 s stall while the balloon still holds it armed.
static constexpr uint32_t IMG_WINDOW_SERVICE_PREEMPT_MS = 5000;

// Inter-window RX-settle gap (01-12 / G-01-7 lever 3): after a window's last
// chunk lands (base side) or a window is freshly (re-)armed (balloon side),
// the FIRST chunk traffic of the next exchange waits out this gap. The 01-11
// session-2 discriminator proved the class it treats: five immediate tail
// re-requests for the 13..14 / 14..14 windows (seq 53/55-59, base2.log:415-
// 498, 6 END MARKER MISS) where the balloon armed and sent each time yet the
// chunks never arrived — the half-duplex immediate-retransmit turnaround-
// collision class: a re-request answered instantly collides with the link
// still turning around. INVARIANT CHAIN: settle (500) < preempt (5000) < stall
// (8000) — a settled window still preempts push work inside the 5000 ms bound
// and can never trip the base's 8000 ms stall clock.
static constexpr uint32_t IMG_WINDOW_RX_SETTLE_MS     = 500;

// Oversize-thumbnail heal fallback (02-05 / CR-01 base half): an oversize
// image's thumbnail never gets a FULL_IMAGE manifest (the balloon parks it at
// THUMB_PUSHED without announcing), so the manifest-arrived proof gate can
// never open. This idle bound (no chunk progress for this long after the
// push provably drained the queue elsewhere) is the only remaining evidence
// the push finished — after it, the base may heal a stalled thumbnail even
// without a same-id FULL slot, and the D-24 3-pass bound then resolves the
// row honestly instead of an infinite RECEIVING stall. 24 s = 3x the stall
// window, beyond any plausible queued-push wait.
static constexpr uint32_t IMG_THUMB_HEAL_IDLE_MS      = 24000;

static constexpr uint32_t IMG_ENTRY_TTL_MS           = 900000;

// Minimal telemetry-over-E32 beacon cadence (PRI-01 / SC-5 observability;
// transmit side lands in 02-02 behind a blocking decision checkpoint)
static constexpr uint32_t TELEMETRY_BEACON_INTERVAL_MS = 5000;

// Fixed manifest body size on the wire (see ImageManifestBody)
static constexpr size_t   IMG_MANIFEST_BODY_SIZE     = 27;

// Fixed telemetry beacon body size on the wire (see TelemetryBeaconBody)
static constexpr size_t   IMG_TELEMETRY_BEACON_BODY_SIZE = 19;

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

// 0x13 body — 6-byte overhead + dataLen data bytes. The framed header's
// bodyLen field carries dataLen, so chunk framing uses the same arithmetic
// shape as commands: expectedTotal = 7 + 6 + bodyLen + 4.
struct ImageChunkBody {
    uint16_t imageId;     // BE16 — must match the in-flight manifest
    uint8_t  imageKind;   // ImageKind: THUMBNAIL or FULL_IMAGE (CR-01, 01-13) —
                          // late thumbnail-heal stragglers were misrouted into
                          // the active FULL slot when the kind was only
                          // inferred; the base now routes by exact
                          // (imageId, imageKind), and this byte is stamped from
                          // windowKind/push kind at both TX call sites
    uint16_t chunkIndex;  // BE16, 0-based
    uint8_t  dataLen;     // <= IMG_CHUNK_PAYLOAD_SIZE
    uint8_t  data[IMG_CHUNK_PAYLOAD_SIZE]; // first dataLen bytes valid
};

// 0x14 body — fixed 19 bytes: seq u16, altitudeCm i32, tempCentiC i16,
// latE6 i32, lonE6 i32, flags u8 (bit0 gpsValid, bit1 batteryValid),
// batteryMilliV u16. Battery rides the beacon (D-41) so ALRT-02 stays
// zero-extra-airtime; the mV field is truth only while bit1 is set.
struct TelemetryBeaconBody {
    uint16_t seq;          // BE16 — rolling beacon sequence
    int32_t  altitudeCm;   // BE32 — altitude in centimeters
    int16_t  tempCentiC;   // BE16 — temperature in centi-degrees C
    int32_t  latE6;        // BE32 — latitude degrees * 1e6
    int32_t  lonE6;        // BE32 — longitude degrees * 1e6
    uint8_t  flags;        // bit0 gpsValid, bit1 batteryValid
    uint16_t batteryMilliV; // BE16 — battery voltage in millivolts (valid iff bit1)
};

// ===========================
// Command Payloads (ride PACKET_TYPE_COMMAND frames)
// ===========================

// IMAGE_WINDOW_REQUEST (0x30) payload — 6 bytes:
//   imageId u16, imageKind u8, startChunk u16, count u8
struct PayloadImageWindowRequest {
    uint16_t imageId;     // BE16
    uint8_t  imageKind;   // u8 — ImageKind the window addresses (THUMBNAIL/FULL_IMAGE);
                          // makes thumbnail heals addressable (D-22, 02-05/CR-01)
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
    ImageChunkBody body;    // 6-byte overhead + dataLen data bytes
};

struct TelemetryBeaconPacket {
    PacketType type;        // PACKET_TYPE_TELEMETRY_BEACON (0x14) — factory-assigned
    TelemetryBeaconBody body; // fixed 19 bytes on the wire
};

#endif // IMAGE_PROTOCOL_H
