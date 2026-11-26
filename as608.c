/** @file as608.c @brief AS60x command and job lifecycle management. */
#include "as608.h"
#include "as608_transaction.h"

#include <string.h>

/* =========================================================================
 * Internal command opcodes and job phases
 * ========================================================================= */

enum as608_command {
    CMD_GET_IMAGE = 0x01,
    CMD_GEN_CHAR = 0x02,
    CMD_MATCH = 0x03,
    CMD_SEARCH = 0x04,
    CMD_REG_MODEL = 0x05,
    CMD_STORE_CHAR = 0x06,
    CMD_LOAD_CHAR = 0x07,
    CMD_UP_CHAR = 0x08,
    CMD_DOWN_CHAR = 0x09,
    CMD_UP_IMAGE = 0x0A,
    CMD_DOWN_IMAGE = 0x0B,
    CMD_DELETE_CHAR = 0x0C,
    CMD_EMPTY = 0x0D,
    CMD_WRITE_REG = 0x0E,
    CMD_READ_PARAMS = 0x0F,
    CMD_ENROLL = 0x10,
    CMD_IDENTIFY = 0x11,
    CMD_SET_PASSWORD = 0x12,
    CMD_VERIFY_PASSWORD = 0x13,
    CMD_GET_RANDOM_CODE = 0x14,
    CMD_SET_ADDRESS = 0x15,
    CMD_READ_INFO = 0x16,
    CMD_PORT_CONTROL = 0x17,
    CMD_WRITE_NOTEPAD = 0x18,
    CMD_READ_NOTEPAD = 0x19,
    CMD_HIGH_SPEED_SEARCH = 0x1B,
    CMD_GEN_BIN_IMAGE = 0x1C,
    CMD_COUNT = 0x1D,
    CMD_READ_INDEX = 0x1F
};

enum as608_register {
    REG_BAUD_RATE = 0x04,
    REG_SECURITY_LEVEL = 0x05,
    REG_PACKET_SIZE = 0x06
};

enum as608_step {
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
    STEP_BYTES32_ACK,
    STEP_DOWN_DRAIN,
    STEP_IDENTIFY_ACK,
    STEP_FEATURE_ACK,
    STEP_FEATURE_DATA,
    STEP_IMAGE_ACK,
    STEP_IMAGE_DATA,
    STEP_DOWN_SOURCE,
    STEP_DEVICE_INFO_ACK,
    STEP_DEVICE_INFO_DATA,
    STEP_U32_ACK,
    STEP_ADDRESS_ACK
};

/* =========================================================================
 * Shared job lifecycle helpers
 * ========================================================================= */

/* Record a host fault and quarantine the link until external recovery. */
static as608_progress_t quarantine(as608_t *ctx, as608_status_t status) {
    ctx->result.status = status;
    ctx->result.confirmation = 0U;
    ctx->state = AS608_STATE_DESYNCHRONIZED;
    return AS608_FAILED;
}

/* Store the raw sensor confirmation and mark the job complete. */
static as608_progress_t finish(as608_t *ctx, uint8_t ack) {
    ctx->result.status = AS608_STATUS_OK;
    ctx->result.confirmation = ack;
    ctx->state = AS608_STATE_DONE;
    return AS608_COMPLETE;
}

/* =========================================================================
 * API 01: as608_init
 * =========================================================================
 * No local static helper is needed. Configuration validation and transport
 * initialization belong to the existing transaction layer.
 */

as608_status_t as608_init(as608_t *ctx, const as608_config_t *config) {
    if (!ctx || !as608_transaction_config_valid(config)) {
        return AS608_STATUS_INVALID_ARG;
    }

    switch (ctx->state) {
        case AS608_STATE_UNINITIALIZED: {
            break;
        }
        case AS608_STATE_BUSY: {
            return AS608_STATUS_BUSY;
        }
        case AS608_STATE_DESYNCHRONIZED: {
            return AS608_STATUS_RECOVERY_REQUIRED;
        }
        default: {
            return AS608_STATUS_INVALID_ARG;
        }
    }

    /* Preserve configuration even if it aliases ctx->transaction.config. */
    const as608_config_t saved = *config;
    (void)memset(ctx, 0, sizeof(*ctx));
    as608_transaction_init(&ctx->transaction, &saved);
    ctx->state = AS608_STATE_IDLE;

    return AS608_STATUS_OK;
}

/* =========================================================================
 * END API 01
 * ========================================================================= */

/* =========================================================================
 * API 03: as608_get_result
 * ========================================================================= */

bool as608_get_result(const as608_t *ctx, as608_result_t *result) {
    if (!ctx || !result) {
        return false;
    }

    if ((ctx->state != AS608_STATE_DONE) && (ctx->state != AS608_STATE_DESYNCHRONIZED)) {
        return false;
    }

    *result = ctx->result;
    return true;
}

/* =========================================================================
 * END API 03
 * ========================================================================= */

/* =========================================================================
 * API 04: as608_abort
 * ========================================================================= */

as608_status_t as608_abort(as608_t *ctx) {
    if (!ctx) {
        return AS608_STATUS_INVALID_ARG;
    }

    if (ctx->state == AS608_STATE_DESYNCHRONIZED) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }

    if (ctx->state != AS608_STATE_BUSY) {
        return AS608_STATUS_INVALID_ARG;
    }

    (void)quarantine(ctx, AS608_STATUS_RECOVERY_REQUIRED);
    return AS608_STATUS_OK;
}

/* =========================================================================
 * END API 04
 * ========================================================================= */

/* =========================================================================
 * API 05: as608_confirm_reset
 * ========================================================================= */

as608_status_t as608_confirm_reset(as608_t *ctx) {
    if (!ctx) {
        return AS608_STATUS_INVALID_ARG;
    }

    switch (ctx->state) {
        case AS608_STATE_UNINITIALIZED: {
            return AS608_STATUS_NOT_INITIALIZED;
        }
        case AS608_STATE_BUSY: {
            return AS608_STATUS_BUSY;
        }
        case AS608_STATE_DESYNCHRONIZED: {
            break;
        }
        default: {
            return AS608_STATUS_INVALID_ARG;
        }
    }

    const as608_config_t config = ctx->transaction.config;
    (void)memset(ctx, 0, sizeof(*ctx));
    as608_transaction_init(&ctx->transaction, &config);
    ctx->state = AS608_STATE_IDLE;

    return AS608_STATUS_OK;
}

/* =========================================================================
 * END API 05
 * ========================================================================= */

/* =========================================================================
 * API 06: as608_verify_password_start
 * =========================================================================
 * PS_VfyPwd (0x13), AS60x Communication Manual V1.0, p. 22.
 * Command admission and launch helpers are shared by later start APIs.
 */

typedef struct {
    uint8_t bytes[6U];
    size_t length;
    uint8_t step;
} command_t;

