/** @file as608.c @brief Cooperative AS60x packet/command engine. */
#include "as608.h"
#include "as608_packet.h"

#include <string.h>

enum {
    STEP_SIMPLE_ACK = 1,
    STEP_LOAD_ACK,
    STEP_UP_ACK,
    STEP_UP_DATA,
    STEP_INFO_ACK,
    STEP_INFO_DATA,
    STEP_PARAMS_ACK,
    STEP_DOWN_ACK,
    STEP_DOWN_DATA,
    STEP_READ_PARAMS_ACK,
    STEP_U16_ACK,
    STEP_SEARCH_ACK,
    STEP_INDEX_ACK
};

enum { WIRE_TX = 1, WIRE_RX_HEADER, WIRE_RX_BODY, WIRE_TX_DRAIN };

enum {
    CMD_GET_IMAGE = 0x01, CMD_GEN_CHAR = 0x02, CMD_MATCH = 0x03,
    CMD_SEARCH = 0x04, CMD_REG_MODEL = 0x05, CMD_STORE_CHAR = 0x06,
    CMD_LOAD_CHAR = 0x07, CMD_UP_CHAR = 0x08, CMD_DOWN_CHAR = 0x09,
    CMD_DELETE_CHAR = 0x0C, CMD_EMPTY = 0x0D, CMD_READ_PARAMS = 0x0F,
    CMD_VERIFY_PASSWORD = 0x13, CMD_READ_INFO = 0x16,
    CMD_COUNT = 0x1D, CMD_READ_INDEX = 0x1F
};

typedef struct {
    uint8_t bytes[6U];
    size_t length;
    uint8_t step;
} command_t;

typedef struct {
    as608_packet_kind_t kind;
    const uint8_t *data;
    size_t length;
} frame_t;

static bool valid_range(uint16_t start, uint16_t count)
{
    return count != 0U && (uint32_t)start + (uint32_t)count <= UINT32_C(65536);
}

static uint16_t load_u16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t load_u32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static void store_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

static void decode_parameters(const uint8_t *ack, as608_parameters_t *out)
{
    out->status_register = load_u16(ack + 1U);
    out->sensor_type = load_u16(ack + 3U);
    out->template_capacity = load_u16(ack + 5U);
    out->security_level = load_u16(ack + 7U);
    out->device_address = load_u32(ack + 9U);
    out->packet_size_code = load_u16(ack + 13U);
    out->baud_multiplier = load_u16(ack + 15U);
}

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

static as608_progress_t quarantine(as608_t *ctx,
                                         as608_status_t status)
{
    ctx->result.status = status;
    ctx->result.confirmation = 0U;
    ctx->state = AS608_STATE_DESYNCHRONIZED;
    return AS608_FAILED;
}

static as608_progress_t finish(as608_t *ctx, uint8_t ack)
{
    ctx->result.status = AS608_STATUS_OK;
    ctx->result.confirmation = ack;
    ctx->state = AS608_STATE_DONE;
    return AS608_COMPLETE;
}

static as608_status_t frame_start(as608_t *ctx, const frame_t *frame)
{
    if (frame->length > 256U || (frame->length != 0U && frame->data == NULL)) {
        return AS608_STATUS_INVALID_ARG;
    }
    const as608_packet_header_t header = {
        ctx->config.device_address, (uint8_t)frame->kind, (uint16_t)frame->length
    };
    const as608_status_t status = as608_packet_encode_header(&header, ctx->tx_frame);
    if (status != AS608_STATUS_OK) return status;
    if (frame->length != 0U) {
        (void)memcpy(ctx->tx_frame + AS608_PACKET_HEADER_BYTES,
                     frame->data, frame->length);
    }
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), frame->data, frame->length);
    as608_packet_checksum_encode(checksum,
        ctx->tx_frame + AS608_PACKET_HEADER_BYTES + frame->length);
    ctx->tx_len = AS608_PACKET_OVERHEAD_BYTES + frame->length;
    ctx->tx_pos = 0U;
    ctx->wire_phase = WIRE_TX;
    return AS608_STATUS_OK;
}

static as608_status_t command_start(as608_t *ctx, const command_t *command)
{
    const frame_t frame = {AS608_PACKET_COMMAND, command->bytes, command->length};
    const as608_status_t status = frame_start(ctx, &frame);
    if (status == AS608_STATUS_OK) ctx->step = command->step;
    return status;
}

