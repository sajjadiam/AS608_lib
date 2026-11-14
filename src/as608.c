/**
 * @file as608.c
 * @brief Blocking AS60x fingerprint commands implemented over transactions.
 * @details C11; no heap or global state. See AS60x Communication Manual v1.0,
 *          command format section (PDF pages 17-32).
 */
#include "as608.h"
#include "as608_transaction.h"

#include <stdbool.h>
#include <string.h>

#define AS608_ACK_PARAMETERS_BYTES 17U
#define AS608_INDEX_BYTES 32U
#define AS608_MAX_DATA_CHUNK 256U

/* Wire command identifiers: these are not packet identifiers. */
enum {
    CMD_GET_IMAGE = 0x01, CMD_GEN_CHAR = 0x02, CMD_MATCH = 0x03,
    CMD_SEARCH = 0x04, CMD_REG_MODEL = 0x05, CMD_STORE_CHAR = 0x06,
    CMD_LOAD_CHAR = 0x07, CMD_UP_CHAR = 0x08, CMD_DOWN_CHAR = 0x09,
    CMD_DELETE_CHAR = 0x0C, CMD_EMPTY = 0x0D, CMD_READ_PARAMS = 0x0F,
    CMD_VERIFY_PASSWORD = 0x13, CMD_READ_INFO = 0x16,
    CMD_COUNT = 0x1D, CMD_READ_INDEX = 0x1F
};

static as608_result_t host_result(as608_status_t status)
{
    const as608_result_t result = {status, 0U};
    return result;
}

static as608_result_t sensor_result(uint8_t confirmation)
{
    const as608_result_t result = {AS608_STATUS_OK, confirmation};
    return result;
}

static bool succeeded(as608_result_t result)
{
    return (result.status == AS608_STATUS_OK) &&
           (result.confirmation == (uint8_t)AS608_ACK_OK);
}

static bool valid_buffer(as608_char_buffer_t buffer)
{
    return (buffer == AS608_CHAR_BUFFER_1) ||
           (buffer == AS608_CHAR_BUFFER_2);
}

static bool valid_range(uint16_t start, uint16_t count)
{
    return (count != 0U) &&
           (((uint32_t)start + (uint32_t)count) <= UINT32_C(65536));
}

static uint16_t load_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
}

static uint32_t load_u32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static void store_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void store_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

/* A syntactically valid frame in the wrong command phase leaves the link
 * ambiguous. Quarantine it rather than consuming a later reply as our own. */
static as608_result_t bad_reply(as608_transaction_t *txn)
{
    txn->fault = AS608_STATUS_UNEXPECTED_REPLY;
    txn->dev->state = AS608_STATE_DESYNCHRONIZED;
    return host_result(AS608_STATUS_UNEXPECTED_REPLY);
}

/* ACK errors are valid protocol exchanges; only malformed successful replies
 * are quarantined. 'ack' is caller-owned and includes the confirmation byte. */
static as608_result_t command_ack(as608_transaction_t *txn,
                                  const uint8_t *command, size_t command_size,
                                  uint8_t *ack, size_t expected_success_size)
{
    as608_status_t status = as608_transaction_send(
        txn, AS608_PACKET_COMMAND, command, command_size);
    if (status != AS608_STATUS_OK) return host_result(status);

    as608_transaction_rx_t rx = {ack, expected_success_size, 0U,
                                 AS608_PACKET_ACK};
    status = as608_transaction_receive(txn, &rx);
    if (status != AS608_STATUS_OK) return host_result(status);
    if ((rx.kind != AS608_PACKET_ACK) || (rx.length == 0U)) {
        return bad_reply(txn);
    }
    if (ack[0] == (uint8_t)AS608_ACK_OK) {
        if (rx.length != expected_success_size) return bad_reply(txn);
    } else if ((rx.length != 1U) &&
               (rx.length != expected_success_size)) {
        return bad_reply(txn);
    }
    return sensor_result(ack[0]);
}

static as608_result_t begin_command(as608_t *dev, as608_transaction_t *txn)
{
    return host_result(as608_transaction_begin(txn, dev));
}

static as608_result_t simple_command(as608_t *dev, uint8_t command)
{
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    uint8_t ack[1];
    return command_ack(&txn, &command, 1U, ack, sizeof(ack));
}

