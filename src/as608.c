/** @file as608.c @brief AS60x command and job lifecycle management. */
#include "as608.h"

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
    CMD_BURN_CODE = 0x1A,
    CMD_HIGH_SPEED_SEARCH = 0x1B,
    CMD_GEN_BIN_IMAGE = 0x1C,
    CMD_COUNT = 0x1D,
    CMD_USER_GPIO = 0x1E,
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
    STEP_ADDRESS_ACK,
    STEP_GPIO_ACK,
    STEP_BURN_READY_ACK,
    STEP_BURN_SOURCE,
    STEP_BURN_DATA_ACK,
    STEP_ENROLL_CAPTURE1_ACK,
    STEP_ENROLL_GEN1_ACK,
    STEP_ENROLL_REMOVE_ACK,
    STEP_ENROLL_CAPTURE2_ACK,
    STEP_ENROLL_GEN2_ACK,
    STEP_ENROLL_MERGE_ACK,
    STEP_ENROLL_STORE_ACK,
    STEP_ENROLL_WAIT_ARM,
    STEP_ENROLL_WAIT,
    STEP_IDENTIFY_CAPTURE_ACK,
    STEP_IDENTIFY_GEN_ACK,
    STEP_FREE_SLOT_PARAMS_ACK,
    STEP_FREE_SLOT_INDEX_ACK
};

enum as608_layout {
    DEVICE_INFO_OFFSET = 28,
    INDEX_PAGE_SLOTS = 256,
    INDEX_PAGE_COUNT = 4,
    INDEX_MAX_SLOTS = INDEX_PAGE_SLOTS * INDEX_PAGE_COUNT
};

_Static_assert(STEP_FREE_SLOT_INDEX_ACK <= UINT8_MAX, "Job step must fit uint8_t");

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

/* Check ACK kind and allowed lengths before any payload access. */
static bool ack_shape_valid(const as608_transaction_frame_t *frame, size_t full_length) {
    return (frame->kind == AS608_PACKET_ACK) &&
           ((frame->length == 1U) || (frame->length == full_length));
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
        case AS608_STATE_DONE_RECOVERY_REQUIRED:
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
 * API 03: as608_get_result
 * ========================================================================= */

bool as608_get_result(const as608_t *ctx, as608_result_t *result) {
    if (!ctx || !result) {
        return false;
    }

    if ((ctx->state != AS608_STATE_DONE) && (ctx->state != AS608_STATE_DONE_RECOVERY_REQUIRED) &&
        (ctx->state != AS608_STATE_DESYNCHRONIZED)) {
        return false;
    }

    *result = ctx->result;
    return true;
}

/* =========================================================================
 * API 04: as608_abort
 * ========================================================================= */

as608_status_t as608_abort(as608_t *ctx) {
    if (!ctx) {
        return AS608_STATUS_INVALID_ARG;
    }

    if ((ctx->state == AS608_STATE_DESYNCHRONIZED) || (ctx->state == AS608_STATE_DONE_RECOVERY_REQUIRED)) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }

    if (ctx->state != AS608_STATE_BUSY) {
        return AS608_STATUS_INVALID_ARG;
    }

    (void)quarantine(ctx, AS608_STATUS_RECOVERY_REQUIRED);
    return AS608_STATUS_OK;
}

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
        case AS608_STATE_DONE_RECOVERY_REQUIRED:
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
        case AS608_STATE_DONE_RECOVERY_REQUIRED:
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
    ctx->input = NULL;
    ctx->stream_data = NULL;
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

    if (!ack_shape_valid(&frame, 17U)) {
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
 * API 08: as608_get_template_count_start
 * =========================================================================
 * PS_ValidTempleteNum (0x1D), AS60x Communication Manual V1.0, pp. 26–27.
 * Successful ACK: confirmation byte followed by a 16-bit template count.
 */

/* Validate an ACK and decode its 16-bit result on success. */
static as608_progress_t accept_u16_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 3U)) {
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
 * API 09: as608_read_index_table_start
 * =========================================================================
 * PS_ReadIndexTable (0x1F), AS60x Communication Manual V1.0, pp. 27–28.
 * Successful ACK: confirmation byte followed by a 32-byte bitmap.
 * Firmware support is not guaranteed; the manual marks this command reserved.
 */

/* Validate an ACK and copy its 32-byte result only on success. */
static as608_progress_t accept_bytes32_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 33U)) {
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
    if ((index_page >= INDEX_PAGE_COUNT) || (capacity < INDEX_PAGE_SLOTS / 8U)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_READ_INDEX, index_page}, 2U, STEP_BYTES32_ACK};

    return reply_start(ctx, &command, bitmap);
}

