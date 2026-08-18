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

    // LoRa packet limit (CR-02): header 7 + command block 5 + CRC 2 + end 2 = 16 overhead
    if (packetLength > CMD_MAX_PACKET_SIZE || cmd.payloadLength > CMD_MAX_PACKET_SIZE - 16) {
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

    // Write payload
    if (cmd.payloadLength > 0 && cmd.payloadLength <= CMD_MAX_PAYLOAD_SIZE) {
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
    resp.type = static_cast<PacketType>(0x11); // RESPONSE
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
    resp.type = static_cast<PacketType>(0x11); // RESPONSE
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
    resp.type = static_cast<PacketType>(0x11); // RESPONSE
    resp.responseType = ResponseType::STATUS;
    resp.refSequence = refSequence;
    resp.dataLength = sizeof(ResponseStatusData);

    memcpy(resp.data, &status, resp.dataLength);

    resp.endByte1 = CMD_END_BYTE1;
    resp.endByte2 = CMD_END_BYTE2;

    return resp;
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
