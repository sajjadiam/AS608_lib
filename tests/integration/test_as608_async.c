/** @file test_as608_async.c @brief Cooperative engine regression tests. */
#include "as608.h"
#include "as608_packet.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[4096U], rx[4096U];
    size_t tx_len, rx_len, rx_pos;
    size_t split;
    uint32_t now;
} mock_t;

static int32_t write_some(void *user, const uint8_t *bytes, size_t count)
{
    mock_t *m = user;
    if (count > m->split) count = m->split;
    assert(m->tx_len + count <= sizeof(m->tx));
    (void)memcpy(m->tx + m->tx_len, bytes, count);
    m->tx_len += count;
    return (int32_t)count;
}

static int32_t read_some(void *user, uint8_t *bytes, size_t count)
{
    mock_t *m = user;
    if (count > m->split) count = m->split;
    if (count > m->rx_len - m->rx_pos) count = m->rx_len - m->rx_pos;
    if (count != 0U) (void)memcpy(bytes, m->rx + m->rx_pos, count);
    m->rx_pos += count;
    return (int32_t)count;
}

static int32_t tx_idle(void *user)
{
    (void)user;
    return 1;
}

static uint32_t clock_ms(void *user) { return ((mock_t *)user)->now; }

static void append_frame(mock_t *m, as608_packet_kind_t kind,
                         const uint8_t *payload, size_t size)
{
    const as608_packet_header_t h = {
        AS608_DEFAULT_ADDRESS, (uint8_t)kind, (uint16_t)size
    };
    assert(m->rx_len + AS608_PACKET_OVERHEAD_BYTES + size <= sizeof(m->rx));
    assert(as608_packet_encode_header(&h, m->rx + m->rx_len) ==
           AS608_STATUS_OK);
    m->rx_len += AS608_PACKET_HEADER_BYTES;
    if (size != 0U) (void)memcpy(m->rx + m->rx_len, payload, size);
    m->rx_len += size;
    const uint16_t sum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&h), payload, size);
    as608_packet_checksum_encode(sum, m->rx + m->rx_len);
    m->rx_len += AS608_PACKET_CHECKSUM_BYTES;
}

static void ack(mock_t *m, uint8_t code)
{
    append_frame(m, AS608_PACKET_ACK, &code, 1U);
}

static void setup(as608_t *dev, mock_t *m)
{
    *dev = (as608_t){0};
    const as608_config_t cfg = {
        .port = {.user = m, .try_write = write_some, .try_read = read_some,
        .clock_ms = clock_ms, .tx_idle = tx_idle},
        .device_address = AS608_DEFAULT_ADDRESS,
        .timeout_ms = 3000U
    };
    assert(as608_init(dev, &cfg) == AS608_STATUS_OK);
}

static as608_progress_t run(as608_t *dev, mock_t *m)
{
    for (size_t i = 0U; i < 4000U; ++i) {
        const as608_progress_t p = as608_process(dev);
        if (p != AS608_PENDING) return p;
        ++m->now;
    }
    assert(0 && "async state machine stalled");
    return AS608_FAILED;
}

static void test_capture_and_negative_ack(void)
{
    mock_t m = {.split = 3U};
    as608_t dev;
    setup(&dev, &m);
    ack(&m, AS608_ACK_NO_FINGER);
    assert(as608_capture_image_start(&dev) == AS608_STATUS_OK);
    assert(run(&dev, &m) == AS608_COMPLETE);
    as608_result_t result;
    assert(as608_get_result(&dev, &result));
    assert(result.status == AS608_STATUS_OK &&
           result.confirmation == AS608_ACK_NO_FINGER);
    assert(dev.state == AS608_STATE_DONE);
    assert(m.tx[9] == 0x01U);
    ack(&m, AS608_ACK_OK);
    assert(as608_generate_character_start(
        &dev, AS608_CHAR_BUFFER_2) == AS608_STATUS_OK);
    assert(run(&dev, &m) == AS608_COMPLETE);
    assert(as608_get_result(&dev, &result));
    assert(result.confirmation == AS608_ACK_OK);
}

