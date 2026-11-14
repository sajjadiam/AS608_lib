/**
 * @file as608_transaction.c
 * @brief Blocking AS60x UART frame transfer with a transaction-wide deadline.
 *
 * This module does not resynchronize or retry an entire command. A corrupted
 * or truncated frame can leave RX bytes pending; device-level recovery must
 * occur before a new command is sent.
 */
#include "as608_transaction.h"

#include <limits.h>
#include <stdbool.h>

static bool as608_tx_kind_is_valid(as608_packet_kind_t kind) {
    return (kind == AS608_PACKET_COMMAND) ||
           (kind == AS608_PACKET_DATA) ||
           (kind == AS608_PACKET_END_DATA);
}

static bool as608_rx_kind_is_valid(uint8_t kind) {
    return (kind == (uint8_t)AS608_PACKET_ACK) ||
           (kind == (uint8_t)AS608_PACKET_DATA) ||
           (kind == (uint8_t)AS608_PACKET_END_DATA);
}

static bool as608_transaction_is_active(const as608_transaction_t *txn) {
    return (txn != NULL) && (txn->dev != NULL) && (txn->dev->initialized != 0U);
}

static as608_status_t as608_remaining_ms(const as608_transaction_t *txn, uint32_t *remaining) {
    const as608_config_t *config = &txn->dev->config;
    const uint32_t now = config->port.clock_ms(config->port.user);
    const uint32_t elapsed = now - txn->started_ms;

    if (elapsed >= config->timeout_ms) {
        *remaining = 0U;
        return AS608_STATUS_TIMEOUT;
    }
    *remaining = config->timeout_ms - elapsed;
    return AS608_STATUS_OK;
}

static as608_status_t as608_write_exact(as608_transaction_t *txn, const uint8_t *data, size_t length) {
    size_t offset = 0U;
    const as608_port_t *port = &txn->dev->config.port;

    while (offset < length) {
        uint32_t remaining_ms = 0U;
        as608_status_t status = as608_remaining_ms(txn, &remaining_ms);
        if (status != AS608_STATUS_OK) return status;
        size_t requested = length - offset;
        if (requested > (size_t)INT32_MAX) requested = (size_t)INT32_MAX;
        const int32_t count = port->write(port->user, data + offset, requested, remaining_ms);
        if ((count < 0) || ((size_t)count > requested)) return AS608_STATUS_IO_ERROR;
        /* A zero-byte transfer is not success: retry within the SAME deadline. */
        offset += (size_t)count;
    }
    return AS608_STATUS_OK;
}

static as608_status_t as608_read_exact(as608_transaction_t *txn, uint8_t *data, size_t length) {
    size_t offset = 0U;
    const as608_port_t *port = &txn->dev->config.port;

    while (offset < length) {
        uint32_t remaining_ms = 0U;
        as608_status_t status = as608_remaining_ms(txn, &remaining_ms);
        if (status != AS608_STATUS_OK) return status;
        size_t requested = length - offset;
        if (requested > (size_t)INT32_MAX) requested = (size_t)INT32_MAX;
        const int32_t count = port->read(port->user, data + offset, requested, remaining_ms);
        if ((count < 0) || ((size_t)count > requested)) return AS608_STATUS_IO_ERROR;
        offset += (size_t)count;
    }
    return AS608_STATUS_OK;
}

static as608_status_t as608_transaction_fail(as608_transaction_t *txn, as608_status_t status) {
    if (status != AS608_STATUS_OK) {
        txn->fault = status;
        txn->dev->link_unsynchronized = 1U;
    }
    return status;
}

as608_status_t as608_transaction_begin(as608_transaction_t *txn, as608_t *dev) {
    if ((txn == NULL) || (dev == NULL)) return AS608_STATUS_INVALID_ARG;
    if (dev->initialized == 0U) return AS608_STATUS_NOT_INITIALIZED;
    if (dev->link_unsynchronized != 0U) return AS608_STATUS_RECOVERY_REQUIRED;
    const as608_config_t *config = &dev->config;
    if (!config->port.write || !config->port.read || !config->port.clock_ms ||
        (config->timeout_ms == 0U) || (config->timeout_ms > AS608_MAX_TIMEOUT_MS)) {
        return AS608_STATUS_INVALID_ARG;
    }
    txn->dev = dev;
    txn->started_ms = config->port.clock_ms(config->port.user);
    txn->fault = AS608_STATUS_OK;
    return AS608_STATUS_OK;
}

/** @brief Serialize and transmit a validated wire header. */
static as608_status_t as608_send_header(as608_transaction_t *txn, const as608_packet_header_t *header) {
    uint8_t wire[AS608_PACKET_HEADER_BYTES];
    const as608_status_t status = as608_packet_encode_header(header, wire);
    if (status != AS608_STATUS_OK) return status;
    return as608_write_exact(txn, wire, sizeof(wire));
}

/** @brief Transmit payload followed by its big-endian additive checksum. */
static as608_status_t as608_send_payload(
    as608_transaction_t *txn,
    const as608_packet_header_t *header,
    const uint8_t *payload,
    size_t size)
{
    uint16_t sum = as608_packet_checksum_begin(header);
    sum = as608_packet_checksum_update(sum, payload, size);

    if (size != 0U) {
        const as608_status_t status = as608_write_exact(txn, payload, size);
        if (status != AS608_STATUS_OK) return status;
    }

    uint8_t trailer[AS608_PACKET_CHECKSUM_BYTES];
    as608_packet_checksum_encode(sum, trailer);
    return as608_write_exact(txn, trailer, sizeof(trailer));
}