/* =========================================================================
 * API 10: as608_read_information_page_start
 * =========================================================================
 * PS_ReadINFpage (0x16), AS60x Communication Manual V1.0, p. 23.
 * ACK_OK is followed by a 512-byte data stream.
 */

/* Initialize the destination buffer and offset for a data stream. */
static void stream_setup(as608_t *ctx, uint8_t *data, size_t capacity) {
    ctx->stream_data = data;
    ctx->stream_capacity = capacity;
    ctx->stream_offset = 0U;
}

/* Check template lengths supported by the documented and tested modules. */
static bool supported_size(size_t length) {
    return (length == AS608_TEMPLATE_512_BYTES) || (length == AS608_TEMPLATE_768_BYTES);
}

/* Validate the initial stream ACK and arm the data receiver on success. */
static as608_progress_t accept_stream_ack(as608_t *ctx, enum as608_step data_step) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    ctx->step = (uint8_t)data_step;
    as608_transaction_receive(&ctx->transaction, ctx->stream_capacity);
    return AS608_PENDING;
}

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
        stream_setup(ctx, data, AS608_INFORMATION_PAGE_BYTES);
    }

    return status;
}

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
 * API 14: as608_create_model_start
 * =========================================================================
 * PS_RegModel (0x05), AS60x Communication Manual V1.0, p. 15.
 * Merges both character buffers and stores the model in both buffers.
 */

as608_status_t as608_create_model_start(as608_t *ctx) {
    return simple_start(ctx, CMD_REG_MODEL);
}

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

    if (!ack_shape_valid(&frame, 5U)) {
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

/* Encode a range-search command shared by standalone and composite jobs. */
static command_t search_command(const as608_search_t *search, uint8_t code) {
    command_t command = {{code, (uint8_t)search->buffer}, 6U, STEP_SEARCH_ACK};
    store_u16(command.bytes + 2U, search->start_page);
    store_u16(command.bytes + 4U, search->page_count);

    return command;
}

/* Validate and launch a range search with the selected opcode. */
static as608_status_t search_start(as608_t *ctx, as608_search_t *search, uint8_t code) {
    if (!search || !valid_buffer(search->buffer) || !valid_range(search->start_page, search->page_count)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = search_command(search, code);

    return reply_start(ctx, &command, search);
}

as608_status_t as608_search_start(as608_t *ctx, as608_search_t *search) {
    return search_start(ctx, search, CMD_SEARCH);
}

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
        const size_t capacity = (output->capacity >= AS608_TEMPLATE_768_BYTES) ?
            AS608_TEMPLATE_768_BYTES : AS608_TEMPLATE_512_BYTES;
        stream_setup(ctx, output->data, capacity);
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
        ((ctx->chunk_size != 32U) && (ctx->chunk_size != 64U) &&
         (ctx->chunk_size != 128U) && (ctx->chunk_size != 256U))) {
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

    if (!ack_shape_valid(&frame, 17U)) {
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

    if (!ack_shape_valid(&frame, 1U)) {
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
 * API 20: as608_read_template_start
 * =========================================================================
 * Composite PS_LoadChar (0x07) and PS_UpChar (0x08) job.
 * AS60x Communication Manual V1.0, pp. 15–16.
 */

/* Validate the load ACK and schedule upload without restarting the job. */
static as608_progress_t accept_load_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 1U)) {
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
 * API 23: as608_empty_database_start
 * =========================================================================
 * PS_Empty (0x0D), AS60x Communication Manual V1.0, pp. 18–19.
 * Deletes all fingerprint templates from the flash database.
 */

as608_status_t as608_empty_database_start(as608_t *ctx) {
    return simple_start(ctx, CMD_EMPTY);
}

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
 * API 26: as608_high_speed_search_start
 * =========================================================================
 * PS_HighSpeedSearch (0x1B), AS60x MCU User's Guide V1.0, p. 30.
 * Uses the same range and match ACK format as PS_Search.
 */

as608_status_t as608_high_speed_search_start(as608_t *ctx, as608_search_t *search) {
    return search_start(ctx, search, CMD_HIGH_SPEED_SEARCH);
}

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
        stream_setup(ctx, output->data, AS608_FEATURE_256_BYTES);
    }

    return status;
}

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
        stream_setup(ctx, NULL, request.length);
        ctx->stream_write = request.write;
        ctx->stream_user = request.user;
    }

    return status;
}

