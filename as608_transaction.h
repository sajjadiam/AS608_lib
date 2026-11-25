/** @file as608_transaction.h
 * @brief Private, command-independent cooperative packet transport.
 */
#ifndef AS608_TRANSACTION_H
#define AS608_TRANSACTION_H

#include "as608_packet.h"

typedef struct {
    as608_packet_kind_t kind;
    const uint8_t *data;
    size_t length;
} as608_transaction_frame_t;

typedef enum {
    AS608_TXN_PENDING = 0,
    AS608_TXN_SENT,
    AS608_TXN_RECEIVED,
    AS608_TXN_DRAINED,
    AS608_TXN_FAULT
} as608_transaction_event_t;

typedef struct {
    as608_transaction_event_t event;
    as608_status_t status;
} as608_transaction_result_t;

/* Internal entry points; command layer owns job admission and lifecycle.
 * The deadline is armed exactly once per job, never per packet.
 * Send copies the payload. Receive limit excludes checksum bytes.
 * Each process call performs at most one UART callback plus clock read.
 * On a terminal event the command layer must schedule the next phase
 * or finish/quarantine the job before calling process again.
 */
void as608_transaction_begin(as608_t *ctx);
as608_status_t as608_transaction_send(as608_t *ctx,
                                    const as608_transaction_frame_t *frame);
void as608_transaction_receive(as608_t *ctx, size_t limit);
void as608_transaction_drain(as608_t *ctx);
as608_transaction_result_t as608_transaction_process(as608_t *ctx);

/* View valid only after RECEIVED, until the next receive/reset. */
as608_transaction_frame_t as608_transaction_received(const as608_t *ctx);

#endif /* AS608_TRANSACTION_H */
