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
//
// Phase 2 image/telemetry clauses (bodies from include/image_protocol.h):
//   (img-a) manifest round-trip preserves every big-endian field; the framed
//       header's bodyLen field == IMG_MANIFEST_BODY_SIZE (27)
//   (img-b) the createManifestPacket mirror assigns the 0x12 type byte on a
//       sweep over many bodies; the defective no-type-assignment variant
//       emits 0x00 and is discarded by the type-dispatch receiver — teeth
//   (img-c) a chunk whose payload contains the consecutive bytes 0x0D 0x0A
//       round-trips intact through serialization AND length-driven framing
//   (img-d) serializeChunk rejects dataLen > IMG_CHUNK_PAYLOAD_SIZE (201)
//       while accepting the 200 boundary
//   (img-e) telemetry beacon round-trip preserves every field including
//       negative int32/int16 values
//   (img-f) window-request payload is 6 bytes (imageId BE16, imageKind u8,
//       startChunk BE16, count u8 — the kind byte at offset 2) and round-trips
//       symmetrically for boundary values of both kinds; event-threshold
//       payloads encode/decode symmetrically (big-endian)
//   (img-g) the receiver type-dispatch rule (WR-12): the balloon accepts
//       0x10 only; the base accepts 0x11/0x12/0x13/0x14; frames of any
//       other type are discarded before body arithmetic
//   (img-h) ResponseStatusData layout arithmetic totals 28 bytes after the
//       reserved shrink (17 -> 10) that made room for the event fields
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
// --- Phase 2 image/telemetry constants (transcribed from include/image_protocol.h) ---
const PACKET_TYPE_IMAGE_MANIFEST = 0x12;
const PACKET_TYPE_IMAGE_CHUNK = 0x13;
const PACKET_TYPE_TELEMETRY_BEACON = 0x14;
const IMG_CHUNK_PAYLOAD_SIZE = 200;
const IMG_MANIFEST_BODY_SIZE = 27;
const IMG_TELEMETRY_BEACON_BODY_SIZE = 17;

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
// Phase 2 image/telemetry serializers (transcribed from src/command_protocol.cpp)
// ============================================================================

// Transcribes createManifestPacket: ImageManifestPacket packet{} zero-initializes
// every field (type 0x00 — exactly the pre-fix defect shape), then the factory
// assigns the wire type. The `defective` option models a factory that never
// assigns packet.type at all.
function createManifestPacketMirror(body, { defective = false } = {}) {
    const packet = { type: 0x00, body: { ...body } };
    if (!defective) {
        packet.type = PACKET_TYPE_IMAGE_MANIFEST; // the factory owns the type byte — FIRST field assigned
    }
    return packet;
}

// Transcribes CommandProtocol::serializeManifest:
//   AA 55 | pkt.type | imageIdLow | bodyLen=27 BE16 | pad | 27-byte body | CRC16 BE | 0D 0A
// Body fields big-endian, field-by-field (writeUint16/writeUint32 mirrors).
function serializeManifest(pkt) {
    const packetLength = CMD_HEADER_SIZE + IMG_MANIFEST_BODY_SIZE + 4; // 38
    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return null;
    }
    const buffer = Buffer.alloc(packetLength);
    let offset = 0;

    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = pkt.type;
    buffer[offset++] = pkt.body.imageId & 0xFF;
    buffer.writeUInt16BE(IMG_MANIFEST_BODY_SIZE, offset); offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    buffer.writeUInt16BE(pkt.body.imageId, offset); offset += 2;
    buffer[offset++] = pkt.body.imageKind;
    buffer[offset++] = pkt.body.captureSource;
    buffer.writeUInt32BE(pkt.body.totalSize >>> 0, offset); offset += 4;
    buffer.writeUInt16BE(pkt.body.chunkSize, offset); offset += 2;
    buffer.writeUInt16BE(pkt.body.totalChunks, offset); offset += 2;
    buffer.writeUInt32BE(pkt.body.crc32 >>> 0, offset); offset += 4;
    buffer.writeUInt32BE(pkt.body.captureTimeMs >>> 0, offset); offset += 4;
    buffer[offset++] = pkt.body.resolution;
    buffer[offset++] = pkt.body.quality;
    buffer[offset++] = pkt.body.brightness & 0xFF;
    buffer[offset++] = pkt.body.contrast & 0xFF;
    buffer[offset++] = pkt.body.saturation & 0xFF;
    buffer[offset++] = pkt.body.exposure & 0xFF;
    buffer[offset++] = pkt.body.wbMode;

    const crc16 = calculateCRC16(buffer, offset);
    buffer.writeUInt16BE(crc16, offset); offset += 2;
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    return buffer.subarray(0, offset);
}