/* Check whether the context can admit a new job. */
static as608_status_t start_allowed(const as608_t *ctx) {
    if (!ctx) {
        return AS608_STATUS_INVALID_ARG;
    }

    switch (ctx->state) {
        case AS608_STATE_IDLE:
        case AS608_STATE_DONE: {
            return AS608_STATUS_OK;
        }
        case AS608_STATE_BUSY: {
            return AS608_STATUS_BUSY;
        }
        case AS608_STATE_DESYNCHRONIZED: {
            return AS608_STATUS_RECOVERY_REQUIRED;
        }
        default: {
            return AS608_STATUS_NOT_INITIALIZED;
        }
    }
}

/* Prepare the command frame and select its next processing phase. */
static as608_status_t command_start(as608_t *ctx, const uint8_t *bytes, size_t length, uint8_t step) {
    const as608_transaction_frame_t frame = {
        AS608_PACKET_COMMAND, bytes, length
    };
    const as608_status_t status = as608_transaction_send(&ctx->transaction, &frame);

    if (status == AS608_STATUS_OK) {
        ctx->step = step;
    }

    return status;
}

/* Admit a command payload, start the job deadline and enter BUSY. */
static as608_status_t launch_payload(as608_t *ctx, const uint8_t *bytes, size_t length, uint8_t step) {
    const as608_status_t allowed = start_allowed(ctx);

    if (allowed != AS608_STATUS_OK) {
        return allowed;
    }

    const as608_status_t status = command_start(ctx, bytes, length, step);

    if (status != AS608_STATUS_OK) {
        return status;
    }

    ctx->result = (as608_result_t){AS608_STATUS_NOT_INITIALIZED, 0U};
    ctx->reply_output = NULL;
    ctx->stream_length = NULL;
    ctx->stream_write = NULL;
    ctx->stream_read = NULL;
    ctx->stream_user = NULL;
    as608_transaction_begin(&ctx->transaction);
    ctx->state = AS608_STATE_BUSY;

    return AS608_STATUS_OK;
}

/* Launch a short command using the shared payload admission path. */
static as608_status_t launch(as608_t *ctx, const command_t *command) {
    return launch_payload(ctx, command->bytes, command->length, command->step);
}

/* Launch a password command with a big-endian 32-bit value. */
static as608_status_t password_start(as608_t *ctx, uint8_t code, uint32_t password) {
    const command_t command = {{
        code,
        (uint8_t)(password >> 24U),
        (uint8_t)(password >> 16U),
        (uint8_t)(password >> 8U),
        (uint8_t)password
    }, 5U, STEP_SIMPLE_ACK};

    return launch(ctx, &command);
}

as608_status_t as608_verify_password_start(as608_t *ctx, uint32_t password) {
    return password_start(ctx, CMD_VERIFY_PASSWORD, password);
}

/* =========================================================================
 * END API 06
 * ========================================================================= */

/* =========================================================================
 * API 07: as608_read_parameters_start
 * =========================================================================
 * PS_ReadSysPara (0x0F), AS60x Communication Manual V1.0, pp. 19–20.
 * Successful ACK: confirmation byte followed by 16 parameter bytes.
 */

/* Decode two big-endian bytes into a uint16_t. */
static uint16_t load_u16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

/* Decode four big-endian bytes into a uint32_t. */
static uint32_t load_u32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

/* Decode the 16-byte system parameter block into the output structure. */
static void decode_parameters(const uint8_t *data, as608_parameters_t *out) {
    out->status_register = load_u16(data);
    out->sensor_type = load_u16(data + 2U);
    out->template_capacity = load_u16(data + 4U);
    out->security_level = load_u16(data + 6U);
    out->device_address = load_u32(data + 8U);
    out->packet_size_code = load_u16(data + 12U);
    out->baud_multiplier = load_u16(data + 14U);
}

/* Validate the parameter ACK and decode its successful response. */
static as608_progress_t accept_parameters_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || ((frame.length != 1U) && (frame.length != 17U))) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if ((frame.length != 17U) || !ctx->reply_output) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    decode_parameters(frame.data + 1U, ctx->reply_output);
    return finish(ctx, AS608_ACK_OK);
}

/* Launch a command and retain its caller-owned output pointer. */
static as608_status_t reply_start(as608_t *ctx, const command_t *command, void *output) {
    if (!output) {
        return AS608_STATUS_INVALID_ARG;
    }

    const as608_status_t status = launch(ctx, command);

    if (status == AS608_STATUS_OK) {
        ctx->reply_output = output;
    }

    return status;
}

as608_status_t as608_read_parameters_start(as608_t *ctx, as608_parameters_t *out) {
    const command_t command = {{CMD_READ_PARAMS}, 1U, STEP_READ_PARAMS_ACK};

    return reply_start(ctx, &command, out);
}

/* =========================================================================
 * END API 07
 * ========================================================================= */

/* =========================================================================
 * API 08: as608_get_template_count_start
 * =========================================================================
 * PS_ValidTempleteNum (0x1D), AS60x Communication Manual V1.0, pp. 26–27.
 * Successful ACK: confirmation byte followed by a 16-bit template count.
 */

/* Validate an ACK and decode its 16-bit result on success. */
static as608_progress_t accept_u16_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || ((frame.length != 1U) && (frame.length != 3U))) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if ((frame.length != 3U) || !ctx->reply_output) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    *(uint16_t *)ctx->reply_output = load_u16(frame.data + 1U);
    return finish(ctx, AS608_ACK_OK);
}

as608_status_t as608_get_template_count_start(as608_t *ctx, uint16_t *count) {
    const command_t command = {{CMD_COUNT}, 1U, STEP_U16_ACK};

    return reply_start(ctx, &command, count);
}

/* =========================================================================
 * END API 08
 * ========================================================================= */

/* =========================================================================
 * API 09: as608_read_index_table_start
 * =========================================================================
 * PS_ReadIndexTable (0x1F), AS60x Communication Manual V1.0, pp. 27–28.
 * Successful ACK: confirmation byte followed by a 32-byte bitmap.
 * Firmware support is not guaranteed; the manual marks this command reserved.
 */

/* Validate an ACK and copy its 32-byte result only on success. */
static as608_progress_t accept_bytes32_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || ((frame.length != 1U) && (frame.length != 33U))) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if ((frame.length != 33U) || !ctx->reply_output) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    (void)memcpy(ctx->reply_output, frame.data + 1U, 32U);
    return finish(ctx, AS608_ACK_OK);
}

as608_status_t as608_read_index_table_start(as608_t *ctx, uint8_t index_page, uint8_t *bitmap, size_t capacity) {
    if ((index_page > 3U) || (capacity < 32U)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_READ_INDEX, index_page}, 2U, STEP_BYTES32_ACK};

    return reply_start(ctx, &command, bitmap);
}

/* =========================================================================
 * END API 09
 * ========================================================================= */

/* =========================================================================
 * API 10: as608_read_information_page_start
 * =========================================================================
 * PS_ReadINFpage (0x16), AS60x Communication Manual V1.0, p. 23.
 * ACK_OK is followed by a 512-byte data stream.
 */

