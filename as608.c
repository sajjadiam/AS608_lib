/** @file as608.c @brief AS60x command and job lifecycle management. */
#include "as608.h"
#include "as608_transaction.h"

#include <string.h>

/* =========================================================================
 * Internal command opcodes and job phases
 * ========================================================================= */

enum as608_command {
    CMD_GET_IMAGE = 0x01,
    CMD_READ_PARAMS = 0x0F,
    CMD_VERIFY_PASSWORD = 0x13,
    CMD_READ_INFO = 0x16,
    CMD_COUNT = 0x1D,
    CMD_READ_INDEX = 0x1F
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
    STEP_INDEX_ACK
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
static as608_status_t command_start(as608_t *ctx, const command_t *command) {
    const as608_transaction_frame_t frame = {
        AS608_PACKET_COMMAND, command->bytes, command->length
    };
    const as608_status_t status = as608_transaction_send(&ctx->transaction, &frame);

    if (status == AS608_STATUS_OK) {
        ctx->step = command->step;
    }

    return status;
}

/* Admit a command, start the job deadline and enter BUSY. */
static as608_status_t launch(as608_t *ctx, const command_t *command) {
    const as608_status_t allowed = start_allowed(ctx);

    if (allowed != AS608_STATUS_OK) {
        return allowed;
    }

    const as608_status_t status = command_start(ctx, command);

    if (status != AS608_STATUS_OK) {
        return status;
    }

    ctx->result = (as608_result_t){AS608_STATUS_NOT_INITIALIZED, 0U};
    ctx->reply_output = NULL;
    ctx->stream_length = NULL;
    as608_transaction_begin(&ctx->transaction);
    ctx->state = AS608_STATE_BUSY;

    return AS608_STATUS_OK;
}

as608_status_t as608_verify_password_start(as608_t *ctx, uint32_t password) {
    const command_t command = {{
        CMD_VERIFY_PASSWORD,
        (uint8_t)(password >> 24U),
        (uint8_t)(password >> 16U),
        (uint8_t)(password >> 8U),
        (uint8_t)password
    }, 5U, STEP_SIMPLE_ACK};

    return launch(ctx, &command);
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

/* Validate the index ACK and copy its 32-byte bitmap on success. */
static as608_progress_t accept_index_ack(as608_t *ctx) {
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

    const command_t command = {{CMD_READ_INDEX, index_page}, 2U, STEP_INDEX_ACK};

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

/* Validate the information-page ACK and arm the data receiver on success. */
static as608_progress_t accept_info_ack(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if ((frame.kind != AS608_PACKET_ACK) || (frame.length != 1U)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if (frame.data[0] != AS608_ACK_OK) {
        return finish(ctx, frame.data[0]);
    }

    ctx->step = STEP_INFO_DATA;
    as608_transaction_receive(&ctx->transaction, AS608_INFORMATION_PAGE_BYTES);
    return AS608_PENDING;
}

/* Validate and copy a verified fragment; complete only at exactly 512 bytes. */
static as608_progress_t accept_info_data(as608_t *ctx) {
    const as608_transaction_frame_t frame = as608_transaction_received(&ctx->transaction);

    if (!ctx->stream_data || (ctx->stream_offset >= AS608_INFORMATION_PAGE_BYTES) ||
        (ctx->stream_capacity != AS608_INFORMATION_PAGE_BYTES)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    const size_t remaining = ctx->stream_capacity - ctx->stream_offset;

    if ((frame.kind != AS608_PACKET_DATA && frame.kind != AS608_PACKET_END_DATA) ||
        (frame.length == 0U) || (frame.length > remaining)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    if ((frame.kind == AS608_PACKET_END_DATA) != (frame.length == remaining)) {
        return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
    }

    (void)memcpy(ctx->stream_data + ctx->stream_offset, frame.data, frame.length);
    ctx->stream_offset += frame.length;

    if (frame.kind == AS608_PACKET_END_DATA) {
        return finish(ctx, AS608_ACK_OK);
    }

    as608_transaction_receive(&ctx->transaction, ctx->stream_capacity - ctx->stream_offset);
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

    size_t limit;

    switch (ctx->step) {
        case STEP_SIMPLE_ACK:
        case STEP_INFO_ACK: {
            limit = 1U;
            break;
        }
        case STEP_READ_PARAMS_ACK: {
            limit = 17U;
            break;
        }
        case STEP_U16_ACK: {
            limit = 3U;
            break;
        }
        case STEP_INDEX_ACK: {
            limit = 33U;
            break;
        }
        case STEP_INFO_DATA: {
            limit = 0U; /* Receive is armed by the preceding ACK or DATA handler. */
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
            if (ctx->step == STEP_INFO_DATA) {
                return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
            }

            as608_transaction_receive(&ctx->transaction, limit);
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
                case STEP_U16_ACK: {
                    return accept_u16_ack(ctx);
                }
                case STEP_INDEX_ACK: {
                    return accept_index_ack(ctx);
                }
                case STEP_INFO_ACK: {
                    return accept_info_ack(ctx);
                }
                case STEP_INFO_DATA: {
                    return accept_info_data(ctx);
                }
                default: {
                    return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
                }
            }
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

/* =========================================================================
 * INACTIVE REFERENCE — incremental API rebuild
 *
 * Previous implementation from commit 53425e4.
 * Move/review one API and its required helpers at a time into active sections
 * above; remove migrated definitions from this reference. Shared helpers
 * are defined once, above their first active user. Insert new API sections
 * before API 02 (process dispatcher). Never compile this block.
 * ========================================================================= */

// Remaining command opcodes; migrate into enum as608_command when needed.
//     CMD_GEN_CHAR = 0x02, CMD_MATCH = 0x03,
//     CMD_SEARCH = 0x04, CMD_REG_MODEL = 0x05, CMD_STORE_CHAR = 0x06,
//     CMD_LOAD_CHAR = 0x07, CMD_UP_CHAR = 0x08, CMD_DOWN_CHAR = 0x09,
//     CMD_DELETE_CHAR = 0x0C, CMD_EMPTY = 0x0D,
// 
// static bool valid_range(uint16_t start, uint16_t count)
// {
//     return count != 0U && (uint32_t)start + (uint32_t)count <= UINT32_C(65536);
// }
// 
// static void store_u16(uint8_t *bytes, uint16_t value)
// {
//     bytes[0] = (uint8_t)(value >> 8U);
//     bytes[1] = (uint8_t)value;
// }
// 
// static bool supported_size(size_t length)
// {
//     return (length == AS608_TEMPLATE_512_BYTES) ||
//            (length == AS608_TEMPLATE_768_BYTES);
// }
// 
// static bool valid_buffer(as608_char_buffer_t buffer)
// {
//     return (buffer == AS608_CHAR_BUFFER_1) ||
//            (buffer == AS608_CHAR_BUFFER_2);
// }
// 
// as608_status_t as608_generate_character_start(as608_t *ctx,
//                                              as608_char_buffer_t buffer)
// {
//     if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
//     const command_t command = {{CMD_GEN_CHAR, (uint8_t)buffer}, 2U, STEP_SIMPLE_ACK};
//     return launch(ctx, &command);
// }
// 
// as608_status_t as608_match_buffers_start(as608_t *ctx, uint16_t *score)
// {
//     const command_t command = {{CMD_MATCH}, 1U, STEP_U16_ACK};
//     return reply_start(ctx, &command, score);
// }
// 
// as608_status_t as608_create_model_start(as608_t *ctx)
// {
//     return simple_start(ctx, CMD_REG_MODEL);
// }
// 
// as608_status_t as608_search_start(as608_t *ctx, as608_search_t *search)
// {
//     if (search == NULL || !valid_buffer(search->buffer) ||
//         !valid_range(search->start_page, search->page_count)) {
//         return AS608_STATUS_INVALID_ARG;
//     }
//     command_t command = {{CMD_SEARCH, (uint8_t)search->buffer}, 6U, STEP_SEARCH_ACK};
//     store_u16(command.bytes + 2U, search->start_page);
//     store_u16(command.bytes + 4U, search->page_count);
//     return reply_start(ctx, &command, search);
// }
// 
// static command_t page_command(uint8_t code, as608_char_buffer_t buffer, uint16_t page)
// {
//     return (command_t){{code, (uint8_t)buffer,
//         (uint8_t)(page >> 8U), (uint8_t)page}, 4U, STEP_SIMPLE_ACK};
// }
// 
// as608_status_t as608_load_template_start(as608_t *ctx, as608_char_buffer_t buffer,
//                                        uint16_t page_id)
// {
//     if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
//     const command_t command = page_command(CMD_LOAD_CHAR, buffer, page_id);
//     return launch(ctx, &command);
// }
// 
// as608_status_t as608_store_template_start(as608_t *ctx, as608_char_buffer_t buffer,
//                                         uint16_t page_id)
// {
//     if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
//     const command_t command = page_command(CMD_STORE_CHAR, buffer, page_id);
//     return launch(ctx, &command);
// }
// 
// static as608_status_t upload_start(as608_t *ctx, const command_t *command,
//                                    as608_template_buffer_t *output)
// {
//     if (output == NULL || output->data == NULL ||
//         output->capacity < AS608_TEMPLATE_512_BYTES) return AS608_STATUS_INVALID_ARG;
//     const as608_status_t status = launch(ctx, command);
//     if (status == AS608_STATUS_OK) {
//         stream_setup(ctx, *output);
//         if (ctx->stream_capacity > AS608_TEMPLATE_MAX_BYTES) {
//             ctx->stream_capacity = AS608_TEMPLATE_MAX_BYTES;
//         }
//         ctx->stream_length = &output->length;
//     }
//     return status;
// }
// 
// as608_status_t as608_upload_template_start(as608_t *ctx, as608_char_buffer_t buffer,
//                                          as608_template_buffer_t *output)
// {
//     if (!valid_buffer(buffer)) return AS608_STATUS_INVALID_ARG;
//     const command_t command = {{CMD_UP_CHAR, (uint8_t)buffer}, 2U, STEP_UP_ACK};
//     return upload_start(ctx, &command, output);
// }
// 
// as608_status_t as608_read_template_start(as608_t *ctx, uint16_t page_id,
//                                        as608_template_buffer_t *output)
// {
//     command_t command = page_command(CMD_LOAD_CHAR, AS608_CHAR_BUFFER_1, page_id);
//     command.step = STEP_LOAD_ACK;
//     return upload_start(ctx, &command, output);
// }
// 
// typedef struct {
//     const uint8_t *data;
//     size_t length;
//     as608_char_buffer_t buffer;
//     bool store;
//     uint16_t page_id;
// } download_t;
// 
// static as608_status_t download_start(as608_t *ctx, const download_t *request)
// {
//     if (request->data == NULL || !supported_size(request->length) ||
//         !valid_buffer(request->buffer)) return AS608_STATUS_INVALID_ARG;
//     const command_t command = {{CMD_READ_PARAMS}, 1U, STEP_PARAMS_ACK};
//     const as608_status_t status = launch(ctx, &command);
//     if (status == AS608_STATUS_OK) {
//         ctx->input = request->data;
//         ctx->input_length = request->length;
//         ctx->input_offset = 0U;
//         ctx->download_buffer = request->buffer;
//         ctx->store_after_download = request->store;
//         ctx->page_id = request->page_id;
//     }
//     return status;
// }
// 
// as608_status_t as608_download_template_start(as608_t *ctx, as608_char_buffer_t buffer,
//                                            const uint8_t *data, size_t length)
// {
//     const download_t request = {data, length, buffer, false, 0U};
//     return download_start(ctx, &request);
// }
// 
// as608_status_t as608_write_template_start(as608_t *ctx, uint16_t page_id,
//                                         const uint8_t *data, size_t length)
// {
//     const download_t request = {data, length, AS608_CHAR_BUFFER_1, true, page_id};
//     return download_start(ctx, &request);
// }
// 
// as608_status_t as608_delete_templates_start(as608_t *ctx, uint16_t start_page,
//                                           uint16_t page_count)
// {
//     if (!valid_range(start_page, page_count)) return AS608_STATUS_INVALID_ARG;
//     command_t command = {{CMD_DELETE_CHAR}, 5U, STEP_SIMPLE_ACK};
//     store_u16(command.bytes + 1U, start_page);
//     store_u16(command.bytes + 3U, page_count);
//     return launch(ctx, &command);
// }
// 
// as608_status_t as608_empty_database_start(as608_t *ctx)
// {
//     return simple_start(ctx, CMD_EMPTY);
// }
// 
// /* The port owns the queued TX bytes after accepting them. */
// static as608_status_t send_next_chunk(as608_t *ctx)
// {
//     size_t length = ctx->input_length - ctx->input_offset;
//     if (length > ctx->chunk_size) length = ctx->chunk_size;
//     const as608_packet_kind_t kind =
//         ctx->input_offset + length == ctx->input_length ?
//         AS608_PACKET_END_DATA : AS608_PACKET_DATA;
//     const as608_transaction_frame_t frame = {kind, ctx->input + ctx->input_offset, length};
//     const as608_status_t status = as608_transaction_send(&ctx->transaction, &frame);
//     if (status == AS608_STATUS_OK) ctx->input_offset += length;
//     return status;
// }
// 
// static as608_status_t sent_frame(as608_t *ctx)
// {
//     if (ctx->step != STEP_DOWN_DATA) {
//         as608_transaction_receive(&ctx->transaction, AS608_TRANSACTION_MAX_PAYLOAD_BYTES);
//         return AS608_STATUS_OK;
//     }
//     if (ctx->input_offset < ctx->input_length) return send_next_chunk(ctx);
//     as608_transaction_drain(&ctx->transaction);
//     return AS608_STATUS_OK;
// }
// 
// static as608_progress_t next_or_fault(as608_t *ctx, as608_status_t status)
// {
//     return status == AS608_STATUS_OK ? AS608_PENDING : quarantine(ctx, status);
// }
// 
// static bool is_stream(const as608_t *ctx)
// {
//     return ctx->step == STEP_UP_DATA || ctx->step == STEP_INFO_DATA;
// }
// 
// static bool stream_size_valid(const as608_t *ctx)
// {
//     return ctx->step == STEP_INFO_DATA ?
//         ctx->stream_offset == AS608_INFORMATION_PAGE_BYTES :
//         supported_size(ctx->stream_offset);
// }
// 
// static as608_progress_t accept_stream(as608_t *ctx, const as608_transaction_frame_t *frame)
// {
//     const size_t length = frame->length;
//     if ((frame->kind != AS608_PACKET_DATA &&
//          frame->kind != AS608_PACKET_END_DATA) ||
//         length == 0U || length > ctx->stream_capacity - ctx->stream_offset) {
//         return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
//     }
//     (void)memcpy(ctx->stream_data + ctx->stream_offset, frame->data, length);
//     ctx->stream_offset += length;
//     if (frame->kind != AS608_PACKET_END_DATA) {
//         as608_transaction_receive(&ctx->transaction, ctx->stream_capacity - ctx->stream_offset);
//         return AS608_PENDING;
//     }
//     if (!stream_size_valid(ctx)) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
//     if (ctx->stream_length != NULL) *ctx->stream_length = ctx->stream_offset;
//     return finish(ctx, AS608_ACK_OK);
// }
// 
// static size_t expected_ack(const as608_t *ctx)
// {
//     switch (ctx->step) {
//     case STEP_PARAMS_ACK:
//     case STEP_READ_PARAMS_ACK: return 17U;
//     case STEP_U16_ACK: return 3U;
//     case STEP_SEARCH_ACK: return 5U;
//     case STEP_INDEX_ACK: return 33U;
//     default: return 1U;
//     }
// }
// 
// static bool ack_valid(const as608_t *ctx,
//                       const as608_transaction_frame_t *frame)
// {
//     if (frame->kind != AS608_PACKET_ACK || frame->length == 0U) return false;
//     return frame->length == expected_ack(ctx) ||
//            (frame->data[0] != AS608_ACK_OK && frame->length == 1U);
// }
// 
// static as608_status_t begin_download(as608_t *ctx, const as608_transaction_frame_t *frame)
// {
//     const uint16_t code = load_u16(frame->data + 13U);
//     if (code > 3U) return AS608_STATUS_UNEXPECTED_REPLY;
//     ctx->chunk_size = (uint16_t)(32U << code);
//     const command_t command = {{CMD_DOWN_CHAR, (uint8_t)ctx->download_buffer},
//                                 2U, STEP_DOWN_ACK};
//     return command_start(ctx, &command);
// }
// 
// static as608_progress_t accept_reply(as608_t *ctx, const as608_transaction_frame_t *frame)
// {
//     switch (ctx->step) {
//     case STEP_READ_PARAMS_ACK:
//         decode_parameters(frame->data, ctx->reply_output);
//         break;
//     case STEP_U16_ACK:
//         *(uint16_t *)ctx->reply_output = load_u16(frame->data + 1U);
//         break;
//     case STEP_SEARCH_ACK: {
//         as608_search_t *search = ctx->reply_output;
//         search->match.page_id = load_u16(frame->data + 1U);
//         search->match.score = load_u16(frame->data + 3U);
//         break;
//     }
//     case STEP_INDEX_ACK:
//         (void)memcpy(ctx->reply_output, frame->data + 1U, 32U);
//         break;
//     default:
//         return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
//     }
//     return finish(ctx, AS608_ACK_OK);
// }
// 
// static as608_progress_t success_ack(as608_t *ctx, const as608_transaction_frame_t *frame)
// {
//     switch (ctx->step) {
//     case STEP_SIMPLE_ACK:
//         return finish(ctx, AS608_ACK_OK);
//     case STEP_LOAD_ACK: {
//         const command_t command = {{CMD_UP_CHAR, AS608_CHAR_BUFFER_1}, 2U, STEP_UP_ACK};
//         return next_or_fault(ctx, command_start(ctx, &command));
//     }
//     case STEP_UP_ACK:
//     case STEP_INFO_ACK:
//         ctx->step = ctx->step == STEP_UP_ACK ? STEP_UP_DATA : STEP_INFO_DATA;
//         as608_transaction_receive(&ctx->transaction, ctx->stream_capacity - ctx->stream_offset);
//         return AS608_PENDING;
//     case STEP_PARAMS_ACK:
//         return next_or_fault(ctx, begin_download(ctx, frame));
//     case STEP_DOWN_ACK:
//         ctx->step = STEP_DOWN_DATA;
//         return next_or_fault(ctx, send_next_chunk(ctx));
//     default:
//         return accept_reply(ctx, frame);
//     }
// }
// 
// static as608_progress_t verified_packet(as608_t *ctx, const as608_transaction_frame_t *frame)
// {
//     if (is_stream(ctx)) return accept_stream(ctx, frame);
//     if (!ack_valid(ctx, frame)) return quarantine(ctx, AS608_STATUS_UNEXPECTED_REPLY);
//     if (frame->data[0] != AS608_ACK_OK) return finish(ctx, frame->data[0]);
//     return success_ack(ctx, frame);
// }
// 
// static as608_progress_t download_drained(as608_t *ctx)
// {
//     if (!ctx->store_after_download) return finish(ctx, AS608_ACK_OK);
//     const command_t command = page_command(CMD_STORE_CHAR,
//         ctx->download_buffer, ctx->page_id);
//     return next_or_fault(ctx, command_start(ctx, &command));
// }
