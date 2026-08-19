#include "command_protocol.h"

// ===========================
// CRC Calculation
// ===========================

uint16_t CommandProtocol::calculateCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

bool CommandProtocol::validateCRC(const uint8_t* buffer, size_t length) {
    if (length < CMD_HEADER_SIZE + 4) {
        return false;
    }

    // Calculate CRC of packet (excluding the CRC field itself)
    size_t crcDataLength = length - 4; // Exclude CRC16 + 2 end bytes
    uint16_t calculatedCRC = calculateCRC16(buffer, crcDataLength);

    // Extract CRC from packet (last 2 bytes before end bytes)
    uint16_t packetCRC = (static_cast<uint16_t>(buffer[length - 4]) << 8) |
                         static_cast<uint16_t>(buffer[length - 3]);

    return (calculatedCRC == packetCRC);
}

// ===========================
// Command Serialization
// ===========================

bool CommandProtocol::serializeCommand(const CommandPacket& cmd, uint8_t* buffer, size_t& length) {
    if (!buffer) {
        return false;
    }

    size_t offset = 0;
    size_t packetLength = CMD_HEADER_SIZE + 5 + cmd.payloadLength + 4; // header + cmd+seq+payloadLen + payload + crc+end

    // LoRa packet limit (CR-02): header 7 + command block 5 + CRC 2 + end 2 = 16 overhead.
    // WR-04: the payload bound is CMD_MAX_PAYLOAD_SIZE itself (200) — the old
    // packet-size-only check (224) accepted lengths whose payload bytes were
    // then silently NOT written (the write below only ran <= 200), emitting a
    // malformed packet whose header advertised absent payload bytes.
    if (packetLength > CMD_MAX_PACKET_SIZE || cmd.payloadLength > CMD_MAX_PAYLOAD_SIZE) {
        return false;
    }

    // Write header
    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = static_cast<uint8_t>(PACKET_TYPE_COMMAND);
    buffer[3] = static_cast<uint8_t>(cmd.sequenceNumber & 0xFF); // real sequence low byte at header offset 3 (CR-01) — CRC below covers it
    offset++;
    writeUint16(buffer + offset, cmd.payloadLength);
    offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte (skip for now, use CRC16 only)

    // Write command data
    buffer[offset++] = static_cast<uint8_t>(cmd.cmd);
    writeUint16(buffer + offset, cmd.sequenceNumber);
    offset += 2;
    writeUint16(buffer + offset, cmd.payloadLength);
    offset += 2;

    // Write payload (bound already enforced above — WR-04)
    if (cmd.payloadLength > 0) {
        memcpy(buffer + offset, cmd.payload, cmd.payloadLength);
        offset += cmd.payloadLength;
    }

    // Calculate and write CRC16
    uint16_t crc16 = calculateCRC16(buffer, offset);
    writeUint16(buffer + offset, crc16);
    offset += 2;

    // Write end bytes
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    length = offset;
    return true;
}

bool CommandProtocol::deserializeCommand(const uint8_t* buffer, size_t length, CommandPacket& cmd) {
    if (!buffer || length < CMD_HEADER_SIZE + 5 + 4) {
        return false;
    }

    // Validate packet markers
    if (buffer[0] != CMD_START_BYTE1 || buffer[1] != CMD_START_BYTE2) {
        return false;
    }

    if (buffer[length - 2] != CMD_END_BYTE1 || buffer[length - 1] != CMD_END_BYTE2) {
        return false;
    }

    // Validate CRC
    if (!validateCRC(buffer, length)) {
        return false;
    }

    // Extract header fields
    cmd.type = static_cast<PacketType>(buffer[2]);

    // Extract command ID
    size_t dataStart = CMD_HEADER_SIZE;
    cmd.cmd = static_cast<CameraCommand>(buffer[dataStart]);

    // Extract sequence from packet data
    cmd.sequenceNumber = readUint16(buffer + dataStart + 1);

    // Extract payload length
    cmd.payloadLength = readUint16(buffer + dataStart + 3);

    // Extract payload
    if (cmd.payloadLength > 0 && cmd.payloadLength <= CMD_MAX_PAYLOAD_SIZE) {
        size_t payloadOffset = dataStart + 5;
        if (payloadOffset + cmd.payloadLength <= length - 4) {
            memcpy(cmd.payload, buffer + payloadOffset, cmd.payloadLength);
        } else {
            cmd.payloadLength = 0;
        }
    } else {
        cmd.payloadLength = 0;
    }

    return true;
}

