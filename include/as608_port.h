/**
 * @file as608_port.h
 * @brief Platform-independent UART I/O contract for AS608.
 */
#ifndef AS608_PORT_H
#define AS608_PORT_H

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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transmit up to length bytes.
 * @return A count in [0,length], or -1 on I/O failure. Partial transfers are valid.
 * @pre 0 < length <= INT32_MAX.
 * @note A call shall not block longer than timeout_ms.
 */
typedef int32_t (*as608_write_fn)(void *user, const uint8_t *data, size_t length, uint32_t timeout_ms);

/**
 * @brief Receive up to length bytes.
 * @return A count in [0,length], or -1 on I/O failure. Partial transfers are valid.
 * @pre 0 < length <= INT32_MAX.
 * @note A call shall not block longer than timeout_ms.
 */
typedef int32_t (*as608_read_fn)(void *user, uint8_t *data, size_t length, uint32_t timeout_ms);

/** @brief Return monotonic milliseconds modulo 2^32. */
typedef uint32_t (*as608_clock_ms_fn)(void *user);

/** @brief Application-provided UART transport and monotonic clock. */
typedef struct {
    void *user;                  /**< Caller-owned platform context. */
    as608_write_fn write;       /**< UART transmit function. */
    as608_read_fn read;         /**< UART receive function. */
    as608_clock_ms_fn clock_ms; /**< Monotonic clock function. */
} as608_port_t;

#ifdef __cplusplus
}
#endif

#endif /* AS608_PORT_H */
