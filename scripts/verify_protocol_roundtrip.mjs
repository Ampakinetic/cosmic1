#!/usr/bin/env node
// ============================================================================
// Wire-format regression harness for the camera command protocol.
//
// MIRRORS src/command_protocol.cpp (CRC16, packet layouts, validation rules)
// and the length-driven receive framing in src/command_sender.cpp /
// src/command_handler.cpp. It MUST be updated whenever the wire format
// changes.
//
// Clauses — exits 0 only if ALL hold:
//   (a) every sequence number 1..65535 serializes into a packet that passes
//       the balloon-side validation rules (start bytes, end bytes, validateCRC)
//   (b) serializeCommand rejects a payload of CMD_MAX_PACKET_SIZE - 15 = 225
//       bytes (would produce a 241-byte packet) while accepting the 224-byte
//       boundary (240-byte packet exactly)
//   (c) a command whose payload contains the consecutive bytes 0x0D 0x0A
//       passes packet validation, and BOTH length-driven receivers
//       (response flavor + command flavor) accumulate it intact
//   (d) the DEFECTIVE pre-fix serializer variant (zero placeholder at header
//       offset 3, sequence patched in after the CRC is written) FAILS the
//       sequence sweep — proof the harness has teeth
//   (e) truncated or bogus-length streams never produce a packet (CR-03)
//   (f) the response construction path carries the documented type byte
//       (CR-01/WR-05): packets built through the createResponsePacket mirror
//       serialize with byte 2 == 0x11 on both the ACK and NACK paths
//       (identical bytes), while the defective no-type-assignment variant
//       emits 0x00 — proof the clause has teeth against the pre-fix factory
// ============================================================================

'use strict';

// --- Constants (transcribed from include/command_protocol.h) ---
const CMD_HEADER_SIZE = 7;
const CMD_MAX_PAYLOAD_SIZE = 200;   // commands
const CMD_MAX_RESPONSE_DATA = 50;   // responses
const CMD_MAX_PACKET_SIZE = 240;    // LoRa packet limit
const HANDLER_RX_BUFFER_SIZE = 256; // CommandHandler::receiveBuffer (already above protocol max)
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

// Transcribes createResponsePacket (src/command_protocol.cpp): the packet
// starts zero-initialized — type 0x00, which is exactly the pre-CR-01 defect —
// then the factory assigns its fields. The `defective` option models the
// pre-fix firmware that never assigned packet.type at all.
function createResponsePacketMirror(responseType, refSequence, data, { defective = false } = {}) {
    const packet = {
        type: 0x00, // ResponsePacket packet{} zero-initializes every field
        responseType: 0x00,
        refSequence: 0,
        data: Buffer.alloc(CMD_MAX_RESPONSE_DATA),
        dataLength: 0,
    };

    if (!defective) {
        packet.type = PACKET_TYPE_RESPONSE; // the CR-01 fix: type assigned at construction
    }
    packet.responseType = responseType;
    packet.refSequence = refSequence;
    const dataLen = data ? data.length : 0;
    packet.dataLength = dataLen > CMD_MAX_RESPONSE_DATA ? CMD_MAX_RESPONSE_DATA : dataLen;

    if (data && dataLen > 0) {
        Buffer.from(data.subarray(0, packet.dataLength)).copy(packet.data);
    }

    // crc16 = 0 (computed at serialization); endByte markers carried by the serializer
    return packet;
}

// Transcribes CommandProtocol::serializeResponse — emits the PACKET'S OWN
// type field at byte 2, exactly as the firmware serializer writes resp.type
// verbatim (src/command_protocol.cpp:159):
//   AA 55 | resp.type | refSeqLow | dataLength BE16 | CRC8 pad | responseType |
//   refSequence BE16 | dataLength low byte | data | CRC16 BE | 0D 0A
function serializeResponse(packet) {
    const dataLength = packet.dataLength;
    const packetLength = CMD_HEADER_SIZE + 4 + dataLength + 4;

    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return null; // rejected
    }

    const buffer = Buffer.alloc(packetLength);
    let offset = 0;

    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = packet.type;
    buffer[offset++] = packet.refSequence & 0xFF;
    buffer.writeUInt16BE(dataLength, offset); offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    buffer[offset++] = packet.responseType;
    buffer.writeUInt16BE(packet.refSequence, offset); offset += 2;
    buffer[offset++] = dataLength & 0xFF;

    if (dataLength > 0 && dataLength <= CMD_MAX_RESPONSE_DATA) {
        packet.data.copy(buffer, offset, 0, dataLength);
        offset += dataLength;
    }

    const crc16 = calculateCRC16(buffer, offset);
    buffer.writeUInt16BE(crc16, offset); offset += 2;

    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    return buffer.subarray(0, offset);
}

