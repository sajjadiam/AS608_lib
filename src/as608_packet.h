/**
 * @file as608_packet.h
 * @brief Internal, transport-independent AS60x packet framing primitives.
 *
 * @details
 * ISO C11; declarations are also usable from C++11. This private
 * header is for the driver, not application code. Functions operate
 * on host values and caller-owned wire-byte buffers. No allocation,
 * UART I/O, mutable global state or transaction deadlines.
 * Packet direction, command semantics, confirmation codes and negotiated
 * payload limits belong to higher layers.
 *
 * Wire layout:
 *   Start(2) | Address(4) | Kind(1) | Length(2) | Payload(N) | Sum(2)
 *
 * Length = N + 2; total frame size = N + 11.
 * All multi-byte wire fields are big-endian. Sum is the unsigned modulo-65536
 * sum of Kind, both Length bytes, and Payload. Start, Address and Sum itself
 * are excluded. It is not a CRC or an authentication mechanism.
 *
 * Header validation permits a zero-byte payload (Length = 2) as a
 * structurally representable frame. This does not establish a valid
 * sensor command, ACK or data fragment. Higher layers enforce semantics.
 *
 * @see AS60x MCU User's Guide V1.0, sections 4.1 and 4.2, pp. 11–12.
 */

#ifndef AS608_PACKET_H
#define AS608_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "as608_types.h"

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
#define AS608_PACKET_OVERHEAD_BYTES (AS608_PACKET_HEADER_BYTES + AS608_PACKET_CHECKSUM_BYTES)

/**
 * @brief Largest payload representable by the 16-bit wire Length field.
 * @details 65535 - 2 = 65533 bytes; this is a mathematical encoding bound,
 * not a supported sensor packet size or a buffer-allocation recommendation.
 * The receiver must apply its actual storage and command/negotiation limits.
 * @warning The theoretical complete frame (65544 bytes) need not fit
 * in size_t on a 16-bit target. Check the actual buffer bound before
 * adding the frame overhead; this codec does not allocate whole frames.
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
 * serialize it using memcpy or a packed-struct cast. Its layout, alignment
 * and padding are ABI-dependent. The kind field holds the raw wire byte.
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
 * @pre Valid non-null objects have the stated readable/writable extents
 * and do not overlap. Null pointers are rejected before dereferencing.
 * @post Exactly nine bytes are written on success; wire is unchanged on error.
 * @note This encodes only the header, including a structurally representable
 * zero payload. The caller supplies the payload and checksum separately.
 */
as608_status_t as608_packet_encode_header(const as608_packet_header_t *header, uint8_t *wire);

/**
 * @brief Decode and validate a complete nine-byte packet header.
 *
 * @param[in] wire Pointer to readable storage of at least AS608_PACKET_HEADER_BYTES bytes.
 * @param[in] expected_address Required device address; 0xFFFFFFFF is NOT
 *            a wildcard.
 * @param[in] max_payload_size Caller-supplied inclusive payload limit,
 * excluding header and checksum. Zero accepts only an empty payload;
 * values above the wire maximum do not enlarge the wire field.
 * @param[out] header Decoded header, unchanged unless successful.
 * @retval AS608_STATUS_OK Header validated.
 * @retval AS608_STATUS_INVALID_ARG Null wire or output pointer.
 * @retval AS608_STATUS_FRAME_ERROR Wrong start code/address, unknown packet
 * kind, wire Length below two, or payload exceeding max_payload_size.
 * @pre Valid non-null objects have the stated extents and do not overlap.
 * @post Output fields are committed only on success; unchanged on error.
 * @warning The header does not establish a valid payload, direction,
 * complete frame or checksum. Receive exactly payload_size bytes and
 * two checksum bytes and verify the checksum before using the payload.
 * The caller separately validates direction and command-specific lengths.
 */
as608_status_t as608_packet_decode_header(
    const uint8_t *wire,
    uint32_t expected_address,
    size_t max_payload_size,
    as608_packet_header_t *header
);

/**
 * @brief Initial checksum of Kind and both wire Length bytes.
 * @param[in] header Validated packet header.
 * @return Partial modulo-65536 checksum.
 * @pre header is non-NULL, readable and contains a valid kind and
 * payload_size <= AS608_PACKET_WIRE_PAYLOAD_MAX.
 * @note No argument validation or status return. Excludes start code,
 * address and the checksum field itself.
 */
uint16_t as608_packet_checksum_begin(const as608_packet_header_t *header);

/**
 * @brief Update an AS60x checksum with arbitrary payload fragments.
 * @param[in] sum Previously accumulated 16-bit checksum.
 * @param[in] data Fragment bytes; may be NULL only when length is zero.
 * @param[in] length Number of bytes to add.
 * @return Updated modulo-65536 checksum.
 * @pre For nonzero length, data points to at least length readable bytes.
 * @note A zero-length fragment returns sum unchanged, including NULL data.
 * No argument validation or status return. Fragment boundaries do not
 * change the sum; the caller must account for the total payload length.
 * The operation is additive modulo 65536, not CRC.
 */
uint16_t as608_packet_checksum_update(uint16_t sum, const uint8_t *data, size_t length);

/**
 * @brief Encode a checksum to the two-byte, big-endian wire trailer.
 * @param[in] sum Final checksum.
 * @param[out] wire Pointer to writable storage of at least AS608_PACKET_CHECKSUM_BYTES bytes.
 * @pre wire is non-NULL and points to at least AS608_PACKET_CHECKSUM_BYTES bytes.
 * @post Exactly two bytes are written in high-byte-first order.
 */
void as608_packet_checksum_encode(uint16_t sum, uint8_t *wire);

/**
 * @brief Check a received checksum trailer against the accumulated sum.
 * @param[in] sum Computed sum including all payload bytes.
 * @param[in] wire Pointer to at least AS608_PACKET_CHECKSUM_BYTES received bytes (big-endian).
 * @return true if equal; false otherwise.
 * @pre wire is non-NULL and points to at least AS608_PACKET_CHECKSUM_BYTES bytes.
 * @note Equality detects some corruption, not all; it does not authenticate
 * the sender or prove that the complete frame was received.
 */
bool as608_packet_checksum_matches(uint16_t sum, const uint8_t *wire);

#ifdef __cplusplus
}
#endif

#endif /* AS608_PACKET_H */
