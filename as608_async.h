/**
 * @file as608_async.h
 * @brief Cooperative, non-blocking AS60x command and template engine.
 *
 * C11; no heap, interrupts, RTOS or HAL dependency. One context per sensor.
 * The existing blocking API remains intact as a known-good baseline.
 *
 * Every as608_async_process() call performs at most one non-blocking port
 * operation. The application must call it repeatedly while a job is BUSY.
 */
#ifndef AS608_ASYNC_H
#define AS608_ASYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "as608.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * try_write must COPY accepted bytes into an owned, ordered TX queue; it
 * must not return successful consumption of a borrowed DMA/IT pointer.
 * try_read removes available bytes. Neither callback may wait for UART.
 * Return 0 if no progress, a count <= length if progress, or -1 on error.
 * RX overrun, UART overrun/framing errors and queue overflow must surface
 * as -1, not silently lose bytes.
 */
typedef int32_t (*as608_async_try_write_fn)(void *, const uint8_t *, size_t);
typedef int32_t (*as608_async_try_read_fn)(void *, uint8_t *, size_t);

typedef struct {
    void *user;
    as608_async_try_write_fn try_write;
    as608_async_try_read_fn try_read;
    as608_clock_ms_fn clock_ms;
    uint32_t device_address;
    uint32_t timeout_ms; /**< Entire operation including all stream packets. */
} as608_async_config_t;

typedef enum {
    AS608_ASYNC_IDLE = 0,
    AS608_ASYNC_BUSY,
    AS608_ASYNC_DONE,
    AS608_ASYNC_DESYNCHRONIZED
} as608_async_state_t;

typedef enum {
    AS608_ASYNC_PENDING = 0,
    AS608_ASYNC_COMPLETE,
    AS608_ASYNC_FAILED
} as608_async_progress_t;

/** Caller-owned state, one per physical UART/sensor. Not re-entrant.
 * Do not copy an active context. No public fields are application-writable. */
typedef struct {
    as608_async_config_t config;
    as608_async_state_t state;
    as608_result_t result;
    uint32_t started_ms;
    uint8_t op;
    uint8_t step;
    uint8_t wire_phase;
    uint8_t tx_frame[267U]; /* 9-byte header + 256-byte payload + 2 checksum. */
    size_t tx_len;
    size_t tx_pos;
    uint8_t rx_header[9U];
    size_t rx_header_pos;
    uint8_t rx_body[258U]; /* 256-byte payload + 2 checksum. */
    size_t rx_body_len;
    size_t rx_body_pos;
    uint8_t rx_kind;
    size_t rx_payload_len;
    uint16_t page_id;
    uint16_t chunk_size;
    const uint8_t *input;
    size_t input_length;
    size_t input_offset;
    as608_template_buffer_t *output;
    size_t output_offset;
} as608_async_t;

/** Must initially be zero initialized. Software init alone never recovers a
 * DESYNCHRONIZED link; a verified physical reset and UART queue purge are
 * needed before discarding/reinitializing such a context. */
as608_status_t as608_async_init(as608_async_t *ctx,
                                const as608_async_config_t *config);

/** Start calls are immediately returning and never access UART. */
as608_status_t as608_async_capture_start(as608_async_t *ctx);
as608_status_t as608_async_generate_character_start(as608_async_t *ctx,
                                                    as608_char_buffer_t buffer);
as608_status_t as608_async_read_template_start(as608_async_t *ctx,
                                               uint16_t page_id,
                                               as608_template_buffer_t *output);
as608_status_t as608_async_write_template_start(as608_async_t *ctx,
                                                uint16_t page_id,
                                                const uint8_t *data,
                                                size_t length);

/** Run at most one bounded nonblocking UART port callback. */
as608_async_progress_t as608_async_process(as608_async_t *ctx);

/** Returns true after DONE (including negative sensor ACK) or host fault. */
bool as608_async_get_result(const as608_async_t *ctx, as608_result_t *result);

/** In-flight cancellation quarantines the link (AS60x has no sequence ID). */
as608_status_t as608_async_abort(as608_async_t *ctx);

#ifdef __cplusplus
}
#endif
#endif /* AS608_ASYNC_H */
