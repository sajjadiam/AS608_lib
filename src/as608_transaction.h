/**
 * @file as608_transaction.h
 * @brief Private blocking AS60x frame transport, with one shared deadline.
 *
 * @details ISO C11; UART callbacks come from as608_port.h. Each transaction
 * owns no payload storage. Send and receive can be called multiple times
 * within one deadline (ACK followed by data packets, for example).
 *
 * @warning This context is per-call. Failure taints this transaction, but
 * cannot by itself make later, separately-created transactions safe: recovery
 * from late bytes after a timeout needs an explicit device-level policy.
 */
#ifndef AS608_TRANSACTION_H
#define AS608_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include "as608.h"
#include "as608_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief In-progress blocking exchange; only one per AS608 device. */
typedef struct {
    as608_t *dev;             /**< Caller-owned AS608 context. */
    uint32_t started_ms;     /**< Timestamp for the shared deadline. */
    as608_status_t fault;    /**< Sticky error after a partial I/O or bad frame. */
} as608_transaction_t;

/**
 * @brief Begin a transaction without touching UART.
 * @param[out] txn Caller-owned transaction context.
 * @param[in,out] dev Already initialized device.
 * @return Host status. The transaction deadline begins at this call.
 * @note No heap allocation, no global state and no implicit locking.
 */
as608_status_t as608_transaction_begin(as608_transaction_t *txn, as608_t *dev);

/**
 * @brief Transmit one complete command/data/end-data frame.
 * @param[in,out] txn Active transaction.
 * @param[in] kind COMMAND, DATA or END_DATA (ACK is module-to-host).
 * @param[in] payload Bytes to transmit; can be NULL only when size is zero.
 * @param[in] size Payload bytes, at most AS608_PACKET_WIRE_PAYLOAD_MAX.
 * @return Host status. A failure after I/O begins makes txn->fault sticky.
 * @note Completion of a partial write is not a whole-command retry.
 */
as608_status_t as608_transaction_send(
    as608_transaction_t *txn,
    as608_packet_kind_t kind,
    const uint8_t *payload,
    size_t size
);

/**
 * @brief Caller-owned destination and verified metadata for one RX frame.
 * @details Set data and capacity before calling receive(). The kind and
 * length fields are updated ONLY after checksum verification succeeds.
 */
typedef struct {
    uint8_t *data;             /**< [in] Caller buffer, may be NULL if capacity=0. */
    size_t capacity;           /**< [in] Maximum acceptable payload bytes. */
    size_t length;             /**< [out] Verified number of payload bytes. */
    as608_packet_kind_t kind; /**< [out] Actual verified ACK/DATA/END_DATA type. */
} as608_transaction_rx_t;

/**
 * @brief Receive one complete frame of any module-to-host packet kind.
 * @param[in,out] txn Active transaction.
 * @param[in,out] frame Destination buffer, capacity and verified metadata.
 * @return Host status; AS608_STATUS_OK only after checksum verification.
 * @note The command layer validates the received kind against its expected
 *       protocol phase. Stream transfers may return DATA or END_DATA.
 * @note ACK confirmation is frame->data[0]; interpretation is done by as608.c.
 * @warning The data buffer can be partially modified on failure, but
 *          frame->kind and frame->length remain unchanged.
 * @warning Recovery of late bytes after timeout needs a device-level policy:
 *          this protocol supplies no sequence number to bind ACK to command.
 */
as608_status_t as608_transaction_receive(as608_transaction_t *txn, as608_transaction_rx_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* AS608_TRANSACTION_H */
