#ifndef COMMAND_PROTOCOL_H
#define COMMAND_PROTOCOL_H

#include <Arduino.h>
#include <stdint.h>
#include "common_types.h"
#include "image_protocol.h" // Phase 2 image/telemetry wire contract (packet structs + payloads)

// ===========================
// Camera Command Protocol
// Phase 1: Command Protocol & Control
// ===========================

// Command Packet Type (extends PacketType from common_types.h)
static constexpr PacketType PACKET_TYPE_COMMAND = static_cast<PacketType>(0x10);
static constexpr PacketType PACKET_TYPE_RESPONSE = static_cast<PacketType>(0x11);

// Response Packet Types
enum class ResponseType : uint8_t {
    ACK = 0x00,           // Command executed successfully
    NACK = 0x01,          // Command failed (unknown error)
    NACK_INVALID = 0x02,  // Invalid command
    NACK_PARAM = 0x03,    // Invalid parameter
    NACK_BUSY = 0x04,     // Camera busy
    STATUS = 0x05         // Status data response
};

// Camera Command IDs
enum class CameraCommand : uint8_t {
    // Manual capture
    CAPTURE_NOW = 0x01,

    // Camera settings
    SET_RESOLUTION = 0x02,
    SET_QUALITY = 0x03,
    SET_BRIGHTNESS = 0x04,
    SET_CONTRAST = 0x05,
    SET_SATURATION = 0x06,
    SET_EXPOSURE = 0x07,
    SET_WB_MODE = 0x08,

    // Auto-capture modes
    AUTO_CAPTURE_ENABLE = 0x10,
    AUTO_CAPTURE_DISABLE = 0x11,

    // Status query
    GET_STATUS = 0x20,

    // Image transfer (Phase 2) — payloads defined in image_protocol.h
    IMAGE_WINDOW_REQUEST = 0x30,     // base pulls a window of chunks (PayloadImageWindowRequest)
    SET_EVENT_THRESHOLDS = 0x31      // event-trigger thresholds (PayloadSetEventThresholds)
};

// White balance modes
enum class WhiteBalanceMode : uint8_t {
    WB_AUTO = 0,
    WB_SUNNY = 1,
    WB_CLOUDY = 2,
    WB_OFFICE = 3,
    WB_HOME = 4
};

// Frame size wire codes — protocol-internal values whose numbering DELIBERATELY
// DIFFERS from the esp32-camera framesize_t enum; the two are translated BY NAME
// in CommandHandler (framesizeFromInt forward, frameSizeFromEsp reverse). Never
// reinterpret one as the other numerically.
enum class FrameSize : uint8_t {
    FRAMESIZE_QQVGA = 5,   // 160x120
    FRAMESIZE_QVGA = 6,    // 320x240
    FRAMESIZE_HQVGA = 7,   // 240x176
    FRAMESIZE_CIF = 8,     // 400x296 (real CIF mode; the real QXGA is 2048x1536, unreachable on the OV2640)
    FRAMESIZE_VGA = 9,     // 640x480
    FRAMESIZE_SVGA = 10,   // 800x600
    FRAMESIZE_XGA = 11,    // 1024x768
    FRAMESIZE_SXGA = 12,   // 1280x1024
    FRAMESIZE_UXGA = 13    // 1600x1200
};

// ===========================
// Command Packet Structure
// ===========================

struct CommandPacket {
    PacketHeader header;     // From common_types.h
    PacketType type;         // PACKET_TYPE_COMMAND (0x10)
    CameraCommand cmd;       // Command ID
    uint16_t sequenceNumber; // Sequence for ACK matching
    uint16_t payloadLength;  // Payload size
    uint8_t payload[200];    // Command parameters (max 200 bytes)
    uint16_t crc16;         // CRC of command + payload
    uint8_t endByte1;       // 0x0D
    uint8_t endByte2;       // 0x0A
};

// ===========================
// Response Packet Structure
// ===========================

struct ResponsePacket {
    PacketHeader header;     // From common_types.h
    PacketType type;         // 0x11 for responses
    ResponseType responseType;// ACK/NACK/STATUS
    uint16_t refSequence;    // Reference to command sequence
    uint8_t data[50];        // Response data (optional)
    uint16_t dataLength;     // Data size
    uint16_t crc16;         // CRC
    uint8_t endByte1;       // 0x0D
    uint8_t endByte2;       // 0x0A
};

// ===========================
// Command Payloads
// ===========================

