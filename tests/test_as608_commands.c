/** @file test_as608_commands.c @brief Host-side AS60x command/stream tests. */
#include "as608.h"
#include "as608_packet.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[4096];
    uint8_t rx[4096];
    size_t tx_len;
    size_t rx_len;
    size_t rx_pos;
    uint32_t now;
    size_t split;
} mock_t;

static int32_t mock_write(void *user, const uint8_t *data, size_t length,
                          uint32_t budget)
{
    mock_t *m = user;
    assert(budget != 0U);
    if (length > m->split) length = m->split;
    assert(m->tx_len + length <= sizeof(m->tx));
    memcpy(m->tx + m->tx_len, data, length);
    m->tx_len += length;
    ++m->now;
    return (int32_t)length;
}

static int32_t mock_read(void *user, uint8_t *data, size_t length,
                         uint32_t budget)
{
    mock_t *m = user;
    assert(budget != 0U);
    if (length > m->split) length = m->split;
    if (length > m->rx_len - m->rx_pos) length = m->rx_len - m->rx_pos;
    if (length != 0U) memcpy(data, m->rx + m->rx_pos, length);
    m->rx_pos += length;
    ++m->now;
    return (int32_t)length;
}

static uint32_t mock_ms(void *user)
{
    return ((mock_t *)user)->now;
}

static void setup(as608_t *dev, mock_t *m)
{
    const as608_config_t cfg = {
        .port = {.user = m, .write = mock_write, .read = mock_read,
                 .clock_ms = mock_ms},
        .device_address = AS608_DEFAULT_ADDRESS, .timeout_ms = 10000U
    };
    *dev = (as608_t){0};
    assert(as608_init(dev, &cfg) == AS608_STATUS_OK);
}

static void append_frame(mock_t *m, uint8_t kind,
                         const uint8_t *payload, size_t length)
{
    const as608_packet_header_t header = {
        AS608_DEFAULT_ADDRESS, kind, (uint16_t)length
    };
    assert(m->rx_len + length + AS608_PACKET_OVERHEAD_BYTES <= sizeof(m->rx));
    assert(as608_packet_encode_header(&header, m->rx + m->rx_len) ==
           AS608_STATUS_OK);
    m->rx_len += AS608_PACKET_HEADER_BYTES;
    if (length != 0U) {
        memcpy(m->rx + m->rx_len, payload, length);
        m->rx_len += length;
    }
    const uint16_t sum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), payload, length);
    as608_packet_checksum_encode(sum, m->rx + m->rx_len);
    m->rx_len += AS608_PACKET_CHECKSUM_BYTES;
}

static void ack(mock_t *m, uint8_t code, const uint8_t *rest, size_t len)
{
    uint8_t payload[33];
    assert(len <= sizeof(payload) - 1U);
    payload[0] = code;
    if (len != 0U) memcpy(payload + 1U, rest, len);
    append_frame(m, AS608_PACKET_ACK, payload, len + 1U);
}

static void assert_success(as608_result_t result)
{
    assert(result.status == AS608_STATUS_OK);
    assert(result.confirmation == AS608_ACK_OK);
}

static void test_init_and_basic_commands(void)
{
    mock_t m = {.split = 3U};
    as608_t dev = {0};
    const as608_config_t invalid = {0};
    assert(as608_init(&dev, &invalid) == AS608_STATUS_INVALID_ARG);
    assert(dev.state == AS608_STATE_UNINITIALIZED);
    setup(&dev, &m);
    assert(dev.state == AS608_STATE_READY);
    assert(as608_init(&dev, &invalid) == AS608_STATUS_INVALID_ARG);

    ack(&m, AS608_ACK_PASSWORD_ERROR, NULL, 0U);
    const as608_result_t p = as608_verify_password(&dev, UINT32_C(0x12345678));
    assert(p.status == AS608_STATUS_OK &&
           p.confirmation == AS608_ACK_PASSWORD_ERROR);
    assert(m.tx[9] == 0x13U && m.tx[10] == 0x12U &&
           m.tx[11] == 0x34U && m.tx[12] == 0x56U && m.tx[13] == 0x78U);
    assert(dev.state == AS608_STATE_READY);

    ack(&m, AS608_ACK_NO_FINGER, NULL, 0U);
    const as608_result_t c = as608_capture_image(&dev);
    assert(c.status == AS608_STATUS_OK &&
           c.confirmation == AS608_ACK_NO_FINGER);

    ack(&m, AS608_ACK_OK, NULL, 0U);
    assert_success(as608_generate_character(&dev, AS608_CHAR_BUFFER_2));
    ack(&m, AS608_ACK_OK, NULL, 0U);
    assert_success(as608_create_model(&dev));
    ack(&m, AS608_ACK_OK, NULL, 0U);
    assert_success(as608_empty_database(&dev));
}

