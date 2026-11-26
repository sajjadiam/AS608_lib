/** @file as608_transaction.c @brief Non-blocking packet transport. */
#include "as608_transaction.h"

#include <string.h>

enum { WIRE_IDLE = 0, WIRE_TX = 1, WIRE_RX_HEADER, WIRE_RX_BODY, WIRE_TX_DRAIN };

static as608_transaction_result_t event(as608_transaction_t *txn,
                                        as608_transaction_event_t code,
                                        as608_status_t status)
{
    if (code != AS608_TXN_PENDING) txn->wire_phase = WIRE_IDLE;
    return (as608_transaction_result_t){code, status};
}

static as608_transaction_result_t pending(as608_transaction_t *txn)
{
    return event(txn, AS608_TXN_PENDING, AS608_STATUS_OK);
}

static as608_transaction_result_t fault(as608_transaction_t *txn, as608_status_t status)
{
    return event(txn, AS608_TXN_FAULT, status);
}

bool as608_transaction_config_valid(const as608_config_t *config)
{
    return config != NULL && config->port.try_read != NULL &&
           config->port.try_write != NULL && config->port.tx_idle != NULL &&
           config->port.clock_ms != NULL && config->timeout_ms != 0U &&
           config->timeout_ms <= AS608_MAX_TIMEOUT_MS;
}

void as608_transaction_init(as608_transaction_t *txn, const as608_config_t *config)
{
    const as608_config_t saved = *config;
    (void)memset(txn, 0, sizeof(*txn));
    txn->config = saved;
}

void as608_transaction_begin(as608_transaction_t *txn)
{
    txn->started_ms = txn->config.port.clock_ms(txn->config.port.user);
}

as608_status_t as608_transaction_send(as608_transaction_t *txn,
                                    const as608_transaction_frame_t *frame)
{
    if (frame->length > AS608_TRANSACTION_MAX_PAYLOAD_BYTES || (frame->length != 0U && frame->data == NULL)) {
        return AS608_STATUS_INVALID_ARG;
    }
    const as608_packet_header_t header = {
        txn->config.device_address, (uint8_t)frame->kind, (uint16_t)frame->length
    };
    const as608_status_t status = as608_packet_encode_header(&header, txn->tx_frame);
    if (status != AS608_STATUS_OK) return status;
    if (frame->length != 0U) {
        (void)memcpy(txn->tx_frame + AS608_PACKET_HEADER_BYTES,
                     frame->data, frame->length);
    }
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), frame->data, frame->length);
    as608_packet_checksum_encode(checksum,
        txn->tx_frame + AS608_PACKET_HEADER_BYTES + frame->length);
    txn->tx_len = AS608_PACKET_OVERHEAD_BYTES + frame->length;
    txn->tx_pos = 0U;
    txn->wire_phase = WIRE_TX;
    return AS608_STATUS_OK;
}

void as608_transaction_receive(as608_transaction_t *txn, size_t limit)
{
    as608_transaction_receive_at(txn, limit, txn->config.device_address);
}

void as608_transaction_receive_at(as608_transaction_t *txn, size_t limit, uint32_t address)
{
    txn->rx_address = address;
    txn->rx_limit = limit;
    txn->rx_header_pos = 0U;
    txn->rx_body_pos = 0U;
    txn->rx_body_len = 0U;
    txn->wire_phase = WIRE_RX_HEADER;
}

void as608_transaction_drain(as608_transaction_t *txn)
{
    txn->wire_phase = WIRE_TX_DRAIN;
}

static as608_status_t parse_header(as608_transaction_t *txn)
{
    as608_packet_header_t header;
    const as608_status_t status = as608_packet_decode_header(
        txn->rx_header, txn->rx_address,
        sizeof(txn->rx_body) - AS608_PACKET_CHECKSUM_BYTES, &header);
    if (status != AS608_STATUS_OK) return status;
    if (header.kind != (uint8_t)AS608_PACKET_ACK &&
        header.kind != (uint8_t)AS608_PACKET_DATA &&
        header.kind != (uint8_t)AS608_PACKET_END_DATA) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    if ((size_t)header.payload_size > txn->rx_limit) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    txn->rx_kind = header.kind;
    txn->rx_payload_len = header.payload_size;
    txn->rx_body_len = (size_t)header.payload_size + AS608_PACKET_CHECKSUM_BYTES;
    txn->rx_body_pos = 0U;
    txn->wire_phase = WIRE_RX_BODY;
    return AS608_STATUS_OK;
}