static void receive_start(as608_t *ctx)
{
    ctx->rx_header_pos = 0U;
    ctx->rx_body_pos = 0U;
    ctx->rx_body_len = 0U;
    ctx->wire_phase = WIRE_RX_HEADER;
}

static as608_status_t start_allowed(const as608_t *ctx)
{
    if (ctx == NULL) return AS608_STATUS_INVALID_ARG;
    switch (ctx->state) {
    case AS608_STATE_IDLE:
    case AS608_STATE_DONE: return AS608_STATUS_OK;
    case AS608_STATE_BUSY: return AS608_STATUS_BUSY;
    case AS608_STATE_DESYNCHRONIZED: return AS608_STATUS_RECOVERY_REQUIRED;
    default: return AS608_STATUS_NOT_INITIALIZED;
    }
}

static as608_status_t launch(as608_t *ctx, const command_t *command)
{
    const as608_status_t allowed = start_allowed(ctx);
    if (allowed != AS608_STATUS_OK) return allowed;
    const as608_status_t status = command_start(ctx, command);
    if (status != AS608_STATUS_OK) return status;
    ctx->result = (as608_result_t){AS608_STATUS_NOT_INITIALIZED, 0U};
    ctx->reply_output = NULL;
    ctx->stream_length = NULL;
    ctx->started_ms = ctx->config.port.clock_ms(ctx->config.port.user);
    ctx->state = AS608_STATE_BUSY;
    return AS608_STATUS_OK;
}

static bool valid_config(const as608_config_t *config)
{
    return config != NULL && config->port.try_read != NULL &&
           config->port.try_write != NULL && config->port.tx_idle != NULL &&
           config->port.clock_ms != NULL && config->timeout_ms != 0U &&
           config->timeout_ms <= AS608_MAX_TIMEOUT_MS;
}

as608_status_t as608_init(as608_t *ctx, const as608_config_t *config)
{
    if (ctx == NULL || !valid_config(config)) return AS608_STATUS_INVALID_ARG;
    if (ctx->state == AS608_STATE_DESYNCHRONIZED) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }
    if (ctx->state == AS608_STATE_BUSY) return AS608_STATUS_BUSY;
    if (ctx->state != AS608_STATE_UNINITIALIZED) return AS608_STATUS_INVALID_ARG;
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->config = *config;
    ctx->state = AS608_STATE_IDLE;
    return AS608_STATUS_OK;
}

static as608_status_t simple_start(as608_t *ctx, uint8_t code)
{
    return launch(ctx, &(const command_t){{code}, 1U, STEP_SIMPLE_ACK});
}

static as608_status_t reply_start(as608_t *ctx, const command_t *command,
                                  void *output)
{
    if (output == NULL) return AS608_STATUS_INVALID_ARG;
    const as608_status_t status = launch(ctx, command);
    if (status == AS608_STATUS_OK) ctx->reply_output = output;
    return status;
}

as608_status_t as608_verify_password_start(as608_t *ctx, uint32_t password)
{
    const command_t command = {{
        CMD_VERIFY_PASSWORD, (uint8_t)(password >> 24U),
        (uint8_t)(password >> 16U), (uint8_t)(password >> 8U), (uint8_t)password
    }, 5U, STEP_SIMPLE_ACK};
    return launch(ctx, &command);
}

as608_status_t as608_read_parameters_start(as608_t *ctx, as608_parameters_t *out)
{
    const command_t command = {{CMD_READ_PARAMS}, 1U, STEP_READ_PARAMS_ACK};
    return reply_start(ctx, &command, out);
}

as608_status_t as608_get_template_count_start(as608_t *ctx, uint16_t *count)
{
    const command_t command = {{CMD_COUNT}, 1U, STEP_U16_ACK};
    return reply_start(ctx, &command, count);
}

as608_status_t as608_read_index_table_start(as608_t *ctx, uint8_t index_page,
                                         uint8_t *bitmap, size_t capacity)
{
    if (index_page > 3U || capacity < 32U) return AS608_STATUS_INVALID_ARG;
    const command_t command = {{CMD_READ_INDEX, index_page}, 2U, STEP_INDEX_ACK};
    return reply_start(ctx, &command, bitmap);
}

