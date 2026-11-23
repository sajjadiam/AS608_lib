/** @file as608_async.c @brief Cooperative AS60x packet/command engine. */
#include "as608_async.h"
#include "as608_packet.h"

#include <string.h>

enum {
    STEP_CAPTURE_ACK = 1, STEP_GEN_CHAR_ACK,
    STEP_LOAD_ACK, STEP_UP_ACK, STEP_UP_DATA,
    STEP_PARAMS_ACK, STEP_DOWN_ACK, STEP_DOWN_DATA, STEP_STORE_ACK
};
enum { WIRE_TX = 1, WIRE_RX_HEADER, WIRE_RX_BODY };
enum {
    CMD_GET_IMAGE = 0x01, CMD_GEN_CHAR = 0x02, CMD_STORE_CHAR = 0x06,
    CMD_LOAD_CHAR = 0x07, CMD_UP_CHAR = 0x08, CMD_DOWN_CHAR = 0x09,
    CMD_READ_PARAMS = 0x0F
};

static bool supported_size(size_t length)
{
    return (length == AS608_TEMPLATE_512_BYTES) ||
           (length == AS608_TEMPLATE_768_BYTES);
}

static bool valid_buffer(as608_char_buffer_t buffer)
{
    return (buffer == AS608_CHAR_BUFFER_1) ||
           (buffer == AS608_CHAR_BUFFER_2);
}

static as608_async_progress_t quarantine(as608_async_t *ctx,
                                         as608_status_t status)
{
    ctx->result.status = status;
    ctx->result.confirmation = 0U;
    ctx->state = AS608_ASYNC_DESYNCHRONIZED;
    return AS608_ASYNC_FAILED;
}

static as608_async_progress_t finish(as608_async_t *ctx, uint8_t ack)
{
    ctx->result.status = AS608_STATUS_OK;
    ctx->result.confirmation = ack;
    ctx->state = AS608_ASYNC_DONE;
    return AS608_ASYNC_COMPLETE;
}

static as608_status_t frame_start(as608_async_t *ctx,
                                  as608_packet_kind_t kind,
                                  const uint8_t *payload, size_t length)
{
    if ((length > 256U) || (length != 0U && payload == NULL)) {
        return AS608_STATUS_INVALID_ARG;
    }
    const as608_packet_header_t header = {
        ctx->config.device_address, (uint8_t)kind, (uint16_t)length
    };
    as608_status_t status = as608_packet_encode_header(&header, ctx->tx_frame);
    if (status != AS608_STATUS_OK) return status;
    if (length != 0U) {
        (void)memcpy(ctx->tx_frame + AS608_PACKET_HEADER_BYTES, payload, length);
    }
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), payload, length);
    as608_packet_checksum_encode(
        checksum, ctx->tx_frame + AS608_PACKET_HEADER_BYTES + length);
    ctx->tx_len = AS608_PACKET_OVERHEAD_BYTES + length;
    ctx->tx_pos = 0U;
    ctx->wire_phase = WIRE_TX;
    return AS608_STATUS_OK;
}

static as608_status_t command_start(as608_async_t *ctx,
                                    const uint8_t *payload, size_t length,
                                    uint8_t next_step)
{
    const as608_status_t status = frame_start(ctx, AS608_PACKET_COMMAND,
                                               payload, length);
    if (status == AS608_STATUS_OK) ctx->step = next_step;
    return status;
}

static void receive_start(as608_async_t *ctx)
{
    ctx->rx_header_pos = 0U;
    ctx->rx_body_pos = 0U;
    ctx->rx_body_len = 0U;
    ctx->wire_phase = WIRE_RX_HEADER;
}

static as608_status_t start_allowed(as608_async_t *ctx)
{
    if (ctx == NULL) return AS608_STATUS_INVALID_ARG;
    if (ctx->state == AS608_ASYNC_IDLE || ctx->state == AS608_ASYNC_DONE) {
        return AS608_STATUS_OK;
    }
    if (ctx->state == AS608_ASYNC_DESYNCHRONIZED) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }
    return (ctx->state == AS608_ASYNC_BUSY) ?
           AS608_STATUS_INVALID_ARG : AS608_STATUS_NOT_INITIALIZED;
}