/* =========================================================================
 * API 30: as608_download_image_start
 * =========================================================================
 * PS_DownImage (0x0B), AS60x MCU User's Guide V1.0, p. 20.
 * Sends packed image bytes in negotiated 64/128/256-byte fragments.
 * Completion means readiness ACK and physical TX drain, not final acceptance.
 */

/* Poll a download or burn source under the original job deadline. */
static as608_progress_t poll_download_source(as608_t *ctx) {
    const as608_transaction_t *txn = &ctx->transaction;
    const uint32_t elapsed = txn->config.port.clock_ms(txn->config.port.user) - txn->started_ms;

    if (elapsed >= txn->config.timeout_ms) {
        return quarantine(ctx, AS608_STATUS_TIMEOUT);
    }

    if (!ctx->stream_read || (ctx->input_offset >= ctx->input_length) ||
        ((ctx->chunk_size != 64U) && (ctx->chunk_size != 128U) &&
         (ctx->chunk_size != 256U)) ||
        (ctx->input_length - ctx->input_offset < ctx->chunk_size)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    uint8_t data[AS608_TRANSACTION_MAX_PAYLOAD_BYTES];
    const size_t length = ctx->chunk_size;
    const int32_t count = ctx->stream_read(ctx->stream_user, data, length);

    if (count == 0) {
        return AS608_PENDING;
    }

    /* Length is at most 256; compare without narrowing the callback result. */
    if (count != (int32_t)length) {
        return quarantine(ctx, AS608_STATUS_IO_ERROR);
    }

    const bool end = (ctx->input_length - ctx->input_offset == length);
    const as608_transaction_frame_t frame = {
        end ? AS608_PACKET_END_DATA : AS608_PACKET_DATA, data, length
    };
    const as608_status_t status = as608_transaction_send(&ctx->transaction, &frame);

    if (status == AS608_STATUS_OK) {
        ctx->input_offset += length;
        ctx->step = (ctx->step == STEP_BURN_SOURCE) ? STEP_BURN_DATA_ACK : STEP_DOWN_DATA;
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
 * API 36: as608_read_device_info_start
 * =========================================================================
 * PS_ReadINFpage (0x16), AS60x MCU User's Guide V1.0, section 2.1.
 * Extracts fixed-width descriptors only after the entire page is valid.
 */

as608_status_t as608_read_device_info_start(as608_t *ctx, as608_device_info_t *info) {
    const command_t command = {{CMD_READ_INFO}, 1U, STEP_DEVICE_INFO_ACK};
    const as608_status_t status = reply_start(ctx, &command, info);

    if (status == AS608_STATUS_OK) {
        stream_setup(ctx, NULL, AS608_INFORMATION_PAGE_BYTES);
    }

    return status;
}

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
 * API 38: as608_get_random_code_start
 * =========================================================================
 * PS_GetRandomCode (0x14), AS60x MCU User's Guide V1.0, p. 26.
 * Successful ACK contains a raw 32-bit random code.
 */

/* Validate a 32-bit result ACK before publishing its decoded value. */
static as608_progress_t accept_u32_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 5U)) {
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
 * API 39: as608_set_device_address_start
 * =========================================================================
 * PS_SetChipAddr (0x15), AS60x MCU User's Guide V1.0, pp. 26–27.
 * Initial provisioning: default-address command, new-address ACK.
 */

/* Commit the new address only after a verified successful transition ACK. */
static as608_progress_t accept_address_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 1U) || (frame.data[0] != AS608_ACK_OK)) {
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
 * API 42: as608_write_user_gpio_start
 * =========================================================================
 * PS_UserGPIOCommand (0x1E), AS60x SOC Communication Manual V1.0
 * (Chinese edition), command 30 (reserved; variant-specific).
 */

/* Validate the echoed GPIO level before publishing it. */
static as608_progress_t accept_gpio_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 2U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if ((frame.length != 2U) || (frame.data[1] > 1U) || !ctx->reply_output) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    *(bool *)ctx->reply_output = (frame.data[1] != 0U);
    return finish(ctx, AS608_ACK_OK);
}