/** @brief Read and validate an incoming header before receiving payload. */
static as608_status_t as608_receive_header(
    as608_transaction_t *txn,
    size_t capacity,
    as608_packet_header_t *header)
{
    uint8_t wire[AS608_PACKET_HEADER_BYTES];
    as608_status_t status = as608_read_exact(txn, wire, sizeof(wire));
    if (status != AS608_STATUS_OK) return status;

    status = as608_packet_decode_header(
        wire, txn->dev->config.device_address, capacity, header);
    if (status != AS608_STATUS_OK) return status;
    if (!as608_rx_kind_is_valid(header->kind) ||
        ((header->kind == (uint8_t)AS608_PACKET_ACK) && (header->payload_size == 0U)))
    {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    return AS608_STATUS_OK;
}

/** @brief Read payload and trailer; expose data only after checksum matches. */
static as608_status_t as608_receive_payload(
    as608_transaction_t *txn,
    const as608_packet_header_t *header,
    uint8_t *data)
{
    uint16_t sum = as608_packet_checksum_begin(header);
    if (header->payload_size != 0U) {
        const as608_status_t status = as608_read_exact(txn, data, (size_t)header->payload_size);
        if (status != AS608_STATUS_OK) return status;
        sum = as608_packet_checksum_update(sum, data, (size_t)header->payload_size);
    }

    uint8_t trailer[AS608_PACKET_CHECKSUM_BYTES];
    const as608_status_t status = as608_read_exact(txn, trailer, sizeof(trailer));
    if (status != AS608_STATUS_OK) return status;
    if (!as608_packet_checksum_matches(sum, trailer)) return AS608_STATUS_CHECKSUM_ERROR;
    return AS608_STATUS_OK;
}

as608_status_t as608_transaction_send(
    as608_transaction_t *txn,
    as608_packet_kind_t kind,
    const uint8_t *payload,
    size_t size)
{
    if (!as608_transaction_is_active(txn)) return AS608_STATUS_NOT_INITIALIZED;
    if (txn->fault != AS608_STATUS_OK) return txn->fault;
    if (txn->dev->link_unsynchronized != 0U) return AS608_STATUS_RECOVERY_REQUIRED;
    if (!as608_tx_kind_is_valid(kind) || (size > AS608_PACKET_WIRE_PAYLOAD_MAX) ||
        ((payload == NULL) && (size != 0U)) || ((kind == AS608_PACKET_COMMAND) && (size == 0U)))
    {
        return AS608_STATUS_INVALID_ARG;
    }

    const as608_packet_header_t header = {
        txn->dev->config.device_address,
        (uint8_t)kind,
        (uint16_t)size
    };

    as608_status_t status = as608_send_header(txn, &header);
    if (status == AS608_STATUS_OK) status = as608_send_payload(txn, &header, payload, size);
    return as608_transaction_fail(txn, status);
}

as608_status_t as608_transaction_receive(
    as608_transaction_t *txn,
    as608_transaction_rx_t *frame)
{
    if (!as608_transaction_is_active(txn)) return AS608_STATUS_NOT_INITIALIZED;
    if (txn->fault != AS608_STATUS_OK) return txn->fault;
    if (txn->dev->link_unsynchronized != 0U) return AS608_STATUS_RECOVERY_REQUIRED;
    if (!frame || (!frame->data && (frame->capacity != 0U))) return AS608_STATUS_INVALID_ARG;

    as608_packet_header_t header;
    as608_status_t status = as608_receive_header(txn, frame->capacity, &header);
    if (status == AS608_STATUS_OK) status = as608_receive_payload(txn, &header, frame->data);
    if (status != AS608_STATUS_OK) return as608_transaction_fail(txn, status);

    frame->kind = (as608_packet_kind_t)header.kind;
    frame->length = (size_t)header.payload_size;
    return AS608_STATUS_OK;
}

/**
 * @brief Reestablish the physical sensor/UART epoch through the platform.
 * @details Reset must cancel prior sensor replies and clear all RX queues.
 * A failed or absent callback never clears the device quarantine.
 */
as608_status_t as608_recover(as608_t *dev)
{
    if (dev == NULL) {
        return AS608_STATUS_INVALID_ARG;
    }
    if (dev->initialized == 0U) {
        return AS608_STATUS_NOT_INITIALIZED;
    }
    const as608_config_t *config = &dev->config;
    if (config->port.recover == NULL) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }
    if ((config->port.clock_ms == NULL) ||
        (config->timeout_ms == 0U) ||
        (config->timeout_ms > AS608_MAX_TIMEOUT_MS)) {
        return AS608_STATUS_INVALID_ARG;
    }

    /* A failed reset can also disrupt a previously valid communication. */
    dev->link_unsynchronized = 1U;
    const uint32_t start = config->port.clock_ms(config->port.user);
    const int32_t result = config->port.recover(config->port.user,
                                                config->timeout_ms);
    const uint32_t elapsed = config->port.clock_ms(config->port.user) - start;
    if (elapsed >= config->timeout_ms) {
        return AS608_STATUS_TIMEOUT;
    }
    if (result != 0) {
        return AS608_STATUS_IO_ERROR;
    }
    dev->link_unsynchronized = 0U;
    return AS608_STATUS_OK;
}