/* Initialize the destination buffer and offset for a data stream. */
static void stream_setup(as608_t *ctx, as608_template_buffer_t output) {
    ctx->stream_data = output.data;
    ctx->stream_capacity = output.capacity;
    ctx->stream_offset = 0U;
}

/* Check template lengths supported by the documented and tested modules. */
static bool supported_size(size_t length) {
    return (length == AS608_TEMPLATE_512_BYTES) || (length == AS608_TEMPLATE_768_BYTES);
}

/* Validate the initial stream ACK and arm the data receiver on success. */
static as608_progress_t accept_stream_ack(as608_t *ctx, enum as608_step data_step) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || (frame.length != 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    ctx->step = (uint8_t)data_step;
    as608_transaction_receive(&ctx->transaction, ctx->stream_capacity);
    return AS608_PENDING;
}

enum {
    DEVICE_INFO_OFFSET = 28
};

/* Retain only the descriptor window and commit it after the full page. */
static void collect_device_info(as608_t *ctx, const as608_transaction_frame_t *frame, size_t total, bool end) {
    const size_t first = (ctx->stream_offset > DEVICE_INFO_OFFSET) ?
        ctx->stream_offset : DEVICE_INFO_OFFSET;
    const size_t stop = DEVICE_INFO_OFFSET + sizeof(ctx->device_info_data);
    const size_t last = (total < stop) ? total : stop;

    if (first < last) {
        (void)memcpy(ctx->device_info_data + (first - DEVICE_INFO_OFFSET),
                     frame->data + (first - ctx->stream_offset), last - first);
    }

    if (end) {
        as608_device_info_t *info = ctx->reply_output;
        const size_t n = AS608_DEVICE_DESCRIPTOR_BYTES;
        (void)memcpy(info->product_model, ctx->device_info_data, n);
        (void)memcpy(info->software_version, ctx->device_info_data + n, n);
        (void)memcpy(info->manufacturer, ctx->device_info_data + 2U * n, n);
        (void)memcpy(info->sensor_name, ctx->device_info_data + 3U * n, n);
    }
}

