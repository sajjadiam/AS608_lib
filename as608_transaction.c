/** @file as608_transaction.c @brief Non-blocking packet transport. */
#include "as608_transaction.h"

#include <string.h>

enum { WIRE_IDLE = 0, WIRE_TX = 1, WIRE_RX_HEADER, WIRE_RX_BODY, WIRE_TX_DRAIN };

static as608_transaction_result_t event(as608_t *ctx,
                                        as608_transaction_event_t code,
                                        as608_status_t status)
{
    if (code != AS608_TXN_PENDING) ctx->transaction.wire_phase = WIRE_IDLE;
    return (as608_transaction_result_t){code, status};
}

static as608_transaction_result_t pending(as608_t *ctx)
{
    return event(ctx, AS608_TXN_PENDING, AS608_STATUS_OK);
}

static as608_transaction_result_t fault(as608_t *ctx, as608_status_t status)
{
    return event(ctx, AS608_TXN_FAULT, status);
}

void as608_transaction_begin(as608_t *ctx)
{
    ctx->transaction.started_ms = ctx->config.port.clock_ms(ctx->config.port.user);
}

as608_status_t as608_transaction_send(as608_t *ctx,
                                    const as608_transaction_frame_t *frame)
{
    if (frame->length > 256U || (frame->length != 0U && frame->data == NULL)) {
        return AS608_STATUS_INVALID_ARG;
    }
    const as608_packet_header_t header = {
        ctx->config.device_address, (uint8_t)frame->kind, (uint16_t)frame->length
    };
    const as608_status_t status = as608_packet_encode_header(&header, ctx->transaction.tx_frame);
    if (status != AS608_STATUS_OK) return status;
    if (frame->length != 0U) {
        (void)memcpy(ctx->transaction.tx_frame + AS608_PACKET_HEADER_BYTES,
                     frame->data, frame->length);
    }
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), frame->data, frame->length);
    as608_packet_checksum_encode(checksum,
        ctx->transaction.tx_frame + AS608_PACKET_HEADER_BYTES + frame->length);
    ctx->transaction.tx_len = AS608_PACKET_OVERHEAD_BYTES + frame->length;
    ctx->transaction.tx_pos = 0U;
    ctx->transaction.wire_phase = WIRE_TX;
    return AS608_STATUS_OK;
}

void as608_transaction_receive(as608_t *ctx, size_t limit)
{
    ctx->transaction.rx_limit = limit;
    ctx->transaction.rx_header_pos = 0U;
    ctx->transaction.rx_body_pos = 0U;
    ctx->transaction.rx_body_len = 0U;
    ctx->transaction.wire_phase = WIRE_RX_HEADER;
}

void as608_transaction_drain(as608_t *ctx)
{
    ctx->transaction.wire_phase = WIRE_TX_DRAIN;
}

static as608_status_t parse_header(as608_t *ctx)
{
    as608_packet_header_t header;
    const as608_status_t status = as608_packet_decode_header(
        ctx->transaction.rx_header, ctx->config.device_address,
        sizeof(ctx->transaction.rx_body) - AS608_PACKET_CHECKSUM_BYTES, &header);
    if (status != AS608_STATUS_OK) return status;
    if (header.kind != (uint8_t)AS608_PACKET_ACK &&
        header.kind != (uint8_t)AS608_PACKET_DATA &&
        header.kind != (uint8_t)AS608_PACKET_END_DATA) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    if ((size_t)header.payload_size > ctx->transaction.rx_limit) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    ctx->transaction.rx_kind = header.kind;
    ctx->transaction.rx_payload_len = header.payload_size;
    ctx->transaction.rx_body_len = (size_t)header.payload_size + AS608_PACKET_CHECKSUM_BYTES;
    ctx->transaction.rx_body_pos = 0U;
    ctx->transaction.wire_phase = WIRE_RX_BODY;
    return AS608_STATUS_OK;
}