// SET_RESOLUTION payload
struct PayloadSetResolution {
    FrameSize frameSize;
    uint8_t reserved[3]; // Pad to 4 bytes
};

// SET_QUALITY payload
struct PayloadSetQuality {
    uint8_t quality;      // 0-63 (lower is better)
    uint8_t reserved[3];
};

// SET_BRIGHTNESS payload
struct PayloadSetBrightness {
    int8_t brightness;    // -2 to 2
    uint8_t reserved[3];
};

// SET_CONTRAST payload
struct PayloadSetContrast {
    int8_t contrast;      // -2 to 2
    uint8_t reserved[3];
};

// SET_SATURATION payload
struct PayloadSetSaturation {
    int8_t saturation;    // -2 to 2
    uint8_t reserved[3];
};

// SET_EXPOSURE payload
struct PayloadSetExposure {
    int8_t exposureLevel; // -2 to 2
    uint8_t reserved[3];
};

// SET_WB_MODE payload
struct PayloadSetWBMode {
    WhiteBalanceMode wbMode;
    uint8_t reserved[3];
};

// AUTO_CAPTURE_ENABLE payload
struct PayloadAutoCaptureEnable {
    uint32_t intervalMs;  // Capture interval in milliseconds
};

// CAPTURE_NOW response data
struct ResponseCaptureData {
    uint16_t imageId;      // Assigned image ID
    uint16_t imageSize;    // Captured image size (if available)
};

// GET_STATUS response data
// NOTE: this struct is memcpy'd little-endian on BOTH ends — a documented
// island (same-platform pair, internally consistent). Do NOT mix
// big-endian-encoded fields into it; new wire fields use writeUint16/writeUint32
// in their own bodies. The Phase 2 event-threshold fields below were consumed
// from the old reserved pad: reserved shrank 17 -> 10, field-sum stays 28
// bytes (compiler padding keeps sizeof at 32, still <= CMD_MAX_RESPONSE_DATA).
struct ResponseStatusData {
    uint16_t imageId;      // Last captured image ID
    uint8_t autoCaptureEnabled;
    uint32_t autoCaptureInterval;
    FrameSize currentResolution;
    uint8_t currentQuality;
    int8_t currentBrightness;
    int8_t currentContrast;
    // Event-trigger thresholds (Phase 2, reported by 02-04)
    uint16_t eventThresholdAltM;   // altitude-delta trigger threshold (m)
    uint16_t eventThresholdDistM;  // horizontal-distance delta threshold (m)
    uint16_t eventMinSpacingSec;   // D-28 global minimum capture spacing (s)
    uint8_t eventFlags;            // bit0 eventsEnabled
    uint8_t reserved[10]; // Pad to structure
};

// ===========================
// Protocol Constants
// ===========================

// Packet limits
static constexpr size_t CMD_MAX_PAYLOAD_SIZE = 200;
static constexpr size_t CMD_MAX_PACKET_SIZE = 240; // LoRa packet limit (shared by sender/handler buffers)
static constexpr size_t CMD_MAX_RESPONSE_DATA = 50;

// ACK-timeout windows by command class (D-05, replacing the flat 2000ms constant)
static constexpr uint32_t CMD_ACK_TIMEOUT_TRIGGER_MS = 2000;   // CAPTURE_NOW (fast capture)
static constexpr uint32_t CMD_ACK_TIMEOUT_SETTINGS_MS = 5000;  // SET_* commands + auto-capture config
static constexpr uint32_t CMD_ACK_TIMEOUT_COMPLEX_MS = 10000;  // GET_STATUS (multi-setting query)
static constexpr uint32_t CMD_ACK_TIMEOUT_WINDOW_MS = 15000;   // IMAGE_WINDOW_REQUEST — a 16-chunk
                                                              // stream costs ~4-6.5 s of airtime, so
                                                              // neither SETTINGS 5000 nor COMPLEX 10000
                                                              // fits (Phase 2, Pitfall 4)

// Exponential retry backoff base (D-07): 2000/4000/8000ms before retries 1/2/3
static constexpr uint32_t CMD_RETRY_BACKOFF_BASE_MS = 2000;

// Serialized header size in bytes (PacketHeader struct is padded to 8 by the compiler)
static constexpr size_t CMD_HEADER_SIZE = 7;

// Protocol version
static constexpr uint8_t CMD_PROTOCOL_VERSION = 0x01;

