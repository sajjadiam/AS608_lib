#include "as608_transaction.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t rx[2048];
    uint8_t tx[2048];
    size_t rx_len, rx_pos, tx_len;
    size_t chunk;
    uint32_t now;
    uint32_t tick_per_call;
    int fail_write, fail_read;
    int recover_fail;
    unsigned recover_calls;
    uint32_t recover_ticks;
} fake_port_t;

static int32_t fake_write(void *user, const uint8_t *src,
                          size_t len, uint32_t timeout_ms)
{
    fake_port_t *f = user;
    assert(timeout_ms > 0U);
    f->now += f->tick_per_call;
    if (f->fail_write) return -1;
    if (len > f->chunk) len = f->chunk;
    assert(f->tx_len + len <= sizeof(f->tx));
    memcpy(&f->tx[f->tx_len], src, len);
    f->tx_len += len;
    return (int32_t)len;
}

static int32_t fake_read(void *user, uint8_t *dst,
                         size_t len, uint32_t timeout_ms)
{
    fake_port_t *f = user;
    assert(timeout_ms > 0U);
    f->now += f->tick_per_call;
    if (f->fail_read) return -1;
    if (len > f->chunk) len = f->chunk;
    if (len > f->rx_len - f->rx_pos) len = f->rx_len - f->rx_pos;
    if (len != 0U) memcpy(dst, &f->rx[f->rx_pos], len);
    f->rx_pos += len;
    return (int32_t)len;
}

static int32_t fake_recover(void *user, uint32_t timeout_ms)
{
    fake_port_t *f = user;
    assert(timeout_ms > 0U);
    ++f->recover_calls;
    f->now += f->recover_ticks;
    if (f->recover_fail) return -1;
    f->rx_pos = f->rx_len; /* Physical reset and UART RX reset simulated. */
    return 0;
}

static uint32_t fake_clock(void *user)
{
    return ((fake_port_t *)user)->now;
}

static void setup(as608_t *dev, fake_port_t *f, uint32_t timeout)
{
    memset(dev, 0, sizeof(*dev));
    dev->initialized = 1U;
    dev->config.device_address = AS608_DEFAULT_ADDRESS;
    dev->config.timeout_ms = timeout;
    dev->config.port.user = f;
    dev->config.port.write = fake_write;
    dev->config.port.read = fake_read;
    dev->config.port.clock_ms = fake_clock;
}

static void test_command_and_ack_split_over_wrap(void)
{
    /* From PS_GetImage example in AS60x communication manual. */
    static const uint8_t expected_tx[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x00, 0x03, 0x01, 0x00, 0x05
    };
    static const uint8_t incoming_ack[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x07, 0x00, 0x03, 0x00, 0x00, 0x0A
    };
    fake_port_t f = {.chunk = 2U, .tick_per_call = 1U,
                     .now = UINT32_MAX - 4U};
    memcpy(f.rx, incoming_ack, sizeof(incoming_ack));
    f.rx_len = sizeof(incoming_ack);
    as608_t dev;
    setup(&dev, &f, 50U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    const uint8_t command = 0x01U;
    assert(as608_transaction_send(&txn, AS608_PACKET_COMMAND,
                                  &command, 1U) == AS608_STATUS_OK);
    assert(f.tx_len == sizeof(expected_tx));
    assert(memcmp(f.tx, expected_tx, sizeof(expected_tx)) == 0);

    uint8_t ack[2] = {0x55U, 0x55U};
    as608_transaction_rx_t rx = {ack, sizeof(ack), 0xDEADU, AS608_PACKET_DATA};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_OK);
    assert(rx.length == 1U && rx.kind == AS608_PACKET_ACK && ack[0] == 0x00U);
    assert(txn.fault == AS608_STATUS_OK);
}

static void test_checksum_fault_is_sticky(void)
{
    fake_port_t f = {.chunk = 20U, .tick_per_call = 1U};
    const uint8_t invalid_ack[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x07, 0x00, 0x03, 0x02, 0x00, 0x0A /* expected checksum 0x0C */
    };
    memcpy(f.rx, invalid_ack, sizeof(invalid_ack));
    f.rx_len = sizeof(invalid_ack);
    as608_t dev;
    setup(&dev, &f, 50U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    uint8_t data = 0;
    as608_transaction_rx_t rx = {&data, 1U, 19U, AS608_PACKET_DATA};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_CHECKSUM_ERROR);
    assert(rx.length == 19U && rx.kind == AS608_PACKET_DATA);
    assert(as608_transaction_send(&txn, AS608_PACKET_COMMAND,
                                  &data, 1U) == AS608_STATUS_CHECKSUM_ERROR);
    assert(f.tx_len == 0U);
}