// Transcribes CommandProtocol::deserializeManifest (exact length, markers, CRC)
function deserializeManifest(buffer) {
    if (buffer.length !== CMD_HEADER_SIZE + IMG_MANIFEST_BODY_SIZE + 4) {
        return null;
    }
    if (buffer[0] !== CMD_START_BYTE1 || buffer[1] !== CMD_START_BYTE2) {
        return null;
    }
    if (buffer[buffer.length - 2] !== CMD_END_BYTE1 || buffer[buffer.length - 1] !== CMD_END_BYTE2) {
        return null;
    }
    if (!validateCRC(buffer, buffer.length)) {
        return null;
    }
    const s8 = (v) => (v >= 128 ? v - 256 : v);
    let off = CMD_HEADER_SIZE;
    const body = {};
    body.imageId = buffer.readUInt16BE(off); off += 2;
    body.imageKind = buffer[off++];
    body.captureSource = buffer[off++];
    body.totalSize = buffer.readUInt32BE(off); off += 4;
    body.chunkSize = buffer.readUInt16BE(off); off += 2;
    body.totalChunks = buffer.readUInt16BE(off); off += 2;
    body.crc32 = buffer.readUInt32BE(off); off += 4;
    body.captureTimeMs = buffer.readUInt32BE(off); off += 4;
    body.resolution = buffer[off++];
    body.quality = buffer[off++];
    body.brightness = s8(buffer[off++]);
    body.contrast = s8(buffer[off++]);
    body.saturation = s8(buffer[off++]);
    body.exposure = s8(buffer[off++]);
    body.wbMode = buffer[off++];
    return { type: buffer[2], body };
}

// Transcribes createChunkPacket (fixed) — same zero-init/defective pattern
function createChunkPacketMirror(imageId, chunkIndex, data, dataLen, { defective = false } = {}) {
    const packet = {
        type: 0x00, // ImageChunkPacket packet{} zero-initializes every field
        body: { imageId: 0, chunkIndex: 0, dataLen: 0, data: Buffer.alloc(IMG_CHUNK_PAYLOAD_SIZE) },
    };
    if (!defective) {
        packet.type = PACKET_TYPE_IMAGE_CHUNK; // the factory owns the type byte
    }
    packet.body.imageId = imageId;
    packet.body.chunkIndex = chunkIndex;
    packet.body.dataLen = dataLen > IMG_CHUNK_PAYLOAD_SIZE ? IMG_CHUNK_PAYLOAD_SIZE : dataLen;
    if (data && packet.body.dataLen > 0) {
        Buffer.from(data.subarray(0, packet.body.dataLen)).copy(packet.body.data);
    }
    return packet;
}

// Transcribes CommandProtocol::serializeChunk — header bodyLen carries
// dataLen, so chunk framing is 7 + 5 + bodyLen + 4 (the command arithmetic)
function serializeChunk(pkt) {
    if (pkt.body.dataLen > IMG_CHUNK_PAYLOAD_SIZE) {
        return null; // rejected — the transport cannot frame more
    }
    const packetLength = CMD_HEADER_SIZE + 5 + pkt.body.dataLen + 4;
    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return null;
    }
    const buffer = Buffer.alloc(packetLength);
    let offset = 0;

    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = pkt.type;
    buffer[offset++] = pkt.body.chunkIndex & 0xFF;
    buffer.writeUInt16BE(pkt.body.dataLen, offset); offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    buffer.writeUInt16BE(pkt.body.imageId, offset); offset += 2;
    buffer.writeUInt16BE(pkt.body.chunkIndex, offset); offset += 2;
    buffer[offset++] = pkt.body.dataLen;
    if (pkt.body.dataLen > 0) {
        pkt.body.data.copy(buffer, offset, 0, pkt.body.dataLen);
        offset += pkt.body.dataLen;
    }

    const crc16 = calculateCRC16(buffer, offset);
    buffer.writeUInt16BE(crc16, offset); offset += 2;
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    return buffer.subarray(0, offset);
}