as608_status_t as608_write_user_gpio_start(as608_t *ctx, as608_gpio_request_t *request) {
    if (!request || ((request->pin != AS608_USER_GPIO_0) && (request->pin != AS608_USER_GPIO_1))) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_USER_GPIO, (uint8_t)request->pin, request->level ? 1U : 0U},
                               3U, STEP_GPIO_ACK};

    return reply_start(ctx, &command, &request->actual_level);
}

/* =========================================================================
 * API 43: as608_burn_code_start
 * =========================================================================
 * PS_BurnCode (0x1A), AS60x MCU User's Guide V1.0, pp. 29–30.
 * Explicit opt-in experimental ACK-each profile; verify on target first.
 */

/* Accept a terminal burn ACK but require a fresh physical link before reuse. */
static as608_progress_t finish_burn(as608_t *ctx, uint8_t ack) {
    const as608_progress_t progress = finish(ctx, ack);

    ctx->state = AS608_STATE_DONE_RECOVERY_REQUIRED;
    return progress;
}

/* Gate each fragment on readiness or its predecessor's ACK. */
static as608_progress_t accept_burn_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const uint8_t ack = frame.data[0];
    const bool ready = (ctx->step == STEP_BURN_READY_ACK);
    const bool end = (!ready && (ctx->input_offset == ctx->input_length));
    const uint8_t expected = (ready || end) ? AS608_ACK_OK : AS608_ACK_DATA_ACCEPTED;

    if (ack != expected) {
        if ((ack == AS608_ACK_OK) || (ack == AS608_ACK_DATA_ACCEPTED) ||
            (ack == AS608_ACK_COMMAND_ACCEPTED)) {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }

        return finish_burn(ctx, ack);
    }

    if (end) {
        return finish_burn(ctx, ack);
    }

    ctx->step = STEP_BURN_SOURCE;
    return AS608_PENDING;
}

as608_status_t as608_burn_code_start(as608_t *ctx, const as608_firmware_request_t *request) {
    if (!request || !request->authorized ||
        (request->profile != AS608_BURN_PROFILE_ACK_EACH) ||
        ((request->mode != AS608_BURN_INFORMATION_PAGE) && (request->mode != AS608_BURN_FULL_FIRMWARE)) ||
        ((request->packet_size != AS608_PACKET_SIZE_64) && (request->packet_size != AS608_PACKET_SIZE_128) &&
         (request->packet_size != AS608_PACKET_SIZE_256)) ||
        !request->source.read || (request->source.length == 0U)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const as608_firmware_request_t saved = *request;
    const uint16_t chunk = (uint16_t)(32U << (uint8_t)saved.packet_size);

    if ((saved.source.length % chunk) != 0U) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_BURN_CODE, (uint8_t)saved.mode}, 2U, STEP_BURN_READY_ACK};
    const as608_status_t status = launch(ctx, &command);

    if (status == AS608_STATUS_OK) {
        ctx->input = NULL;
        ctx->input_length = saved.source.length;
        ctx->input_offset = 0U;
        ctx->chunk_size = chunk;
        ctx->store_after_download = false;
        ctx->stream_read = saved.source.read;
        ctx->stream_user = saved.source.user;
    }

    return status;
}

/* =========================================================================
 * API 44: as608_enroll_two_scan_start
 * =========================================================================
 * Host-managed GetImage, GenChar(1), removal, GetImage, GenChar(2),
 * RegModel, StoreChar(2). AS60x MCU User's Guide V1.0, pp. 33–34.
 */

/* Schedule a paced GetImage poll without reading the clock twice per call. */
static as608_progress_t enroll_wait(as608_t *ctx) {
    ctx->poll_step = ctx->step;
    ctx->step = STEP_ENROLL_WAIT_ARM;
    return AS608_PENDING;
}