// Packet markers (matching common_types.h)
static constexpr uint8_t CMD_START_BYTE1 = 0xAA;
static constexpr uint8_t CMD_START_BYTE2 = 0x55;
static constexpr uint8_t CMD_END_BYTE1 = 0x0D;
static constexpr uint8_t CMD_END_BYTE2 = 0x0A;

// Retry settings
static constexpr uint8_t CMD_MAX_RETRIES = 3;

// ===========================
// Command Protocol Class
// ===========================

class CommandProtocol {
public:
    // Serialize command to byte buffer
    static bool serializeCommand(const CommandPacket& cmd, uint8_t* buffer, size_t& length);

    // Deserialize command from byte buffer
    static bool deserializeCommand(const uint8_t* buffer, size_t length, CommandPacket& cmd);

    // Serialize response to byte buffer
    static bool serializeResponse(const ResponsePacket& resp, uint8_t* buffer, size_t& length);

    // Deserialize response from byte buffer
    static bool deserializeResponse(const uint8_t* buffer, size_t length, ResponsePacket& resp);

    // --- Image/telemetry packets (Phase 2; bodies in image_protocol.h) ---
    // All emit/expect the complete framed packet (7-byte header + body +
    // CRC16 + 0x0D 0x0A) with the packet's own type field at byte 2.

    // Manifest (0x12): header bodyLen = IMG_MANIFEST_BODY_SIZE (27)
    static bool serializeManifest(const ImageManifestPacket& pkt, uint8_t* buffer, size_t& length);
    static bool deserializeManifest(const uint8_t* buffer, size_t length, ImageManifestPacket& pkt);

    // Chunk (0x13): header bodyLen carries dataLen (5-byte overhead + data)
    static bool serializeChunk(const ImageChunkPacket& pkt, uint8_t* buffer, size_t& length);
    static bool deserializeChunk(const uint8_t* buffer, size_t length, ImageChunkPacket& pkt);

    // Telemetry beacon (0x14): header bodyLen = 17
    static bool serializeTelemetryBeacon(const TelemetryBeaconPacket& pkt, uint8_t* buffer, size_t& length);
    static bool deserializeTelemetryBeacon(const uint8_t* buffer, size_t length, TelemetryBeaconPacket& pkt);

    // Create ACK response
    static ResponsePacket createACK(uint16_t refSequence, const uint8_t* data = nullptr, uint16_t dataLen = 0);

    // Create NACK response
    static ResponsePacket createNACK(uint16_t refSequence, ResponseType nackType, const char* message = nullptr);

    // Create STATUS response
    static ResponsePacket createStatus(uint16_t refSequence, const ResponseStatusData& status);

    // Calculate CRC16
    static uint16_t calculateCRC16(const uint8_t* data, size_t length);

    // Validate CRC
    static bool validateCRC(const uint8_t* buffer, size_t length);

    // Get command name for debugging
    static const char* commandToString(CameraCommand cmd);

    // Get response type name for debugging
    static const char* responseToString(ResponseType type);

    // Byte-order helpers (big-endian)
    static void writeUint16(uint8_t* buffer, uint16_t value);
    static void writeUint32(uint8_t* buffer, uint32_t value);
    static uint16_t readUint16(const uint8_t* buffer);
    static uint32_t readUint32(const uint8_t* buffer);
};

// ===========================
// Utility Functions
// ===========================

// Create a command packet with proper initialization
CommandPacket createCommandPacket(CameraCommand cmd, uint16_t sequence, const void* payload = nullptr, size_t payloadSize = 0);

// Create a response packet with proper initialization
ResponsePacket createResponsePacket(ResponseType type, uint16_t refSequence, const void* data = nullptr, size_t dataLen = 0);

// Create an image manifest packet — assigns PACKET_TYPE_IMAGE_MANIFEST (0x12)
// as the FIRST field (CR-01 lesson: the factory owns the wire type byte)
ImageManifestPacket createManifestPacket(const ImageManifestBody& body);

// Create an image chunk packet — assigns PACKET_TYPE_IMAGE_CHUNK (0x13) as
// the FIRST field and copies dataLen payload bytes
ImageChunkPacket createChunkPacket(uint16_t imageId, uint16_t chunkIndex, const uint8_t* data, uint8_t dataLen);

// Create a telemetry beacon packet — assigns PACKET_TYPE_TELEMETRY_BEACON
// (0x14) as the FIRST field (CR-01 lesson: the factory owns the wire type
// byte; callers never assign packet.type by hand)
TelemetryBeaconPacket createTelemetryBeaconPacket(const TelemetryBeaconBody& body);

#endif // COMMAND_PROTOCOL_H
