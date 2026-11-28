/**
 * @file as608_transaction.h
 * @brief Internal, command-independent cooperative packet transport.
 *
 * @details Owns fixed frame storage, partial UART I/O and one absolute
 * job timeout budget measured from started_ms. No heap, command opcode
 * interpretation, sensor ACK interpretation or automatic resynchronization.
 * The command layer owns admission, sequencing, completion and quarantine.
 *
 * Calls on a context must be serialized. Internal entry points assume
 * valid pointers and lifecycle preconditions unless stated otherwise.
 * A phase may be armed only while no previous phase is active. After
 * SENT, RECEIVED or DRAINED, schedule the next phase or finish the job;
 * after FAULT, quarantine and recover externally. Never poll an idle
 * transaction or reinitialize it to bypass physical recovery.
 *
 * @see as608_packet.h
 * @see as608_port.h
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

/**
 * @brief Fixed maximum payload stored in one transport frame.
 * @details 256 payload bytes; excludes header and checksum. This is a
 * storage bound, not the sensor's negotiated packet size or total stream
 * length. A command-specific RX limit may be smaller or larger than
 * this bound, but no single received payload may exceed it.
 */
#define AS608_TRANSACTION_MAX_PAYLOAD_BYTES ((size_t)256U)

/** @brief Transport configuration, copied by as608_init(). */
typedef struct {
    as608_port_t port;       /**< Four mandatory callbacks and borrowed user state. */
    uint32_t device_address; /**< Exact configured TX and default RX address. */
    uint32_t timeout_ms;     /**< Whole-job budget, 1..AS608_MAX_TIMEOUT_MS. */
} as608_config_t;

/**
 * @brief Caller-owned transport state, independent of command semantics.
 * @details Storage is embedded so no dynamic allocation is required.
 * Fields are internal except configuration supplied at initialization;
 * the command layer uses started_ms for cooperative source/wait phases
 * and rx_address for address provisioning. Application code must not
 * modify live state or copy an active context. Layout is ABI-dependent.
 * Accepted TX bytes are owned by the port, not by this context.
 */
typedef struct {
    as608_config_t config;  /**< Retained configuration. */
    uint32_t started_ms;    /**< Original job tick, modulo 2^32. */
    uint8_t wire_phase;     /**< Private transport state. */
    uint8_t tx_frame[AS608_TRANSACTION_MAX_PAYLOAD_BYTES + AS608_PACKET_OVERHEAD_BYTES];
    size_t tx_len;          /**< Complete encoded TX frame length. */
    size_t tx_pos;          /**< Accepted prefix length; 0..tx_len. */
    uint8_t rx_header[AS608_PACKET_HEADER_BYTES];
    size_t rx_header_pos;   /**< Received header bytes. */
    uint8_t rx_body[AS608_TRANSACTION_MAX_PAYLOAD_BYTES + AS608_PACKET_CHECKSUM_BYTES];
    size_t rx_body_len;     /**< Expected payload plus checksum. */
    size_t rx_body_pos;     /**< Received body bytes; 0..rx_body_len. */
    uint8_t rx_kind;        /**< Validated raw packet identifier. */
    size_t rx_payload_len;  /**< Decoded payload length, excluding checksum. */
    size_t rx_limit;        /**< Inclusive caller-supplied payload bound. */
    uint32_t rx_address;    /**< Exact expected address for the receive phase. */
} as608_transaction_t;

/**
 * @brief Host frame descriptor; not a wire representation.
 * @details Send borrows external data only during the call and copies it.
 * Alternatively, data may be exactly the idle TX payload view.
 * A received descriptor is a read-only view of context-owned RX storage.
 * length excludes header and checksum in either direction.
 */
typedef struct {
    as608_packet_kind_t kind;
    const uint8_t *data;
    size_t length;
} as608_transaction_frame_t;

/** @brief Result of one cooperative transport poll. */
typedef enum {
    AS608_TXN_PENDING  = 0, /**< Phase active; no terminal event yet. */
    AS608_TXN_SENT     = 1, /**< Entire frame accepted by port; not physical TX completion. */
    AS608_TXN_RECEIVED = 2, /**< Complete structurally valid, checksum-verified RX frame. */
    AS608_TXN_DRAINED  = 3, /**< Port reports physical TX completion. */
    AS608_TXN_FAULT    = 4  /**< Host error; external recovery required. */
} as608_transaction_event_t;

/**
 * @brief Transport event and associated host status.
 * @note Non-fault events carry AS608_STATUS_OK. FAULT carries a host
 * error; no sensor confirmation code is interpreted here.
 */
typedef struct {
    as608_transaction_event_t event;
    as608_status_t status;
} as608_transaction_result_t;

/**
 * @brief Validate configuration without modifying it or invoking callbacks.
 * @param[in] config Configuration pointer; NULL is permitted and rejected.
 * @return true if all four callbacks exist and timeout_ms is in range.
 * @note Any uint32_t address and a NULL port.user are representable.
 * Callback behavior and actual sensor settings cannot be validated here.
 */
bool as608_transaction_config_valid(const as608_config_t *config);

/**
 * @brief Initialize transport storage and copy configuration.
 * @param[out] txn Writable context.
 * @param[in] config Valid configuration; may alias txn->config.
 * @pre Both pointers valid; config_valid(config); fresh physical link epoch.
 * No active job or unhandled accepted port data.
 * @post Context initialized in idle phase; no callback invoked.
 * @warning Does not flush UART, stop DMA or recover the sensor. Never
 * use initialization to bypass command-layer quarantine.
 */
void as608_transaction_init(as608_transaction_t *txn, const as608_config_t *config);