static void stream_setup(as608_t *ctx, as608_template_buffer_t output)
{
    ctx->stream_data = output.data;
    ctx->stream_capacity = output.capacity;
    ctx->stream_offset = 0U;
}

as608_status_t as608_read_information_page_start(as608_t *ctx, uint8_t *data,
                                               size_t capacity)
{
    if (data == NULL || capacity < AS608_INFORMATION_PAGE_BYTES) {
        return AS608_STATUS_INVALID_ARG;
    }
    const command_t command = {{CMD_READ_INFO}, 1U, STEP_INFO_ACK};
    const as608_status_t status = launch(ctx, &command);
    if (status == AS608_STATUS_OK) {
        const as608_template_buffer_t output = {data, AS608_INFORMATION_PAGE_BYTES, 0U};
        stream_setup(ctx, output);
    }
    return status;
}

as608_status_t as608_capture_image_start(as608_t *ctx)
{
    return simple_start(ctx, CMD_GET_IMAGE);
}

as608_status_t as608_generate_character_start(as608_t *ctx,
                                             as608_char_buffer_t buffer)
{
    if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
    const command_t command = {{CMD_GEN_CHAR, (uint8_t)buffer}, 2U, STEP_SIMPLE_ACK};
    return launch(ctx, &command);
}

as608_status_t as608_match_buffers_start(as608_t *ctx, uint16_t *score)
{
    const command_t command = {{CMD_MATCH}, 1U, STEP_U16_ACK};
    return reply_start(ctx, &command, score);
}

as608_status_t as608_create_model_start(as608_t *ctx)
{
    return simple_start(ctx, CMD_REG_MODEL);
}

as608_status_t as608_search_start(as608_t *ctx, as608_search_t *search)
{
    if (search == NULL || !valid_buffer(search->buffer) ||
        !valid_range(search->start_page, search->page_count)) {
        return AS608_STATUS_INVALID_ARG;
    }
    command_t command = {{CMD_SEARCH, (uint8_t)search->buffer}, 6U, STEP_SEARCH_ACK};
    store_u16(command.bytes + 2U, search->start_page);
    store_u16(command.bytes + 4U, search->page_count);
    return reply_start(ctx, &command, search);
}

static command_t page_command(uint8_t code, as608_char_buffer_t buffer, uint16_t page)
{
    return (command_t){{code, (uint8_t)buffer,
        (uint8_t)(page >> 8U), (uint8_t)page}, 4U, STEP_SIMPLE_ACK};
}

as608_status_t as608_load_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                       uint16_t page_id)
{
    if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
    const command_t command = page_command(CMD_LOAD_CHAR, buffer, page_id);
    return launch(ctx, &command);
}

as608_status_t as608_store_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                        uint16_t page_id)
{
    if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
    const command_t command = page_command(CMD_STORE_CHAR, buffer, page_id);
    return launch(ctx, &command);
}

static as608_status_t upload_start(as608_t *ctx, const command_t *command,
                                   as608_template_buffer_t *output)
{
    if (output == NULL || output->data == NULL ||
        output->capacity < AS608_TEMPLATE_512_BYTES) return AS608_STATUS_INVALID_ARG;
    const as608_status_t status = launch(ctx, command);
    if (status == AS608_STATUS_OK) {
        stream_setup(ctx, *output);
        if (ctx->stream_capacity > AS608_TEMPLATE_MAX_BYTES) {
            ctx->stream_capacity = AS608_TEMPLATE_MAX_BYTES;
        }
        ctx->stream_length = &output->length;
    }
    return status;
}

as608_status_t as608_upload_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                         as608_template_buffer_t *output)
{
    if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
    const command_t command = {{CMD_UP_CHAR, (uint8_t)buffer}, 2U, STEP_UP_ACK};
    return upload_start(ctx, &command, output);
}

as608_status_t as608_read_template_start(as608_t *ctx, uint16_t page_id,
                                       as608_template_buffer_t *output)
{
    command_t command = page_command(CMD_LOAD_CHAR, AS608_CHAR_BUFFER_1, page_id);
    command.step = STEP_LOAD_ACK;
    return upload_start(ctx, &command, output);
}

typedef struct {
    const uint8_t *data;
    size_t length;
    as608_char_buffer_t buffer;
    bool store;
    uint16_t page_id;
} download_t;