static bool checksum_valid(const as608_transaction_t *txn)
{
    const as608_packet_header_t header = {
        txn->rx_address, txn->rx_kind,
        (uint16_t)txn->rx_payload_len
    };
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), txn->rx_body, txn->rx_payload_len);
    return as608_packet_checksum_matches(
        checksum, txn->rx_body + txn->rx_payload_len);
}

static as608_transaction_result_t transmit_step(as608_transaction_t *txn)
{
    const size_t count = txn->tx_len - txn->tx_pos;
    if (count == 0U) return fault(txn, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = txn->config.port.try_write(
        txn->config.port.user, txn->tx_frame + txn->tx_pos, count);
    if (n < 0 || (size_t)n > count) return fault(txn, AS608_STATUS_IO_ERROR);
    txn->tx_pos += (size_t)n;
    return txn->tx_pos == txn->tx_len ?
        event(txn, AS608_TXN_SENT, AS608_STATUS_OK) : pending(txn);
}

static as608_transaction_result_t header_step(as608_transaction_t *txn)
{
    const size_t count = sizeof(txn->rx_header) - txn->rx_header_pos;
    if (count == 0U) return fault(txn, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = txn->config.port.try_read(
        txn->config.port.user, txn->rx_header + txn->rx_header_pos, count);
    if (n < 0 || (size_t)n > count) return fault(txn, AS608_STATUS_IO_ERROR);
    txn->rx_header_pos += (size_t)n;
    if (txn->rx_header_pos != sizeof(txn->rx_header)) return pending(txn);
    const as608_status_t status = parse_header(txn);
    return status == AS608_STATUS_OK ? pending(txn) : fault(txn, status);
}

static as608_transaction_result_t payload_step(as608_transaction_t *txn)
{
    const size_t count = txn->rx_body_len - txn->rx_body_pos;
    if (count == 0U) return fault(txn, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = txn->config.port.try_read(
        txn->config.port.user, txn->rx_body + txn->rx_body_pos, count);
    if (n < 0 || (size_t)n > count) return fault(txn, AS608_STATUS_IO_ERROR);
    txn->rx_body_pos += (size_t)n;
    if (txn->rx_body_pos != txn->rx_body_len) return pending(txn);
    if (!checksum_valid(txn)) return fault(txn, AS608_STATUS_CHECKSUM_ERROR);
    return event(txn, AS608_TXN_RECEIVED, AS608_STATUS_OK);
}

static as608_transaction_result_t drain_step(as608_transaction_t *txn)
{
    const int32_t idle = txn->config.port.tx_idle(txn->config.port.user);
    if (idle == 0) return pending(txn);
    if (idle != 1) return fault(txn, AS608_STATUS_IO_ERROR);
    return event(txn, AS608_TXN_DRAINED, AS608_STATUS_OK);
}

as608_transaction_result_t as608_transaction_process(as608_transaction_t *txn)
{
    const uint32_t elapsed = txn->config.port.clock_ms(txn->config.port.user) -
                             txn->started_ms;
    if (elapsed >= txn->config.timeout_ms) return fault(txn, AS608_STATUS_TIMEOUT);
    switch (txn->wire_phase) {
    case WIRE_TX:       return transmit_step(txn);
    case WIRE_RX_HEADER: return header_step(txn);
    case WIRE_RX_BODY:  return payload_step(txn);
    case WIRE_TX_DRAIN: return drain_step(txn);
    default: return fault(txn, AS608_STATUS_UNEXPECTED_REPLY);
    }
}

as608_transaction_frame_t as608_transaction_received(const as608_transaction_t *txn)
{
    return (as608_transaction_frame_t){
        (as608_packet_kind_t)txn->rx_kind, txn->rx_body, txn->rx_payload_len
    };
}
