/** @file test_as608_async_faults.c
 * @brief Deterministic fault and partial-I/O regression tests for the
 * cooperative UART state machine, using the production packet codec.
 */
#include "as608_async.h"
#include "as608_packet.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { MOCK_CAPACITY = 4096U };

typedef struct {
    uint8_t rx[MOCK_CAPACITY];
    uint8_t tx[MOCK_CAPACITY];
    size_t rx_length;
    size_t rx_visible;
    size_t rx_pos;
    size_t tx_length;
    size_t chunk;
    size_t write_calls;
    size_t read_calls;
    uint32_t now;
    bool stall_tx;
    bool stall_rx;
    bool fail_tx;
    bool fail_rx;
} mock_t;

static int32_t try_write(void *user, const uint8_t *data, size_t length)
{
    mock_t *mock = user;
    ++mock->write_calls;
    if (mock->fail_tx) return -1;
    if (mock->stall_tx) return 0;
    if (length > mock->chunk) length = mock->chunk;
    assert(mock->tx_length + length <= sizeof(mock->tx));
    (void)memcpy(mock->tx + mock->tx_length, data, length);
    mock->tx_length += length;
    return (int32_t)length;
}

static int32_t try_read(void *user, uint8_t *data, size_t length)
{
    mock_t *mock = user;
    ++mock->read_calls;
    if (mock->fail_rx) return -1;
    if (mock->stall_rx) return 0;
    const size_t available = mock->rx_visible - mock->rx_pos;
    if (length > available) length = available;
    if (length > mock->chunk) length = mock->chunk;
    if (length != 0U) {
        (void)memcpy(data, mock->rx + mock->rx_pos, length);
    }
    mock->rx_pos += length;
    return (int32_t)length;
}

static uint32_t clock_ms(void *user)
{
    return ((mock_t *)user)->now;
}

static void setup(as608_async_t *ctx, mock_t *mock, uint32_t timeout)
{
    *ctx = (as608_async_t){0};
    const as608_async_config_t config = {
        .user = mock,
        .try_write = try_write,
        .try_read = try_read,
        .clock_ms = clock_ms,
        .device_address = AS608_DEFAULT_ADDRESS,
        .timeout_ms = timeout
    };
    assert(as608_async_init(ctx, &config) == AS608_STATUS_OK);
}

static void append_frame(mock_t *mock, as608_packet_kind_t kind,
                         const uint8_t *data, size_t length)
{
    const as608_packet_header_t header = {
        AS608_DEFAULT_ADDRESS, (uint8_t)kind, (uint16_t)length
    };
    assert(mock->rx_length + AS608_PACKET_OVERHEAD_BYTES + length <=
           sizeof(mock->rx));
    uint8_t *destination = mock->rx + mock->rx_length;
    assert(as608_packet_encode_header(&header, destination) ==
           AS608_STATUS_OK);
    mock->rx_length += AS608_PACKET_HEADER_BYTES;
    if (length != 0U) {
        (void)memcpy(mock->rx + mock->rx_length, data, length);
        mock->rx_length += length;
    }
    const uint16_t checksum = as608_packet_checksum_update(
        as608_packet_checksum_begin(&header), data, length);
    as608_packet_checksum_encode(checksum, mock->rx + mock->rx_length);
    mock->rx_length += AS608_PACKET_CHECKSUM_BYTES;
}

static void append_ack(mock_t *mock, uint8_t code)
{
    append_frame(mock, AS608_PACKET_ACK, &code, 1U);
}

static as608_async_progress_t advance(as608_async_t *ctx, mock_t *mock,
                                      size_t budget)
{
    as608_async_progress_t progress = AS608_ASYNC_PENDING;
    for (size_t i = 0U; i < budget; ++i) {
        progress = as608_async_process(ctx);
        if (progress != AS608_ASYNC_PENDING) return progress;
        ++mock->now;
    }
    return progress;
}

static void expect_failure(as608_async_t *ctx, as608_status_t expected)
{
    as608_result_t result = {0};
    assert(ctx->state == AS608_ASYNC_DESYNCHRONIZED);
    assert(as608_async_get_result(ctx, &result));
    assert(result.status == expected);
    assert(as608_async_capture_start(ctx) ==
           AS608_STATUS_RECOVERY_REQUIRED);
}