static as608_status_t download_start(as608_t *ctx, const download_t *request)
{
    if (request->data == NULL || !supported_size(request->length) ||
        !valid_buffer(request->buffer)) return AS608_STATUS_INVALID_ARG;
    const command_t command = {{CMD_READ_PARAMS}, 1U, STEP_PARAMS_ACK};
    const as608_status_t status = launch(ctx, &command);
    if (status == AS608_STATUS_OK) {
        ctx->input = request->data;
        ctx->input_length = request->length;
        ctx->input_offset = 0U;
        ctx->download_buffer = request->buffer;
        ctx->store_after_download = request->store;
        ctx->page_id = request->page_id;
    }
    return status;
}

as608_status_t as608_download_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                           const uint8_t *data, size_t length)
{
    const download_t request = {data, length, buffer, false, 0U};
    return download_start(ctx, &request);
}

as608_status_t as608_write_template_start(as608_t *ctx, uint16_t page_id,
                                        const uint8_t *data, size_t length)
{
    const download_t request = {data, length, AS608_CHAR_BUFFER_1, true, page_id};
    return download_start(ctx, &request);
}

as608_status_t as608_delete_templates_start(as608_t *ctx, uint16_t start_page,
                                          uint16_t page_count)
{
    if (!valid_range(start_page, page_count)) return AS608_STATUS_INVALID_ARG;
    command_t command = {{CMD_DELETE_CHAR}, 5U, STEP_SIMPLE_ACK};
    store_u16(command.bytes + 1U, start_page);
    store_u16(command.bytes + 3U, page_count);
    return launch(ctx, &command);
}

as608_status_t as608_empty_database_start(as608_t *ctx)
{
    return simple_start(ctx, CMD_EMPTY);
}

/* The port owns the queued TX bytes after accepting them. */
static as608_status_t send_next_chunk(as608_t *ctx)
{
    size_t length = ctx->input_length - ctx->input_offset;
    if (length > ctx->chunk_size) length = ctx->chunk_size;
    const as608_packet_kind_t kind =
        ctx->input_offset + length == ctx->input_length ?
        AS608_PACKET_END_DATA : AS608_PACKET_DATA;
    const frame_t frame = {kind, ctx->input + ctx->input_offset, length};
    const as608_status_t status = frame_start(ctx, &frame);
    if (status == AS608_STATUS_OK) ctx->input_offset += length;
    return status;
}

static as608_status_t sent_frame(as608_t *ctx)
{
    if (ctx->step != STEP_DOWN_DATA) {
        receive_start(ctx);
        return AS608_STATUS_OK;
    }
    if (ctx->input_offset < ctx->input_length) return send_next_chunk(ctx);
    ctx->wire_phase = WIRE_TX_DRAIN;
    return AS608_STATUS_OK;
}

static as608_progress_t next_or_fault(as608_t *ctx, as608_status_t status)
{
    return status == AS608_STATUS_OK ? AS608_PENDING : quarantine(ctx, status);
}

static bool is_stream(const as608_t *ctx)
{
    return ctx->step == STEP_UP_DATA || ctx->step == STEP_INFO_DATA;
}

static bool stream_size_valid(const as608_t *ctx)
{
    return ctx->step == STEP_INFO_DATA ?
        ctx->stream_offset == AS608_INFORMATION_PAGE_BYTES :
        supported_size(ctx->stream_offset);
}

static as608_progress_t accept_stream(as608_t *ctx)
{
    const size_t length = ctx->rx_payload_len;
    if ((ctx->rx_kind != AS608_PACKET_DATA &&
         ctx->rx_kind != AS608_PACKET_END_DATA) ||
        length == 0U || length > ctx->stream_capacity - ctx->stream_offset) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
    (void)memcpy(ctx->stream_data + ctx->stream_offset, ctx->rx_body, length);
    ctx->stream_offset += length;
    if (ctx->rx_kind != AS608_PACKET_END_DATA) {
        receive_start(ctx);
        return AS608_PENDING;
    }
    if (!stream_size_valid(ctx)) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    if (ctx->stream_length != NULL) *ctx->stream_length = ctx->stream_offset;
    return finish(ctx, AS608_ACK_OK);
}