/**
 * @brief Record the original job start tick using one clock callback.
 * @param[in,out] txn Initialized context.
 * @pre Called exactly once for a newly admitted job, before its first
 * process call. The first TX frame may already be prepared.
 * @post started_ms updated; wire phase and frame storage unchanged.
 * @warning Do not call between frames or during an active job.
 */
void as608_transaction_begin(as608_transaction_t *txn);

/**
 * @brief Borrow writable payload storage for direct frame preparation.
 * @param[in,out] txn Initialized context; NULL is permitted.
 * @return View of exactly AS608_TRANSACTION_MAX_PAYLOAD_BYTES writable
 * bytes when idle; NULL for an active phase or NULL context.
 * @note No phase change, callbacks, or deadline update. The caller may
 * write only the intended payload and must not retain the view across
 * other transport operations. Submit the exact pointer to send() after
 * producing a complete payload, or discard the view without sending.
 * @pre Serialized access; command layer has admitted a valid job.
 * @warning Never retain this view in a callback or use it for async DMA.
 */
uint8_t *as608_transaction_tx_payload(as608_transaction_t *txn);

/**
 * @brief Encode and queue one frame in context-owned TX storage.
 * @param[in,out] txn Initialized idle-phase context.
 * @param[in] frame Readable descriptor with a valid packet kind.
 * Nonzero length requires readable data of that extent; zero permits NULL.
 * @retval AS608_STATUS_OK Frame prepared; no UART callback invoked.
 * @retval AS608_STATUS_INVALID_ARG Null pointer, invalid payload size,
 * missing nonempty payload or kind other than COMMAND/DATA/END_DATA.
 * @retval AS608_STATUS_BUSY An earlier transport phase is active.
 * @pre Non-null pointers valid; frame descriptor does not overlap modified
 * storage. Payload either points exactly to the view returned by
 * tx_payload() for this context, or does not overlap TX storage or fields
 * modified during preparation. Partial TX overlap is not supported.
 * Caller chooses semantic lengths and initializes all submitted bytes.
 * @note Rejected preparation preserves context and invokes no callbacks.
 * @post On success the complete frame is encoded and TX phase armed;
 * external input storage may be reused. An in-place payload is no longer
 * writable by the caller. The original deadline is unchanged.
 * @note The actual UART transmission occurs in process calls.
 */
as608_status_t as608_transaction_send(as608_transaction_t *txn,
                                    const as608_transaction_frame_t *frame);

/**
 * @brief Arm one receive phase using the configured device address.
 * @param[in,out] txn Initialized idle-phase context.
 * @param[in] limit Inclusive payload bound, excluding checksum.
 * Zero allows only a structurally empty payload. Values above 256 are
 * allowed but do not increase the transport storage bound.
 * @pre No active phase; consume any previous received view first.
 * @post RX counters reset; no callbacks invoked; deadline unchanged.
 */
void as608_transaction_receive(as608_transaction_t *txn, size_t limit);

/**
 * @brief Arm one receive phase using an exact explicit address.
 * @param[in,out] txn Initialized idle-phase context.
 * @param[in] limit Inclusive payload bound, as in receive().
 * @param[in] address Exact expected address, not a wildcard.
 * @pre Same phase and view preconditions as receive().
 * @post Only the receive phase address changes; config.device_address
 * remains unchanged. The next ordinary receive uses the configured address.
 * @note Used for initial address provisioning; command layer decides
 * whether to commit the new address after a valid sensor ACK.
 */
void as608_transaction_receive_at(as608_transaction_t *txn, size_t limit, uint32_t address);

/**
 * @brief Arm physical TX completion polling.
 * @param[in,out] txn Initialized idle-phase context.
 * @pre All intended frame bytes accepted by port; no active phase.
 * @post Drain phase armed; no callback invoked; deadline unchanged.
 * @note DRAINED does not establish sensor receipt or flash success.
 */
void as608_transaction_drain(as608_transaction_t *txn);

/**
 * @brief Advance one active transport phase without blocking.
 * @param[in,out] txn Initialized context with an armed phase and job deadline.
 * @return PENDING, SENT, RECEIVED, DRAINED or FAULT with host status.
 * @details Reads the clock once before any UART callback. On deadline
 * expiration returns FAULT/TIMEOUT without UART I/O. Otherwise invokes
 * at most one port I/O callback. RX validates start, exact address,
 * kind (ACK/DATA/END_DATA), storage bound, caller limit and checksum.
 * Command-specific ACK meanings and lengths are not checked here.
 * Invalid phase counters or inconsistent RX metadata produce FAULT with
 * UNEXPECTED_REPLY before pointer arithmetic or UART I/O. Callback
 * counts are checked at full width before conversion to size_t.
 * @post Non-PENDING events leave the wire phase idle. The caller must
 * schedule the next phase or finish; FAULT requires quarantine.
 * @warning No retry, byte skipping, flush or resynchronization. The
 * port may still own accepted TX bytes after a terminal event.
 */
as608_transaction_result_t as608_transaction_process(as608_transaction_t *txn);

/**
 * @brief Borrow the last validated RX payload.
 * @param[in] txn Context immediately following a RECEIVED event.
 * @return Descriptor pointing into the context's RX body, excluding checksum.
 * @pre A RECEIVED event has just been observed; not valid after a fault
 * or while a receive is incomplete. This accessor does not validate state.
 * @post No state change or callback.
 * @warning Read-only borrowed view; consume before the next receive or
 * context reset. Do not retain it as application-owned output.
 */
as608_transaction_frame_t as608_transaction_received(const as608_transaction_t *txn);

#ifdef __cplusplus
}
#endif
#endif /* AS608_TRANSACTION_H */
