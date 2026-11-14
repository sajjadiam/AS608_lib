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

/**
 * @brief Reestablish a fresh physical sensor/UART communication epoch.
 * @details A successful callback MUST ensure that prior sensor operations
 * cannot later send a reply and that old host TX-DMA, RX FIFO/DMA and software
 * UART bytes will not be delivered after the call. Normally this requires
 * aborting outstanding TX, a verified sensor reset/power cycle, a boot wait,
 * and clearing the entire receive path. RX flush alone is NOT sufficient.
 * @param[in] user Application-owned platform context.
 * @param[in] timeout_ms Overall recovery time budget.
 * @return Zero only when the entire guarantee holds; nonzero otherwise.
 * @note Optional: if not supplied, a quarantined device remains blocked.
 */
typedef int32_t (*as608_recover_fn)(void *user, uint32_t timeout_ms);

/** @brief Application-provided UART transport and monotonic clock. */
typedef struct {
    void *user;                  /**< Caller-owned platform context. */
    as608_write_fn write;       /**< UART transmit function. */
    as608_read_fn read;         /**< UART receive function. */
    as608_clock_ms_fn clock_ms; /**< Monotonic clock function. */
    as608_recover_fn recover;   /**< Optional physical-link recovery callback. */
} as608_port_t;

#ifdef __cplusplus
}
#endif

#endif /* AS608_PORT_H */
