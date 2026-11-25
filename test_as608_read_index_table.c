/** @file test_as608_read_index_table.c @brief API 09: index-table wire and buffer tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[45U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_pos;
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
} mock_t;

static int32_t write_byte(void *user, const uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U) && (mock->tx_length < sizeof(mock->tx)));
    ++mock->io_calls;
    mock->tx[mock->tx_length++] = *data;
    return 1;
}

static int32_t read_byte(void *user, uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U));
    ++mock->io_calls;

    if (mock->rx_pos == mock->rx_length) {
        return 0;
    }

    *data = mock->rx[mock->rx_pos++];
    return 1;
}

static int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    return 1;
}

static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

static void setup(as608_t *ctx, mock_t *mock) {
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

static void prepare_reply(mock_t *mock, uint8_t kind, uint8_t ack, size_t length) {
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind, 0x00U, (uint8_t)(length + 2U)
    };

    assert((length >= 1U) && (length <= 34U));
    (void)memcpy(mock->rx, header, sizeof(header));
    mock->rx[9] = ack;

    for (size_t i = 1U; i < length; ++i) {
        mock->rx[9U + i] = (uint8_t)(i * 7U);
    }

    uint16_t sum = 0U;
    for (size_t i = 6U; i < 9U + length; ++i) {
        sum = (uint16_t)(sum + mock->rx[i]);
    }

    mock->rx[9U + length] = (uint8_t)(sum >> 8U);
    mock->rx[10U + length] = (uint8_t)sum;
    mock->rx_length = 11U + length;
    mock->rx_pos = 0U;
}

static void assert_pattern(const uint8_t *buffer, size_t length, uint8_t value) {
    for (size_t i = 0U; i < length; ++i) {
        assert(buffer[i] == value);
    }
}

static as608_progress_t run(as608_t *ctx, mock_t *mock, const uint8_t *out, size_t length) {
    for (unsigned int i = 0U; i < 100U; ++i) {
        const unsigned int io = mock->io_calls;
        const as608_progress_t progress = as608_process(ctx);
        assert(mock->io_calls - io <= 1U);

        if (progress != AS608_PENDING) {
            return progress;
        }

        assert_pattern(out, length, 0xA5U);
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

static void test_success(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t out[34U];

    setup(&ctx, &mock);

    for (uint8_t page = 0U; page <= 3U; ++page) {
        const uint8_t command[] = {
            0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
            0x04U, 0x1FU, page, 0x00U, (uint8_t)(0x24U + page)
        };

        mock.tx_length = 0U;
        mock.io_calls = 0U;
        mock.clock_calls = 0U;
        (void)memset(out, 0xA5, sizeof(out));
        prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, 33U);

        /* Test exactly 32 bytes and a larger caller buffer with guards. */
        const size_t capacity = (page == 0U) ? 32U : 33U;
        assert(as608_read_index_table_start(&ctx, page, out + 1U, capacity) == AS608_STATUS_OK);
        assert(ctx.state == AS608_STATE_BUSY);
        assert(mock.io_calls == 0U && mock.clock_calls == 1U);
        assert_pattern(out, sizeof(out), 0xA5U);
        assert(run(&ctx, &mock, out, sizeof(out)) == AS608_COMPLETE);
        assert(as608_get_result(&ctx, &result));
        assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
        assert(memcmp(out + 1U, mock.rx + 10U, 32U) == 0);
        assert(out[0] == 0xA5U && out[33] == 0xA5U);
        assert(mock.tx_length == sizeof(command));
        assert(memcmp(mock.tx, command, sizeof(command)) == 0);
    }
}

static void test_reply(uint8_t kind, uint8_t ack, size_t length, bool corrupt, as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    uint8_t out[32U];
    as608_result_t result;

    (void)memset(out, 0xA5, sizeof(out));
    setup(&ctx, &mock);
    prepare_reply(&mock, kind, ack, length);

    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }

    assert(as608_read_index_table_start(&ctx, 1U, out, sizeof(out)) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, out, sizeof(out)) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);

    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack);
    }

    assert_pattern(out, sizeof(out), 0xA5U);
}

static void test_admission_and_abort(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    uint8_t out[32U];
    uint8_t other[32U];
    as608_result_t result;

    (void)memset(out, 0xA5, sizeof(out));
    (void)memset(other, 0xA5, sizeof(other));
    assert(as608_read_index_table_start(NULL, 0U, out, sizeof(out)) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_index_table_start(&ctx, 0U, out, sizeof(out)) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_read_index_table_start(&ctx, 4U, out, sizeof(out)) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_index_table_start(&ctx, UINT8_MAX, out, sizeof(out)) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_index_table_start(&ctx, 0U, NULL, sizeof(out)) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_index_table_start(&ctx, 0U, out, 31U) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_index_table_start(&ctx, 0U, out, 0U) == AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    assert(ctx.state == AS608_STATE_IDLE);

    assert(as608_read_index_table_start(&ctx, 0U, out, sizeof(out)) == AS608_STATUS_OK);
    assert(as608_read_index_table_start(&ctx, 1U, other, sizeof(other)) == AS608_STATUS_BUSY);
    assert(ctx.reply_output == out);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(as608_read_index_table_start(&ctx, 0U, out, sizeof(out)) == AS608_STATUS_RECOVERY_REQUIRED);
    assert_pattern(out, sizeof(out), 0xA5U);
    assert_pattern(other, sizeof(other), 0xA5U);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_read_index_table_start(&ctx, 0U, out, sizeof(out)) == AS608_STATUS_OK);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert_pattern(out, sizeof(out), 0xA5U);
}

int main(void) {
    test_success();
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 1U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 33U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 32U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 34U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_DATA, AS608_ACK_OK, 33U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 33U, true, AS608_STATUS_CHECKSUM_ERROR);
    test_admission_and_abort();

    puts("as608 API 09 read_index_table: PASS");
    return 0;
}