/* Arm/check the poll interval under the original wrap-safe job deadline. */
static as608_progress_t poll_enroll_wait(as608_t *ctx) {
    const as608_transaction_t *txn = &ctx->transaction;
    const uint32_t now = txn->config.port.clock_ms(txn->config.port.user);

    if ((uint32_t)(now - txn->started_ms) >= txn->config.timeout_ms) {
        return quarantine(ctx, AS608_STATUS_TIMEOUT);
    }

    if (ctx->step == STEP_ENROLL_WAIT_ARM) {
        ctx->poll_started_ms = now;
        ctx->step = STEP_ENROLL_WAIT;
        return AS608_PENDING;
    }

    if ((uint32_t)(now - ctx->poll_started_ms) < ctx->poll_interval_ms) {
        return AS608_PENDING;
    }

    if ((ctx->poll_step != STEP_ENROLL_CAPTURE1_ACK) &&
        (ctx->poll_step != STEP_ENROLL_REMOVE_ACK) &&
        (ctx->poll_step != STEP_ENROLL_CAPTURE2_ACK)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const uint8_t bytes[] = {CMD_GET_IMAGE};
    return next_or_fault(ctx, command_start(ctx, bytes, sizeof(bytes), ctx->poll_step));
}

/* Advance the enrollment sequence only on the phase-appropriate ACK. */
static as608_progress_t accept_enroll_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const uint8_t ack = frame.data[0];

    if (ctx->step == STEP_ENROLL_REMOVE_ACK) {
        if (ack == AS608_ACK_OK) {
            return enroll_wait(ctx);
        }
        if (ack == AS608_ACK_NO_FINGER) {
            const uint8_t bytes[] = {CMD_GET_IMAGE};
            return next_or_fault(ctx, command_start(ctx, bytes, sizeof(bytes), STEP_ENROLL_CAPTURE2_ACK));
        }
        return finish(ctx, ack);
    }

    if (((ctx->step == STEP_ENROLL_CAPTURE1_ACK) || (ctx->step == STEP_ENROLL_CAPTURE2_ACK)) &&
        (ack == AS608_ACK_NO_FINGER)) {
        return enroll_wait(ctx);
    }

    if (ack != AS608_ACK_OK) {
        return finish(ctx, ack);
    }

    switch (ctx->step) {
        case STEP_ENROLL_CAPTURE1_ACK:
        case STEP_ENROLL_CAPTURE2_ACK: {
            const bool first = (ctx->step == STEP_ENROLL_CAPTURE1_ACK);
            const uint8_t bytes[] = {CMD_GEN_CHAR, first ? AS608_CHAR_BUFFER_1 : AS608_CHAR_BUFFER_2};
            return next_or_fault(ctx, command_start(ctx, bytes, sizeof(bytes),
                                                  first ? STEP_ENROLL_GEN1_ACK : STEP_ENROLL_GEN2_ACK));
        }
        case STEP_ENROLL_GEN1_ACK: {
            const uint8_t bytes[] = {CMD_GET_IMAGE};
            return next_or_fault(ctx, command_start(ctx, bytes, sizeof(bytes), STEP_ENROLL_REMOVE_ACK));
        }
        case STEP_ENROLL_GEN2_ACK: {
            const uint8_t bytes[] = {CMD_REG_MODEL};
            return next_or_fault(ctx, command_start(ctx, bytes, sizeof(bytes), STEP_ENROLL_MERGE_ACK));
        }
        case STEP_ENROLL_MERGE_ACK: {
            const command_t command = page_command(CMD_STORE_CHAR, AS608_CHAR_BUFFER_2, ctx->page_id);
            return next_or_fault(ctx, command_start(ctx, command.bytes, command.length, STEP_ENROLL_STORE_ACK));
        }
        case STEP_ENROLL_STORE_ACK: {
            return finish(ctx, ack);
        }
        default: {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }
    }
}