static void test_global_timeout_includes_tx_and_rx(void)
{
    fake_port_t f = {.chunk = 1U, .tick_per_call = 1U};
    as608_t dev;
    setup(&dev, &f, 16U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    const uint8_t command = 0x01U;
    assert(as608_transaction_send(&txn, AS608_PACKET_COMMAND,
                                  &command, 1U) == AS608_STATUS_OK);
    assert(f.now == 12U);
    uint8_t ack[4];
    as608_transaction_rx_t rx = {ack, sizeof(ack), 88U, AS608_PACKET_DATA};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_TIMEOUT);
    assert(rx.length == 88U && rx.kind == AS608_PACKET_DATA);
    assert(f.now == 16U);
}

static void test_oversized_reply_rejected_early(void)
{
    fake_port_t f = {.chunk = 20U, .tick_per_call = 1U};
    /* ACK claims 17-byte payload, but caller supplies only 1-byte buffer. */
    const uint8_t header[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x07, 0x00, 0x13
    };
    memcpy(f.rx, header, sizeof(header));
    f.rx_len = sizeof(header);
    as608_t dev;
    setup(&dev, &f, 50U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    uint8_t ack = 0xA5U;
    as608_transaction_rx_t rx = {&ack, 1U, 123U, AS608_PACKET_DATA};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_FRAME_ERROR);
    assert(f.rx_pos == sizeof(header));
    assert(ack == 0xA5U && rx.length == 123U && rx.kind == AS608_PACKET_DATA);
}

static void test_stream_data_and_end_data(void)
{
    /* Two valid RX frames with different packet IDs and checksum trailers. */
    const uint8_t stream[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x02, 0x00, 0x04, 0xAA, 0x55, 0x01, 0x05,
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x08, 0x00, 0x03, 0x44, 0x00, 0x4F
    };
    fake_port_t f = {.chunk = 4U, .tick_per_call = 1U};
    memcpy(f.rx, stream, sizeof(stream));
    f.rx_len = sizeof(stream);
    as608_t dev;
    setup(&dev, &f, 100U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);

    uint8_t data[4] = {0};
    as608_transaction_rx_t rx = {data, sizeof(data), 99U, AS608_PACKET_ACK};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_OK);
    assert(rx.kind == AS608_PACKET_DATA && rx.length == 2U);
    assert(data[0] == 0xAAU && data[1] == 0x55U);
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_OK);
    assert(rx.kind == AS608_PACKET_END_DATA && rx.length == 1U);
    assert(data[0] == 0x44U);
}

static void test_local_invalid_arg_does_not_taint(void)
{
    fake_port_t f = {.chunk = 20U, .tick_per_call = 1U};
    as608_t dev;
    setup(&dev, &f, 50U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    assert(as608_transaction_send(&txn, AS608_PACKET_ACK,
                                  NULL, 0U) == AS608_STATUS_INVALID_ARG);
    assert(txn.fault == AS608_STATUS_OK && f.tx_len == 0U);
}

static void test_io_error(void)
{
    fake_port_t f = {.chunk = 20U, .tick_per_call = 1U, .fail_write = 1};
    as608_t dev;
    setup(&dev, &f, 50U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    const uint8_t command = 0x01U;
    assert(as608_transaction_send(&txn, AS608_PACKET_COMMAND,
                                  &command, 1U) == AS608_STATUS_IO_ERROR);
}

static void test_unexpected_rx_packet_is_sticky(void)
{
    /* A valid COMMAND frame is illegal in the module-to-host direction. */
    const uint8_t unexpected[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x00, 0x03, 0x01, 0x00, 0x05
    };
    fake_port_t f = {.chunk = 20U, .tick_per_call = 1U};
    memcpy(f.rx, unexpected, sizeof(unexpected));
    f.rx_len = sizeof(unexpected);
    as608_t dev;
    setup(&dev, &f, 50U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);

    uint8_t byte = 0xAAU;
    as608_transaction_rx_t rx = {&byte, 1U, 99U, AS608_PACKET_DATA};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_UNEXPECTED_REPLY);
    assert(txn.fault == AS608_STATUS_UNEXPECTED_REPLY);
    assert(rx.length == 99U && rx.kind == AS608_PACKET_DATA);
    assert(byte == 0xAAU);
}

static void test_empty_ack_is_rejected(void)
{
    const uint8_t empty_ack_header[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x07, 0x00, 0x02
    };
    fake_port_t f = {.chunk = 20U, .tick_per_call = 1U};
    memcpy(f.rx, empty_ack_header, sizeof(empty_ack_header));
    f.rx_len = sizeof(empty_ack_header);
    as608_t dev;
    setup(&dev, &f, 50U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);

    uint8_t byte = 0xBBU;
    as608_transaction_rx_t rx = {&byte, 1U, 42U, AS608_PACKET_ACK};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_UNEXPECTED_REPLY);
    assert(txn.fault == AS608_STATUS_UNEXPECTED_REPLY);
    assert(rx.length == 42U && rx.kind == AS608_PACKET_ACK);
    assert(byte == 0xBBU);
}


