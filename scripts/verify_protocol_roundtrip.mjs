#!/usr/bin/env node
// ============================================================================
// Wire-format regression harness for the camera command protocol.
//
// MIRRORS src/command_protocol.cpp (CRC16, packet layouts, validation rules).
// It MUST be updated whenever the wire format changes.
//
// Clauses — exits 0 only if ALL hold:
//   (a) every sequence number 1..65535 serializes into a packet that passes
//       the balloon-side validation rules (start bytes, end bytes, validateCRC)
//   (b) serializeCommand rejects a payload of CMD_MAX_PACKET_SIZE - 15 = 225
//       bytes (would produce a 241-byte packet) while accepting the 224-byte
//       boundary (240-byte packet exactly)
//   (c) a command whose payload contains the consecutive bytes 0x0D 0x0A
//       passes packet validation (the receiver-framing survival regression
//       for CR-03 is added alongside the length-driven receiver code)
//   (d) the DEFECTIVE pre-fix serializer variant (zero placeholder at header
//       offset 3, sequence patched in after the CRC is written) FAILS the
//       sequence sweep — proof the harness has teeth
// ============================================================================

'use strict';

// --- Constants (transcribed from include/command_protocol.h) ---
const CMD_HEADER_SIZE = 7;
const CMD_MAX_PAYLOAD_SIZE = 200;   // commands
const CMD_MAX_RESPONSE_DATA = 50;   // responses
const CMD_MAX_PACKET_SIZE = 240;    // LoRa packet limit
const CMD_START_BYTE1 = 0xAA;
const CMD_START_BYTE2 = 0x55;
const CMD_END_BYTE1 = 0x0D;
const CMD_END_BYTE2 = 0x0A;
const PACKET_TYPE_COMMAND = 0x10;
const PACKET_TYPE_RESPONSE = 0x11;

