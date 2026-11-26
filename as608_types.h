/** @file as608_types.h
 * @brief Shared low-level types and constants; no driver-layer dependencies.
 */
#ifndef AS608_TYPES_H
#define AS608_TYPES_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L)
#error "AS608 requires ISO C11 or later"
#endif

#if defined(__cplusplus)
static_assert(CHAR_BIT == 8, "AS608 requires 8-bit bytes");
#else
_Static_assert(CHAR_BIT == 8, "AS608 requires 8-bit bytes");
#endif

/** @brief Factory-default AS60x packet address; not a unique serial number. */
#define AS608_DEFAULT_ADDRESS UINT32_C(0xFFFFFFFF)

/** @brief Maximum whole-transaction timeout for wrap-safe uint32_t ticks. */
#define AS608_MAX_TIMEOUT_MS UINT32_C(0x7FFFFFFF)

/**
 * @brief Host-side transaction status; never contains a sensor ACK code.
 * @note AS608_STATUS_OK means a syntactically valid response was received,
 *       not necessarily that the sensor executed the command successfully.
 */
typedef enum {
    AS608_STATUS_OK = 0,          /**< A valid response was received. */
    AS608_STATUS_INVALID_ARG,     /**< Caller supplied an invalid argument. */
    AS608_STATUS_NOT_INITIALIZED, /**< Device context is not initialized. */
    AS608_STATUS_IO_ERROR,        /**< Platform transport or stream callback failed. */
    AS608_STATUS_TIMEOUT,         /**< Overall transaction deadline expired. */
    AS608_STATUS_FRAME_ERROR,     /**< Invalid frame header, address or length. */
    AS608_STATUS_CHECKSUM_ERROR,  /**< Received frame checksum is invalid. */
    AS608_STATUS_UNEXPECTED_REPLY, /**< Unexpected packet kind or payload size. */
    AS608_STATUS_RECOVERY_REQUIRED, /**< An earlier transaction left the link uncertain. */
    AS608_STATUS_BUSY /**< Another operation owns this context. */
} as608_status_t;

#endif /* AS608_TYPES_H */
