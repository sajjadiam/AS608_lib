/**
 * @file as608_packet.c
 * @brief AS60x packet header encoding, decoding and additive checksum.
 *
 * @details
 * This module is deliberately transport-independent and allocation-free.
 * It operates on caller-owned buffers, never performs UART I/O, and never
 * interprets the command byte or ACK confirmation value.
 *
 * The wire Length field includes the two checksum bytes but excludes the
 * nine-byte header. The checksum covers Kind, both Length bytes and Payload,
 * and excludes the start code, address and received checksum trailer.
 */

#include "as608_packet.h"

/** @brief Return true for a packet identifier defined by the AS60x protocol. */
static bool as608_packet_kind_is_valid(uint8_t kind) {
    return (kind == (uint8_t)AS608_PACKET_COMMAND) || (kind == (uint8_t)AS608_PACKET_DATA) ||
           (kind == (uint8_t)AS608_PACKET_ACK) || (kind == (uint8_t)AS608_PACKET_END_DATA);
}

/** @brief Read a two-byte unsigned integer in network (big-endian) order. */
static uint16_t as608_packet_read_u16_be(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8U) | (uint16_t)src[1]);
}

/** @brief Read a four-byte unsigned integer in network (big-endian) order. */
static uint32_t as608_packet_read_u32_be(const uint8_t *src) {
    return ((uint32_t)src[0] << 24U) | ((uint32_t)src[1] << 16U) |
           ((uint32_t)src[2] << 8U)  | (uint32_t)src[3];
}

as608_status_t as608_packet_encode_header(const as608_packet_header_t *header, uint8_t *wire) {
    if (!header || !wire) return AS608_STATUS_INVALID_ARG;

    if (!as608_packet_kind_is_valid(header->kind) || ((size_t)header->payload_size > AS608_PACKET_WIRE_PAYLOAD_MAX)) {
        return AS608_STATUS_INVALID_ARG;
    }

    /* Checked above: payload size + checksum fits uint16_t. */
    const uint16_t wire_length = (uint16_t)((uint32_t)header->payload_size + (uint32_t)AS608_PACKET_CHECKSUM_BYTES);

    /* Do not write anything to wire before all argument validation. */
    wire[0] = AS608_PACKET_START_BYTE_0;
    wire[1] = AS608_PACKET_START_BYTE_1;
    wire[2] = (uint8_t)(header->address >> 24U);
    wire[3] = (uint8_t)(header->address >> 16U);
    wire[4] = (uint8_t)(header->address >> 8U);
    wire[5] = (uint8_t)header->address;
    wire[6] = header->kind;
    wire[7] = (uint8_t)(wire_length >> 8U);
    wire[8] = (uint8_t)wire_length;

    return AS608_STATUS_OK;
}

as608_status_t as608_packet_decode_header(
    const uint8_t *wire,
    uint32_t expected_address,
    size_t max_payload_size,
    as608_packet_header_t *header
) {
    if (!wire || !header) return AS608_STATUS_INVALID_ARG;

    if ((wire[0] != AS608_PACKET_START_BYTE_0) || (wire[1] != AS608_PACKET_START_BYTE_1)) {
        return AS608_STATUS_FRAME_ERROR;
    }

    const uint32_t address = as608_packet_read_u32_be(&wire[2]);
    const uint8_t kind = wire[6];
    const uint16_t wire_length = as608_packet_read_u16_be(&wire[7]);

    if ((address != expected_address) || !as608_packet_kind_is_valid(kind) ||
        (wire_length < (uint16_t)AS608_PACKET_CHECKSUM_BYTES))
    {
        return AS608_STATUS_FRAME_ERROR;
    }

    const uint16_t payload_size = (uint16_t)(wire_length - (uint16_t)AS608_PACKET_CHECKSUM_BYTES);

    if ((size_t)payload_size > max_payload_size) return AS608_STATUS_FRAME_ERROR;

    /* Commit output only after every validation has passed. */
    header->address = address;
    header->kind = kind;
    header->payload_size = payload_size;

    return AS608_STATUS_OK;
}

uint16_t as608_packet_checksum_begin(const as608_packet_header_t *header) {
    /* Preconditions: header != NULL, kind valid, payload_size <= 65533. */
    const uint32_t wire_length = (uint32_t)header->payload_size + (uint32_t)AS608_PACKET_CHECKSUM_BYTES;

    const uint32_t sum = (uint32_t)header->kind + ((wire_length >> 8U) & UINT32_C(0xFF)) + (wire_length & UINT32_C(0xFF));

    return (uint16_t)sum;
}

uint16_t as608_packet_checksum_update(uint16_t sum, const uint8_t *data, size_t length) {
    /* Preconditions: data != NULL when length > 0. */
    uint32_t accumulator = (uint32_t)sum;

    for (size_t i = 0U; i < length; ++i) accumulator += (uint32_t)data[i];

    /* uint32_t wrap is defined and preserves the low 16 bits. */
    return (uint16_t)accumulator;
}

void as608_packet_checksum_encode(uint16_t sum, uint8_t *wire) {
    /* Precondition: wire points to at least two writable bytes. */
    wire[0] = (uint8_t)(sum >> 8U);
    wire[1] = (uint8_t)sum;
}

bool as608_packet_checksum_matches(uint16_t sum, const uint8_t *wire) {
    /* Precondition: wire points to at least two readable bytes. */
    return (wire[0] == (uint8_t)(sum >> 8U)) && (wire[1] == (uint8_t)sum);
}
