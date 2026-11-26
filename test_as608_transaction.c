/** @file test_as608_transaction.c @brief Independent transport tests. */
#include "as608_transaction.h"

#ifdef AS608_H
#error "Transaction must not depend on the public command header"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[267U];
    uint8_t rx[267U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_offset;
    size_t calls;
    uint32_t now;
    int32_t idle;
    bool stall;
    size_t burst;
    int32_t write_fault;
    int32_t read_fault;
} mock_t;

/* Accept an ordered prefix into the mock port-owned TX queue. */
static int32_t write_byte(void *user, const uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->calls;
    if (m->write_fault != 0) return m->write_fault;
    if (m->stall) return 0;
    assert(length != 0U && m->tx_length < sizeof(m->tx));
    const size_t cap = m->burst != 0U ? m->burst : 1U;
    const size_t count = length < cap ? length : cap;
    assert(count <= sizeof(m->tx) - m->tx_length);
    (void)memcpy(m->tx + m->tx_length, data, count);
    m->tx_length += count;
    return (int32_t)count;
}

/* Consume available mock RX bytes in order. */
static int32_t read_byte(void *user, uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->calls;
    assert(length != 0U);
    if (m->read_fault != 0) return m->read_fault;
    const size_t available = m->rx_length - m->rx_offset;
    const size_t cap = m->burst != 0U ? m->burst : 1U;
    size_t count = length < cap ? length : cap;
    if (count > available) count = available;
    if (count == 0U) return 0;
    (void)memcpy(data, m->rx + m->rx_offset, count);
    m->rx_offset += count;
    return (int32_t)count;
}

/* Return the simulated physical TX completion state. */
static int32_t idle(void *user)
{
    mock_t *m = user;
    ++m->calls;
    return m->idle;
}

/* Return the mock monotonic clock. */
static uint32_t clock_ms(void *user)
{
    return ((mock_t *)user)->now;
}

/* Initialize a fresh transaction with all mandatory callbacks. */
static void setup(as608_transaction_t *ctx, mock_t *mock)
{
    const as608_config_t config = {
        .port = { .user = mock, .try_write = write_byte,
                  .try_read = read_byte, .tx_idle = idle, .clock_ms = clock_ms },
        .device_address = AS608_DEFAULT_ADDRESS, .timeout_ms = 100U
    };
    assert(as608_transaction_config_valid(&config));
    as608_transaction_init(ctx, &config);
    as608_transaction_begin(ctx);
}

/* Poll once and enforce the UART callback budget. */
static as608_transaction_result_t step(as608_transaction_t *ctx, mock_t *mock)
{
    const size_t calls = mock->calls;
    const as608_transaction_result_t result = as608_transaction_process(ctx);
    assert(mock->calls - calls <= 1U);
    return result;
}

/* Poll until a terminal transaction event. */
static as608_transaction_result_t run(as608_transaction_t *ctx, mock_t *mock)
{
    for (size_t i = 0U; i < 300U; ++i) {
        const as608_transaction_result_t result = step(ctx, mock);
        if (result.event != AS608_TXN_PENDING) return result;
    }
    assert(0 && "transport stalled");
    return (as608_transaction_result_t){AS608_TXN_FAULT, AS608_STATUS_TIMEOUT};
}

/* Load an independently encoded ACK into the mock RX queue. */
static void prepare_rx(mock_t *mock)
{
    /* Fixed independent wire vector: ACK with one arbitrary payload byte.
     * Sum = 0x07 + 0x00 + 0x03 + 0x42 = 0x004C. */
    const uint8_t bytes[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x07U, 0x00U, 0x03U, 0x42U, 0x00U, 0x4CU
    };
    (void)memcpy(mock->rx, bytes, sizeof(bytes));
    mock->rx_length = sizeof(bytes);
    mock->rx_offset = 0U;
}