/* Validate and deliver a stream fragment; commit length only on valid END_DATA. */
static as608_progress_t accept_stream(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    const bool image = (ctx->step == STEP_IMAGE_DATA);
    const bool device_info = (ctx->step == STEP_DEVICE_INFO_DATA);

    if ((device_info ? !ctx->reply_output : (image ? !ctx->stream_write : !ctx->stream_data)) ||
        (ctx->stream_offset >= ctx->stream_capacity)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const size_t remaining = ctx->stream_capacity - ctx->stream_offset;

    if (((frame.kind != AS608_PACKET_DATA) && (frame.kind != AS608_PACKET_END_DATA)) ||
        (frame.length == 0U) || (frame.length > remaining)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const size_t total = ctx->stream_offset + frame.length;
    const bool end = (frame.kind == AS608_PACKET_END_DATA);

    if (end) {
        if ((ctx->step == STEP_INFO_DATA) || device_info) {
            if (total != AS608_INFORMATION_PAGE_BYTES) {
                return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
            }
        } else if (ctx->step == STEP_FEATURE_DATA) {
            if (total != AS608_FEATURE_256_BYTES) {
                return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
            }
        } else if (image) {
            if (total != ctx->stream_capacity) {
                return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
            }
        } else {
            if (!supported_size(total) || !ctx->stream_length) {
                return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
            }
        }
    } else if (total == ctx->stream_capacity) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (device_info) {
        collect_device_info(ctx, &frame, total, end);
    } else if (image) {
        if (!ctx->stream_write(ctx->stream_user, frame.data, frame.length)) {
            return quarantine(ctx, AS608_STATUS_IO_ERROR);
        }
    } else {
        (void)memcpy(ctx->stream_data + ctx->stream_offset, frame.data, frame.length);
    }

    ctx->stream_offset = total;

    if (end) {
        if (ctx->stream_length) {
            *ctx->stream_length = total;
        }

        return finish(ctx, AS608_ACK_OK);
    }

    as608_transaction_receive(&ctx->transaction, ctx->stream_capacity - total);
    return AS608_PENDING;
}

as608_status_t as608_read_information_page_start(as608_t *ctx, uint8_t *data, size_t capacity) {
    if (!data || (capacity < AS608_INFORMATION_PAGE_BYTES)) {
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

/* =========================================================================
 * END API 10
 * ========================================================================= */

/* =========================================================================
 * API 11: as608_capture_image_start
 * =========================================================================
 * PS_GetImage (0x01), AS60x Communication Manual V1.0, p. 13.
 * Captures an image into the sensor's internal ImageBuffer.
 */

/* Launch an argument-free command expecting a one-byte ACK. */
static as608_status_t simple_start(as608_t *ctx, uint8_t code) {
    const command_t command = {{code}, 1U, STEP_SIMPLE_ACK};

    return launch(ctx, &command);
}

as608_status_t as608_capture_image_start(as608_t *ctx) {
    return simple_start(ctx, CMD_GET_IMAGE);
}

/* =========================================================================
 * END API 11
 * ========================================================================= */

/* =========================================================================
 * API 12: as608_generate_character_start
 * =========================================================================
 * PS_GenChar (0x02), AS60x Communication Manual V1.0, pp. 13–14.
 * Generates fingerprint features from ImageBuffer into a CharBuffer.
 */

/* Check whether the requested character buffer ID is supported. */
static bool valid_buffer(as608_char_buffer_t buffer) {
    return (buffer == AS608_CHAR_BUFFER_1) || (buffer == AS608_CHAR_BUFFER_2);
}

as608_status_t as608_generate_character_start(as608_t *ctx, as608_char_buffer_t buffer) {
    if (!valid_buffer(buffer)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_GEN_CHAR, (uint8_t)buffer}, 2U, STEP_SIMPLE_ACK};

    return launch(ctx, &command);
}

/* =========================================================================
 * END API 12
 * ========================================================================= */

/* =========================================================================
 * API 13: as608_match_buffers_start
 * =========================================================================
 * PS_Match (0x03), AS60x Communication Manual V1.0, p. 14.
 * Compares CharBuffer1 and CharBuffer2 and returns a 16-bit match score.
 */

as608_status_t as608_match_buffers_start(as608_t *ctx, uint16_t *score) {
    const command_t command = {{CMD_MATCH}, 1U, STEP_U16_ACK};

    return reply_start(ctx, &command, score);
}

/* =========================================================================
 * END API 13
 * ========================================================================= */

/* =========================================================================
 * API 14: as608_create_model_start
 * =========================================================================
 * PS_RegModel (0x05), AS60x Communication Manual V1.0, p. 15.
 * Merges both character buffers and stores the model in both buffers.
 */

as608_status_t as608_create_model_start(as608_t *ctx) {
    return simple_start(ctx, CMD_REG_MODEL);
}

/* =========================================================================
 * END API 14
 * ========================================================================= */

/* =========================================================================
 * API 15: as608_search_start
 * =========================================================================
 * PS_Search (0x04), AS60x Communication Manual V1.0, p. 14.
 * Successful ACK: confirmation, 16-bit PageID and 16-bit MatchScore.
 */

/* Check that a nonempty page range fits the 16-bit PageID space. */
static bool valid_range(uint16_t start, uint16_t count) {
    return (count != 0U) && ((uint32_t)start + (uint32_t)count <= UINT32_C(65536));
}

/* Encode a uint16_t into two big-endian wire bytes. */
static void store_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

/* Validate a match ACK and enforce the requested range for Search. */
static as608_progress_t accept_match_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || ((frame.length != 1U) && (frame.length != 5U))) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if ((frame.length != 5U) || !ctx->reply_output) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const as608_match_t match = {load_u16(frame.data + 1U), load_u16(frame.data + 3U)};

    if (ctx->step == STEP_SEARCH_ACK) {
        as608_search_t *search = ctx->reply_output;
        const uint32_t end = (uint32_t)search->start_page + (uint32_t)search->page_count;

        if ((match.page_id < search->start_page) || ((uint32_t)match.page_id >= end)) {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }

        search->match = match;
    } else {
        *(as608_match_t *)ctx->reply_output = match;
    }
    return finish(ctx, AS608_ACK_OK);
}

/* Validate and launch a range search with the selected opcode. */
static as608_status_t search_start(as608_t *ctx, as608_search_t *search, uint8_t code) {
    if (!search || !valid_buffer(search->buffer) || !valid_range(search->start_page, search->page_count)) {
        return AS608_STATUS_INVALID_ARG;
    }

    command_t command = {{code, (uint8_t)search->buffer}, 6U, STEP_SEARCH_ACK};
    store_u16(command.bytes + 2U, search->start_page);
    store_u16(command.bytes + 4U, search->page_count);

    return reply_start(ctx, &command, search);
}

as608_status_t as608_search_start(as608_t *ctx, as608_search_t *search) {
    return search_start(ctx, search, CMD_SEARCH);
}

/* =========================================================================
 * END API 15
 * ========================================================================= */

/* =========================================================================
 * API 16: as608_load_template_start
 * =========================================================================
 * PS_LoadChar (0x07), AS60x Communication Manual V1.0, pp. 15–16.
 * Loads a flash template into the selected sensor character buffer.
 */

/* Build a page-addressed command with a buffer ID and big-endian PageID. */
static command_t page_command(uint8_t code, as608_char_buffer_t buffer, uint16_t page) {
    command_t command = {{code, (uint8_t)buffer}, 4U, STEP_SIMPLE_ACK};
    store_u16(command.bytes + 2U, page);

    return command;
}

as608_status_t as608_load_template_start(as608_t *ctx, as608_char_buffer_t buffer, uint16_t page_id) {
    if (!valid_buffer(buffer)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = page_command(CMD_LOAD_CHAR, buffer, page_id);

    return launch(ctx, &command);
}

/* =========================================================================
 * END API 16
 * ========================================================================= */

/* =========================================================================
 * API 17: as608_upload_template_start
 * =========================================================================
 * PS_UpChar (0x08), AS60x Communication Manual V1.0, pp. 6, 16.
 * Receives a template from a selected character buffer.
 * Supported template sizes: documented 512 and observed 768 bytes.
 */

/* Admit a template upload and retain the caller's buffer and length output. */
static as608_status_t upload_start(as608_t *ctx, const command_t *command, as608_template_buffer_t *output) {
    if (!output || !output->data || (output->capacity < AS608_TEMPLATE_512_BYTES)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const as608_status_t status = launch(ctx, command);

    if (status == AS608_STATUS_OK) {
        stream_setup(ctx, *output);
        ctx->stream_capacity = (output->capacity >= AS608_TEMPLATE_768_BYTES) ?
            AS608_TEMPLATE_768_BYTES : AS608_TEMPLATE_512_BYTES;
        ctx->stream_length = &output->length;
    }

    return status;
}

as608_status_t as608_upload_template_start(as608_t *ctx, as608_char_buffer_t buffer, as608_template_buffer_t *output) {
    if (!valid_buffer(buffer)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_UP_CHAR, (uint8_t)buffer}, 2U, STEP_UP_ACK};

    return upload_start(ctx, &command, output);
}

/* =========================================================================
 * END API 17
 * ========================================================================= */

/* =========================================================================
 * API 18: as608_download_template_start
 * =========================================================================
 * PS_DownChar (0x09), AS60x Communication Manual V1.0, pp. 16–17.
 * ReadSysPara selects the fragment size before DownChar.
 * Completion means physical TX drain after a positive readiness ACK,
 * not verified final sensor acceptance (manual example, p. 31).
 */

typedef struct {
    const uint8_t *data;
    size_t length;
    as608_char_buffer_t buffer;
    bool store;
    uint16_t page_id;
} download_t;

/* Admit a download and retain its source and optional store request. */
static as608_status_t download_start(as608_t *ctx, const download_t *request) {
    if (!request->data || (request->length == 0U) || !valid_buffer(request->buffer)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_READ_PARAMS}, 1U, STEP_PARAMS_ACK};
    const as608_status_t status = launch(ctx, &command);

    if (status == AS608_STATUS_OK) {
        ctx->input = request->data;
        ctx->input_length = request->length;
        ctx->input_offset = 0U;
        ctx->download_buffer = request->buffer;
        ctx->store_after_download = request->store;
        ctx->page_id = request->page_id;
        ctx->chunk_size = 0U;
    }

    return status;
}

/* Convert a scheduling result to pending or a quarantined fault. */
static as608_progress_t next_or_fault(as608_t *ctx, as608_status_t status) {
    return (status == AS608_STATUS_OK) ? AS608_PENDING : quarantine(ctx, status);
}

/* Schedule one fragment, using END_DATA only for the last fragment. */
static as608_status_t send_next_chunk(as608_t *ctx) {
    if (!ctx->input || (ctx->input_offset >= ctx->input_length) ||
        (ctx->chunk_size == 0U) || (ctx->chunk_size > AS608_TRANSACTION_MAX_PAYLOAD_BYTES)) {
        return AS608_STATUS_UNEXPECTED_REPLY;
    }

    const size_t remaining = ctx->input_length - ctx->input_offset;
    const size_t length = (remaining > ctx->chunk_size) ? ctx->chunk_size : remaining;
    const as608_packet_kind_t kind = (length == remaining) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
    const as608_transaction_frame_t frame = {kind, ctx->input + ctx->input_offset, length};
    const as608_status_t status = as608_transaction_send(&ctx->transaction, &frame);

    if (status == AS608_STATUS_OK) {
        ctx->input_offset += length;
    }

    return status;
}

/* Read the packet-size code and schedule DownChar. */
static as608_progress_t accept_download_parameters(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || ((frame.length != 1U) && (frame.length != 17U))) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if (frame.length != 17U) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const uint16_t code = load_u16(frame.data + 13U);

    if (code > 3U) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    ctx->chunk_size = (uint16_t)(32U << code);

    if (ctx->stream_read) {
        if ((code == 0U) || ((ctx->input_length % ctx->chunk_size) != 0U)) {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }

        const command_t command = {{CMD_DOWN_IMAGE}, 1U, STEP_DOWN_ACK};

        return next_or_fault(ctx, command_start(ctx, command.bytes, command.length, command.step));
    }

    const command_t command = {{CMD_DOWN_CHAR, (uint8_t)ctx->download_buffer}, 2U, STEP_DOWN_ACK};

    return next_or_fault(ctx, command_start(ctx, command.bytes, command.length, command.step));
}

