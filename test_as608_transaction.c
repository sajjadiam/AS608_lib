/** @file test_as608_transaction.c @brief Independent transport tests. */
#include "as608_transaction.h"

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
} mock_t;

static int32_t write_byte(void *user, const uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->calls;
    if (m->stall) return 0;
    assert(length != 0U && m->tx_length < sizeof(m->tx));
    m->tx[m->tx_length++] = *data;
    return 1;
}

static int32_t read_byte(void *user, uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->calls;
    assert(length != 0U);
    if (m->rx_offset == m->rx_length) return 0;
    *data = m->rx[m->rx_offset++];
    return 1;
}

static int32_t idle(void *user)
{
    mock_t *m = user;
    ++m->calls;
    return m->idle;
}

static uint32_t clock_ms(void *user)
{
    return ((mock_t *)user)->now;
}

static void setup(as608_t *ctx, mock_t *mock)
{
    *ctx = (as608_t){0};
    ctx->config = (as608_config_t){
        .port = { .user = mock, .try_write = write_byte,
                  .try_read = read_byte, .tx_idle = idle, .clock_ms = clock_ms },
        .device_address = AS608_DEFAULT_ADDRESS, .timeout_ms = 100U
    };
    as608_transaction_begin(ctx);
}

static as608_transaction_result_t step(as608_t *ctx, mock_t *mock)
{
    const size_t calls = mock->calls;
    const as608_transaction_result_t result = as608_transaction_process(ctx);
    assert(mock->calls - calls <= 1U);
    return result;
}

static as608_transaction_result_t run(as608_t *ctx, mock_t *mock)
{
    for (size_t i = 0U; i < 300U; ++i) {
        const as608_transaction_result_t result = step(ctx, mock);
        if (result.event != AS608_TXN_PENDING) return result;
    }
    assert(0 && "transport stalled");
    return (as608_transaction_result_t){AS608_TXN_FAULT, AS608_STATUS_TIMEOUT};
}

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

static void test_packet_events(void)
{
    mock_t mock = {0};
    as608_t ctx;
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

static void test_faults(void)
{
    mock_t mock = {0};
    as608_t ctx;
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

static void test_shared_deadline(void)
{
    mock_t mock = {.now = UINT32_MAX - 20U};
    as608_t ctx;
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

int main(void)
{
    test_packet_events();
    test_faults();
    test_shared_deadline();
    puts("as608 transaction tests: PASS");
    return 0;
}