/* Check send, receive and drain events. */
static void test_packet_events(void)
{
    mock_t mock = {0};
    as608_transaction_t ctx;
    setup(&ctx, &mock);
    uint8_t data = 0x42U;
    const as608_transaction_frame_t frame = {AS608_PACKET_DATA, &data, 1U};
    assert(as608_transaction_send(&ctx, &frame) == AS608_STATUS_OK);
    data = 0U; /* Send must own a copy. */
    mock.stall = true;
    assert(step(&ctx, &mock).event == AS608_TXN_PENDING);
    assert(mock.tx_length == 0U);
    mock.stall = false;
    assert(run(&ctx, &mock).event == AS608_TXN_SENT);
    assert(mock.tx_length == 12U && mock.tx[9] == 0x42U);
    assert(mock.tx[10] == 0U && mock.tx[11] == 0x47U);

    as608_transaction_receive(&ctx, 1U);
    assert(step(&ctx, &mock).event == AS608_TXN_PENDING);
    prepare_rx(&mock);
    assert(run(&ctx, &mock).event == AS608_TXN_RECEIVED);
    const as608_transaction_frame_t reply = as608_transaction_received(&ctx);
    assert(reply.kind == AS608_PACKET_ACK && reply.length == 1U);
    assert(reply.data[0] == 0x42U); /* No command/ACK interpretation. */
    as608_transaction_drain(&ctx);
    assert(step(&ctx, &mock).event == AS608_TXN_PENDING);
    mock.idle = 1;
    assert(step(&ctx, &mock).event == AS608_TXN_DRAINED);
}