static void test_read_768_and_write_768(void)
{
    mock_t m = {.split = 7U};
    as608_t dev;
    setup(&dev, &m);
    uint8_t expected[768U];
    for (size_t i = 0U; i < sizeof(expected); ++i) expected[i] = (uint8_t)i;
    ack(&m, AS608_ACK_OK); /* LoadChar */
    ack(&m, AS608_ACK_OK); /* UpChar */
    for (size_t i = 0U; i < 6U; ++i) {
        append_frame(&m, (i == 5U) ?
            AS608_PACKET_END_DATA : AS608_PACKET_DATA,
            expected + 128U * i, 128U);
    }
    uint8_t buffer[1024U];
    (void)memset(buffer, 0x5AU, sizeof(buffer));
    as608_template_buffer_t out = {buffer, sizeof(buffer), 91U};
    assert(as608_read_template_start(&dev, 1U, &out) == AS608_STATUS_OK);
    assert(run(&dev, &m) == AS608_COMPLETE);
    assert(out.length == 768U);
    assert(memcmp(expected, buffer, 768U) == 0);
    assert(buffer[768U] == 0x5AU);

    uint8_t params[17U] = {0};
    params[0] = AS608_ACK_OK;
    params[13U] = 0U;
    params[14U] = 2U; /* 128-byte packet code */
    append_frame(&m, AS608_PACKET_ACK, params, sizeof(params));
    ack(&m, AS608_ACK_OK); /* DownChar */
    ack(&m, AS608_ACK_OK); /* StoreChar */
    const size_t old_tx = m.tx_len;
    assert(as608_write_template_start(&dev, 2U, buffer, out.length) ==
           AS608_STATUS_OK);
    assert(run(&dev, &m) == AS608_COMPLETE);
    size_t pos = old_tx, data_bytes = 0U, data_frames = 0U, end_frames = 0U;
    while (pos < m.tx_len) {
        assert(pos + AS608_PACKET_HEADER_BYTES <= m.tx_len);
        const size_t length = ((size_t)m.tx[pos + 7U] << 8U) |
                               (size_t)m.tx[pos + 8U];
        const uint8_t kind = m.tx[pos + 6U];
        assert(length >= 2U && pos + 9U + length <= m.tx_len);
        if (kind == (uint8_t)AS608_PACKET_DATA ||
            kind == (uint8_t)AS608_PACKET_END_DATA) {
            data_bytes += length - 2U;
            ++data_frames;
            if (kind == (uint8_t)AS608_PACKET_END_DATA) ++end_frames;
        }
        pos += 9U + length;
    }
    assert(data_bytes == 768U && data_frames == 6U && end_frames == 1U);
}

static void test_checksum_and_timeout(void)
{
    mock_t m = {.split = 16U};
    as608_t dev;
    setup(&dev, &m);
    ack(&m, AS608_ACK_OK);
    m.rx[m.rx_len - 1U] ^= 0x01U;
    assert(as608_capture_image_start(&dev) == AS608_STATUS_OK);
    assert(run(&dev, &m) == AS608_FAILED);
    as608_result_t result;
    assert(as608_get_result(&dev, &result));
    assert(result.status == AS608_STATUS_CHECKSUM_ERROR);
    assert(dev.state == AS608_STATE_DESYNCHRONIZED);

    mock_t other = {.split = 8U};
    as608_t idle;
    setup(&idle, &other);
    assert(as608_capture_image_start(&idle) == AS608_STATUS_OK);
    other.now = 3001U;
    assert(as608_process(&idle) == AS608_FAILED);
    assert(as608_get_result(&idle, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
}

static void test_two_sensors_independent(void)
{
    mock_t m1 = {.split = 1U}, m2 = {.split = 8U};
    as608_t a, b;
    setup(&a, &m1);
    setup(&b, &m2);
    ack(&m1, AS608_ACK_NO_FINGER);
    ack(&m2, AS608_ACK_OK);
    assert(as608_capture_image_start(&a) == AS608_STATUS_OK);
    assert(as608_capture_image_start(&b) == AS608_STATUS_OK);
    for (size_t i = 0U; i < 200U; ++i) {
        (void)as608_process(&a);
        (void)as608_process(&b);
        ++m1.now;
        ++m2.now;
        if (a.state == AS608_STATE_DONE && b.state == AS608_STATE_DONE) break;
    }
    assert(a.state == AS608_STATE_DONE && b.state == AS608_STATE_DONE);
    assert(a.result.confirmation == AS608_ACK_NO_FINGER);
    assert(b.result.confirmation == AS608_ACK_OK);
}

int main(void)
{
    test_capture_and_negative_ack();
    test_read_768_and_write_768();
    test_checksum_and_timeout();
    test_two_sensors_independent();
    puts("as608 async tests: PASS");
    return 0;
}