static void test_data_replies(void)
{
    mock_t m = {.split = 5U};
    as608_t dev;
    setup(&dev, &m);
    const uint8_t params[16] = {
        0x00, 0x01, 0x00, 0x02, 0x03, 0xE8, 0x00, 0x03,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x02, 0x00, 0x06
    };
    ack(&m, AS608_ACK_OK, params, sizeof(params));
    as608_parameters_t out = {0};
    assert_success(as608_read_parameters(&dev, &out));
    assert(out.sensor_type == 2U && out.template_capacity == 1000U &&
           out.packet_size_code == 2U && out.baud_multiplier == 6U);

    const uint8_t count[] = {0x01, 0x02};
    ack(&m, AS608_ACK_OK, count, sizeof(count));
    uint16_t number = 0U;
    assert_success(as608_get_template_count(&dev, &number));
    assert(number == 258U);

    const uint8_t match[] = {0x04U, 0xD2U};
    ack(&m, AS608_ACK_OK, match, sizeof(match));
    uint16_t score = 0U;
    assert_success(as608_match_buffers(&dev, &score));
    assert(score == 1234U);

    const uint8_t find[] = {0x00U, 0x2AU, 0x00U, 0xC8U};
    ack(&m, AS608_ACK_OK, find, sizeof(find));
    as608_search_t search = {.buffer = AS608_CHAR_BUFFER_1,
                             .start_page = 0U, .page_count = 100U};
    assert_success(as608_search(&dev, &search));
    assert(search.match.page_id == 42U && search.match.score == 200U);
    ack(&m, AS608_ACK_NOT_FOUND, NULL, 0U);
    const as608_result_t miss = as608_search(&dev, &search);
    assert(miss.status == AS608_STATUS_OK && miss.confirmation == AS608_ACK_NOT_FOUND);
    assert(search.match.page_id == 42U && search.match.score == 200U);

    uint8_t index[32];
    for (size_t i = 0U; i < sizeof(index); ++i) index[i] = (uint8_t)i;
    ack(&m, AS608_ACK_OK, index, sizeof(index));
    uint8_t bitmap[32] = {0};
    assert_success(as608_read_index_table(&dev, 2U, bitmap, sizeof(bitmap)));
    assert(memcmp(bitmap, index, sizeof(index)) == 0);
}

static void test_multiframe_read_and_write(void)
{
    mock_t m = {.split = 16U};
    as608_t dev;
    setup(&dev, &m);
    ack(&m, AS608_ACK_OK, NULL, 0U); /* LoadChar */
    ack(&m, AS608_ACK_OK, NULL, 0U); /* UpChar */
    uint8_t part[128];
    uint8_t expected[512];
    for (size_t i = 0U; i < sizeof(expected); ++i) expected[i] = (uint8_t)i;
    for (size_t i = 0U; i < 4U; ++i) {
        memcpy(part, expected + i * 128U, sizeof(part));
        append_frame(&m, i == 3U ? AS608_PACKET_END_DATA : AS608_PACKET_DATA,
                     part, sizeof(part));
    }
    uint8_t out[AS608_TEMPLATE_BYTES] = {0};
    assert_success(as608_read_template(&dev, 25U, out, sizeof(out)));
    assert(memcmp(out, expected, sizeof(expected)) == 0);

    const uint8_t params[16] = {
        0U, 1U, 0U, 2U, 3U, 0xE8U, 0U, 3U,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0U, 2U, 0U, 6U
    };
    ack(&m, AS608_ACK_OK, params, sizeof(params)); /* ReadSysPara */
    ack(&m, AS608_ACK_OK, NULL, 0U); /* DownChar */
    ack(&m, AS608_ACK_OK, NULL, 0U); /* StoreChar */
    const size_t old_tx = m.tx_len;
    assert_success(as608_write_template(&dev, 25U, expected, sizeof(expected)));

    size_t pos = old_tx;
    size_t packets = 0U;
    size_t data_bytes = 0U;
    uint8_t seen[8] = {0};
    while (pos < m.tx_len) {
        assert(pos + AS608_PACKET_HEADER_BYTES <= m.tx_len);
        const uint16_t wire_len = (uint16_t)(((uint16_t)m.tx[pos+7U] << 8U) |
                                              m.tx[pos+8U]);
        const size_t payload_size = (size_t)wire_len - 2U;
        assert(pos + 9U + payload_size + 2U <= m.tx_len);
        assert(packets < sizeof(seen));
        seen[packets] = m.tx[pos + 6U];
        if ((seen[packets] == AS608_PACKET_DATA) ||
            (seen[packets] == AS608_PACKET_END_DATA)) {
            assert(payload_size == 128U);
            data_bytes += payload_size;
        }
        ++packets;
        pos += payload_size + AS608_PACKET_OVERHEAD_BYTES;
    }
    assert(packets == 7U);
    assert(seen[0] == AS608_PACKET_COMMAND &&
           seen[1] == AS608_PACKET_COMMAND &&
           seen[2] == AS608_PACKET_DATA &&
           seen[3] == AS608_PACKET_DATA &&
           seen[4] == AS608_PACKET_DATA &&
           seen[5] == AS608_PACKET_END_DATA &&
           seen[6] == AS608_PACKET_COMMAND);
    assert(data_bytes == 512U);
}

