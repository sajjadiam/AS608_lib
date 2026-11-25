/** @file as608_transaction.h
 * @brief Private, command-independent cooperative packet transport.
 */
#ifndef AS608_TRANSACTION_H
#define AS608_TRANSACTION_H

#include <stdbool.h>
#include "as608_types.h"
#include "as608_packet.h"
#include "as608_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum payload per transport frame supported by the driver. */
#define AS608_TRANSACTION_MAX_PAYLOAD_BYTES ((size_t)256U)

/** @brief Transport and timeout configuration, copied by as608_init(). */
typedef struct {
    as608_port_t port;
    uint32_t device_address;
    uint32_t timeout_ms;
} as608_config_t;

/** @brief Caller-owned transport context, independent of command state. */
typedef struct {
    as608_config_t config;
    uint32_t started_ms;
    uint8_t wire_phase;
    uint8_t tx_frame[AS608_TRANSACTION_MAX_PAYLOAD_BYTES + AS608_PACKET_OVERHEAD_BYTES];
    size_t tx_len;
    size_t tx_pos;
    uint8_t rx_header[AS608_PACKET_HEADER_BYTES];
    size_t rx_header_pos;
    uint8_t rx_body[AS608_TRANSACTION_MAX_PAYLOAD_BYTES + AS608_PACKET_CHECKSUM_BYTES];
    size_t rx_body_len;
    size_t rx_body_pos;
    uint8_t rx_kind;
    size_t rx_payload_len;
    size_t rx_limit;
} as608_transaction_t;

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
/** @brief Validate all transport callbacks and the deadline budget. */
bool as608_transaction_config_valid(const as608_config_t *config);

/** @brief Initialize fresh transport storage, copying its configuration.
 * @pre txn != NULL, config_valid(config), and a fresh physical link epoch.
 * Never use this to bypass command-layer quarantine.
 */
void as608_transaction_init(as608_transaction_t *txn, const as608_config_t *config);

void as608_transaction_begin(as608_transaction_t *txn);
as608_status_t as608_transaction_send(as608_transaction_t *txn,
                                    const as608_transaction_frame_t *frame);
void as608_transaction_receive(as608_transaction_t *txn, size_t limit);
void as608_transaction_drain(as608_transaction_t *txn);
as608_transaction_result_t as608_transaction_process(as608_transaction_t *txn);

/* View valid only after RECEIVED, until the next receive/reset. */
as608_transaction_frame_t as608_transaction_received(const as608_transaction_t *txn);

#ifdef __cplusplus
}
#endif
#endif /* AS608_TRANSACTION_H */