// Transcribes CommandProtocol::deserializeChunk (markers, CRC, dataLen bound,
// frame-arithmetic agreement)
function deserializeChunk(buffer) {
    if (buffer.length < CMD_HEADER_SIZE + 5 + 4) {
        return null;
    }
    if (buffer[0] !== CMD_START_BYTE1 || buffer[1] !== CMD_START_BYTE2) {
        return null;
    }
    if (buffer[buffer.length - 2] !== CMD_END_BYTE1 || buffer[buffer.length - 1] !== CMD_END_BYTE2) {
        return null;
    }
    if (!validateCRC(buffer, buffer.length)) {
        return null;
    }
    let off = CMD_HEADER_SIZE;
    const body = { imageId: buffer.readUInt16BE(off), chunkIndex: 0, dataLen: 0, data: null };
    off += 2;
    body.chunkIndex = buffer.readUInt16BE(off); off += 2;
    body.dataLen = buffer[off++];
    if (body.dataLen > IMG_CHUNK_PAYLOAD_SIZE) {
        return null;
    }
    if (CMD_HEADER_SIZE + 5 + body.dataLen + 4 !== buffer.length) {
        return null;
    }
    body.data = Buffer.from(buffer.subarray(off, off + body.dataLen));
    return { type: buffer[2], body };
}

// Transcribes CommandProtocol::serializeTelemetryBeacon — 17-byte body.
// NOTE: the firmware has no beacon factory yet (transmit side is 02-02,
// gated behind a blocking decision checkpoint); the packet is constructed
// with the type set directly, which is what the serializer emits verbatim.
function serializeTelemetryBeacon(pkt) {
    const packetLength = CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4; // 28
    if (packetLength > CMD_MAX_PACKET_SIZE) {
        return null;
    }
    const buffer = Buffer.alloc(packetLength);
    let offset = 0;

    buffer[offset++] = CMD_START_BYTE1;
    buffer[offset++] = CMD_START_BYTE2;
    buffer[offset++] = pkt.type;
    buffer[offset++] = pkt.body.seq & 0xFF;
    buffer.writeUInt16BE(IMG_TELEMETRY_BEACON_BODY_SIZE, offset); offset += 2;
    buffer[offset++] = 0x00; // CRC8 pad byte

    buffer.writeUInt16BE(pkt.body.seq, offset); offset += 2;
    buffer.writeInt32BE(pkt.body.altitudeCm | 0, offset); offset += 4;
    buffer.writeInt16BE(pkt.body.tempCentiC | 0, offset); offset += 2;
    buffer.writeInt32BE(pkt.body.latE6 | 0, offset); offset += 4;
    buffer.writeInt32BE(pkt.body.lonE6 | 0, offset); offset += 4;
    buffer[offset++] = pkt.body.flags;

    const crc16 = calculateCRC16(buffer, offset);
    buffer.writeUInt16BE(crc16, offset); offset += 2;
    buffer[offset++] = CMD_END_BYTE1;
    buffer[offset++] = CMD_END_BYTE2;

    return buffer.subarray(0, offset);
}

// Transcribes CommandProtocol::deserializeTelemetryBeacon
function deserializeTelemetryBeacon(buffer) {
    if (buffer.length !== CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4) {
        return null;
    }
    if (buffer[0] !== CMD_START_BYTE1 || buffer[1] !== CMD_START_BYTE2) {
        return null;
    }
    if (buffer[buffer.length - 2] !== CMD_END_BYTE1 || buffer[buffer.length - 1] !== CMD_END_BYTE2) {
        return null;
    }
    if (!validateCRC(buffer, buffer.length)) {
        return null;
    }
    let off = CMD_HEADER_SIZE;
    const body = {};
    body.seq = buffer.readUInt16BE(off); off += 2;
    body.altitudeCm = buffer.readInt32BE(off); off += 4;
    body.tempCentiC = buffer.readInt16BE(off); off += 2;
    body.latE6 = buffer.readInt32BE(off); off += 4;
    body.lonE6 = buffer.readInt32BE(off); off += 4;
    body.flags = buffer[off++];
    return { type: buffer[2], body };
}