static void test_one_byte_io_and_delayed_ack(void)
{
    mock_t mock = {.chunk = 1U};
    as608_async_t ctx;
    setup(&ctx, &mock, 1000U);
    append_ack(&mock, AS608_ACK_OK);
    assert(as608_async_capture_start(&ctx) == AS608_STATUS_OK);

    /* Command is transmitted but its ACK is not yet visible to the driver. */
    assert(advance(&ctx, &mock, 40U) == AS608_ASYNC_PENDING);
    assert(ctx.state == AS608_ASYNC_BUSY);
    assert(mock.tx_length == 12U);
    assert(mock.rx_pos == 0U);

    /* The existing partial-read state resumes without re-sending the command. */
    mock.rx_visible = mock.rx_length;
    assert(advance(&ctx, &mock, 100U) == AS608_ASYNC_COMPLETE);
    assert(mock.tx_length == 12U);
    assert(mock.read_calls > 10U);
    assert(mock.write_calls >= 12U);
    assert(ctx.result.status == AS608_STATUS_OK);
    assert(ctx.result.confirmation == AS608_ACK_OK);
}

static void test_stalled_tx_timeout_and_late_reply(void)
{
    mock_t mock = {.chunk = 16U, .stall_tx = true};
    as608_async_t ctx;
    setup(&ctx, &mock, 25U);
    assert(as608_async_capture_start(&ctx) == AS608_STATUS_OK);
    assert(advance(&ctx, &mock, 26U) == AS608_ASYNC_FAILED);
    expect_failure(&ctx, AS608_STATUS_TIMEOUT);
    assert(mock.tx_length == 0U);

    /* A later ACK must not revive the quarantined transaction. */
    append_ack(&mock, AS608_ACK_OK);
    mock.rx_visible = mock.rx_length;
    mock.stall_tx = false;
    const size_t before = mock.read_calls;
    assert(as608_async_process(&ctx) == AS608_ASYNC_FAILED);
    assert(mock.read_calls == before);
    assert(as608_async_abort(&ctx) == AS608_STATUS_RECOVERY_REQUIRED);
}

static void test_incomplete_reply_times_out(void)
{
    mock_t mock = {.chunk = 8U};
    as608_async_t ctx;
    setup(&ctx, &mock, 50U);
    append_ack(&mock, AS608_ACK_OK);
    mock.rx_visible = 10U; /* Nine header bytes and one payload byte, no sum. */
    assert(as608_async_capture_start(&ctx) == AS608_STATUS_OK);
    assert(advance(&ctx, &mock, 52U) == AS608_ASYNC_FAILED);
    expect_failure(&ctx, AS608_STATUS_TIMEOUT);
    assert(mock.rx_pos == 10U);
}

static void test_port_faults(void)
{
    mock_t tx = {.chunk = 16U, .fail_tx = true};
    as608_async_t a;
    setup(&a, &tx, 100U);
    assert(as608_async_capture_start(&a) == AS608_STATUS_OK);
    assert(as608_async_process(&a) == AS608_ASYNC_FAILED);
    expect_failure(&a, AS608_STATUS_IO_ERROR);

    mock_t rx = {.chunk = 16U, .fail_rx = true};
    as608_async_t b;
    setup(&b, &rx, 100U);
    assert(as608_async_capture_start(&b) == AS608_STATUS_OK);
    assert(as608_async_process(&b) == AS608_ASYNC_PENDING);
    assert(as608_async_process(&b) == AS608_ASYNC_FAILED);
    expect_failure(&b, AS608_STATUS_IO_ERROR);
}

static void test_checksum_fault_and_invalid_header(void)
{
    mock_t checksum = {.chunk = 16U};
    as608_async_t a;
    setup(&a, &checksum, 100U);
    append_ack(&checksum, AS608_ACK_OK);
    checksum.rx[checksum.rx_length - 1U] ^= 0x80U;
    checksum.rx_visible = checksum.rx_length;
    assert(as608_async_capture_start(&a) == AS608_STATUS_OK);
    assert(advance(&a, &checksum, 15U) == AS608_ASYNC_FAILED);
    expect_failure(&a, AS608_STATUS_CHECKSUM_ERROR);

    mock_t header = {.chunk = 16U};
    as608_async_t b;
    setup(&b, &header, 100U);
    append_ack(&header, AS608_ACK_OK);
    header.rx[0] = 0x00U;
    header.rx_visible = header.rx_length;
    assert(as608_async_capture_start(&b) == AS608_STATUS_OK);
    assert(advance(&b, &header, 15U) == AS608_ASYNC_FAILED);
    expect_failure(&b, AS608_STATUS_FRAME_ERROR);
}