static as608_result_t buffer_command(as608_transaction_t *txn,
                                    uint8_t command, as608_char_buffer_t buffer)
{
    const uint8_t packet[] = {command, (uint8_t)buffer};
    uint8_t ack[1];
    return command_ack(txn, packet, sizeof(packet), ack, sizeof(ack));
}

static as608_result_t page_command(as608_transaction_t *txn, uint8_t command,
                                  as608_char_buffer_t buffer, uint16_t page)
{
    uint8_t packet[4] = {command, (uint8_t)buffer, 0U, 0U};
    store_u16(&packet[2], page);
    uint8_t ack[1];
    return command_ack(txn, packet, sizeof(packet), ack, sizeof(ack));
}

static void decode_parameters(const uint8_t *ack, as608_parameters_t *out)
{
    out->status_register = load_u16(&ack[1]);
    out->sensor_type = load_u16(&ack[3]);
    out->template_capacity = load_u16(&ack[5]);
    out->security_level = load_u16(&ack[7]);
    out->device_address = load_u32(&ack[9]);
    out->packet_size_code = load_u16(&ack[13]);
    out->baud_multiplier = load_u16(&ack[15]);
}

static as608_result_t parameters_command(as608_transaction_t *txn,
                                         as608_parameters_t *parameters)
{
    const uint8_t command = CMD_READ_PARAMS;
    uint8_t ack[AS608_ACK_PARAMETERS_BYTES];
    const as608_result_t result = command_ack(txn, &command, 1U,
                                               ack, sizeof(ack));
    if (succeeded(result)) decode_parameters(ack, parameters);
    return result;
}

/* One receive deadline covers ACK plus all DATA/END_DATA packets. The
 * destination may be partially changed on failure and must then be ignored. */
static as608_result_t receive_blob(as608_transaction_t *txn,
                                   uint8_t *destination, size_t total)
{
    size_t offset = 0U;
    for (;;) {
        as608_transaction_rx_t rx = {
            destination + offset,
            (total - offset < AS608_MAX_DATA_CHUNK) ?
                total - offset : AS608_MAX_DATA_CHUNK,
            0U, AS608_PACKET_DATA
        };
        const as608_status_t status = as608_transaction_receive(txn, &rx);
        if (status != AS608_STATUS_OK) return host_result(status);
        if ((rx.length == 0U) ||
            ((rx.kind != AS608_PACKET_DATA) &&
             (rx.kind != AS608_PACKET_END_DATA))) {
            return bad_reply(txn);
        }
        offset += rx.length;
        if (rx.kind == AS608_PACKET_END_DATA) {
            return (offset == total) ? sensor_result(AS608_ACK_OK) :
                                       bad_reply(txn);
        }
        if (offset == total) return bad_reply(txn);
    }
}

static as608_result_t upload_command(as608_transaction_t *txn,
                                     as608_char_buffer_t buffer,
                                     uint8_t *data, size_t length)
{
    const as608_result_t ack = buffer_command(txn, CMD_UP_CHAR, buffer);
    if (!succeeded(ack)) return ack;
    return receive_blob(txn, data, length);
}

/* Send DATA followed by precisely one END_DATA fragment. The chunk size is
 * obtained from ReadSysPara rather than guessed from the module's package. */
static as608_result_t send_blob(as608_transaction_t *txn,
                                const uint8_t *data, size_t total,
                                size_t chunk_size)
{
    for (size_t offset = 0U; offset < total;) {
        size_t count = total - offset;
        if (count > chunk_size) count = chunk_size;
        const as608_packet_kind_t kind =
            ((offset + count) == total) ? AS608_PACKET_END_DATA :
                                          AS608_PACKET_DATA;
        const as608_status_t status = as608_transaction_send(
            txn, kind, data + offset, count);
        if (status != AS608_STATUS_OK) return host_result(status);
        offset += count;
    }
    return sensor_result(AS608_ACK_OK);
}

static as608_result_t download_command(as608_transaction_t *txn,
                                       as608_char_buffer_t buffer,
                                       const uint8_t *data, size_t length)
{
    as608_parameters_t parameters;
    const as608_result_t info = parameters_command(txn, &parameters);
    if (!succeeded(info)) return info;
    if (parameters.packet_size_code > 3U) return bad_reply(txn);
    const size_t chunk_size = (size_t)32U << parameters.packet_size_code;
    const as608_result_t ack = buffer_command(txn, CMD_DOWN_CHAR, buffer);
    if (!succeeded(ack)) return ack;
    return send_blob(txn, data, length, chunk_size);
}