static as608_status_t launch(as608_async_t *ctx, const uint8_t *cmd,
                             size_t len, uint8_t step)
{
    const as608_status_t allowed = start_allowed(ctx);
    if (allowed != AS608_STATUS_OK) return allowed;
    const as608_status_t status = command_start(ctx, cmd, len, step);
    if (status != AS608_STATUS_OK) return status;
    ctx->result.status = AS608_STATUS_NOT_INITIALIZED;
    ctx->result.confirmation = 0U;
    ctx->started_ms = ctx->config.clock_ms(ctx->config.user);
    ctx->state = AS608_ASYNC_BUSY;
    return AS608_STATUS_OK;
}

as608_status_t as608_async_init(as608_async_t *ctx,
                                const as608_async_config_t *config)
{
    if (ctx == NULL || config == NULL ||
        config->try_read == NULL || config->try_write == NULL ||
        config->clock_ms == NULL || config->timeout_ms == 0U ||
        config->timeout_ms > AS608_MAX_TIMEOUT_MS) {
        return AS608_STATUS_INVALID_ARG;
    }
    if (ctx->state == AS608_ASYNC_DESYNCHRONIZED ||
        ctx->state == AS608_ASYNC_BUSY) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->config = *config;
    ctx->state = AS608_ASYNC_IDLE;
    return AS608_STATUS_OK;
}

as608_status_t as608_async_capture_start(as608_async_t *ctx)
{
    const uint8_t command[] = {CMD_GET_IMAGE};
    return launch(ctx, command, sizeof(command), STEP_CAPTURE_ACK);
}

as608_status_t as608_async_generate_character_start(as608_async_t *ctx,
                                                    as608_char_buffer_t buffer)
{
    if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
    const uint8_t command[] = {CMD_GEN_CHAR, (uint8_t)buffer};
    return launch(ctx, command, sizeof(command), STEP_GEN_CHAR_ACK);
}

as608_status_t as608_async_read_template_start(as608_async_t *ctx,
                                               uint16_t page_id,
                                               as608_template_buffer_t *output)
{
    if (output == NULL || output->data == NULL ||
        output->capacity < AS608_TEMPLATE_512_BYTES) {
        return AS608_STATUS_INVALID_ARG;
    }
    const uint8_t command[] = {
        CMD_LOAD_CHAR, 1U, (uint8_t)(page_id >> 8U), (uint8_t)page_id
    };
    const as608_status_t status = launch(ctx,
                                         command, sizeof(command), STEP_LOAD_ACK);
    if (status == AS608_STATUS_OK) {
        ctx->page_id = page_id;
        ctx->output = output;
        ctx->output_offset = 0U;
    }
    return status;
}

as608_status_t as608_async_write_template_start(as608_async_t *ctx,
                                                uint16_t page_id,
                                                const uint8_t *data,
                                                size_t length)
{
    if (data == NULL || !supported_size(length)) return AS608_STATUS_INVALID_ARG;
    const uint8_t command[] = {CMD_READ_PARAMS};
    const as608_status_t status = launch(ctx,
                                         command, sizeof(command), STEP_PARAMS_ACK);
    if (status == AS608_STATUS_OK) {
        ctx->page_id = page_id;
        ctx->input = data;
        ctx->input_length = length;
        ctx->input_offset = 0U;
    }
    return status;
}


/* The port owns the queued TX bytes after accepting them. */
static as608_status_t send_next_chunk(as608_async_t *ctx)
{
    size_t length = ctx->input_length - ctx->input_offset;
    if (length > ctx->chunk_size) length = ctx->chunk_size;
    const as608_packet_kind_t kind =
        ctx->input_offset + length == ctx->input_length ?
        AS608_PACKET_END_DATA : AS608_PACKET_DATA;
    const as608_status_t status = frame_start(
        ctx, kind, ctx->input + ctx->input_offset, length);
    if (status == AS608_STATUS_OK) ctx->input_offset += length;
    return status;
}

static as608_status_t sent_frame(as608_async_t *ctx)
{
    if (ctx->step != STEP_DOWN_DATA) {
        receive_start(ctx);
        return AS608_STATUS_OK;
    }
    if (ctx->input_offset < ctx->input_length) {
        return send_next_chunk(ctx);
    }
    const uint8_t command[] = {
        CMD_STORE_CHAR, 1U, (uint8_t)(ctx->page_id >> 8U),
        (uint8_t)ctx->page_id
    };
    return command_start(ctx, command, sizeof(command), STEP_STORE_ACK);
}