// --- Command payload codecs (big-endian, transcribed from the Phase 2
//     payload layouts in include/image_protocol.h) ---

function encodeWindowRequest(p) {
    const b = Buffer.alloc(6);
    b.writeUInt16BE(p.imageId, 0);
    b[2] = p.imageKind;
    b.writeUInt16BE(p.startChunk, 3);
    b[5] = p.count;
    return b;
}
function decodeWindowRequest(b) {
    return { imageId: b.readUInt16BE(0), imageKind: b[2], startChunk: b.readUInt16BE(3), count: b[5] };
}

function encodeThresholds(p) {
    const b = Buffer.alloc(7);
    b.writeUInt16BE(p.altDeltaM, 0);
    b.writeUInt16BE(p.distDeltaM, 2);
    b.writeUInt16BE(p.minSpacingSec, 4);
    b[6] = p.flags;
    return b;
}
function decodeThresholds(b) {
    return { altDeltaM: b.readUInt16BE(0), distDeltaM: b.readUInt16BE(2), minSpacingSec: b.readUInt16BE(4), flags: b[6] };
}

// ============================================================================
// Type-dispatch receiver (transcribes the WR-12 fix shape both firmwares
// implement: switch on the type byte at buffer[2] BEFORE body arithmetic;
// discard frames whose type the side does not accept)
// ============================================================================

const RECEIVER_DISPATCH_RULES = {
    balloon: [PACKET_TYPE_COMMAND],
    base: [PACKET_TYPE_RESPONSE, PACKET_TYPE_IMAGE_MANIFEST, PACKET_TYPE_IMAGE_CHUNK, PACKET_TYPE_TELEMETRY_BEACON],
};