static void test_bad_stream_quarantines(void)
{
    mock_t m = {.split = 16U};
    as608_t dev;
    setup(&dev, &m);
    ack(&m, AS608_ACK_OK, NULL, 0U);
    uint8_t short_part[8] = {0};
    append_frame(&m, AS608_PACKET_END_DATA, short_part, sizeof(short_part));
    uint8_t out[AS608_TEMPLATE_BYTES];
    const as608_result_t r = as608_upload_template(
        &dev, AS608_CHAR_BUFFER_1, out, sizeof(out));
    assert(r.status == AS608_STATUS_UNEXPECTED_REPLY);
    assert(dev.state == AS608_STATE_DESYNCHRONIZED);
    assert(as608_capture_image(&dev).status == AS608_STATUS_RECOVERY_REQUIRED);
}


static void test_information_and_database_commands(void)
{
    mock_t m = {.split = 32U};
    as608_t dev;
    setup(&dev, &m);

    ack(&m, AS608_ACK_OK, NULL, 0U); /* ReadINFpage */
    uint8_t chunk[256];
    for (size_t i = 0U; i < sizeof(chunk); ++i) chunk[i] = (uint8_t)i;
    append_frame(&m, AS608_PACKET_DATA, chunk, sizeof(chunk));
    append_frame(&m, AS608_PACKET_END_DATA, chunk, sizeof(chunk));
    uint8_t info[AS608_INFORMATION_PAGE_BYTES] = {0};
    assert_success(as608_read_information_page(&dev, info, sizeof(info)));
    assert(memcmp(info, chunk, sizeof(chunk)) == 0);
    assert(memcmp(info + sizeof(chunk), chunk, sizeof(chunk)) == 0);

    ack(&m, AS608_ACK_OK, NULL, 0U);
    assert_success(as608_load_template(&dev, AS608_CHAR_BUFFER_2, 18U));
    ack(&m, AS608_ACK_OK, NULL, 0U);
    assert_success(as608_store_template(&dev, AS608_CHAR_BUFFER_2, 18U));
    ack(&m, AS608_ACK_OK, NULL, 0U);
    assert_success(as608_delete_templates(&dev, 18U, 2U));

    const size_t previous_tx = m.tx_len;
    assert(as608_delete_templates(&dev, 0U, 0U).status == AS608_STATUS_INVALID_ARG);
    assert(as608_read_index_table(&dev, 4U, chunk, 32U).status == AS608_STATUS_INVALID_ARG);
    assert(m.tx_len == previous_tx);
}

static void test_protocol_error_quarantines_device(void)
{
    mock_t m = {.split = 16U};
    as608_t dev;
    setup(&dev, &m);
    const uint8_t wrong_payload = 0xA5U;
    append_frame(&m, AS608_PACKET_DATA, &wrong_payload, 1U);
    const as608_result_t error = as608_capture_image(&dev);
    assert(error.status == AS608_STATUS_UNEXPECTED_REPLY);
    assert(dev.state == AS608_STATE_DESYNCHRONIZED);
    assert(as608_capture_image(&dev).status == AS608_STATUS_RECOVERY_REQUIRED);
}

int main(void)
{
    test_init_and_basic_commands();
    test_data_replies();
    test_multiframe_read_and_write();
    test_bad_stream_quarantines();
    test_information_and_database_commands();
    test_protocol_error_quarantines_device();
    puts("as608 command tests: PASS");
    return 0;
}