// --- CRC16 (transcribed from CommandProtocol::calculateCRC16) ---
// Modbus-style: initial 0xFFFF, reflected polynomial 0xA001,
// per-byte XOR then 8 shift steps.
function calculateCRC16(data, length) {
    let crc = 0xFFFF;
    for (let i = 0; i < length; i++) {
        crc ^= data[i];
        for (let j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc & 0xFFFF;
}

// --- validateCRC (transcribed from CommandProtocol::validateCRC) ---
// CRC over all bytes except the last 4 (CRC16 big-endian + 2 end bytes).
function validateCRC(buffer, length) {
    if (length < CMD_HEADER_SIZE + 4) {
        return false;
    }
    const crcDataLength = length - 4;
    const calculatedCRC = calculateCRC16(buffer, crcDataLength);
    const packetCRC = (buffer[length - 4] << 8) | buffer[length - 3];
    return calculatedCRC === packetCRC;
}

// ============================================================================
// Serializers (transcribed from src/command_protocol.cpp)
// ============================================================================

// Transcribes CommandProtocol::serializeCommand (fixed layout):
//   AA 55 | type | seqLow | payloadLength BE16 | CRC8 pad | cmd | seq BE16 |
//   payloadLength BE16 | payload | CRC16 BE | 0D 0A
//
// The DEFECTIVE pre-CR-01 layout is kept (defectivelyPatchSequenceAfterCrc)
// to prove the sweep below has teeth: the old code wrote 0x00 at header
// offset 3, computed the CRC over it, and patched the real sequence byte in
// after serialization — so the receiver's CRC recomputation failed for every
// sequence whose low byte is nonzero.
function serializeCommand(sequenceNumber, cmdByte, payload, { defective = false } = {}) {
    const payloadLength = payload ? payload.length : 0;
    const packetLength = CMD_HEADER_SIZE + 5 + payloadLength + 4;

    if (packetLength > CMD_MAX_PACKET_SIZE || payloadLength > CMD_MAX_PACKET_SIZE - 16) {
        return null; // rejected (CR-02)
    }

    const buffer = Buffer.alloc(packetLength);
    let offset = 0;

    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = PACKET_TYPE_COMMAND;
    buffer[offset++] = defective ? 0x00 : (sequenceNumber & 0xFF);
    buffer.writeUInt16BE(payloadLength, offset); offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    buffer[offset++] = cmdByte;
    buffer.writeUInt16BE(sequenceNumber, offset); offset += 2;
    buffer.writeUInt16BE(payloadLength, offset); offset += 2;

    if (payloadLength > 0 && payloadLength <= CMD_MAX_PAYLOAD_SIZE) {
        payload.copy(buffer, offset);
        offset += payloadLength;
    }

    const crc16 = calculateCRC16(buffer, offset);
    buffer.writeUInt16BE(crc16, offset); offset += 2;

    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    if (defective) {
        buffer[3] = sequenceNumber & 0xFF; // post-serialization patch (the CR-01 defect)
    }

    return buffer.subarray(0, offset);
}

// Transcribes CommandProtocol::serializeResponse:
//   AA 55 | type | refSeqLow | dataLength BE16 | CRC8 pad | responseType |
//   refSequence BE16 | dataLength low byte | data | CRC16 BE | 0D 0A
function serializeResponse(responseType, refSequence, data) {
    const dataLength = data ? data.length : 0;
    const packetLength = CMD_HEADER_SIZE + 4 + dataLength + 4;

    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return null; // rejected
    }

    const buffer = Buffer.alloc(packetLength);
    let offset = 0;

    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = PACKET_TYPE_RESPONSE;
    buffer[offset++] = refSequence & 0xFF;
    buffer.writeUInt16BE(dataLength, offset); offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    buffer[offset++] = responseType;
    buffer.writeUInt16BE(refSequence, offset); offset += 2;
    buffer[offset++] = dataLength & 0xFF;

    if (dataLength > 0 && dataLength <= CMD_MAX_RESPONSE_DATA) {
        data.copy(buffer, offset);
        offset += dataLength;
    }

    const crc16 = calculateCRC16(buffer, offset);
    buffer.writeUInt16BE(crc16, offset); offset += 2;

    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    return buffer.subarray(0, offset);
}

// ============================================================================
// Balloon-side packet acceptance (start bytes + end bytes + validateCRC —
// the checks deserializeCommand runs before trusting a framed packet)
// ============================================================================
function balloonAccepts(packet) {
    if (packet.length < CMD_HEADER_SIZE + 5 + 4) {
        return false;
    }
    if (packet[0] !== CMD_START_BYTE1 || packet[1] !== CMD_START_BYTE2) {
        return false;
    }
    if (packet[packet.length - 2] !== CMD_END_BYTE1 || packet[packet.length - 1] !== CMD_END_BYTE2) {
        return false;
    }
    return validateCRC(packet, packet.length);
}

// ============================================================================
// Assertions
// ============================================================================

let failures = 0;

function assert(condition, label) {
    if (condition) {
        console.log(`PASS  ${label}`);
    } else {
        failures += 1;
        console.error(`FAIL  ${label}`);
    }
}

// (a) Full sequence sweep, fixed serializer
{
    let accepted = 0;
    for (let seq = 1; seq <= 0xFFFF; seq++) {
        const packet = serializeCommand(seq, 0x01, null);
        if (packet && balloonAccepts(packet)) {
            accepted += 1;
        }
    }
    assert(accepted === 0xFFFF, `(a) fixed serializer: all 65535 sequences (1..65535) pass balloon-side validation (${accepted}/65535)`);
}

// (b) Oversize rejection and boundary acceptance
{
    const oversize = serializeCommand(1, 0x01, Buffer.alloc(CMD_MAX_PACKET_SIZE - 15)); // 225-byte payload -> 241-byte packet
    const boundary = serializeCommand(1, 0x01, Buffer.alloc(CMD_MAX_PACKET_SIZE - 16)); // 224-byte payload -> exactly 240 bytes
    assert(oversize === null, '(b) serializeCommand rejects a 225-byte payload (241-byte packet)');
    assert(boundary !== null && balloonAccepts(boundary), '(b) serializeCommand accepts the 224-byte payload boundary (240-byte packet)');
}

// (c) Embedded end-marker bytes survive as packet content
{
    const packet = serializeCommand(7, 0x10, Buffer.from([0x00, 0x0D, 0x0A, 0x00]));
    assert(packet !== null && balloonAccepts(packet), '(c) command payload containing 0x0D 0x0A validates as a packet');
}

// (d) Defective pre-CR-01 variant fails the sweep (harness teeth)
{
    let accepted = 0;
    let seq1Accepted = false;
    for (let seq = 1; seq <= 0xFFFF; seq++) {
        const packet = serializeCommand(seq, 0x01, null, { defective: true });
        if (packet && balloonAccepts(packet)) {
            accepted += 1;
            if (seq === 1) seq1Accepted = true;
        }
    }
    // Only sequences whose low byte is 0 (multiples of 256) passed: 255 of 65535
    assert(accepted === 255, `(d) defective variant: sweep fails (${accepted}/65535 accepted — expected exactly 255)`);
    assert(!seq1Accepted, "(d) defective variant: sequence 1 (the sender's first command) is rejected");
}

// ============================================================================
console.log(failures === 0 ? '\nAll wire-format regression checks passed.' : `\n${failures} check(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);