// Per-type expectedTotal arithmetic (what each receiver computes once the
// type byte is known): manifest and beacon lengths are FORCED to their fixed
// body sizes; command/chunk read bodyLen from the header; response reads
// bodyLen from the header with its own 4-byte overhead.
function makeTypeDispatchReceiver(acceptedTypes) {
    const receiveBuffer = Buffer.alloc(CMD_MAX_PACKET_SIZE);
    let receiveIndex = 0;
    let inPacket = false;
    const packets = [];

    function resetReceiveState() {
        receiveIndex = 0;
        inPacket = false;
    }

    function processIncomingByte(byte) {
        if (!inPacket) {
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

        // WR-12: type dispatch BEFORE body arithmetic
        const typeByte = receiveBuffer[2];
        if (!acceptedTypes.includes(typeByte)) {
            resetReceiveState();
            return;
        }

        const bodyLen = (receiveBuffer[4] << 8) | receiveBuffer[5];
        let expectedTotal;
        switch (typeByte) {
            case PACKET_TYPE_COMMAND:
                if (bodyLen > CMD_MAX_PAYLOAD_SIZE) { resetReceiveState(); return; }
                expectedTotal = CMD_HEADER_SIZE + 5 + bodyLen + 4;
                break;
            case PACKET_TYPE_RESPONSE:
                if (bodyLen > CMD_MAX_RESPONSE_DATA) { resetReceiveState(); return; }
                expectedTotal = CMD_HEADER_SIZE + 4 + bodyLen + 4;
                break;
            case PACKET_TYPE_IMAGE_MANIFEST:
                expectedTotal = CMD_HEADER_SIZE + IMG_MANIFEST_BODY_SIZE + 4; // bodyLen forced to 27
                break;
            case PACKET_TYPE_IMAGE_CHUNK:
                if (bodyLen > IMG_CHUNK_PAYLOAD_SIZE) { resetReceiveState(); return; }
                expectedTotal = CMD_HEADER_SIZE + 5 + bodyLen + 4; // bodyLen == dataLen
                break;
            case PACKET_TYPE_TELEMETRY_BEACON:
                expectedTotal = CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4; // forced 17
                break;
            default:
                resetReceiveState();
                return;
        }

        if (expectedTotal > receiveBuffer.length) {
            resetReceiveState();
            return;
        }

        if (receiveIndex < expectedTotal) {
            return;
        }

        if (receiveBuffer[expectedTotal - 2] === CMD_END_BYTE1 &&
            receiveBuffer[expectedTotal - 1] === CMD_END_BYTE2) {
            packets.push(Buffer.from(receiveBuffer.subarray(0, expectedTotal)));
        }
        resetReceiveState();
    }

    return { packets, feed: processIncomingByte };
}

// ============================================================================
// Phase 2 assertions
// ============================================================================

// (img-a) Manifest round-trip: every big-endian field survives; the framed
// header's bodyLen field == 27
{
    const body = {
        imageId: 0xBEEF,
        imageKind: 0,        // THUMBNAIL
        captureSource: 2,    // EVENT_ALTITUDE
        totalSize: 3456,
        chunkSize: 200,
        totalChunks: 18,     // ceil(3456/200)
        crc32: 0xDEADBEEF >>> 0,
        captureTimeMs: 123456789,
        resolution: 5,       // FrameSize QQVGA wire code
        quality: 20,
        brightness: -2,
        contrast: 1,
        saturation: -1,
        exposure: 2,
        wbMode: 3,
    };
    const packet = serializeManifest(createManifestPacketMirror(body));
    assert(packet !== null && packet.length === CMD_HEADER_SIZE + IMG_MANIFEST_BODY_SIZE + 4,
        '(img-a) manifest serializes to exactly 7+27+4 = 38 bytes');
    assert(packet[2] === PACKET_TYPE_IMAGE_MANIFEST, '(img-a) manifest packet carries the 0x12 type byte at byte 2');
    assert(((packet[4] << 8) | packet[5]) === IMG_MANIFEST_BODY_SIZE,
        '(img-a) manifest framed header bodyLen field == 27');
    assert(validateCRC(packet, packet.length), '(img-a) manifest packet passes validateCRC');
    const round = deserializeManifest(packet);
    assert(round !== null &&
           round.body.imageId === body.imageId &&
           round.body.imageKind === body.imageKind &&
           round.body.captureSource === body.captureSource &&
           round.body.totalSize === body.totalSize &&
           round.body.chunkSize === body.chunkSize &&
           round.body.totalChunks === body.totalChunks &&
           round.body.crc32 === body.crc32 &&
           round.body.captureTimeMs === body.captureTimeMs &&
           round.body.resolution === body.resolution &&
           round.body.quality === body.quality &&
           round.body.brightness === body.brightness &&
           round.body.contrast === body.contrast &&
           round.body.saturation === body.saturation &&
           round.body.exposure === body.exposure &&
           round.body.wbMode === body.wbMode,
        '(img-a) manifest round-trip preserves every field (including signed i8 values and 0xBEEF ids)');
}

// (img-b) createManifestPacket assigns the 0x12 type byte — sweep with teeth
{
    let fixedOk = 0;
    let defectiveRejected = 0;
    const balloonRx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.balloon);
    const baseRx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.base);
    for (let id = 0; id <= 255; id++) {
        const body = { imageId: (id << 8) | id, imageKind: id & 1, captureSource: id % 5,
                       totalSize: 200 * id + 17, chunkSize: 200, totalChunks: id + 1,
                       crc32: (0xCAFEF00D + id) >>> 0, captureTimeMs: id * 1000,
                       resolution: 5 + (id % 9), quality: 10 + (id % 20),
                       brightness: (id % 5) - 2, contrast: (id % 5) - 2,
                       saturation: (id % 5) - 2, exposure: (id % 5) - 2, wbMode: id % 5 };
        const fixed = serializeManifest(createManifestPacketMirror(body));
        const defective = serializeManifest(createManifestPacketMirror(body, { defective: true }));
        if (fixed && fixed[2] === PACKET_TYPE_IMAGE_MANIFEST && validateCRC(fixed, fixed.length)) {
            fixedOk += 1;
        }
        // The defective variant emits type 0x00 (unknown to both sides) — the
        // dispatch rule must discard it; the fixed one must be accepted by base
        const dBase = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.base);
        for (const b of defective) dBase.feed(b);
        if (defective && defective[2] === 0x00 && dBase.packets.length === 0) {
            defectiveRejected += 1;
        }
        for (const b of fixed) baseRx.feed(b);
    }
    assert(fixedOk === 256, `(img-b) fixed factory: all 256 swept manifests carry the 0x12 type byte and validate (${fixedOk}/256)`);
    assert(defectiveRejected === 256, `(img-b) defective factory (type never assigned): all 256 variants emit 0x00 and are discarded by the dispatch rule (${defectiveRejected}/256)`);
    assert(baseRx.packets.length === 256, '(img-b) base dispatch receiver accepts all 256 fixed-factory manifests');
    // The balloon never accepts manifests at all
    const balloonOnly = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.balloon);
    for (const b of serializeManifest(createManifestPacketMirror({ imageId: 7, imageKind: 0, captureSource: 1, totalSize: 100, chunkSize: 200, totalChunks: 1, crc32: 5, captureTimeMs: 9, resolution: 5, quality: 20, brightness: 0, contrast: 0, saturation: 0, exposure: 0, wbMode: 0 }))) balloonOnly.feed(b);
    assert(balloonOnly.packets.length === 0, '(img-b) balloon dispatch receiver discards a manifest (not in its accepted set)');
}

