/**
 * @file as608_port.h
 * @brief Platform-independent, cooperative UART I/O contract for AS608.
 *
 * @details The port owns the physical UART, RX/TX queues and any ISR/DMA
 * state. It supplies an ordered, binary-transparent byte stream; framing
 * and protocol parsing belong to the driver. No HAL, RTOS or MCU-specific
 * definitions are required here.
 *
 * All callbacks execute synchronously in the calling application's
 * context and must return in bounded time: no waiting for UART, DMA,
 * locks, queue space or incoming data, no sleeps, and no driver re-entry.
 * Serialize all calls sharing a driver context. An ISR/DMA implementation
 * must independently synchronize access to its own queues and state;
 * volatile alone is not a synchronization contract. Do not share the
 * same physical byte stream between independent driver contexts without
 * an external exclusive-ownership mechanism.
 *
 * Callback arguments are borrowed only during the call. Keep the
 * callbacks and the objects referenced by user valid for the entire
 * lifetime of the initialized context. The user pointer may be NULL
 * when the callbacks do not require it.
 *
 * @warning Latch UART/DMA errors, dropped bytes and queue overruns until
 * physical recovery. Report a detected link failure on the next I/O
 * callback, including a callback for the other direction, rather than
 * silently returning zero or normal progress. A callback cannot undo
 * bytes already sent or consumed. The driver detects faults only when
 * it calls the port; it does not monitor the UART continuously.
 *
 * Abort and confirm_reset are software lifecycle operations, not hardware
 * flushes. The application must stop outstanding TX/DMA, recover the
 * sensor/UART and purge stale bytes/errors before confirming a fresh
 * physical link. The port must not silently discard received bytes.
 */
#ifndef AS608_PORT_H
#define AS608_PORT_H

#include "as608_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Accept a prefix of bytes into port-owned TX storage or hardware.
 * @param[in,out] user Application-owned port state.
 * @param[in] data Readable byte buffer, borrowed until return.
 * @param[in] length Maximum bytes to accept; the driver requests nonzero lengths.
 * @return 1..length for the exact accepted prefix, 0 for temporary
 * backpressure, or a negative value for I/O failure. Returning a positive
 * count greater than length violates the contract.
 * @post Accepted bytes are copied into port-owned storage or already
 * handed to hardware without retaining the caller's pointer. Preserve
 * their order across calls. Zero accepts no bytes. On negative return,
 * the physical stream may be incomplete and must be recovered.
 * @warning Do not pass a borrowed driver buffer to asynchronous DMA/IT
 * unless all accepted bytes have first been copied to port-owned storage.
 * Acceptance is not physical transmission completion.
 */
typedef int32_t (*as608_try_write_fn)(void *user, const uint8_t *data, size_t length);

/**
 * @brief Consume a prefix of available RX bytes without waiting.
 * @param[in,out] user Application-owned port state.
 * @param[out] data Writable byte buffer, borrowed until return.
 * @param[in] length Maximum bytes to consume; the driver requests nonzero lengths.
 * @return 1..length for the exact number written and consumed, 0 if no
 * bytes are available, or a negative value for I/O failure. Returning
 * a positive count greater than length violates the contract.
 * @post On a positive return, only the first returned-count bytes are
 * written. Zero consumes and writes no bytes. Never retain data after
 * returning, including as a DMA destination.
 * @warning Keep RX active while TX is queued or transmitting. Preserve
 * order and report framing, parity, noise, overrun, DMA and software
 * queue failures; never hide dropped bytes by returning zero.
 */
typedef int32_t (*as608_try_read_fn)(void *user, uint8_t *data, size_t length);

/**
 * @brief Query physical TX completion without waiting.
 * @param[in,out] user Application-owned port state.
 * @return Exactly 1 if no accepted TX bytes remain in software queues,
 * pending DMA/interrupt transfers or the UART transmitter; 0 while
 * pending; negative on transport failure. Any other positive value
 * violates the contract.
 * @note Completion includes the final UART stop bit. On STM32F1, TXE
 * and DMA transfer-complete alone are insufficient; check USART TC
 * together with the software queue and outstanding DMA/IT state.
 * The result must represent a coherent snapshot of those states.
 * @warning This does not confirm sensor reception, checksum acceptance
 * or flash programming. Do not clear a latched error to report idle.
 * @see ST RM0008 Rev 21, USART transmitter, pp. 792-793, and DMA transmission.
 */
typedef int32_t (*as608_tx_idle_fn)(void *user);

/**
 * @brief Return monotonic milliseconds modulo 2^32, without waiting.
 * @param[in,out] user Application-owned port state.
 * @return Current unsigned millisecond tick; zero and wraparound are valid.
 * @note Read a coherent counter snapshot, including across ISR updates
 * on narrow MCUs. The timebase must advance during active jobs and
 * must not be reset or jump backward (except natural wraparound).
 * The scheduler must poll sufficiently often to observe the job deadline.
 * No error sentinel is reserved; the clock has no failure return.
 */
typedef uint32_t (*as608_clock_ms_fn)(void *user);

/**
 * @brief Caller-supplied port descriptor copied during initialization.
 * @note All four callbacks are mandatory. The driver copies the descriptor,
 * not the application state referenced by user. No ownership of user
 * or hardware resources is transferred to the driver.
 */
typedef struct {
    void *user;                    /**< Opaque application-owned state. */
    as608_try_write_fn try_write;  /**< Ordered TX acceptance. */
    as608_try_read_fn try_read;    /**< Ordered RX consumption. */
    as608_tx_idle_fn tx_idle;      /**< Physical TX completion. */
    as608_clock_ms_fn clock_ms;    /**< Monotonic millisecond timebase. */
} as608_port_t;

#ifdef __cplusplus
}
#endif
#endif /* AS608_PORT_H */