static as608_async_progress_t next_or_fault(as608_async_t *ctx,
                                             as608_status_t status)
{
    return status == AS608_STATUS_OK ? AS608_ASYNC_PENDING :
                                      quarantine(ctx, status);
}

static as608_async_progress_t accept_template(as608_async_t *ctx)
{
    if (ctx->rx_kind != (uint8_t)AS608_PACKET_DATA &&
        ctx->rx_kind != (uint8_t)AS608_PACKET_END_DATA) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
    const size_t length = ctx->rx_payload_len;
    if (length == 0U || length > ctx->output->capacity - ctx->output_offset) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
    (void)memcpy(ctx->output->data + ctx->output_offset, ctx->rx_body, length);
    ctx->output_offset += length;
    if (ctx->rx_kind == (uint8_t)AS608_PACKET_END_DATA) {
        if (!supported_size(ctx->output_offset)) {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }
        ctx->output->length = ctx->output_offset;
        return finish(ctx, (uint8_t)AS608_ACK_OK);
    }
    receive_start(ctx);
    return AS608_ASYNC_PENDING;
}

static bool ack_valid(const as608_async_t *ctx)
{
    if (ctx->rx_kind != (uint8_t)AS608_PACKET_ACK ||
        ctx->rx_payload_len == 0U) return false;
    const size_t expected = ctx->step == STEP_PARAMS_ACK ? 17U : 1U;
    return ctx->rx_payload_len == expected ||
           (ctx->rx_body[0] != (uint8_t)AS608_ACK_OK &&
            ctx->rx_payload_len == 1U);
}

static as608_status_t begin_download(as608_async_t *ctx)
{
    const uint16_t code = ((uint16_t)ctx->rx_body[13U] << 8U) |
                          (uint16_t)ctx->rx_body[14U];
    if (code > 3U) return AS608_STATUS_UNEXPECTED_REPLY;
    ctx->chunk_size = (uint16_t)(32U << code);
    const uint8_t command[] = {CMD_DOWN_CHAR, 1U};
    return command_start(ctx, command, sizeof(command), STEP_DOWN_ACK);
}

/* ACK phase transitions do not perform UART I/O. */
static as608_async_progress_t success_ack(as608_async_t *ctx)
{
    switch (ctx->step) {
    case STEP_CAPTURE_ACK:
    case STEP_GEN_CHAR_ACK:
    case STEP_STORE_ACK:
        return finish(ctx, (uint8_t)AS608_ACK_OK);
    case STEP_LOAD_ACK: {
        const uint8_t command[] = {CMD_UP_CHAR, 1U};
        return next_or_fault(ctx,
            command_start(ctx, command, sizeof(command), STEP_UP_ACK));
    }
    case STEP_UP_ACK:
        ctx->step = STEP_UP_DATA;
        receive_start(ctx);
        return AS608_ASYNC_PENDING;
    case STEP_PARAMS_ACK:
        return next_or_fault(ctx, begin_download(ctx));
    case STEP_DOWN_ACK:
        ctx->step = STEP_DOWN_DATA;
        return next_or_fault(ctx, send_next_chunk(ctx));
    default:
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
}

static as608_async_progress_t verified_packet(as608_async_t *ctx)
{
    if (ctx->step == STEP_UP_DATA) return accept_template(ctx);
    if (!ack_valid(ctx)) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    if (ctx->rx_body[0] != (uint8_t)AS608_ACK_OK) {
        return finish(ctx, ctx->rx_body[0]);
    }
    return success_ack(ctx);
}

static as608_status_t parse_header(as608_async_t *ctx)
{
    as608_packet_header_t header;
    const as608_status_t status = as608_packet_decode_header(
        ctx->rx_header, ctx->config.device_address,
        sizeof(ctx->rx_body) - AS608_PACKET_CHECKSUM_BYTES, &header);
    if (status != AS608_STATUS_OK) return status;
    if (header.kind != (uint8_t)AS608_PACKET_ACK &&
        header.kind != (uint8_t)AS608_PACKET_DATA &&
        header.kind != (uint8_t)AS608_PACKET_END_DATA) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    if (ctx->step == STEP_UP_DATA &&
        (size_t)header.payload_size > ctx->output->capacity - ctx->output_offset) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    ctx->rx_kind = header.kind;
    ctx->rx_payload_len = header.payload_size;
    ctx->rx_body_len = (size_t)header.payload_size + AS608_PACKET_CHECKSUM_BYTES;
    ctx->rx_body_pos = 0U;
    ctx->wire_phase = WIRE_RX_BODY;
    return AS608_STATUS_OK;
}

static bool checksum_valid(const as608_async_t *ctx)
{
    const as608_packet_header_t header = {
        ctx->config.device_address, ctx->rx_kind,
        (uint16_t)ctx->rx_payload_len
    };
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), ctx->rx_body, ctx->rx_payload_len);
    return as608_packet_checksum_matches(
        checksum, ctx->rx_body + ctx->rx_payload_len);
}