// (img-c) Chunk round-trip with an embedded 0x0D 0x0A pair in the payload
{
    const payload = Buffer.from([0xFF, 0xD8, 0x0D, 0x0A, 0x00, 0x11, 0x0D, 0x0A, 0xFF, 0xD9]);
    const packet = serializeChunk(createChunkPacketMirror(0x1234, 77, payload, payload.length));
    assert(packet !== null && packet[2] === PACKET_TYPE_IMAGE_CHUNK,
        '(img-c) chunk packet carries the 0x13 type byte at byte 2');
    assert(((packet[4] << 8) | packet[5]) === payload.length,
        '(img-c) chunk framed header bodyLen field carries dataLen');

    // Serialization round-trip
    const round = deserializeChunk(packet);
    assert(round !== null &&
           round.body.imageId === 0x1234 &&
           round.body.chunkIndex === 77 &&
           round.body.dataLen === payload.length &&
           round.body.data.equals(payload),
        '(img-c) chunk round-trip preserves imageId/chunkIndex/dataLen and the payload bytes verbatim');

    // Length-driven framing round-trip (the embedded 0x0D 0x0A pairs must not
    // corrupt reassembly — end markers tested only at the framed position)
    const baseRx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.base);
    for (const b of packet) baseRx.feed(b);
    assert(baseRx.packets.length === 1 &&
           baseRx.packets[0].equals(packet) &&
           validateCRC(baseRx.packets[0], baseRx.packets[0].length),
        '(img-c) type-dispatch receiver accumulates a chunk containing embedded 0x0D 0x0A bytes, intact');
}

// (img-d) Oversize chunk rejection
{
    // Bypass the factory (which clamps) — the serializer must enforce the
    // transport bound independently, exactly as the firmware does
    const oversizePkt = { type: PACKET_TYPE_IMAGE_CHUNK,
                          body: { imageId: 1, chunkIndex: 0, dataLen: 201, data: Buffer.alloc(201) } };
    const oversize = serializeChunk(oversizePkt);
    const boundary = serializeChunk(createChunkPacketMirror(1, 0, Buffer.alloc(200), 200));
    assert(oversize === null, '(img-d) serializeChunk rejects dataLen 201 (> IMG_CHUNK_PAYLOAD_SIZE)');
    assert(boundary !== null && boundary.length === CMD_HEADER_SIZE + 5 + 200 + 4,
        '(img-d) serializeChunk accepts the 200-byte boundary (216-byte packet)');
}