// ===========================
// Response Serialization
// ===========================

bool CommandProtocol::serializeResponse(const ResponsePacket& resp, uint8_t* buffer, size_t& length) {
    if (!buffer) {
        return false;
    }

    size_t offset = 0;
    size_t packetLength = CMD_HEADER_SIZE + 4 + resp.dataLength + 4;

    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return false;
    }

    // Write header
    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = static_cast<uint8_t>(resp.type);
    buffer[offset++] = static_cast<uint8_t>(resp.refSequence & 0xFF);
    writeUint16(buffer + offset, resp.dataLength);
    offset += 2;
    buffer[offset++] = 0x00; // CRC8 placeholder

    // Write response data
    buffer[offset++] = static_cast<uint8_t>(resp.responseType);
    writeUint16(buffer + offset, resp.refSequence);
    offset += 2;
    buffer[offset++] = static_cast<uint8_t>(resp.dataLength & 0xFF);

    // Write data payload
    if (resp.dataLength > 0 && resp.dataLength <= CMD_MAX_RESPONSE_DATA) {
        memcpy(buffer + offset, resp.data, resp.dataLength);
        offset += resp.dataLength;
    }

    // Calculate and write CRC16
    uint16_t crc16 = calculateCRC16(buffer, offset);
    writeUint16(buffer + offset, crc16);
    offset += 2;

    // Write end bytes
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    length = offset;
    return true;
}

bool CommandProtocol::deserializeResponse(const uint8_t* buffer, size_t length, ResponsePacket& resp) {
    if (!buffer || length < CMD_HEADER_SIZE + 4 + 4) {
        return false;
    }

    // Validate packet markers
    if (buffer[0] != CMD_START_BYTE1 || buffer[1] != CMD_START_BYTE2) {
        return false;
    }

    if (buffer[length - 2] != CMD_END_BYTE1 || buffer[length - 1] != CMD_END_BYTE2) {
        return false;
    }

    // Validate CRC
    if (!validateCRC(buffer, length)) {
        return false;
    }

    // Extract header
    resp.type = static_cast<PacketType>(buffer[2]);

    // Extract response data
    size_t dataStart = CMD_HEADER_SIZE;
    resp.responseType = static_cast<ResponseType>(buffer[dataStart]);
    resp.refSequence = readUint16(buffer + dataStart + 1);
    resp.dataLength = static_cast<uint8_t>(buffer[dataStart + 3]);

    // Extract data payload
    if (resp.dataLength > 0 && resp.dataLength <= CMD_MAX_RESPONSE_DATA) {
        size_t payloadOffset = dataStart + 4;
        if (payloadOffset + resp.dataLength <= length - 4) {
            memcpy(resp.data, buffer + payloadOffset, resp.dataLength);
        } else {
            resp.dataLength = 0;
        }
    } else {
        resp.dataLength = 0;
    }

    return true;
}

// ===========================
// Response Creation Helpers
// ===========================

ResponsePacket CommandProtocol::createACK(uint16_t refSequence, const uint8_t* data, uint16_t dataLen) {
    ResponsePacket resp{};
    resp.type = PACKET_TYPE_RESPONSE;
    resp.responseType = ResponseType::ACK;
    resp.refSequence = refSequence;
    resp.dataLength = (dataLen > CMD_MAX_RESPONSE_DATA) ? CMD_MAX_RESPONSE_DATA : dataLen;

    if (data && dataLen > 0) {
        memcpy(resp.data, data, resp.dataLength);
    }

    resp.endByte1 = CMD_END_BYTE1;
    resp.endByte2 = CMD_END_BYTE2;

    return resp;
}