as608_status_t as608_enroll_two_scan_start(as608_t *ctx, const as608_enroll_request_t *request) {
    if (!request || (request->poll_interval_ms == 0U) ||
        (request->poll_interval_ms > AS608_MAX_TIMEOUT_MS)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const as608_enroll_request_t saved = *request;
    const command_t command = {{CMD_GET_IMAGE}, 1U, STEP_ENROLL_CAPTURE1_ACK};
    const as608_status_t status = launch(ctx, &command);

    if (status == AS608_STATUS_OK) {
        ctx->page_id = saved.page_id;
        ctx->poll_interval_ms = saved.poll_interval_ms;
    }

    return status;
}

/* =========================================================================
 * API 45: as608_identify_range_start
 * =========================================================================
 * Host-managed GetImage, GenChar and Search under one deadline.
 * AS60x MCU User's Guide V1.0, p. 38, workflow (e).
 */

/* Chain capture and feature extraction to the existing range-search ACK. */
static as608_progress_t accept_identify_range_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    const as608_search_t *search = ctx->reply_output;

    if (!search || !valid_buffer(search->buffer) || !valid_range(search->start_page, search->page_count)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (ctx->step == STEP_IDENTIFY_CAPTURE_ACK) {
        const uint8_t bytes[] = {CMD_GEN_CHAR, (uint8_t)search->buffer};

        return next_or_fault(ctx, command_start(ctx, bytes, sizeof(bytes), STEP_IDENTIFY_GEN_ACK));
    }

    if (ctx->step == STEP_IDENTIFY_GEN_ACK) {
        const command_t command = search_command(search, CMD_SEARCH);

        return next_or_fault(ctx, command_start(ctx, command.bytes, command.length, command.step));
    }

    return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
}

as608_status_t as608_identify_range_start(as608_t *ctx, as608_search_t *search) {
    if (!search || !valid_buffer(search->buffer) || !valid_range(search->start_page, search->page_count)) {
        return AS608_STATUS_INVALID_ARG;
    }

    const command_t command = {{CMD_GET_IMAGE}, 1U, STEP_IDENTIFY_CAPTURE_ACK};

    return reply_start(ctx, &command, search);
}

/* =========================================================================
 * API 46: as608_find_free_slot_start
 * =========================================================================
 * ReadSysPara + ReadIndexTable, AS60x SOC Communication Manual V1.0
 * (Chinese edition), commands 15 and 31. LSB mapping follows the
 * related R503 manual; verify on the target before using the result.
 */

/* Read capacity, then inspect one validated bitmap at a time in wire order. */
static as608_progress_t accept_free_slot_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);
    const bool parameters = (ctx->step == STEP_FREE_SLOT_PARAMS_ACK);
    const size_t expected = parameters ? 17U : 33U;

    if (!ack_shape_valid(&frame, expected)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    if ((frame.length != expected) || !ctx->reply_output) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (parameters) {
        const uint16_t capacity = load_u16(frame.data + 5U);

        if ((capacity == 0U) || (capacity > INDEX_MAX_SLOTS)) {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }

        ctx->free_slot_capacity = capacity;
        ctx->page_id = 0U;
    } else {
        if ((ctx->free_slot_capacity == 0U) || (ctx->free_slot_capacity > INDEX_MAX_SLOTS) ||
            (ctx->page_id >= ctx->free_slot_capacity) || ((ctx->page_id % INDEX_PAGE_SLOTS) != 0U)) {
            return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
        }

        const uint16_t remaining = (uint16_t)(ctx->free_slot_capacity - ctx->page_id);
        const uint16_t count = (remaining < INDEX_PAGE_SLOTS) ? remaining : INDEX_PAGE_SLOTS;
        as608_free_slot_t *slot = ctx->reply_output;

        for (uint16_t offset = 0U; offset < count; offset += 8U) {
            const uint8_t occupied = frame.data[1U + offset / 8U];

            if (occupied == UINT8_MAX) {
                continue;
            }

            for (uint16_t bit = 0U; (bit < 8U) && (offset + bit < count); ++bit) {
                if ((occupied & (uint8_t)(1U << bit)) == 0U) {
                    *slot = (as608_free_slot_t){true, (uint16_t)(ctx->page_id + offset + bit)};
                    return finish(ctx, AS608_ACK_OK);
                }
            }
        }

        if (remaining <= INDEX_PAGE_SLOTS) {
            *slot = (as608_free_slot_t){false, 0U};
            return finish(ctx, AS608_ACK_OK);
        }

        ctx->page_id = (uint16_t)(ctx->page_id + INDEX_PAGE_SLOTS);
    }

    const uint8_t bytes[] = {CMD_READ_INDEX, (uint8_t)(ctx->page_id / INDEX_PAGE_SLOTS)};
    return next_or_fault(ctx, command_start(ctx, bytes, sizeof(bytes), STEP_FREE_SLOT_INDEX_ACK));
}

