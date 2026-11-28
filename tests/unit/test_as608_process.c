/** @file test_as608_process.c @brief API 02: bounded simple-ACK progression. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[16U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_offset;
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
    bool stall;
    bool fail_write;
} mock_t;

static int32_t write_byte(void *user, const uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->io_calls;
    if (m->fail_write) return -1;
    if (m->stall) return 0;
    assert(length > 0U && m->tx_length < sizeof(m->tx));
    m->tx[m->tx_length++] = *data;
    return 1;
}

static int32_t read_byte(void *user, uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->io_calls;
    assert(length > 0U);
    if (m->rx_offset == m->rx_length) return 0;
    *data = m->rx[m->rx_offset++];
    return 1;
}

static int32_t tx_idle(void *user)
{
    mock_t *m = user;
    ++m->io_calls;
    return 1;
}

static uint32_t clock_ms(void *user)
{
    mock_t *m = user;
    ++m->clock_calls;
    return m->now;
}

static void setup(as608_t *ctx, mock_t *mock)
{
    const as608_config_t config = {
        .port = {
            .user = mock, .try_write = write_byte, .try_read = read_byte,
            .tx_idle = tx_idle, .clock_ms = clock_ms
        },
        .device_address = AS608_DEFAULT_ADDRESS,
        .timeout_ms = 100U
    };
    *ctx = (as608_t){0};
    assert(as608_init(ctx, &config) == AS608_STATUS_OK);
}

/* Admit a real public job instead of constructing a private step. */
static void arm(as608_t *ctx) {
    assert(as608_capture_image_start(ctx) == AS608_STATUS_OK);
}

static void prepare_reply(mock_t *mock, uint8_t kind, uint8_t ack)
{
    /* Independent fixed wire format, one payload byte and checksum. */
    uint8_t reply[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        kind, 0x00U, 0x03U, ack, 0x00U, 0x00U
    };
    const uint16_t checksum = (uint16_t)(kind + 3U + ack);
    reply[10] = (uint8_t)(checksum >> 8U);
    reply[11] = (uint8_t)checksum;
    (void)memcpy(mock->rx, reply, sizeof(reply));
    mock->rx_length = sizeof(reply);
    mock->rx_offset = 0U;
}

static as608_progress_t step(as608_t *ctx, mock_t *mock)
{
    const unsigned int io = mock->io_calls;
    const unsigned int clocks = mock->clock_calls;
    const as608_progress_t result = as608_process(ctx);
    assert(mock->io_calls - io <= 1U);
    assert(mock->clock_calls - clocks <= 1U);
    return result;
}

static as608_progress_t run(as608_t *ctx, mock_t *mock)
{
    for (unsigned int i = 0U; i < 100U; ++i) {
        const as608_progress_t progress = step(ctx, mock);
        if (progress != AS608_PENDING) return progress;
    }
    assert(0 && "process stalled");
    return AS608_FAILED;
}

static void test_terminal_ack(uint8_t ack)
{
    mock_t mock = {0};
    as608_t ctx;
    setup(&ctx, &mock);
    arm(&ctx);
    mock.stall = true;
    assert(step(&ctx, &mock) == AS608_PENDING);
    assert(mock.tx_length == 0U);
    mock.stall = false;
    for (size_t i = 0U; i < 12U; ++i) {
        assert(step(&ctx, &mock) == AS608_PENDING);
    }
    assert(mock.tx_length == 12U);
    assert(step(&ctx, &mock) == AS608_PENDING); /* RX not ready yet. */
    prepare_reply(&mock, AS608_PACKET_ACK, ack);

    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(ctx.state == AS608_STATE_DONE);
    assert(ctx.result.status == AS608_STATUS_OK);
    assert(ctx.result.confirmation == ack);
    const unsigned int io = mock.io_calls;
    const unsigned int clocks = mock.clock_calls;
    assert(step(&ctx, &mock) == AS608_COMPLETE);
    assert(mock.io_calls == io && mock.clock_calls == clocks);
}

static void test_idle_and_invalid(void)
{
    mock_t mock = {0};
    as608_t ctx = {0};
    assert(as608_process(NULL) == AS608_FAILED);
    assert(as608_process(&ctx) == AS608_FAILED);
    setup(&ctx, &mock);
    assert(step(&ctx, &mock) == AS608_FAILED);
    assert(ctx.state == AS608_STATE_IDLE);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);

    ctx.state = AS608_STATE_DESYNCHRONIZED;
    ctx.result.status = AS608_STATUS_TIMEOUT;
    assert(step(&ctx, &mock) == AS608_FAILED);
    assert(ctx.result.status == AS608_STATUS_TIMEOUT);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
}

static void assert_fault(const as608_t *ctx, as608_status_t status)
{
    assert(ctx->state == AS608_STATE_DESYNCHRONIZED);
    assert(ctx->result.status == status);
}

static void test_wire_faults(void)
{
    mock_t mock = {0};
    as608_t ctx;
    setup(&ctx, &mock);
    arm(&ctx);
    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK);
    mock.rx[11] ^= 1U;
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert_fault(&ctx, AS608_STATUS_CHECKSUM_ERROR);

    mock = (mock_t){0};
    setup(&ctx, &mock);
    arm(&ctx);
    prepare_reply(&mock, AS608_PACKET_DATA, AS608_ACK_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert_fault(&ctx, AS608_STATUS_UNEXPECTED_REPLY);

    mock = (mock_t){0};
    setup(&ctx, &mock);
    arm(&ctx);
    mock.fail_write = true;
    assert(step(&ctx, &mock) == AS608_FAILED);
    assert_fault(&ctx, AS608_STATUS_IO_ERROR);
}

static void test_timeout_and_phase_guard(void)
{
    mock_t mock = {.now = UINT32_MAX - 50U};
    as608_t ctx;
    setup(&ctx, &mock);
    arm(&ctx);
    mock.stall = true;
    mock.now += 99U;
    assert(step(&ctx, &mock) == AS608_PENDING);
    ++mock.now;
    const unsigned int io = mock.io_calls;
    assert(step(&ctx, &mock) == AS608_FAILED);
    assert_fault(&ctx, AS608_STATUS_TIMEOUT);
    assert(mock.io_calls == io);

    mock = (mock_t){0};
    setup(&ctx, &mock);
    arm(&ctx);
    ctx.step = 0U;
    assert(step(&ctx, &mock) == AS608_FAILED);
    assert_fault(&ctx, AS608_STATUS_UNEXPECTED_REPLY);
    assert(mock.io_calls == 0U);
}

int main(void)
{
    test_idle_and_invalid();
    test_terminal_ack(AS608_ACK_OK);
    test_terminal_ack(AS608_ACK_NO_FINGER);
    test_terminal_ack(0x42U); /* Preserve unknown confirmation values. */
    test_wire_faults();
    test_timeout_and_phase_guard();
    puts("as608 API 02 process: PASS");
    return 0;
}