static void test_template_capacity_overrun(void)
{
    mock_t mock = {.chunk = 128U};
    as608_async_t ctx;
    setup(&ctx, &mock, 1000U);
    append_ack(&mock, AS608_ACK_OK); /* LoadChar */
    append_ack(&mock, AS608_ACK_OK); /* UpChar */
    uint8_t segment[128U];
    (void)memset(segment, 0x7DU, sizeof(segment));
    for (size_t i = 0U; i < 6U; ++i) {
        append_frame(&mock, i == 5U ? AS608_PACKET_END_DATA :
                     AS608_PACKET_DATA, segment, sizeof(segment));
    }
    mock.rx_visible = mock.rx_length;
    uint8_t guarded[514U];
    (void)memset(guarded, 0xA5U, sizeof(guarded));
    as608_template_buffer_t output = {guarded + 1U, 512U, 47U};
    assert(as608_async_read_template_start(&ctx, 1U, &output) ==
           AS608_STATUS_OK);
    assert(advance(&ctx, &mock, 200U) == AS608_ASYNC_FAILED);
    expect_failure(&ctx, AS608_STATUS_UNEXPECTED_REPLY);
    assert(guarded[0] == 0xA5U && guarded[513U] == 0xA5U);
    assert(output.length == 47U);
}

static void test_template_stream_missing_end(void)
{
    mock_t mock = {.chunk = 128U};
    as608_async_t ctx;
    setup(&ctx, &mock, 100U);
    append_ack(&mock, AS608_ACK_OK); /* LoadChar */
    append_ack(&mock, AS608_ACK_OK); /* UpChar */
    uint8_t segment[128U] = {0};
    for (size_t i = 0U; i < 4U; ++i) {
        append_frame(&mock, AS608_PACKET_DATA, segment, sizeof(segment));
    }
    mock.rx_visible = mock.rx_length;
    uint8_t buffer[768U] = {0};
    as608_template_buffer_t output = {buffer, sizeof(buffer), 23U};
    assert(as608_async_read_template_start(&ctx, 0U, &output) ==
           AS608_STATUS_OK);
    assert(advance(&ctx, &mock, 105U) == AS608_ASYNC_FAILED);
    expect_failure(&ctx, AS608_STATUS_TIMEOUT);
    assert(output.length == 23U);
}

static void test_abort_quarantines_without_reading_reply(void)
{
    mock_t mock = {.chunk = 8U};
    as608_async_t ctx;
    setup(&ctx, &mock, 200U);
    assert(as608_async_capture_start(&ctx) == AS608_STATUS_OK);
    assert(as608_async_abort(&ctx) == AS608_STATUS_OK);
    expect_failure(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
    append_ack(&mock, AS608_ACK_OK);
    mock.rx_visible = mock.rx_length;
    assert(as608_async_process(&ctx) == AS608_ASYNC_FAILED);
    assert(mock.rx_pos == 0U);
}

static void test_wraparound_timeout(void)
{
    mock_t mock = {.chunk = 16U, .now = UINT32_MAX - 5U};
    as608_async_t ctx;
    setup(&ctx, &mock, 12U);
    assert(as608_async_capture_start(&ctx) == AS608_STATUS_OK);
    assert(advance(&ctx, &mock, 14U) == AS608_ASYNC_FAILED);
    expect_failure(&ctx, AS608_STATUS_TIMEOUT);
}

int main(void)
{
    test_one_byte_io_and_delayed_ack();
    test_stalled_tx_timeout_and_late_reply();
    test_incomplete_reply_times_out();
    test_port_faults();
    test_checksum_fault_and_invalid_header();
    test_template_capacity_overrun();
    test_template_stream_missing_end();
    test_abort_quarantines_without_reading_reply();
    test_wraparound_timeout();
    puts("as608 async fault tests: PASS");
    return 0;
}