static size_t expected_ack(const as608_t *ctx)
{
    switch (ctx->step) {
    case STEP_PARAMS_ACK:
    case STEP_READ_PARAMS_ACK: return 17U;
    case STEP_U16_ACK: return 3U;
    case STEP_SEARCH_ACK: return 5U;
    case STEP_INDEX_ACK: return 33U;
    default: return 1U;
    }
}

static bool ack_valid(const as608_t *ctx)
{
    if (ctx->rx_kind != AS608_PACKET_ACK || ctx->rx_payload_len == 0U) return false;
    return ctx->rx_payload_len == expected_ack(ctx) ||
           (ctx->rx_body[0] != AS608_ACK_OK && ctx->rx_payload_len == 1U);
}

static as608_status_t begin_download(as608_t *ctx)
{
    const uint16_t code = load_u16(ctx->rx_body + 13U);
    if (code > 3U) return AS608_STATUS_UNEXPECTED_REPLY;
    ctx->chunk_size = (uint16_t)(32U << code);
    const command_t command = {{CMD_DOWN_CHAR, (uint8_t)ctx->download_buffer},
                                2U, STEP_DOWN_ACK};
    return command_start(ctx, &command);
}

static as608_progress_t accept_reply(as608_t *ctx)
{
    switch (ctx->step) {
    case STEP_READ_PARAMS_ACK:
        decode_parameters(ctx->rx_body, ctx->reply_output);
        break;
    case STEP_U16_ACK:
        *(uint16_t *)ctx->reply_output = load_u16(ctx->rx_body + 1U);
        break;
    case STEP_SEARCH_ACK: {
        as608_search_t *search = ctx->reply_output;
        search->match.page_id = load_u16(ctx->rx_body + 1U);
        search->match.score = load_u16(ctx->rx_body + 3U);
        break;
    }
    case STEP_INDEX_ACK:
        (void)memcpy(ctx->reply_output, ctx->rx_body + 1U, 32U);
        break;
    default:
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
    return finish(ctx, AS608_ACK_OK);
}

static as608_progress_t success_ack(as608_t *ctx)
{
    switch (ctx->step) {
    case STEP_SIMPLE_ACK:
        return finish(ctx, AS608_ACK_OK);
    case STEP_LOAD_ACK: {
        const command_t command = {{CMD_UP_CHAR, AS608_CHAR_BUFFER_1}, 2U, STEP_UP_ACK};
        return next_or_fault(ctx, command_start(ctx, &command));
    }
    case STEP_UP_ACK:
    case STEP_INFO_ACK:
        ctx->step = ctx->step == STEP_UP_ACK ? STEP_UP_DATA : STEP_INFO_DATA;
        receive_start(ctx);
        return AS608_PENDING;
    case STEP_PARAMS_ACK:
        return next_or_fault(ctx, begin_download(ctx));
    case STEP_DOWN_ACK:
        ctx->step = STEP_DOWN_DATA;
        return next_or_fault(ctx, send_next_chunk(ctx));
    default:
        return accept_reply(ctx);
    }
}

static as608_progress_t verified_packet(as608_t *ctx)
{
    if (is_stream(ctx)) return accept_stream(ctx);
    if (!ack_valid(ctx)) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    if (ctx->rx_body[0] != AS608_ACK_OK) return finish(ctx, ctx->rx_body[0]);
    return success_ack(ctx);
}

static as608_status_t parse_header(as608_t *ctx)
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
    if (is_stream(ctx) &&
        (size_t)header.payload_size > ctx->stream_capacity - ctx->stream_offset) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }
    ctx->rx_kind = header.kind;
    ctx->rx_payload_len = header.payload_size;
    ctx->rx_body_len = (size_t)header.payload_size + AS608_PACKET_CHECKSUM_BYTES;
    ctx->rx_body_pos = 0U;
    ctx->wire_phase = WIRE_RX_BODY;
    return AS608_STATUS_OK;
}

static bool checksum_valid(const as608_t *ctx)
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

static as608_progress_t transmit_step(as608_t *ctx)
{
    const size_t count = ctx->tx_len - ctx->tx_pos;
    if (count == 0U) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.port.try_write(
        ctx->config.port.user, ctx->tx_frame + ctx->tx_pos, count);
    if (n < 0 || (size_t)n > count) return quarantine(ctx, AS608_STATUS_IO_ERROR);
    ctx->tx_pos += (size_t)n;
    return ctx->tx_pos == ctx->tx_len ?
        next_or_fault(ctx, sent_frame(ctx)) : AS608_PENDING;
}