ResponsePacket CommandProtocol::createNACK(uint16_t refSequence, ResponseType nackType, const char* message) {
    ResponsePacket resp{};
    resp.type = PACKET_TYPE_RESPONSE;
    resp.responseType = nackType;
    resp.refSequence = refSequence;

    // Add message as data if provided
    if (message) {
        size_t msgLen = strlen(message);
        resp.dataLength = (msgLen > CMD_MAX_RESPONSE_DATA) ? CMD_MAX_RESPONSE_DATA : msgLen;
        memcpy(resp.data, message, resp.dataLength);
    }

    resp.endByte1 = CMD_END_BYTE1;
    resp.endByte2 = CMD_END_BYTE2;

    return resp;
}

ResponsePacket CommandProtocol::createStatus(uint16_t refSequence, const ResponseStatusData& status) {
    ResponsePacket resp{};
    resp.type = PACKET_TYPE_RESPONSE;
    resp.responseType = ResponseType::STATUS;
    resp.refSequence = refSequence;
    resp.dataLength = sizeof(ResponseStatusData);

    memcpy(resp.data, &status, resp.dataLength);

    resp.endByte1 = CMD_END_BYTE1;
    resp.endByte2 = CMD_END_BYTE2;

    return resp;
}

// ===========================
// Image/Telemetry Packet Serialization (Phase 2)
// ===========================
// All bodies are encoded big-endian field-by-field via the helpers above —
// never memcpy'd native structs (Pitfall 9 / IN-08). The serializers emit the
// packet's own type field at byte 2, so the factory type assignment
// (createManifestPacket/createChunkPacket) is what the wire actually carries.

bool CommandProtocol::serializeManifest(const ImageManifestPacket& pkt, uint8_t* buffer, size_t& length) {
    if (!buffer) {
        return false;
    }

    size_t packetLength = CMD_HEADER_SIZE + IMG_MANIFEST_BODY_SIZE + 4; // 7 + 27 + 4 = 38
    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return false;
    }

    size_t offset = 0;

    // Header — bodyLen carries the fixed manifest body size
    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = static_cast<uint8_t>(pkt.type);
    buffer[offset++] = static_cast<uint8_t>(pkt.body.imageId & 0xFF); // header seq echo: imageId low byte
    writeUint16(buffer + offset, static_cast<uint16_t>(IMG_MANIFEST_BODY_SIZE));
    offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    // Body — 27 bytes, field-by-field
    writeUint16(buffer + offset, pkt.body.imageId); offset += 2;
    buffer[offset++] = pkt.body.imageKind;
    buffer[offset++] = pkt.body.captureSource;
    writeUint32(buffer + offset, pkt.body.totalSize); offset += 4;
    writeUint16(buffer + offset, pkt.body.chunkSize); offset += 2;
    writeUint16(buffer + offset, pkt.body.totalChunks); offset += 2;
    writeUint32(buffer + offset, pkt.body.crc32); offset += 4;
    writeUint32(buffer + offset, pkt.body.captureTimeMs); offset += 4;
    buffer[offset++] = pkt.body.resolution;
    buffer[offset++] = pkt.body.quality;
    buffer[offset++] = static_cast<uint8_t>(pkt.body.brightness);
    buffer[offset++] = static_cast<uint8_t>(pkt.body.contrast);
    buffer[offset++] = static_cast<uint8_t>(pkt.body.saturation);
    buffer[offset++] = static_cast<uint8_t>(pkt.body.exposure);
    buffer[offset++] = pkt.body.wbMode;

    // CRC16 + end bytes
    uint16_t crc16 = calculateCRC16(buffer, offset);
    writeUint16(buffer + offset, crc16);
    offset += 2;
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    length = offset;
    return true;
}

