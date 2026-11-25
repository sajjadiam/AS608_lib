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
 * @brief Copy up to length bytes into a port-owned ordered TX queue.
 * @return Bytes accepted, zero for backpressure, or a negative I/O error.
 * @note Never wait for UART. Accepted bytes must no longer borrow the
 * caller's memory. DMA/IT ownership and TX ordering are port responsibilities.
 */
typedef int32_t (*as608_try_write_fn)(void *user, const uint8_t *data, size_t length);

/**
 * @brief Consume currently available RX bytes, without waiting.
 * @return Bytes consumed, zero if empty, or a negative I/O error.
 * @note UART errors and lost bytes/queue overflow must be reported as errors.
 */
typedef int32_t (*as608_try_read_fn)(void *user, uint8_t *data, size_t length);

/**
 * @brief Query TX completion without waiting.
 * @return 1 only if the TX queue is empty AND the final UART stop bit has
 * been transmitted; 0 while pending; negative on transport failure.
 * Required for safe completion of standalone template downloads.
 */
typedef int32_t (*as608_tx_idle_fn)(void *user);

/** @brief Monotonic milliseconds modulo 2^32; must not wait. */
typedef uint32_t (*as608_clock_ms_fn)(void *user);

typedef struct {
    void *user;
    as608_try_write_fn try_write;
    as608_try_read_fn try_read;
    as608_tx_idle_fn tx_idle;
    as608_clock_ms_fn clock_ms;
} as608_port_t;

#ifdef __cplusplus
}
#endif
#endif /* AS608_PORT_H */
