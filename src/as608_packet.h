/**
 * @file as608_packet.h
 * @brief Internal, transport-independent AS60x packet framing primitives.
 *
 * @details
 * ISO C11. This private header is for src/ only, not application code.
 * Functions operate on host values and caller-owned wire-byte buffers.
 * UART I/O, transaction deadlines, resynchronization, command meanings,
 * and sensor confirmation-code interpretation are outside this layer.
 *
 * Wire layout:
 *   Start(2) | Address(4) | Kind(1) | Length(2) | Payload(N) | Sum(2)
 *
 * Length is the big-endian count of Payload(N) plus Sum(2).
 * All multi-byte wire fields are big-endian. Sum is the 16-bit modular sum
 * of Kind, both Length bytes, and Payload; it is NOT CRC-16.
 */

#ifndef AS608_PACKET_H
#define AS608_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "as608.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief First wire byte of the AS60x start sequence. */
#define AS608_PACKET_START_BYTE_0 UINT8_C(0xEF)

/** @brief Second wire byte of the AS60x start sequence. */
#define AS608_PACKET_START_BYTE_1 UINT8_C(0x01)

/** @brief Bytes preceding the payload, including the two Length bytes. */
#define AS608_PACKET_HEADER_BYTES ((size_t)9U)

/** @brief Two trailing big-endian checksum bytes. */
#define AS608_PACKET_CHECKSUM_BYTES ((size_t)2U)

/** @brief Fixed wire-frame overhead, excluding the payload. */
#define AS608_PACKET_OVERHEAD_BYTES \
    (AS608_PACKET_HEADER_BYTES + AS608_PACKET_CHECKSUM_BYTES)

/**
 * @brief Largest payload representable by the 16-bit wire Length field.
 * @note Not a safe default RX buffer size. The receiver must apply a
 *       smaller, command/negotiation-specific size limit before reading.
 */
#define AS608_PACKET_WIRE_PAYLOAD_MAX ((size_t)(UINT16_MAX - 2U))

/** @brief Packet category codes defined by the AS60x protocol. */
typedef enum {
    AS608_PACKET_COMMAND  = 0x01, /**< Host-to-module instruction. */
    AS608_PACKET_DATA     = 0x02, /**< Nonfinal data fragment. */
    AS608_PACKET_ACK      = 0x07, /**< Module response; payload starts with ACK code. */
    AS608_PACKET_END_DATA = 0x08  /**< Final data fragment. */
} as608_packet_kind_t;

/**
 * @brief Decoded packet header, with the checksum excluded from payload_size.
 * @note This is a host-side description, not an on-wire struct; never
 *       serialize it using memcpy or a packed-struct cast.
 */
typedef struct {
    uint32_t address;      /**< Four-byte protocol address. */
    uint8_t kind;          /**< One of @ref as608_packet_kind_t. */
    uint16_t payload_size; /**< Payload bytes, not including checksum. */
} as608_packet_header_t;

/**
 * @brief Encode the nine-byte header of a packet.
 *
 * @param[in] header Host-side header values.
 * @param[out] wire Pointer to writable storage of at least AS608_PACKET_HEADER_BYTES bytes.
 * @retval AS608_STATUS_OK Header encoded in wire order.
 * @retval AS608_STATUS_INVALID_ARG Null pointer, unknown kind, or a payload
 *         that cannot fit in the two-byte wire Length field.
 * @post wire is unchanged on error.
 *
 * @note Payload and checksum must be transmitted separately by the caller.
 */
as608_status_t as608_packet_encode_header(
    const as608_packet_header_t *header,
    uint8_t *wire);

/**
 * @brief Decode and validate a complete nine-byte packet header.
 *
 * @param[in] wire Pointer to readable storage of at least AS608_PACKET_HEADER_BYTES bytes.
 * @param[in] expected_address Required device address; 0xFFFFFFFF is NOT
 *            a wildcard.
 * @param[in] max_payload_size Command/negotiated upper limit (in bytes).
 * @param[out] header Decoded header, unchanged unless successful.
 * @retval AS608_STATUS_OK Header validated.
 * @retval AS608_STATUS_INVALID_ARG Invalid arguments.
 * @retval AS608_STATUS_FRAME_ERROR Wrong start code/address, invalid packet
 *         kind/length, or payload longer than max_payload_size.
 *
 * @warning This validates only the header. The caller must subsequently
 *          receive exactly payload_size bytes and two checksum bytes,
 *          and verify the checksum before trusting the payload.
 */
as608_status_t as608_packet_decode_header(
    const uint8_t *wire,
    uint32_t expected_address,
    size_t max_payload_size,
    as608_packet_header_t *header);

/**
 * @brief Initial checksum of Kind and both wire Length bytes.
 * @param[in] header Validated packet header.
 * @return Partial modulo-65536 checksum.
 * @pre header is non-NULL and contains a valid kind and payload_size.
 * @note Excludes start code, address and the checksum field itself.
 */
uint16_t as608_packet_checksum_begin(const as608_packet_header_t *header);

/**
 * @brief Update an AS60x checksum with arbitrary payload fragments.
 * @param[in] sum Previously accumulated 16-bit checksum.
 * @param[in] data Fragment bytes; may be NULL only when length is zero.
 * @param[in] length Number of bytes to add.
 * @return Updated modulo-65536 checksum.
 * @note Use repeatedly for streamed payloads to avoid large temporary
 *       allocations. This is additive checksum, not CRC.
 */
uint16_t as608_packet_checksum_update(
    uint16_t sum, const uint8_t *data, size_t length);

/**
 * @brief Encode a checksum to the two-byte, big-endian wire trailer.
 * @param[in] sum Final checksum.
 * @param[out] wire Pointer to writable storage of at least AS608_PACKET_CHECKSUM_BYTES bytes.
 * @pre wire is non-NULL and points to at least AS608_PACKET_CHECKSUM_BYTES bytes.
 */
void as608_packet_checksum_encode(
    uint16_t sum, uint8_t *wire);

/**
 * @brief Check a received checksum trailer against the accumulated sum.
 * @param[in] sum Computed sum including all payload bytes.
 * @param[in] wire Pointer to at least AS608_PACKET_CHECKSUM_BYTES received bytes (big-endian).
 * @return true if equal; false otherwise.
 * @pre wire is non-NULL and points to at least AS608_PACKET_CHECKSUM_BYTES bytes.
 */
bool as608_packet_checksum_matches(
    uint16_t sum, const uint8_t *wire);

#ifdef __cplusplus
}
#endif

#endif /* AS608_PACKET_H */