static as608_async_progress_t transmit_step(as608_async_t *ctx)
{
    const size_t count = ctx->tx_len - ctx->tx_pos;
    if (count == 0U) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.try_write(
        ctx->config.user, ctx->tx_frame + ctx->tx_pos, count);
    if (n < 0 || (size_t)n > count) return quarantine(ctx, AS608_STATUS_IO_ERROR);
    ctx->tx_pos += (size_t)n;
    return ctx->tx_pos == ctx->tx_len ?
        next_or_fault(ctx, sent_frame(ctx)) : AS608_ASYNC_PENDING;
}

static as608_async_progress_t header_step(as608_async_t *ctx)
{
    const size_t count = sizeof(ctx->rx_header) - ctx->rx_header_pos;
    if (count == 0U) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.try_read(
        ctx->config.user, ctx->rx_header + ctx->rx_header_pos, count);
    if (n < 0 || (size_t)n > count) return quarantine(ctx, AS608_STATUS_IO_ERROR);
    ctx->rx_header_pos += (size_t)n;
    return ctx->rx_header_pos == sizeof(ctx->rx_header) ?
        next_or_fault(ctx, parse_header(ctx)) : AS608_ASYNC_PENDING;
}

static as608_async_progress_t payload_step(as608_async_t *ctx)
{
    const size_t count = ctx->rx_body_len - ctx->rx_body_pos;
    if (count == 0U) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.try_read(
        ctx->config.user, ctx->rx_body + ctx->rx_body_pos, count);
    if (n < 0 || (size_t)n > count) return quarantine(ctx, AS608_STATUS_IO_ERROR);
    ctx->rx_body_pos += (size_t)n;
    if (ctx->rx_body_pos != ctx->rx_body_len) return AS608_ASYNC_PENDING;
    if (!checksum_valid(ctx)) return quarantine(ctx, AS608_STATUS_CHECKSUM_ERROR);
    return verified_packet(ctx);
}



as608_async_progress_t as608_async_process(as608_async_t *ctx)
{
    if (ctx == NULL) return AS608_ASYNC_FAILED;
    if (ctx->state == AS608_ASYNC_DONE) return AS608_ASYNC_COMPLETE;
    if (ctx->state != AS608_ASYNC_BUSY) return AS608_ASYNC_FAILED;
    const uint32_t elapsed = ctx->config.clock_ms(ctx->config.user) -
                             ctx->started_ms;
    if (elapsed >= ctx->config.timeout_ms) {
        return quarantine(ctx, AS608_STATUS_TIMEOUT);
    }
    switch (ctx->wire_phase) {
    case WIRE_TX:        return transmit_step(ctx);
    case WIRE_RX_HEADER: return header_step(ctx);
    case WIRE_RX_BODY:   return payload_step(ctx);
    default:             return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
}


bool as608_async_get_result(const as608_async_t *ctx, as608_result_t *result)
{
    if (ctx == NULL || result == NULL ||
        (ctx->state != AS608_ASYNC_DONE &&
         ctx->state != AS608_ASYNC_DESYNCHRONIZED)) {
        return false;
    }
    *result = ctx->result;
    return true;
}

as608_status_t as608_async_abort(as608_async_t *ctx)
{
    if (ctx == NULL) return AS608_STATUS_INVALID_ARG;
    if (ctx->state == AS608_ASYNC_DESYNCHRONIZED) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }
    if (ctx->state != AS608_ASYNC_BUSY) return AS608_STATUS_INVALID_ARG;
    (void)quarantine(ctx, AS608_STATUS_RECOVERY_REQUIRED);
    return AS608_STATUS_OK;
}