static void test_late_ack_and_reset(void)
{
    static const uint8_t stale_ack[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
        0x07, 0x00, 0x03, 0x00, 0x00, 0x0A
    };
    fake_port_t f = {.chunk = 1U, .tick_per_call = 1U,
                     .recover_ticks = 2U};
    as608_t dev;
    setup(&dev, &f, 16U);
    dev.config.port.recover = fake_recover;
    as608_transaction_t txn;
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    const uint8_t cmd = 1U;
    assert(as608_transaction_send(&txn, AS608_PACKET_COMMAND,
                                  &cmd, 1U) == AS608_STATUS_OK);
    uint8_t ack = 0U;
    as608_transaction_rx_t rx = {&ack, 1U, 0U, AS608_PACKET_ACK};
    assert(as608_transaction_receive(&txn, &rx) == AS608_STATUS_TIMEOUT);
    assert(dev.link_unsynchronized != 0U);

    memcpy(f.rx, stale_ack, sizeof(stale_ack));
    f.rx_len = sizeof(stale_ack);
    f.rx_pos = 0U;
    as608_transaction_t next;
    const size_t tx_len = f.tx_len;
    assert(as608_transaction_begin(&next, &dev) ==
           AS608_STATUS_RECOVERY_REQUIRED);
    assert(f.rx_pos == 0U && f.tx_len == tx_len);
    assert(as608_recover(&dev) == AS608_STATUS_OK);
    assert(f.recover_calls == 1U && f.rx_pos == f.rx_len);
    assert(dev.link_unsynchronized == 0U);
    assert(as608_transaction_begin(&next, &dev) == AS608_STATUS_OK);
}

static void test_missing_failed_and_timed_out_reset(void)
{
    fake_port_t f = {.chunk = 16U, .tick_per_call = 1U,
                     .recover_ticks = 1U, .recover_fail = 1};
    as608_t dev;
    setup(&dev, &f, 16U);
    dev.link_unsynchronized = 1U;
    assert(as608_recover(&dev) == AS608_STATUS_RECOVERY_REQUIRED);
    dev.config.port.recover = fake_recover;
    assert(as608_recover(&dev) == AS608_STATUS_IO_ERROR);
    assert(dev.link_unsynchronized != 0U);
    f.recover_fail = 0;
    f.recover_ticks = 16U;
    assert(as608_recover(&dev) == AS608_STATUS_TIMEOUT);
    assert(dev.link_unsynchronized != 0U);
    f.recover_ticks = 1U;
    assert(as608_recover(&dev) == AS608_STATUS_OK);
    assert(dev.link_unsynchronized == 0U);
}

static void test_invalid_input_not_quarantined(void)
{
    fake_port_t f = {.chunk = 16U, .tick_per_call = 1U};
    as608_t dev;
    setup(&dev, &f, 16U);
    as608_transaction_t txn;
    assert(as608_transaction_begin(NULL, &dev) == AS608_STATUS_INVALID_ARG);
    assert(as608_transaction_begin(&txn, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_transaction_begin(&txn, &dev) == AS608_STATUS_OK);
    assert(as608_transaction_send(&txn, AS608_PACKET_ACK,
                                  NULL, 0U) == AS608_STATUS_INVALID_ARG);
    assert(dev.link_unsynchronized == 0U);
}

int main(void)
{
    test_command_and_ack_split_over_wrap();
    test_checksum_fault_is_sticky();
    test_global_timeout_includes_tx_and_rx();
    test_oversized_reply_rejected_early();
    test_stream_data_and_end_data();
    test_local_invalid_arg_does_not_taint();
    test_io_error();
    test_unexpected_rx_packet_is_sticky();
    test_empty_ack_is_rejected();
    test_late_ack_and_reset();
    test_missing_failed_and_timed_out_reset();
    test_invalid_input_not_quarantined();
    puts("as608 transaction tests: PASS");
    return 0;
}