// ============================================================================
// Receive framing (transcribed from the length-driven processIncomingByte
// rule shared by CommandSender and CommandHandler — CR-03)
// ============================================================================

// bodyOverhead: 4 for responses (responseType + refSequence BE16 + dataLength),
//               5 for commands (cmd + sequence BE16 + payloadLength BE16).
// bufferSize:   sizeof the receiver's receiveBuffer member.
function makeLengthDrivenReceiver({ bodyOverhead, maxBody, bufferSize }) {
    const receiveBuffer = Buffer.alloc(bufferSize);
    let receiveIndex = 0;
    let inPacket = false;
    const packets = [];

    function resetReceiveState() {
        receiveIndex = 0;
        inPacket = false;
    }

    function processIncomingByte(byte) {
        if (!inPacket) {
            // Start-byte hunt: 0xAA then 0x55
            if (receiveIndex === 0 && byte === CMD_START_BYTE1) {
                receiveBuffer[receiveIndex++] = byte;
            } else if (receiveIndex === 1 && byte === CMD_START_BYTE2) {
                receiveBuffer[receiveIndex++] = byte;
                inPacket = true;
            } else {
                receiveIndex = 0;
            }
            return;
        }

        receiveBuffer[receiveIndex++] = byte;

        if (receiveIndex < CMD_HEADER_SIZE) {
            return;
        }

        // Big-endian body length from header offsets 4-5
        const bodyLen = (receiveBuffer[4] << 8) | receiveBuffer[5];
        const expectedTotal = CMD_HEADER_SIZE + bodyOverhead + bodyLen + 4;

        if (expectedTotal > bufferSize || bodyLen > maxBody) {
            resetReceiveState(); // bogus header
            return;
        }

        if (receiveIndex < expectedTotal) {
            return; // still accumulating
        }

        // End marker verified ONLY at the expected framed position
        if (receiveBuffer[expectedTotal - 2] === CMD_END_BYTE1 &&
            receiveBuffer[expectedTotal - 1] === CMD_END_BYTE2) {
            packets.push(Buffer.from(receiveBuffer.subarray(0, expectedTotal)));
        }
        resetReceiveState();
    }

    return { packets, feed: processIncomingByte };
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

// (c)+(e) Length-driven framing regression (CR-03)
{
    // Response flavor (base station): data starting with the 0x0D 0x0A pair
    const responseRx = makeLengthDrivenReceiver({ bodyOverhead: 4, maxBody: CMD_MAX_RESPONSE_DATA, bufferSize: CMD_MAX_PACKET_SIZE });
    const responsePacket = serializeResponse(createResponsePacketMirror(0x05, 42, Buffer.from([0x0D, 0x0A, 0x00, 0x00]))); // STATUS with CRLF-leading data
    for (const b of responsePacket) responseRx.feed(b);
    assert(responseRx.packets.length === 1 &&
           responseRx.packets[0].equals(responsePacket) &&
           validateCRC(responseRx.packets[0], responseRx.packets[0].length),
           '(c) response receiver accumulates a packet with 0x0D 0x0A leading the data, intact');

    // Command flavor (balloon): payload containing the 0x0D 0x0A pair mid-payload
    const commandRx = makeLengthDrivenReceiver({ bodyOverhead: 5, maxBody: CMD_MAX_PAYLOAD_SIZE, bufferSize: HANDLER_RX_BUFFER_SIZE });
    const commandPacket = serializeCommand(9, 0x10, Buffer.from([0x00, 0x0D, 0x0A, 0x00]));
    for (const b of commandPacket) commandRx.feed(b);
    assert(commandRx.packets.length === 1 &&
           commandRx.packets[0].equals(commandPacket) &&
           balloonAccepts(commandRx.packets[0]),
           '(c) command receiver accumulates a packet with embedded 0x0D 0x0A payload, intact');

    // Truncated stream: half the expected bytes, then idle — nothing validates
    const truncatedRx = makeLengthDrivenReceiver({ bodyOverhead: 4, maxBody: CMD_MAX_RESPONSE_DATA, bufferSize: CMD_MAX_PACKET_SIZE });
    const full = serializeResponse(createResponsePacketMirror(0x00, 5, Buffer.alloc(20)));
    for (const b of full.subarray(0, Math.floor(full.length / 2))) truncatedRx.feed(b);
    assert(truncatedRx.packets.length === 0, '(e) truncated stream (half the expected bytes, then idle) produces no packet');

    // Bogus header length: bodyLen above the protocol maximum resets the receiver
    const bogusRx = makeLengthDrivenReceiver({ bodyOverhead: 4, maxBody: CMD_MAX_RESPONSE_DATA, bufferSize: CMD_MAX_PACKET_SIZE });
    bogusRx.feed(CMD_START_BYTE1);
    bogusRx.feed(CMD_START_BYTE2);
    bogusRx.feed(PACKET_TYPE_RESPONSE);
    bogusRx.feed(0x00);
    bogusRx.feed(0x01); // bodyLen high byte...
    bogusRx.feed(0xF4); // ...bodyLen = 500 > CMD_MAX_RESPONSE_DATA
    bogusRx.feed(0x00);
    for (let i = 0; i < 32; i++) bogusRx.feed(0x00);
    assert(bogusRx.packets.length === 0, '(e) bogus header length (bodyLen over protocol max) resets the receiver, no packet');
}

// (f) Response construction path carries the documented type byte (CR-01/WR-05)
{
    const RESPONSE_TYPE_ACK = 0x00;        // ResponseType::ACK
    const RESPONSE_TYPE_NACK_PARAM = 0x03; // ResponseType::NACK_PARAM

    // (f1) fixed-factory ACK: response built through the transcribed factory
    const ackPacket = serializeResponse(createResponsePacketMirror(RESPONSE_TYPE_ACK, 0x2A7B, Buffer.from([0x34, 0x12])));
    assert(ackPacket !== null && ackPacket[2] === PACKET_TYPE_RESPONSE, '(f1) ACK path: factory-built packet serializes with byte 2 == 0x11');

    // (f2) fixed-factory NACK with a short message as data
    const nackPacket = serializeResponse(createResponsePacketMirror(RESPONSE_TYPE_NACK_PARAM, 0x2A7B, Buffer.from('invalid parameter')));
    assert(nackPacket !== null && nackPacket[2] === PACKET_TYPE_RESPONSE, '(f2) NACK path: factory-built packet serializes with byte 2 == 0x11');

    // (f3) both construction paths emit the identical type byte
    assert(ackPacket[2] === nackPacket[2], '(f3) ACK-path and NACK-path type bytes are identical');

    // (f4) defective variant: the pre-CR-01 factory that never assigned packet.type
    const defectivePacket = serializeResponse(createResponsePacketMirror(RESPONSE_TYPE_ACK, 0x2A7B, Buffer.from([0x34, 0x12]), { defective: true }));
    assert(defectivePacket !== null && defectivePacket[2] === 0x00 && defectivePacket[2] !== ackPacket[2], '(f4) defective variant (type never assigned): byte 2 equals 0x00 and differs from the fixed path');

    // The type byte sits inside the CRC-covered region — the fixed ACK still validates
    assert(validateCRC(ackPacket, ackPacket.length), '(f) fixed ACK packet passes validateCRC (type byte inside the CRC-covered region)');
}

// ============================================================================
console.log(failures === 0 ? '\nAll wire-format regression checks passed.' : `\n${failures} check(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);