as608_status_t as608_init(as608_t *dev, const as608_config_t *config)
{
    if ((dev == NULL) || (config == NULL)) return AS608_STATUS_INVALID_ARG;
    if (dev->state == AS608_STATE_DESYNCHRONIZED) {
        return AS608_STATUS_RECOVERY_REQUIRED;
    }
    if (dev->state != AS608_STATE_UNINITIALIZED) {
        return AS608_STATUS_INVALID_ARG;
    }
    if ((config->port.read == NULL) || (config->port.write == NULL) ||
        (config->port.clock_ms == NULL) || (config->timeout_ms == 0U) ||
        (config->timeout_ms > AS608_MAX_TIMEOUT_MS)) {
        return AS608_STATUS_INVALID_ARG;
    }
    dev->config = *config;
    dev->state = AS608_STATE_READY;
    return AS608_STATUS_OK;
}

as608_result_t as608_verify_password(as608_t *dev, uint32_t password)
{
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    uint8_t command[5] = {CMD_VERIFY_PASSWORD, 0U, 0U, 0U, 0U};
    store_u32(&command[1], password);
    uint8_t ack[1];
    return command_ack(&txn, command, sizeof(command), ack, sizeof(ack));
}

as608_result_t as608_read_parameters(as608_t *dev, as608_parameters_t *out)
{
    if (out == NULL) return host_result(AS608_STATUS_INVALID_ARG);
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    return parameters_command(&txn, out);
}

as608_result_t as608_get_template_count(as608_t *dev, uint16_t *count)
{
    if (count == NULL) return host_result(AS608_STATUS_INVALID_ARG);
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    const uint8_t cmd = CMD_COUNT;
    uint8_t ack[3];
    const as608_result_t result = command_ack(&txn, &cmd, 1U, ack, sizeof(ack));
    if (succeeded(result)) *count = load_u16(&ack[1]);
    return result;
}

as608_result_t as608_read_index_table(as608_t *dev, uint8_t index_page,
                                       uint8_t *bitmap, size_t capacity)
{
    if ((bitmap == NULL) || (capacity < AS608_INDEX_BYTES) ||
        (index_page > 3U)) return host_result(AS608_STATUS_INVALID_ARG);
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    const uint8_t cmd[] = {CMD_READ_INDEX, index_page};
    uint8_t ack[1U + AS608_INDEX_BYTES];
    const as608_result_t result = command_ack(&txn, cmd, sizeof(cmd),
                                               ack, sizeof(ack));
    if (succeeded(result)) memcpy(bitmap, &ack[1], AS608_INDEX_BYTES);
    return result;
}

as608_result_t as608_read_information_page(as608_t *dev,
                                            uint8_t *data, size_t capacity)
{
    if ((data == NULL) || (capacity < AS608_INFORMATION_PAGE_BYTES)) {
        return host_result(AS608_STATUS_INVALID_ARG);
    }
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    const uint8_t cmd = CMD_READ_INFO;
    uint8_t ack[1];
    const as608_result_t result = command_ack(&txn, &cmd, 1U, ack, sizeof(ack));
    if (!succeeded(result)) return result;
    return receive_blob(&txn, data, AS608_INFORMATION_PAGE_BYTES);
}

as608_result_t as608_capture_image(as608_t *dev)
{
    return simple_command(dev, CMD_GET_IMAGE);
}

as608_result_t as608_generate_character(as608_t *dev,
                                         as608_char_buffer_t buffer)
{
    if (!valid_buffer(buffer)) return host_result(AS608_STATUS_INVALID_ARG);
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    return buffer_command(&txn, CMD_GEN_CHAR, buffer);
}

as608_result_t as608_match_buffers(as608_t *dev, uint16_t *score)
{
    if (score == NULL) return host_result(AS608_STATUS_INVALID_ARG);
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    const uint8_t cmd = CMD_MATCH;
    uint8_t ack[3];
    const as608_result_t result = command_ack(&txn, &cmd, 1U, ack, sizeof(ack));
    if (succeeded(result)) *score = load_u16(&ack[1]);
    return result;
}

as608_result_t as608_create_model(as608_t *dev)
{
    return simple_command(dev, CMD_REG_MODEL);
}

