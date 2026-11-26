/** @file test_as608_get_random_code.c @brief API 38: random code wire and output tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[17U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_pos;
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
} mock_t;

/* Accept one TX byte into the mock wire buffer. */
static int32_t write_byte(void *user, const uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U) && (mock->tx_length < sizeof(mock->tx)));
    ++mock->io_calls;
    mock->tx[mock->tx_length++] = *data;
    return 1;
}

/* Return one available RX byte or report no data. */
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

/* Report the mock transmitter idle. */
static int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    return 1;
}

/* Return the mock clock and count accesses. */
static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

/* Initialize a fresh device context. */
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

/* Build an independent reply and checksum. */
static void prepare_reply(mock_t *mock, uint8_t kind, uint8_t ack, uint32_t value, size_t length) {
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind, 0x00U, (uint8_t)(length + 2U)
    };
    const uint8_t payload[] = {ack, (uint8_t)(value >> 24U), (uint8_t)(value >> 16U),
                               (uint8_t)(value >> 8U), (uint8_t)value, 0U};

    assert((length >= 1U) && (length <= sizeof(payload)));
    (void)memcpy(mock->rx, header, sizeof(header));
    (void)memcpy(mock->rx + sizeof(header), payload, length);

    uint16_t sum = 0U;
    for (size_t i = 6U; i < 9U + length; ++i) {
        sum = (uint16_t)(sum + mock->rx[i]);
    }

    mock->rx[9U + length] = (uint8_t)(sum >> 8U);
    mock->rx[10U + length] = (uint8_t)sum;
    mock->rx_length = 11U + length;
    mock->rx_pos = 0U;
}

/* Poll with bounded I/O and verify output stays unchanged while pending. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, const uint32_t *out, uint32_t before) {
    for (unsigned int i = 0U; i < 100U; ++i) {
        const unsigned int io = mock->io_calls;
        const as608_progress_t progress = as608_process(ctx);
        assert(mock->io_calls - io <= 1U);

        if (progress != AS608_PENDING) {
            return progress;
        }

        assert(*out == before);
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Verify wire bytes and full-width decoded results, including repeats. */
static void test_success(void) {
    static const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
        0x03U, 0x14U, 0x00U, 0x18U
    };
    const uint32_t values[] = {0U, 1U, UINT32_C(0x12345678), UINT32_MAX,
                               UINT32_C(0x80000001), UINT32_C(0x12345678)};
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);

    for (size_t i = 0U; i < sizeof(values) / sizeof(values[0]); ++i) {
        uint32_t out = UINT32_C(0xA55A5AA5);
        mock.tx_length = 0U;
        mock.io_calls = 0U;
        mock.clock_calls = 0U;

        prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, values[i], 5U);
        assert(as608_get_random_code_start(&ctx, &out) == AS608_STATUS_OK);
        assert(ctx.state == AS608_STATE_BUSY && out == UINT32_C(0xA55A5AA5));
        assert(mock.io_calls == 0U && mock.clock_calls == 1U);
        assert(!as608_get_result(&ctx, &result));
        assert(run(&ctx, &mock, &out, UINT32_C(0xA55A5AA5)) == AS608_COMPLETE);
        assert(as608_get_result(&ctx, &result));
        assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
        assert(out == values[i]);
        assert(mock.tx_length == sizeof(command));
        assert(memcmp(mock.tx, command, sizeof(command)) == 0);
    }
}

/* Check negative and malformed replies preserve the output. */
static void test_reply(uint8_t kind, uint8_t ack, size_t length, bool corrupt, as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    uint32_t out = UINT32_C(0xA55A5AA5);
    as608_result_t result;

    setup(&ctx, &mock);
    prepare_reply(&mock, kind, ack, UINT32_C(0x89ABCDEF), length);

    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }

    assert(as608_get_random_code_start(&ctx, &out) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &out, UINT32_C(0xA55A5AA5)) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);
    assert(ctx.state == ((expected == AS608_STATUS_OK) ? AS608_STATE_DONE : AS608_STATE_DESYNCHRONIZED));

    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack);
    }

    assert(out == UINT32_C(0xA55A5AA5));
}

/* Verify admission, abort, timeout and recovery rules. */
static void test_admission_and_abort(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    uint32_t out = 123U;
    uint32_t other = 456U;
    as608_result_t result;

    assert(as608_get_random_code_start(NULL, &out) == AS608_STATUS_INVALID_ARG);
    assert(as608_get_random_code_start(&ctx, &out) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_get_random_code_start(&ctx, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_get_random_code_start(&ctx, &out) == AS608_STATUS_OK);
    assert(as608_get_random_code_start(&ctx, &other) == AS608_STATUS_BUSY);
    assert(ctx.reply_output == &out);
    assert(mock.clock_calls == 1U && mock.io_calls == 0U);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(as608_get_random_code_start(&ctx, &out) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(out == 123U && other == 456U);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_get_random_code_start(&ctx, &out) == AS608_STATUS_OK);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert(out == 123U);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
}

int main(void) {
    test_success();
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 1U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 5U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, 0x42U, 1U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, 0x42U, 5U, false, AS608_STATUS_OK);
    for (size_t length = 1U; length < 5U; ++length) {
        test_reply(AS608_PACKET_ACK, AS608_ACK_OK, length, false, AS608_STATUS_UNEXPECTED_REPLY);
    }
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 6U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 3U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_DATA, AS608_ACK_OK, 5U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 5U, true, AS608_STATUS_CHECKSUM_ERROR);
    test_admission_and_abort();

    puts("as608 API 38 get_random_code: PASS");
    return 0;
}
