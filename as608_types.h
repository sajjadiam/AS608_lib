/**
 * @file as608_types.h
 * @brief Shared low-level types and constants.
 *
 * @details Independent of packet, transport and command layers.
 * Requires ISO C11 (or C++11 for header consumers), 8-bit bytes and
 * the exact-width integer types supplied by <stdint.h>. Host values
 * are not wire representations; do not serialize an enum or struct
 * using its object representation.
 */
#ifndef AS608_TYPES_H
#define AS608_TYPES_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#if __cplusplus < 201103L
#error "AS608 headers require ISO C++11 or later"
#endif
static_assert(CHAR_BIT == 8, "AS608 requires 8-bit bytes");
#else
#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#error "AS608 requires ISO C11 or later"
#endif
_Static_assert(CHAR_BIT == 8, "AS608 requires 8-bit bytes");
#endif

/**
 * @brief Factory-default AS60x packet address.
 * @note This is an exact address, not a wildcard or a unique serial number.
 * The configured address may differ after device provisioning.
 */
#define AS608_DEFAULT_ADDRESS UINT32_C(0xFFFFFFFF)

/**
 * @brief Maximum whole-job timeout in milliseconds (2^31 - 1).
 * @details Valid configured timeouts are 1 through this value.
 * The clock is monotonic modulo 2^32; elapsed time is calculated as
 * (uint32_t)(now - started_ms). The scheduler must check deadlines
 * before a complete counter wrap can hide elapsed time.
 * This half-range limit is a driver policy, not a wire-protocol field.
 */
#define AS608_MAX_TIMEOUT_MS UINT32_C(0x7FFFFFFF)

/**
 * @brief Host-side operation status; never contains a sensor ACK code.
 * @details AS608_STATUS_OK reports success of the current host operation.
 * For a start call it means admission, not sensor completion. For a
 * terminal job result it means the host operation completed without a
 * host fault; the sensor's raw confirmation must also be checked.
 * @note Numeric values are fixed for source compatibility. Enum storage
 * size is compiler/ABI-dependent; do not transmit or persist raw enum bytes.
 */
typedef enum {
    AS608_STATUS_OK                = 0, /**< Host operation succeeded. */
    AS608_STATUS_INVALID_ARG       = 1, /**< Invalid caller argument. */
    AS608_STATUS_NOT_INITIALIZED   = 2, /**< Context not initialized. */
    AS608_STATUS_IO_ERROR          = 3, /**< Transport or stream callback failed. */
    AS608_STATUS_TIMEOUT           = 4, /**< Whole-job deadline expired. */
    AS608_STATUS_FRAME_ERROR       = 5, /**< Invalid frame header, address or length. */
    AS608_STATUS_CHECKSUM_ERROR    = 6, /**< Invalid received checksum. */
    AS608_STATUS_UNEXPECTED_REPLY  = 7, /**< Invalid reply kind, length or semantics. */
    AS608_STATUS_RECOVERY_REQUIRED = 8, /**< Physical link recovery required. */
    AS608_STATUS_BUSY              = 9  /**< Another job owns the context. */
} as608_status_t;

#endif /* AS608_TYPES_H */