/* Check malformed packets and drain failures. */
static void test_faults(void)
{
    mock_t mock = {0};
    as608_transaction_t ctx;
    setup(&ctx, &mock);
    prepare_rx(&mock);
    mock.rx[11] ^= 1U;
    as608_transaction_receive(&ctx, 1U);
    as608_transaction_result_t result = run(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT &&
           result.status == AS608_STATUS_CHECKSUM_ERROR);

    prepare_rx(&mock);
    as608_transaction_receive(&ctx, 0U);
    result = run(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT &&
           result.status == AS608_STATUS_UNEXPECTED_REPLY);
    assert(mock.rx_offset == 9U);

    as608_transaction_drain(&ctx);
    mock.idle = -1;
    result = step(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
}

/* Check one wrap-safe deadline across phases. */
static void test_shared_deadline(void)
{
    mock_t mock = {.now = UINT32_MAX - 20U};
    as608_transaction_t ctx;
    setup(&ctx, &mock);
    const uint8_t data = 1U;
    const as608_transaction_frame_t frame = {AS608_PACKET_COMMAND, &data, 1U};
    assert(as608_transaction_send(&ctx, &frame) == AS608_STATUS_OK);
    mock.now += 80U;
    assert(run(&ctx, &mock).event == AS608_TXN_SENT);
    as608_transaction_receive(&ctx, 1U);
    mock.now += 20U;
    const size_t calls = mock.calls;
    const as608_transaction_result_t result = step(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_TIMEOUT);
    assert(mock.calls == calls);
}

/* Verify timeout bounds and required callbacks. */
static void test_config_validation(void)
{
    mock_t mock = {0};
    as608_transaction_t txn;
    setup(&txn, &mock);
    as608_config_t config = txn.config;
    config.timeout_ms = 0U;
    assert(!as608_transaction_config_valid(&config));
    config.timeout_ms = AS608_MAX_TIMEOUT_MS + 1U;
    assert(!as608_transaction_config_valid(&config));
    config.timeout_ms = 1U;
    config.port.user = NULL;
    assert(as608_transaction_config_valid(&config));
    config.port.try_read = NULL;
    assert(!as608_transaction_config_valid(&config));
    config.port.try_read = read_byte;
    config.port.try_write = NULL;
    assert(!as608_transaction_config_valid(&config));
    config.port.try_write = write_byte;
    config.port.tx_idle = NULL;
    assert(!as608_transaction_config_valid(&config));
    config.port.tx_idle = idle;
    config.port.clock_ms = NULL;
    assert(!as608_transaction_config_valid(&config));
    assert(!as608_transaction_config_valid(NULL));
    assert(mock.calls == 0U);
}

/* Verify accepted TX is distinct from drain and ordered across frames. */
static void test_port_queue(void) {
    mock_t mock = {.burst = 5U};
    as608_transaction_t txn;
    setup(&txn, &mock);
    uint8_t payload = 0x42U;
    const as608_transaction_frame_t frame = {AS608_PACKET_DATA, &payload, 1U};
    const uint8_t first[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                              2U, 0U, 3U, 0x42U, 0U, 0x47U};
    const uint8_t second[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                               2U, 0U, 3U, 0x99U, 0U, 0x9EU};
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
    assert(step(&txn, &mock).event == AS608_TXN_PENDING);
    assert(mock.tx_length == 5U);
    mock.stall = true;
    assert(step(&txn, &mock).event == AS608_TXN_PENDING);
    assert(mock.tx_length == 5U);
    mock.stall = false;
    assert(run(&txn, &mock).event == AS608_TXN_SENT);
    assert(mock.idle == 0 && mock.tx_length == sizeof(first));

    payload = 0x99U;
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
    assert(run(&txn, &mock).event == AS608_TXN_SENT);
    assert(mock.tx_length == sizeof(first) + sizeof(second));
    assert(memcmp(mock.tx, first, sizeof(first)) == 0);
    assert(memcmp(mock.tx + sizeof(first), second, sizeof(second)) == 0);

    /* A fast RX reply may already be queued before explicit TX drain. */
    prepare_rx(&mock);
    as608_transaction_receive(&txn, 1U);
    assert(run(&txn, &mock).event == AS608_TXN_RECEIVED);
    assert(as608_transaction_received(&txn).data[0] == 0x42U);
    as608_transaction_drain(&txn);
    assert(step(&txn, &mock).event == AS608_TXN_PENDING);
    mock.idle = 1;
    assert(step(&txn, &mock).event == AS608_TXN_DRAINED);
}

/* Verify all UART phases reject negative or overreported callback results. */
static void test_port_faults(void) {
    const int32_t faults[] = {INT32_MIN, -1, INT32_MAX};
    const uint8_t payload = 1U;
    const as608_transaction_frame_t frame = {AS608_PACKET_COMMAND, &payload, 1U};
    for (size_t i = 0U; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        mock_t mock = {.burst = 9U};
        as608_transaction_t txn;
        setup(&txn, &mock);
        assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
        mock.write_fault = faults[i];
        as608_transaction_result_t result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
        assert(mock.tx_length == 0U);
        mock.write_fault = 0;

        prepare_rx(&mock);
        as608_transaction_receive(&txn, 1U);
        mock.read_fault = faults[i];
        result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
        assert(mock.rx_offset == 0U);
        mock.read_fault = 0;

        as608_transaction_receive(&txn, 1U);
        assert(step(&txn, &mock).event == AS608_TXN_PENDING);
        assert(mock.rx_offset == 9U);
        mock.read_fault = faults[i];
        result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
        assert(mock.rx_offset == 9U);
        mock.read_fault = 0;

        as608_transaction_drain(&txn);
        mock.idle = faults[i];
        result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
    }
    mock_t mock = {.idle = 2};
    as608_transaction_t txn;
    setup(&txn, &mock);
    as608_transaction_drain(&txn);
    const as608_transaction_result_t result = step(&txn, &mock);
    assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
}

int main(void)
{
    test_config_validation();
    test_packet_events();
    test_faults();
    test_shared_deadline();
    test_port_queue();
    test_port_faults();
    puts("as608 transaction tests: PASS");
    return 0;
}