bool CommandProtocol::deserializeManifest(const uint8_t* buffer, size_t length, ImageManifestPacket& pkt) {
    if (!buffer || length != CMD_HEADER_SIZE + IMG_MANIFEST_BODY_SIZE + 4) {
        return false;
    }

    if (buffer[0] != CMD_START_BYTE1 || buffer[1] != CMD_START_BYTE2) {
        return false;
    }

    if (buffer[length - 2] != CMD_END_BYTE1 || buffer[length - 1] != CMD_END_BYTE2) {
        return false;
    }

    if (!validateCRC(buffer, length)) {
        return false;
    }

    pkt.type = static_cast<PacketType>(buffer[2]);

    size_t off = CMD_HEADER_SIZE;
    pkt.body.imageId = readUint16(buffer + off); off += 2;
    pkt.body.imageKind = buffer[off++];
    pkt.body.captureSource = buffer[off++];
    pkt.body.totalSize = readUint32(buffer + off); off += 4;
    pkt.body.chunkSize = readUint16(buffer + off); off += 2;
    pkt.body.totalChunks = readUint16(buffer + off); off += 2;
    pkt.body.crc32 = readUint32(buffer + off); off += 4;
    pkt.body.captureTimeMs = readUint32(buffer + off); off += 4;
    pkt.body.resolution = buffer[off++];
    pkt.body.quality = buffer[off++];
    pkt.body.brightness = static_cast<int8_t>(buffer[off++]);
    pkt.body.contrast = static_cast<int8_t>(buffer[off++]);
    pkt.body.saturation = static_cast<int8_t>(buffer[off++]);
    pkt.body.exposure = static_cast<int8_t>(buffer[off++]);
    pkt.body.wbMode = buffer[off++];

    return true;
}

bool CommandProtocol::serializeChunk(const ImageChunkPacket& pkt, uint8_t* buffer, size_t& length) {
    if (!buffer) {
        return false;
    }

    // Chunk payload bound — the transport cannot frame more
    if (pkt.body.dataLen > IMG_CHUNK_PAYLOAD_SIZE) {
        return false;
    }

    size_t packetLength = CMD_HEADER_SIZE + 5 + pkt.body.dataLen + 4;
    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return false;
    }

    size_t offset = 0;

    // Header — bodyLen carries dataLen so chunk framing uses the command
    // arithmetic (7 + 5 + bodyLen + 4)
    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = static_cast<uint8_t>(pkt.type);
    buffer[offset++] = static_cast<uint8_t>(pkt.body.chunkIndex & 0xFF); // header seq echo: chunkIndex low byte
    writeUint16(buffer + offset, pkt.body.dataLen);
    offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    // Body — 5-byte overhead + data
    writeUint16(buffer + offset, pkt.body.imageId); offset += 2;
    writeUint16(buffer + offset, pkt.body.chunkIndex); offset += 2;
    buffer[offset++] = pkt.body.dataLen;
    if (pkt.body.dataLen > 0) {
        memcpy(buffer + offset, pkt.body.data, pkt.body.dataLen);
        offset += pkt.body.dataLen;
    }

    // CRC16 + end bytes
    uint16_t crc16 = calculateCRC16(buffer, offset);
    writeUint16(buffer + offset, crc16);
    offset += 2;
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    length = offset;
    return true;
}

bool CommandProtocol::deserializeChunk(const uint8_t* buffer, size_t length, ImageChunkPacket& pkt) {
    if (!buffer || length < CMD_HEADER_SIZE + 5 + 4) {
        return false;
    }

    if (buffer[0] != CMD_START_BYTE1 || buffer[1] != CMD_START_BYTE2) {
        return false;
    }

    if (buffer[length - 2] != CMD_END_BYTE1 || buffer[length - 1] != CMD_END_BYTE2) {
        return false;
    }

    if (!validateCRC(buffer, length)) {
        return false;
    }

    pkt.type = static_cast<PacketType>(buffer[2]);

    size_t off = CMD_HEADER_SIZE;
    pkt.body.imageId = readUint16(buffer + off); off += 2;
    pkt.body.chunkIndex = readUint16(buffer + off); off += 2;
    pkt.body.dataLen = buffer[off++];

    if (pkt.body.dataLen > IMG_CHUNK_PAYLOAD_SIZE) {
        return false;
    }

    // Frame arithmetic must agree: the header bodyLen field carries dataLen
    if (CMD_HEADER_SIZE + 5 + pkt.body.dataLen + 4 != length) {
        return false;
    }

    if (pkt.body.dataLen > 0) {
        memcpy(pkt.body.data, buffer + off, pkt.body.dataLen);
    }

    return true;
}