// (img-e) Telemetry beacon round-trip
{
    const body = { seq: 4096, altitudeCm: -12345, tempCentiC: -450, latE6: 52367999, lonE6: -13456000, flags: 0x01 };
    const packet = serializeTelemetryBeacon({ type: PACKET_TYPE_TELEMETRY_BEACON, body });
    assert(packet !== null && packet.length === CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4,
        '(img-e) beacon serializes to exactly 7+17+4 = 28 bytes');
    assert(packet[2] === PACKET_TYPE_TELEMETRY_BEACON, '(img-e) beacon packet carries the 0x14 type byte at byte 2');
    assert(validateCRC(packet, packet.length), '(img-e) beacon packet passes validateCRC');
    const round = deserializeTelemetryBeacon(packet);
    assert(round !== null &&
           round.body.seq === body.seq &&
           round.body.altitudeCm === body.altitudeCm &&
           round.body.tempCentiC === body.tempCentiC &&
           round.body.latE6 === body.latE6 &&
           round.body.lonE6 === body.lonE6 &&
           round.body.flags === body.flags,
        '(img-e) beacon round-trip preserves every field including negative int32/int16 values');
}

// (img-f) Window-request and threshold payloads encode/decode symmetrically
{
    // Exact wire layout (02-05 / CR-01): imageId BE16, imageKind u8 at
    // offset 2, startChunk BE16, count u8 — 6 bytes total
    const exact = encodeWindowRequest({ imageId: 42, imageKind: 1, startChunk: 7, count: 16 });
    assert(exact.length === 6 &&
           exact[0] === 0x00 && exact[1] === 0x2A && exact[2] === 0x01 &&
           exact[3] === 0x00 && exact[4] === 0x07 && exact[5] === 0x10,
        '(img-f) window request encodes exactly 00 2A 01 00 07 10 (imageId BE16, imageKind u8, startChunk BE16, count u8)');

    const windows = [
        { imageId: 0, imageKind: 0, startChunk: 0, count: 1 },
        { imageId: 0xFFFF, imageKind: 1, startChunk: 0xFFFF, count: 16 },
        { imageId: 0x1234, imageKind: 0, startChunk: 240, count: 16 },
    ];
    let windowsOk = 0;
    for (const w of windows) {
        const round = decodeWindowRequest(encodeWindowRequest(w));
        if (round.imageId === w.imageId && round.imageKind === w.imageKind &&
            round.startChunk === w.startChunk && round.count === w.count) {
            windowsOk += 1;
        }
    }
    assert(windowsOk === windows.length && encodeWindowRequest(windows[0]).length === 6,
        '(img-f) window-request payload (6 bytes) round-trips symmetrically for boundary values (both kinds)');

    const thresholds = [
        { altDeltaM: 150, distDeltaM: 500, minSpacingSec: 20, flags: 1 },
        { altDeltaM: 0, distDeltaM: 0, minSpacingSec: 0, flags: 0 },
        { altDeltaM: 0xFFFF, distDeltaM: 0xFFFF, minSpacingSec: 0xFFFF, flags: 0x01 },
    ];
    let thresholdsOk = 0;
    for (const t of thresholds) {
        const round = decodeThresholds(encodeThresholds(t));
        if (round.altDeltaM === t.altDeltaM && round.distDeltaM === t.distDeltaM &&
            round.minSpacingSec === t.minSpacingSec && round.flags === t.flags) {
            thresholdsOk += 1;
        }
    }
    assert(thresholdsOk === thresholds.length && encodeThresholds(thresholds[0]).length === 7,
        '(img-f) event-thresholds payload (7 bytes) round-trips symmetrically for boundary values');
}