static as608_progress_t header_step(as608_t *ctx)
{
    const size_t count = sizeof(ctx->rx_header) - ctx->rx_header_pos;
    if (count == 0U) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.port.try_read(
        ctx->config.port.user, ctx->rx_header + ctx->rx_header_pos, count);
    if (n < 0 || (size_t)n > count) return quarantine(ctx, AS608_STATUS_IO_ERROR);
    ctx->rx_header_pos += (size_t)n;
    return ctx->rx_header_pos == sizeof(ctx->rx_header) ?
        next_or_fault(ctx, parse_header(ctx)) : AS608_PENDING;
}

static as608_progress_t payload_step(as608_t *ctx)
{
    const size_t count = ctx->rx_body_len - ctx->rx_body_pos;
    if (count == 0U) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    const int32_t n = ctx->config.port.try_read(
        ctx->config.port.user, ctx->rx_body + ctx->rx_body_pos, count);
    if (n < 0 || (size_t)n > count) return quarantine(ctx, AS608_STATUS_IO_ERROR);
    ctx->rx_body_pos += (size_t)n;
    if (ctx->rx_body_pos != ctx->rx_body_len) return AS608_PENDING;
    if (!checksum_valid(ctx)) return quarantine(ctx, AS608_STATUS_CHECKSUM_ERROR);
    return verified_packet(ctx);
}

static as608_progress_t drain_step(as608_t *ctx)
{
    const int32_t idle = ctx->config.port.tx_idle(ctx->config.port.user);
    if (idle == 0) return AS608_PENDING;
    if (idle != 1) return quarantine(ctx, AS608_STATUS_IO_ERROR);
    if (!ctx->store_after_download) return finish(ctx, AS608_ACK_OK);
    const command_t command = page_command(CMD_STORE_CHAR,
        ctx->download_buffer, ctx->page_id);
    return next_or_fault(ctx, command_start(ctx, &command));
}

as608_progress_t as608_process(as608_t *ctx)
{
    if (ctx == NULL) return AS608_FAILED;
    if (ctx->state == AS608_STATE_DONE) return AS608_COMPLETE;
    if (ctx->state != AS608_STATE_BUSY) return AS608_FAILED;
    const uint32_t elapsed = ctx->config.port.clock_ms(ctx->config.port.user) -
                             ctx->started_ms;
    if (elapsed >= ctx->config.timeout_ms) {
        return quarantine(ctx, AS608_STATUS_TIMEOUT);
    }
    switch (ctx->wire_phase) {
    case WIRE_TX:        return transmit_step(ctx);
    case WIRE_RX_HEADER: return header_step(ctx);
    case WIRE_RX_BODY:   return payload_step(ctx);
    case WIRE_TX_DRAIN:  return drain_step(ctx);
    default:             return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }
}

bool as608_get_result(const as608_t *ctx, as608_result_t *result)
{
    if (ctx == NULL || result == NULL ||
        (ctx->state != AS608_STATE_DONE &&
         ctx->state != AS608_STATE_DESYNCHRONIZED)) {
        return false;
    }
    *result = ctx->result;
    return true;
}

as608_status_t as608_abort(as608_t *ctx)
{
    if (ctx == NULL) return AS608_STATUS_INVALID_ARG;
    if (ctx->state == AS608_STATE_DESYNCHRONIZED) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }
    if (ctx->state != AS608_STATE_BUSY) return AS608_STATUS_INVALID_ARG;
    (void)quarantine(ctx, AS608_STATUS_RECOVERY_REQUIRED);
    return AS608_STATUS_OK;
}

as608_status_t as608_confirm_reset(as608_t *ctx)
{
    if (ctx == NULL) return AS608_STATUS_INVALID_ARG;
    if (ctx->state == AS608_STATE_UNINITIALIZED) return AS608_STATUS_NOT_INITIALIZED;
    if (ctx->state == AS608_STATE_BUSY) return AS608_STATUS_BUSY;
    if (ctx->state != AS608_STATE_DESYNCHRONIZED) return AS608_STATUS_INVALID_ARG;
    const as608_config_t config = ctx->config;
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->config = config;
    ctx->state = AS608_STATE_IDLE;
    return AS608_STATUS_OK;
}