bool CommandProtocol::serializeTelemetryBeacon(const TelemetryBeaconPacket& pkt, uint8_t* buffer, size_t& length) {
    if (!buffer) {
        return false;
    }

    size_t packetLength = CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4; // 7 + 17 + 4 = 28
    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return false;
    }

    size_t offset = 0;

    // Header
    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = static_cast<uint8_t>(pkt.type);
    buffer[offset++] = static_cast<uint8_t>(pkt.body.seq & 0xFF); // header seq echo: beacon seq low byte
    writeUint16(buffer + offset, static_cast<uint16_t>(IMG_TELEMETRY_BEACON_BODY_SIZE));
    offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    // Body — 17 bytes, field-by-field
    writeUint16(buffer + offset, pkt.body.seq); offset += 2;
    writeUint32(buffer + offset, static_cast<uint32_t>(pkt.body.altitudeCm)); offset += 4;
    writeUint16(buffer + offset, static_cast<uint16_t>(pkt.body.tempCentiC)); offset += 2;
    writeUint32(buffer + offset, static_cast<uint32_t>(pkt.body.latE6)); offset += 4;
    writeUint32(buffer + offset, static_cast<uint32_t>(pkt.body.lonE6)); offset += 4;
    buffer[offset++] = pkt.body.flags;

    // CRC16 + end bytes
    uint16_t crc16 = calculateCRC16(buffer, offset);
    writeUint16(buffer + offset, crc16);
    offset += 2;
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    length = offset;
    return true;
}

bool CommandProtocol::deserializeTelemetryBeacon(const uint8_t* buffer, size_t length, TelemetryBeaconPacket& pkt) {
    if (!buffer || length != CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4) {
        return false;
    }

    if (buffer[0] != CMD_START_BYTE1 || buffer[1] != CMD_START_BYTE2) {
        return false;
    }

    if (buffer[length - 2] != CMD_END_BYTE1 || buffer[length - 1] != CMD_END_BYTE2) {
        return false;
    }

    if (!validateCRC(buffer, length)) {
        return false;
    }

    pkt.type = static_cast<PacketType>(buffer[2]);

    size_t off = CMD_HEADER_SIZE;
    pkt.body.seq = readUint16(buffer + off); off += 2;
    pkt.body.altitudeCm = static_cast<int32_t>(readUint32(buffer + off)); off += 4;
    pkt.body.tempCentiC = static_cast<int16_t>(readUint16(buffer + off)); off += 2;
    pkt.body.latE6 = static_cast<int32_t>(readUint32(buffer + off)); off += 4;
    pkt.body.lonE6 = static_cast<int32_t>(readUint32(buffer + off)); off += 4;
    pkt.body.flags = buffer[off++];

    return true;
}

// ===========================
// Debug Helpers
// ===========================

const char* CommandProtocol::commandToString(CameraCommand cmd) {
    switch (cmd) {
        case CameraCommand::CAPTURE_NOW: return "CAPTURE_NOW";
        case CameraCommand::SET_RESOLUTION: return "SET_RESOLUTION";
        case CameraCommand::SET_QUALITY: return "SET_QUALITY";
        case CameraCommand::SET_BRIGHTNESS: return "SET_BRIGHTNESS";
        case CameraCommand::SET_CONTRAST: return "SET_CONTRAST";
        case CameraCommand::SET_SATURATION: return "SET_SATURATION";
        case CameraCommand::SET_EXPOSURE: return "SET_EXPOSURE";
        case CameraCommand::SET_WB_MODE: return "SET_WB_MODE";
        case CameraCommand::AUTO_CAPTURE_ENABLE: return "AUTO_CAPTURE_ENABLE";
        case CameraCommand::AUTO_CAPTURE_DISABLE: return "AUTO_CAPTURE_DISABLE";
        case CameraCommand::GET_STATUS: return "GET_STATUS";
        case CameraCommand::IMAGE_WINDOW_REQUEST: return "IMAGE_WINDOW_REQUEST";
        case CameraCommand::SET_EVENT_THRESHOLDS: return "SET_EVENT_THRESHOLDS";
        default: return "UNKNOWN";
    }
}