static bool checksum_valid(const as608_t *ctx)
{
    const as608_packet_header_t header = {
        ctx->config.device_address, ctx->transaction.rx_kind,
        (uint16_t)ctx->transaction.rx_payload_len
    };
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), ctx->transaction.rx_body, ctx->transaction.rx_payload_len);
    return as608_packet_checksum_matches(
        checksum, ctx->transaction.rx_body + ctx->transaction.rx_payload_len);
}

static as608_transaction_result_t transmit_step(as608_t *ctx)
{
    const size_t count = ctx->transaction.tx_len - ctx->transaction.tx_pos;
    if (count == 0U) return fault(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.port.try_write(
        ctx->config.port.user, ctx->transaction.tx_frame + ctx->transaction.tx_pos, count);
    if (n < 0 || (size_t)n > count) return fault(ctx, AS608_STATUS_IO_ERROR);
    ctx->transaction.tx_pos += (size_t)n;
    return ctx->transaction.tx_pos == ctx->transaction.tx_len ?
        event(ctx, AS608_TXN_SENT, AS608_STATUS_OK) : pending(ctx);
}

static as608_transaction_result_t header_step(as608_t *ctx)
{
    const size_t count = sizeof(ctx->transaction.rx_header) - ctx->transaction.rx_header_pos;
    if (count == 0U) return fault(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.port.try_read(
        ctx->config.port.user, ctx->transaction.rx_header + ctx->transaction.rx_header_pos, count);
    if (n < 0 || (size_t)n > count) return fault(ctx, AS608_STATUS_IO_ERROR);
    ctx->transaction.rx_header_pos += (size_t)n;
    if (ctx->transaction.rx_header_pos != sizeof(ctx->transaction.rx_header)) return pending(ctx);
    const as608_status_t status = parse_header(ctx);
    return status == AS608_STATUS_OK ? pending(ctx) : fault(ctx, status);
}

static as608_transaction_result_t payload_step(as608_t *ctx)
{
    const size_t count = ctx->transaction.rx_body_len - ctx->transaction.rx_body_pos;
    if (count == 0U) return fault(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.port.try_read(
        ctx->config.port.user, ctx->transaction.rx_body + ctx->transaction.rx_body_pos, count);
    if (n < 0 || (size_t)n > count) return fault(ctx, AS608_STATUS_IO_ERROR);
    ctx->transaction.rx_body_pos += (size_t)n;
    if (ctx->transaction.rx_body_pos != ctx->transaction.rx_body_len) return pending(ctx);
    if (!checksum_valid(ctx)) return fault(ctx, AS608_STATUS_CHECKSUM_ERROR);
    return event(ctx, AS608_TXN_RECEIVED, AS608_STATUS_OK);
}

static as608_transaction_result_t drain_step(as608_t *ctx)
{
    const int32_t idle = ctx->config.port.tx_idle(ctx->config.port.user);
    if (idle == 0) return pending(ctx);
    if (idle != 1) return fault(ctx, AS608_STATUS_IO_ERROR);
    return event(ctx, AS608_TXN_DRAINED, AS608_STATUS_OK);
}

as608_transaction_result_t as608_transaction_process(as608_t *ctx)
{
    const uint32_t elapsed = ctx->config.port.clock_ms(ctx->config.port.user) -
                             ctx->transaction.started_ms;
    if (elapsed >= ctx->config.timeout_ms) return fault(ctx, AS608_STATUS_TIMEOUT);
    switch (ctx->transaction.wire_phase) {
    case WIRE_TX:       return transmit_step(ctx);
    case WIRE_RX_HEADER: return header_step(ctx);
    case WIRE_RX_BODY:  return payload_step(ctx);
    case WIRE_TX_DRAIN: return drain_step(ctx);
    default: return fault(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
}

as608_transaction_frame_t as608_transaction_received(const as608_t *ctx)
{
    return (as608_transaction_frame_t){
        (as608_packet_kind_t)ctx->transaction.rx_kind, ctx->transaction.rx_body, ctx->transaction.rx_payload_len
    };
}