// (img-g) Receiver type-dispatch rule (WR-12): a frame whose buffer[2] is not
// a type accepted by that side is discarded before any body arithmetic
{
    const manifest = serializeManifest(createManifestPacketMirror(
        { imageId: 9, imageKind: 0, captureSource: 1, totalSize: 400, chunkSize: 200, totalChunks: 2,
          crc32: 77, captureTimeMs: 1000, resolution: 5, quality: 20, brightness: 0, contrast: 0,
          saturation: 0, exposure: 0, wbMode: 0 }));
    const chunk = serializeChunk(createChunkPacketMirror(9, 1, Buffer.from([1, 2, 3]), 3));
    const beacon = serializeTelemetryBeacon({ type: PACKET_TYPE_TELEMETRY_BEACON,
        body: { seq: 1, altitudeCm: 1000, tempCentiC: 200, latE6: 0, lonE6: 0, flags: 0 } });
    const response = serializeResponse(createResponsePacketMirror(0x00, 5, Buffer.alloc(8)));
    const command = serializeCommand(21, 0x01, null);

    const feed = (rx, bytes) => { for (const b of bytes) rx.feed(b); return rx.packets.length; };

    // Balloon accepts COMMAND only
    let rx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.balloon);
    assert(feed(rx, command) === 1, '(img-g) balloon accepts a 0x10 COMMAND frame');
    rx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.balloon);
    assert(feed(rx, response) === 0, '(img-g) balloon discards a CRC-valid 0x11 RESPONSE frame (WR-12)');
    rx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.balloon);
    assert(feed(rx, manifest) === 0 && feed(rx, chunk) === 0 && feed(rx, beacon) === 0,
        '(img-g) balloon discards 0x12/0x13/0x14 frames');

    // Base accepts RESPONSE, MANIFEST, CHUNK, BEACON — not COMMAND
    rx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.base);
    feed(rx, response);
    feed(rx, manifest);
    feed(rx, chunk);
    feed(rx, beacon);
    assert(rx.packets.length === 4, '(img-g) base accepts 0x11/0x12/0x13/0x14 frames (all four, exactly once each)');
    rx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.base);
    assert(feed(rx, command) === 0, '(img-g) base discards a 0x10 COMMAND frame');

    // A foreign type byte (0x77) is discarded by both sides
    const forged = Buffer.from(manifest);
    forged[2] = 0x77;
    // Recompute the CRC so the forged frame is CRC-valid — the type rule, not
    // the CRC, must reject it
    const crc16 = calculateCRC16(forged, forged.length - 4);
    forged.writeUInt16BE(crc16, forged.length - 4);
    rx = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.base);
    const rx2 = makeTypeDispatchReceiver(RECEIVER_DISPATCH_RULES.balloon);
    assert(feed(rx, forged) === 0 && feed(rx2, forged) === 0,
        '(img-g) a CRC-valid frame with unknown type byte 0x77 is discarded by both sides');
}

// (img-h) ResponseStatusData layout arithmetic after the reserved shrink
{
    const RESPONSE_STATUS_LAYOUT = [
        ['imageId', 2],
        ['autoCaptureEnabled', 1],
        ['autoCaptureInterval', 4],
        ['currentResolution', 1],
        ['currentQuality', 1],
        ['currentBrightness', 1],
        ['currentContrast', 1],
        ['eventThresholdAltM', 2],
        ['eventThresholdDistM', 2],
        ['eventMinSpacingSec', 2],
        ['eventFlags', 1],
        ['reserved', 10],
    ];
    const total = RESPONSE_STATUS_LAYOUT.reduce((sum, [, w]) => sum + w, 0);
    const reserved = RESPONSE_STATUS_LAYOUT.find(([n]) => n === 'reserved')[1];
    const newFields = ['eventThresholdAltM', 'eventThresholdDistM', 'eventMinSpacingSec', 'eventFlags']
        .every((n) => RESPONSE_STATUS_LAYOUT.some(([ln]) => ln === n));
    assert(total === 28, `(img-h) ResponseStatusData field widths sum to 28 bytes (got ${total})`);
    assert(reserved === 10, `(img-h) reserved shrank from 17 to 10 (got ${reserved})`);
    assert(newFields, '(img-h) all four event fields present in the layout');
    assert(total <= CMD_MAX_RESPONSE_DATA, '(img-h) 28 bytes still fits CMD_MAX_RESPONSE_DATA (50)');
}

// ============================================================================
console.log(failures === 0 ? '\nAll wire-format regression checks passed.' : `\n${failures} check(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);