const char* CommandProtocol::responseToString(ResponseType type) {
    switch (type) {
        case ResponseType::ACK: return "ACK";
        case ResponseType::NACK: return "NACK";
        case ResponseType::NACK_INVALID: return "NACK_INVALID";
        case ResponseType::NACK_PARAM: return "NACK_PARAM";
        case ResponseType::NACK_BUSY: return "NACK_BUSY";
        case ResponseType::STATUS: return "STATUS";
        default: return "UNKNOWN";
    }
}

// ===========================
// Helper Functions
// ===========================

void CommandProtocol::writeUint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;
}

void CommandProtocol::writeUint32(uint8_t* buffer, uint32_t value) {
    buffer[0] = (value >> 24) & 0xFF;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
}

uint16_t CommandProtocol::readUint16(const uint8_t* buffer) {
    return (static_cast<uint16_t>(buffer[0]) << 8) |
           static_cast<uint16_t>(buffer[1]);
}

uint32_t CommandProtocol::readUint32(const uint8_t* buffer) {
    return (static_cast<uint32_t>(buffer[0]) << 24) |
           (static_cast<uint32_t>(buffer[1]) << 16) |
           (static_cast<uint32_t>(buffer[2]) << 8) |
           static_cast<uint32_t>(buffer[3]);
}

// ===========================
// Utility Functions
// ===========================

CommandPacket createCommandPacket(CameraCommand cmd, uint16_t sequence, const void* payload, size_t payloadSize) {
    CommandPacket packet{};
    packet.type = PACKET_TYPE_COMMAND; // wire type assigned at construction (symmetry with createResponsePacket)
    packet.cmd = cmd;
    packet.sequenceNumber = sequence;
    packet.payloadLength = (payloadSize > CMD_MAX_PAYLOAD_SIZE) ?
                           CMD_MAX_PAYLOAD_SIZE : payloadSize;

    if (payload && payloadSize > 0) {
        memcpy(packet.payload, payload, packet.payloadLength);
    }

    packet.crc16 = 0; // Will be calculated during serialization
    packet.endByte1 = CMD_END_BYTE1;
    packet.endByte2 = CMD_END_BYTE2;

    return packet;
}

ResponsePacket createResponsePacket(ResponseType type, uint16_t refSequence, const void* data, size_t dataLen) {
    ResponsePacket packet{};
    packet.type = PACKET_TYPE_RESPONSE; // CR-01: documented 0x11 set where the packet is constructed — serializeResponse emits this field verbatim
    packet.responseType = type;
    packet.refSequence = refSequence;
    packet.dataLength = (dataLen > CMD_MAX_RESPONSE_DATA) ?
                        CMD_MAX_RESPONSE_DATA : dataLen;

    if (data && dataLen > 0) {
        memcpy(packet.data, data, packet.dataLength);
    }

    packet.crc16 = 0; // Will be calculated during serialization
    packet.endByte1 = CMD_END_BYTE1;
    packet.endByte2 = CMD_END_BYTE2;

    return packet;
}

ImageManifestPacket createManifestPacket(const ImageManifestBody& body) {
    ImageManifestPacket packet{};
    packet.type = PACKET_TYPE_IMAGE_MANIFEST; // CR-01 lesson: the factory owns the wire type byte — FIRST field assigned
    packet.body = body;

    return packet;
}

ImageChunkPacket createChunkPacket(uint16_t imageId, uint16_t chunkIndex, const uint8_t* data, uint8_t dataLen) {
    ImageChunkPacket packet{};
    packet.type = PACKET_TYPE_IMAGE_CHUNK; // CR-01 lesson: the factory owns the wire type byte — FIRST field assigned
    packet.body.imageId = imageId;
    packet.body.chunkIndex = chunkIndex;
    packet.body.dataLen = (dataLen > IMG_CHUNK_PAYLOAD_SIZE) ? IMG_CHUNK_PAYLOAD_SIZE : dataLen;

    if (data && packet.body.dataLen > 0) {
        memcpy(packet.body.data, data, packet.body.dataLen);
    }

    return packet;
}

TelemetryBeaconPacket createTelemetryBeaconPacket(const TelemetryBeaconBody& body) {
    TelemetryBeaconPacket packet{};
    packet.type = PACKET_TYPE_TELEMETRY_BEACON; // CR-01 lesson: the factory owns the wire type byte — FIRST field assigned
    packet.body = body;

    return packet;
}