as608_result_t as608_search(as608_t *dev, as608_search_t *search)
{
    if ((search == NULL) || !valid_buffer(search->buffer) ||
        !valid_range(search->start_page, search->page_count)) {
        return host_result(AS608_STATUS_INVALID_ARG);
    }
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    uint8_t cmd[6] = {CMD_SEARCH, (uint8_t)search->buffer, 0U, 0U, 0U, 0U};
    store_u16(&cmd[2], search->start_page);
    store_u16(&cmd[4], search->page_count);
    uint8_t ack[5];
    const as608_result_t result = command_ack(&txn, cmd, sizeof(cmd), ack,
                                               sizeof(ack));
    if (succeeded(result)) {
        search->match.page_id = load_u16(&ack[1]);
        search->match.score = load_u16(&ack[3]);
    }
    return result;
}

as608_result_t as608_load_template(as608_t *dev,
                                    as608_char_buffer_t buffer,
                                    uint16_t page_id)
{
    if (!valid_buffer(buffer)) return host_result(AS608_STATUS_INVALID_ARG);
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    return page_command(&txn, CMD_LOAD_CHAR, buffer, page_id);
}

as608_result_t as608_upload_template(as608_t *dev,
                                      as608_char_buffer_t buffer,
                                      uint8_t *data, size_t capacity)
{
    if (!valid_buffer(buffer) || (data == NULL) ||
        (capacity < AS608_TEMPLATE_BYTES)) {
        return host_result(AS608_STATUS_INVALID_ARG);
    }
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    return upload_command(&txn, buffer, data, AS608_TEMPLATE_BYTES);
}

as608_result_t as608_download_template(as608_t *dev,
                                        as608_char_buffer_t buffer,
                                        const uint8_t *data, size_t length)
{
    if (!valid_buffer(buffer) || (data == NULL) ||
        (length != AS608_TEMPLATE_BYTES)) {
        return host_result(AS608_STATUS_INVALID_ARG);
    }
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    return download_command(&txn, buffer, data, length);
}

as608_result_t as608_store_template(as608_t *dev,
                                     as608_char_buffer_t buffer,
                                     uint16_t page_id)
{
    if (!valid_buffer(buffer)) return host_result(AS608_STATUS_INVALID_ARG);
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    return page_command(&txn, CMD_STORE_CHAR, buffer, page_id);
}

as608_result_t as608_read_template(as608_t *dev, uint16_t page_id,
                                    uint8_t *data, size_t capacity)
{
    if ((data == NULL) || (capacity < AS608_TEMPLATE_BYTES)) {
        return host_result(AS608_STATUS_INVALID_ARG);
    }
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    const as608_result_t loaded = page_command(&txn, CMD_LOAD_CHAR,
                                                AS608_CHAR_BUFFER_1, page_id);
    if (!succeeded(loaded)) return loaded;
    return upload_command(&txn, AS608_CHAR_BUFFER_1, data,
                           AS608_TEMPLATE_BYTES);
}

as608_result_t as608_write_template(as608_t *dev, uint16_t page_id,
                                     const uint8_t *data, size_t length)
{
    if ((data == NULL) || (length != AS608_TEMPLATE_BYTES)) {
        return host_result(AS608_STATUS_INVALID_ARG);
    }
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    const as608_result_t downloaded = download_command(&txn,
        AS608_CHAR_BUFFER_1, data, length);
    if (!succeeded(downloaded)) return downloaded;
    return page_command(&txn, CMD_STORE_CHAR, AS608_CHAR_BUFFER_1, page_id);
}

as608_result_t as608_delete_templates(as608_t *dev,
                                       uint16_t start_page, uint16_t page_count)
{
    if (!valid_range(start_page, page_count)) {
        return host_result(AS608_STATUS_INVALID_ARG);
    }
    as608_transaction_t txn;
    const as608_result_t begun = begin_command(dev, &txn);
    if (begun.status != AS608_STATUS_OK) return begun;
    uint8_t cmd[5] = {CMD_DELETE_CHAR, 0U, 0U, 0U, 0U};
    store_u16(&cmd[1], start_page);
    store_u16(&cmd[3], page_count);
    uint8_t ack[1];
    return command_ack(&txn, cmd, sizeof(cmd), ack, sizeof(ack));
}

as608_result_t as608_empty_database(as608_t *dev)
{
    return simple_command(dev, CMD_EMPTY);
}