as608_status_t as608_find_free_slot_start(as608_t *ctx, as608_free_slot_t *slot) {
    const command_t command = {{CMD_READ_PARAMS}, 1U, STEP_FREE_SLOT_PARAMS_ACK};

    return reply_start(ctx, &command, slot);
}

/* =========================================================================
 * API 02: as608_process
 * =========================================================================
 * Keep the process dispatcher after the active command API sections so
 * their static reply handlers are defined before use.
 * ========================================================================= */

/* Validate a one-byte ACK and preserve its raw confirmation code. */
static as608_progress_t accept_simple_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ack_shape_valid(&frame, 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    return finish(ctx, frame.data[0]);
}

as608_progress_t as608_process(as608_t *ctx) {
    if (!ctx) {
        return AS608_FAILED;
    }

    if ((ctx->state == AS608_STATE_DONE) || (ctx->state == AS608_STATE_DONE_RECOVERY_REQUIRED)) {
        return AS608_COMPLETE;
    }

    if (ctx->state != AS608_STATE_BUSY) {
        return AS608_FAILED;
    }

    if ((ctx->step == STEP_DOWN_SOURCE) || (ctx->step == STEP_BURN_SOURCE)) {
        return poll_download_source(ctx);
    }

    if ((ctx->step == STEP_ENROLL_WAIT_ARM) || (ctx->step == STEP_ENROLL_WAIT)) {
        return poll_enroll_wait(ctx);
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
        case STEP_BURN_READY_ACK:
        case STEP_BURN_DATA_ACK:
        case STEP_ENROLL_CAPTURE1_ACK:
        case STEP_ENROLL_GEN1_ACK:
        case STEP_ENROLL_REMOVE_ACK:
        case STEP_ENROLL_CAPTURE2_ACK:
        case STEP_ENROLL_GEN2_ACK:
        case STEP_ENROLL_MERGE_ACK:
        case STEP_ENROLL_STORE_ACK:
        case STEP_IDENTIFY_CAPTURE_ACK:
        case STEP_IDENTIFY_GEN_ACK:
        case STEP_INFO_ACK: {
            limit = 1U;
            break;
        }
        case STEP_FREE_SLOT_PARAMS_ACK:
        case STEP_READ_PARAMS_ACK:
        case STEP_PARAMS_ACK: {
            limit = 17U;
            break;
        }
        case STEP_GPIO_ACK: {
            limit = 2U;
            break;
        }
        case STEP_U16_ACK: {
            limit = 3U;
            break;
        }
        case STEP_FREE_SLOT_INDEX_ACK:
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

            if (limit == 0U) {
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
                case STEP_BURN_READY_ACK:
                case STEP_BURN_DATA_ACK: {
                    return accept_burn_ack(ctx);
                }
                case STEP_ENROLL_CAPTURE1_ACK:
                case STEP_ENROLL_GEN1_ACK:
                case STEP_ENROLL_REMOVE_ACK:
                case STEP_ENROLL_CAPTURE2_ACK:
                case STEP_ENROLL_GEN2_ACK:
                case STEP_ENROLL_MERGE_ACK:
                case STEP_ENROLL_STORE_ACK: {
                    return accept_enroll_ack(ctx);
                }
                case STEP_IDENTIFY_CAPTURE_ACK:
                case STEP_IDENTIFY_GEN_ACK: {
                    return accept_identify_range_ack(ctx);
                }
                case STEP_FREE_SLOT_PARAMS_ACK:
                case STEP_FREE_SLOT_INDEX_ACK: {
                    return accept_free_slot_ack(ctx);
                }
                case STEP_GPIO_ACK: {
                    return accept_gpio_ack(ctx);
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