/* Start sending fragments only after a valid readiness ACK. */
static as608_progress_t accept_download_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || (frame.length != 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if (ctx->stream_read) {
        ctx->step = STEP_DOWN_SOURCE;
        return AS608_PENDING;
    }

    ctx->step = STEP_DOWN_DATA;
    return next_or_fault(ctx, send_next_chunk(ctx));
}

/* Schedule the next fragment or wait for physical UART transmission. */
static as608_progress_t download_sent(as608_t *ctx) {
    if (ctx->input_offset < ctx->input_length) {
        if (ctx->stream_read) {
            ctx->step = STEP_DOWN_SOURCE;
            return AS608_PENDING;
        }

        return next_or_fault(ctx, send_next_chunk(ctx));
    }

    if (ctx->input_offset != ctx->input_length) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    ctx->step = STEP_DOWN_DRAIN;
    as608_transaction_drain(&ctx->transaction);
    return AS608_PENDING;
}

as608_status_t as608_download_template_start(as608_t *ctx, as608_char_buffer_t buffer, const uint8_t *data, size_t length) {
    if (!supported_size(length)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const download_t request = {data, length, buffer, false, 0U};

    return download_start(ctx, &request);
}

/* =========================================================================
 * END API 18
 * ========================================================================= */

/* =========================================================================
 * API 19: as608_store_template_start
 * =========================================================================
 * PS_StoreChar (0x06), AS60x Communication Manual V1.0, p. 15.
 * Stores a template from the selected character buffer into flash.
 */

as608_status_t as608_store_template_start(as608_t *ctx, as608_char_buffer_t buffer, uint16_t page_id) {
    if (!valid_buffer(buffer)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = page_command(CMD_STORE_CHAR, buffer, page_id);

    return launch(ctx, &command);
}

/* =========================================================================
 * END API 19
 * ========================================================================= */

/* =========================================================================
 * API 20: as608_read_template_start
 * =========================================================================
 * Composite PS_LoadChar (0x07) and PS_UpChar (0x08) job.
 * AS60x Communication Manual V1.0, pp. 15–16.
 */

/* Validate the load ACK and schedule upload without restarting the job. */
static as608_progress_t accept_load_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || (frame.length != 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    const command_t command = {{CMD_UP_CHAR, AS608_CHAR_BUFFER_1}, 2U, STEP_UP_ACK};

    return next_or_fault(ctx, command_start(ctx, command.bytes, command.length, command.step));
}

as608_status_t as608_read_template_start(as608_t *ctx, uint16_t page_id, as608_template_buffer_t *output) {
    command_t command = page_command(CMD_LOAD_CHAR, AS608_CHAR_BUFFER_1, page_id);
    command.step = STEP_LOAD_ACK;

    return upload_start(ctx, &command, output);
}

/* =========================================================================
 * END API 20
 * ========================================================================= */

/* =========================================================================
 * API 21: as608_write_template_start
 * =========================================================================
 * Composite ReadSysPara (0x0F), DownChar (0x09), StoreChar (0x06) job.
 * AS60x Communication Manual V1.0, pp. 15–17, 19–20.
 * Requires the no-final-ACK DownChar firmware profile.
 */

/* Finish a standalone download or schedule flash storage after TX drain. */
static as608_progress_t download_drained(as608_t *ctx) {
    if ((ctx->step != STEP_DOWN_DRAIN) || (ctx->input_offset != ctx->input_length)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (!ctx->store_after_download) {
        /* Preserve the readiness ACK; no final ACK was read. */
        return finish(ctx, AS608_ACK_OK);
    }

    const command_t command = page_command(CMD_STORE_CHAR, ctx->download_buffer, ctx->page_id);

    return next_or_fault(ctx, command_start(ctx, command.bytes, command.length, command.step));
}

as608_status_t as608_write_template_start(as608_t *ctx, uint16_t page_id, const uint8_t *data, size_t length) {
    if (!supported_size(length)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const download_t request = {data, length, AS608_CHAR_BUFFER_1, true, page_id};

    return download_start(ctx, &request);
}

/* =========================================================================
 * END API 21
 * ========================================================================= */

/* =========================================================================
 * API 22: as608_delete_templates_start
 * =========================================================================
 * PS_DeletChar (0x0C), AS60x Communication Manual V1.0, p. 18.
 * Deletes a contiguous range of templates from the flash database.
 */

as608_status_t as608_delete_templates_start(as608_t *ctx, uint16_t start_page, uint16_t page_count) {
    if (!valid_range(start_page, page_count)) {
        return AS608_STATUS_INVALID_ARG;
    }

    command_t command = {{CMD_DELETE_CHAR}, 5U, STEP_SIMPLE_ACK};
    store_u16(command.bytes + 1U, start_page);
    store_u16(command.bytes + 3U, page_count);

    return launch(ctx, &command);
}

/* =========================================================================
 * END API 22
 * ========================================================================= */

/* =========================================================================
 * API 23: as608_empty_database_start
 * =========================================================================
 * PS_Empty (0x0D), AS60x Communication Manual V1.0, pp. 18–19.
 * Deletes all fingerprint templates from the flash database.
 */

as608_status_t as608_empty_database_start(as608_t *ctx) {
    return simple_start(ctx, CMD_EMPTY);
}

/* =========================================================================
 * END API 23
 * ========================================================================= */

/* =========================================================================
 * API 24: as608_enroll_start
 * =========================================================================
 * PS_Enroll (0x10), AS60x Communication Manual V1.0, pp. 20–21.
 * Automatic enrollment; successful ACK returns the selected flash PageID.
 */

as608_status_t as608_enroll_start(as608_t *ctx, uint16_t *page_id) {
    const command_t command = {{CMD_ENROLL}, 1U, STEP_U16_ACK};

    return reply_start(ctx, &command, page_id);
}

/* =========================================================================
 * END API 24
 * ========================================================================= */

/* =========================================================================
 * API 25: as608_identify_start
 * =========================================================================
 * PS_Identify (0x11), AS60x Communication Manual V1.0, p. 21.
 * Automatic acquisition and identification; may update an incomplete template.
 */

as608_status_t as608_identify_start(as608_t *ctx, as608_match_t *match) {
    const command_t command = {{CMD_IDENTIFY}, 1U, STEP_IDENTIFY_ACK};

    return reply_start(ctx, &command, match);
}

/* =========================================================================
 * END API 25
 * ========================================================================= */

/* =========================================================================
 * API 26: as608_high_speed_search_start
 * =========================================================================
 * PS_HighSpeedSearch (0x1B), AS60x MCU User's Guide V1.0, p. 30.
 * Uses the same range and match ACK format as PS_Search.
 */

as608_status_t as608_high_speed_search_start(as608_t *ctx, as608_search_t *search) {
    return search_start(ctx, search, CMD_HIGH_SPEED_SEARCH);
}

/* =========================================================================
 * END API 26
 * ========================================================================= */

/* =========================================================================
 * API 27: as608_upload_feature_start
 * =========================================================================
 * PS_UpChar (0x08), AS60x MCU User's Guide V1.0, sections 2.6 and 4.
 * Receives exactly one 256-byte feature file from the selected CharBuffer.
 */

as608_status_t as608_upload_feature_start(as608_t *ctx, as608_char_buffer_t buffer, as608_feature_file_t *output) {
    if (!output || !valid_buffer(buffer)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_UP_CHAR, (uint8_t)buffer}, 2U, STEP_FEATURE_ACK};
    const as608_status_t status = launch(ctx, &command);

    if (status == AS608_STATUS_OK) {
        const as608_template_buffer_t stream = {output->data, AS608_FEATURE_256_BYTES, 0U};
        stream_setup(ctx, stream);
    }

    return status;
}

/* =========================================================================
 * END API 27
 * ========================================================================= */

/* =========================================================================
 * API 28: as608_download_feature_start
 * =========================================================================
 * PS_DownChar (0x09), AS60x MCU User's Guide V1.0, section 2.6 and p. 19.
 * Sends a 256-byte feature file using the existing no-final-ACK profile.
 */

as608_status_t as608_download_feature_start(as608_t *ctx, as608_char_buffer_t buffer, const as608_feature_file_t *input) {
    if (!input) {
        return AS608_STATUS_INVALID_ARG;
    }

    const download_t request = {input->data, AS608_FEATURE_256_BYTES, buffer, false, 0U};

    return download_start(ctx, &request);
}

/* =========================================================================
 * END API 28
 * ========================================================================= */

/* =========================================================================
 * API 29: as608_upload_image_start
 * =========================================================================
 * PS_UpImage (0x0A), AS60x MCU User's Guide V1.0, pp. 19–20.
 * Delivers verified packed image fragments to a caller-owned stream sink.
 */

as608_status_t as608_upload_image_start(as608_t *ctx, const as608_stream_sink_t *sink) {
    if (!sink || !sink->write || (sink->length == 0U)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const as608_stream_sink_t request = *sink;
    const command_t command = {{CMD_UP_IMAGE}, 1U, STEP_IMAGE_ACK};
    const as608_status_t status = launch(ctx, &command);

    if (status == AS608_STATUS_OK) {
        ctx->stream_data = NULL;
        ctx->stream_capacity = request.length;
        ctx->stream_offset = 0U;
        ctx->stream_write = request.write;
        ctx->stream_user = request.user;
    }

    return status;
}

/* =========================================================================
 * END API 29
 * ========================================================================= */

/* =========================================================================
 * API 30: as608_download_image_start
 * =========================================================================
 * PS_DownImage (0x0B), AS60x MCU User's Guide V1.0, p. 20.
 * Sends packed image bytes in negotiated 64/128/256-byte fragments.
 * Completion means readiness ACK and physical TX drain, not final acceptance.
 */

/* Poll a non-blocking source under the original job deadline. */
static as608_progress_t poll_download_source(as608_t *ctx) {
    const as608_transaction_t *txn = &ctx->transaction;
    const uint32_t elapsed = txn->config.port.clock_ms(txn->config.port.user) - txn->started_ms;

    if (elapsed >= txn->config.timeout_ms) {
        return quarantine(ctx, AS608_STATUS_TIMEOUT);
    }

    if (!ctx->stream_read || (ctx->input_offset >= ctx->input_length) ||
        (ctx->chunk_size < 64U) || (ctx->chunk_size > AS608_TRANSACTION_MAX_PAYLOAD_BYTES) ||
        (ctx->input_length - ctx->input_offset < ctx->chunk_size)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    uint8_t data[AS608_TRANSACTION_MAX_PAYLOAD_BYTES];
    const size_t length = ctx->chunk_size;
    const int32_t count = ctx->stream_read(ctx->stream_user, data, length);

    if (count == 0) {
        return AS608_PENDING;
    }

    if ((count < 0) || ((size_t)count != length)) {
        return quarantine(ctx, AS608_STATUS_IO_ERROR);
    }

    const bool end = (ctx->input_length - ctx->input_offset == length);
    const as608_transaction_frame_t frame = {
        end ? AS608_PACKET_END_DATA : AS608_PACKET_DATA, data, length
    };
    const as608_status_t status = as608_transaction_send(&ctx->transaction, &frame);

    if (status == AS608_STATUS_OK) {
        ctx->input_offset += length;
        ctx->step = STEP_DOWN_DATA;
    }

    return next_or_fault(ctx, status);
}

as608_status_t as608_download_image_start(as608_t *ctx, const as608_stream_source_t *source) {
    if (!source || !source->read || (source->length == 0U) || ((source->length % 64U) != 0U)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const as608_stream_source_t request = *source;
    const command_t command = {{CMD_READ_PARAMS}, 1U, STEP_PARAMS_ACK};
    const as608_status_t status = launch(ctx, &command);

    if (status == AS608_STATUS_OK) {
        ctx->input = NULL;
        ctx->input_length = request.length;
        ctx->input_offset = 0U;
        ctx->chunk_size = 0U;
        ctx->store_after_download = false;
        ctx->stream_read = request.read;
        ctx->stream_user = request.user;
    }

    return status;
}

/* =========================================================================
 * END API 30
 * ========================================================================= */

/* =========================================================================
 * API 31: as608_generate_binary_image_start
 * =========================================================================
 * PS_GenBinImage (0x1C), AS60x MCU User's Guide V1.0, pp. 30–31.
 * Processes the fingerprint image in ImageBuffer; upload separately.
 */

as608_status_t as608_generate_binary_image_start(as608_t *ctx, as608_image_mode_t mode) {
    if ((mode != AS608_IMAGE_BINARY) && (mode != AS608_IMAGE_THINNED) &&
        (mode != AS608_IMAGE_THINNED_WITH_MINUTIAE)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_GEN_BIN_IMAGE, (uint8_t)mode}, 2U, STEP_SIMPLE_ACK};

    return launch(ctx, &command);
}

/* =========================================================================
 * END API 31
 * ========================================================================= */

/* =========================================================================
 * API 32: as608_set_baud_rate_start
 * =========================================================================
 * PS_WriteReg (0x0E), AS60x Communication Manual V1.0, p. 19.
 * Register 4 selects UART baud rate as 9600 times the multiplier.
 * Restrict the multiplier to the AS608 module profile (1 through 12).
 */

/* Launch a system-register write after caller-specific validation. */
static as608_status_t write_reg_start(as608_t *ctx, enum as608_register reg, uint8_t value) {
    const command_t command = {{CMD_WRITE_REG, (uint8_t)reg, value}, 3U, STEP_SIMPLE_ACK};

    return launch(ctx, &command);
}

as608_status_t as608_set_baud_rate_start(as608_t *ctx, uint32_t baud_rate) {
    if ((baud_rate < UINT32_C(9600)) || (baud_rate > UINT32_C(115200)) ||
        ((baud_rate % UINT32_C(9600)) != 0U)) {
        return AS608_STATUS_INVALID_ARG;
    }

    return write_reg_start(ctx, REG_BAUD_RATE, (uint8_t)(baud_rate / UINT32_C(9600)));
}

/* =========================================================================
 * END API 32
 * ========================================================================= */

/* =========================================================================
 * API 33: as608_set_security_level_start
 * =========================================================================
 * PS_WriteReg (0x0E), AS60x Communication Manual V1.0, p. 19.
 * Register 5 selects one of five sensor-side match threshold levels.
 */

as608_status_t as608_set_security_level_start(as608_t *ctx, as608_security_level_t level) {
    if ((level < AS608_SECURITY_LEVEL_1) || (level > AS608_SECURITY_LEVEL_5)) {
        return AS608_STATUS_INVALID_ARG;
    }

    return write_reg_start(ctx, REG_SECURITY_LEVEL, (uint8_t)level);
}

/* =========================================================================
 * END API 33
 * ========================================================================= */

/* =========================================================================
 * API 34: as608_set_packet_size_start
 * =========================================================================
 * PS_WriteReg (0x0E), AS60x Communication Manual V1.0, p. 19.
 * Register 6 selects the 32/64/128/256-byte data payload size.
 */

as608_status_t as608_set_packet_size_start(as608_t *ctx, as608_packet_size_t size) {
    if ((size != AS608_PACKET_SIZE_32) && (size != AS608_PACKET_SIZE_64) &&
        (size != AS608_PACKET_SIZE_128) && (size != AS608_PACKET_SIZE_256)) {
        return AS608_STATUS_INVALID_ARG;
    }

    return write_reg_start(ctx, REG_PACKET_SIZE, (uint8_t)size);
}

/* =========================================================================
 * END API 34
 * ========================================================================= */

/* =========================================================================
 * API 35: as608_control_other_port_start
 * =========================================================================
 * PS_Port_Control (0x17), AS60x MCU User's Guide V1.0, pp. 27–28.
 * Enables or disables the other communication interface.
 */

as608_status_t as608_control_other_port_start(as608_t *ctx, bool enabled) {
    const command_t command = {{CMD_PORT_CONTROL, enabled ? 1U : 0U}, 2U, STEP_SIMPLE_ACK};

    return launch(ctx, &command);
}

/* =========================================================================
 * END API 35
 * ========================================================================= */

/* =========================================================================
 * API 36: as608_read_device_info_start
 * =========================================================================
 * PS_ReadINFpage (0x16), AS60x MCU User's Guide V1.0, section 2.1.
 * Extracts fixed-width descriptors only after the entire page is valid.
 */

as608_status_t as608_read_device_info_start(as608_t *ctx, as608_device_info_t *info) {
    const command_t command = {{CMD_READ_INFO}, 1U, STEP_DEVICE_INFO_ACK};
    const as608_status_t status = reply_start(ctx, &command, info);

    if (status == AS608_STATUS_OK) {
        ctx->stream_data = NULL;
        ctx->stream_capacity = AS608_INFORMATION_PAGE_BYTES;
        ctx->stream_offset = 0U;
    }

    return status;
}

/* =========================================================================
 * END API 36
 * ========================================================================= */

/* =========================================================================
 * API 37: as608_set_password_start
 * =========================================================================
 * PS_SetPwd (0x12), AS60x MCU User's Guide V1.0, p. 25.
 * Changes the sensor handshake password; verify separately.
 */

as608_status_t as608_set_password_start(as608_t *ctx, uint32_t password) {
    return password_start(ctx, CMD_SET_PASSWORD, password);
}

/* =========================================================================
 * END API 37
 * ========================================================================= */

/* =========================================================================
 * API 38: as608_get_random_code_start
 * =========================================================================
 * PS_GetRandomCode (0x14), AS60x MCU User's Guide V1.0, p. 26.
 * Successful ACK contains a raw 32-bit random code.
 */

/* Validate a 32-bit result ACK before publishing its decoded value. */
static as608_progress_t accept_u32_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || ((frame.length != 1U) && (frame.length != 5U))) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if ((frame.length != 5U) || !ctx->reply_output) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    *(uint32_t *)ctx->reply_output = load_u32(frame.data + 1U);
    return finish(ctx, AS608_ACK_OK);
}

as608_status_t as608_get_random_code_start(as608_t *ctx, uint32_t *code) {
    const command_t command = {{CMD_GET_RANDOM_CODE}, 1U, STEP_U32_ACK};

    return reply_start(ctx, &command, code);
}

/* =========================================================================
 * END API 38
 * ========================================================================= */

/* =========================================================================
 * API 39: as608_set_device_address_start
 * =========================================================================
 * PS_SetChipAddr (0x15), AS60x MCU User's Guide V1.0, pp. 26–27.
 * Initial provisioning: default-address command, new-address ACK.
 */

/* Commit the new address only after a verified successful transition ACK. */
static as608_progress_t accept_address_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || (frame.length != 1U) ||
        (frame.data[0] != AS608_ACK_OK)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    ctx->transaction.config.device_address = ctx->transaction.rx_address;
    return finish(ctx, AS608_ACK_OK);
}

as608_status_t as608_set_device_address_start(as608_t *ctx, uint32_t address) {
    const as608_status_t allowed = start_allowed(ctx);

    if (allowed != AS608_STATUS_OK) {
        return allowed;
    }

    if ((ctx->transaction.config.device_address != AS608_DEFAULT_ADDRESS) ||
        (address == AS608_DEFAULT_ADDRESS)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{
        CMD_SET_ADDRESS,
        (uint8_t)(address >> 24U),
        (uint8_t)(address >> 16U),
        (uint8_t)(address >> 8U),
        (uint8_t)address
    }, 5U, STEP_ADDRESS_ACK};
    const as608_status_t status = launch(ctx, &command);

    if (status == AS608_STATUS_OK) {
        ctx->transaction.rx_address = address;
    }

    return status;
}

/* =========================================================================
 * END API 39
 * ========================================================================= */

/* =========================================================================
 * API 40: as608_write_notepad_start
 * =========================================================================
 * PS_WriteNotepad (0x18), AS60x MCU User's Guide V1.0, section 2.4, p. 28.
 * Replaces the entire 32-byte user flash page.
 */

as608_status_t as608_write_notepad_start(as608_t *ctx, const as608_notepad_page_t *page) {
    if (!page || (page->page >= AS608_NOTEPAD_PAGE_COUNT)) {
        return AS608_STATUS_INVALID_ARG;
    }

    uint8_t bytes[2U + AS608_NOTEPAD_PAGE_BYTES] = {CMD_WRITE_NOTEPAD, page->page};
    (void)memcpy(bytes + 2U, page->data, AS608_NOTEPAD_PAGE_BYTES);

    return launch_payload(ctx, bytes, sizeof(bytes), STEP_SIMPLE_ACK);
}

/* =========================================================================
 * END API 40
 * ========================================================================= */

/* =========================================================================
 * API 41: as608_read_notepad_start
 * =========================================================================
 * PS_ReadNotepad (0x19), AS60x MCU User's Guide V1.0, p. 29.
 * Reads one 32-byte user-flash page using the shared fixed-size ACK.
 */

as608_status_t as608_read_notepad_start(as608_t *ctx, as608_notepad_page_t *page) {
    if (!page || (page->page >= AS608_NOTEPAD_PAGE_COUNT)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_READ_NOTEPAD, page->page}, 2U, STEP_BYTES32_ACK};

    return reply_start(ctx, &command, page->data);
}

/* =========================================================================
 * END API 41
 * ========================================================================= */

/* =========================================================================
 * API 02: as608_process
 * =========================================================================
 * Keep the process dispatcher after the active command API sections so
 * their static reply handlers are defined before use.
 * ========================================================================= */

/* Validate a one-byte ACK and preserve its raw confirmation code. */
static as608_progress_t accept_simple_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || (frame.length != 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    return finish(ctx, frame.data[0]);
}

as608_progress_t as608_process(as608_t *ctx) {
    if (!ctx) {
        return AS608_FAILED;
    }

    if (ctx->state == AS608_STATE_DONE) {
        return AS608_COMPLETE;
    }

    if (ctx->state != AS608_STATE_BUSY) {
        return AS608_FAILED;
    }

    if (ctx->step == STEP_DOWN_SOURCE) {
        return poll_download_source(ctx);
    }

    size_t limit;

    switch (ctx->step) {
        case STEP_SIMPLE_ACK:
        case STEP_LOAD_ACK:
        case STEP_UP_ACK:
        case STEP_DOWN_ACK:
        case STEP_FEATURE_ACK:
        case STEP_IMAGE_ACK:
        case STEP_DEVICE_INFO_ACK:
        case STEP_ADDRESS_ACK:
        case STEP_INFO_ACK: {
            limit = 1U;
            break;
        }
        case STEP_READ_PARAMS_ACK:
        case STEP_PARAMS_ACK: {
            limit = 17U;
            break;
        }
        case STEP_U16_ACK: {
            limit = 3U;
            break;
        }
        case STEP_BYTES32_ACK: {
            limit = 33U;
            break;
        }
        case STEP_U32_ACK:
        case STEP_SEARCH_ACK:
        case STEP_IDENTIFY_ACK: {
            limit = 5U;
            break;
        }
        case STEP_UP_DATA:
        case STEP_INFO_DATA:
        case STEP_FEATURE_DATA:
        case STEP_IMAGE_DATA:
        case STEP_DEVICE_INFO_DATA:
        case STEP_DOWN_DATA:
        case STEP_DOWN_DRAIN: {
            limit = 0U; /* Stream handlers schedule their own transport phases. */
            break;
        }
        default: {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }
    }

    const as608_transaction_result_t result = as608_transaction_process(&ctx->transaction);

    switch (result.event) {
        case AS608_TXN_PENDING: {
            return AS608_PENDING;
        }

        case AS608_TXN_SENT: {
            if (ctx->step == STEP_DOWN_DATA) {
                return download_sent(ctx);
            }

            if ((ctx->step == STEP_INFO_DATA) || (ctx->step == STEP_UP_DATA) || (ctx->step == STEP_FEATURE_DATA) ||
                (ctx->step == STEP_IMAGE_DATA) || (ctx->step == STEP_DEVICE_INFO_DATA) ||
                (ctx->step == STEP_DOWN_DRAIN)) {
                return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
            }

            if (ctx->step == STEP_ADDRESS_ACK) {
                as608_transaction_receive_at(&ctx->transaction, limit, ctx->transaction.rx_address);
            } else {
                as608_transaction_receive(&ctx->transaction, limit);
            }
            return AS608_PENDING;
        }

        case AS608_TXN_RECEIVED: {
            switch (ctx->step) {
                case STEP_SIMPLE_ACK: {
                    return accept_simple_ack(ctx);
                }
                case STEP_READ_PARAMS_ACK: {
                    return accept_parameters_ack(ctx);
                }
                case STEP_PARAMS_ACK: {
                    return accept_download_parameters(ctx);
                }
                case STEP_DOWN_ACK: {
                    return accept_download_ack(ctx);
                }
                case STEP_U16_ACK: {
                    return accept_u16_ack(ctx);
                }
                case STEP_U32_ACK: {
                    return accept_u32_ack(ctx);
                }
                case STEP_ADDRESS_ACK: {
                    return accept_address_ack(ctx);
                }
                case STEP_BYTES32_ACK: {
                    return accept_bytes32_ack(ctx);
                }
                case STEP_SEARCH_ACK:
                case STEP_IDENTIFY_ACK: {
                    return accept_match_ack(ctx);
                }
                case STEP_INFO_ACK: {
                    return accept_stream_ack(ctx, STEP_INFO_DATA);
                }
                case STEP_LOAD_ACK: {
                    return accept_load_ack(ctx);
                }
                case STEP_UP_ACK: {
                    return accept_stream_ack(ctx, STEP_UP_DATA);
                }
                case STEP_FEATURE_ACK: {
                    return accept_stream_ack(ctx, STEP_FEATURE_DATA);
                }
                case STEP_IMAGE_ACK: {
                    return accept_stream_ack(ctx, STEP_IMAGE_DATA);
                }
                case STEP_DEVICE_INFO_ACK: {
                    return accept_stream_ack(ctx, STEP_DEVICE_INFO_DATA);
                }
                case STEP_DEVICE_INFO_DATA:
                case STEP_INFO_DATA:
                case STEP_FEATURE_DATA:
                case STEP_IMAGE_DATA:
                case STEP_UP_DATA: {
                    return accept_stream(ctx);
                }
                default: {
                    return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
                }
            }
        }

        case AS608_TXN_DRAINED: {
            return download_drained(ctx);
        }

        case AS608_TXN_FAULT: {
            return quarantine(ctx, result.status);
        }

        default: {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }
    }
}

/* =========================================================================
 * END API 02
 * ========================================================================= */
