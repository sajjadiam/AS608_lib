/** @file test_as608_delete_templates.c @brief API 22: range deletion wire and lifecycle tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[12U];
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

/* Return the mock clock and count clock accesses. */
static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

/* Initialize a device with the mock port. */
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

/* Construct an independent one-byte ACK frame. */
static void prepare_ack(mock_t *mock, uint8_t ack) {
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U, 0x00U, 0x03U
    };
    const uint16_t sum = (uint16_t)(0x07U + 0x03U + ack);

    (void)memcpy(mock->rx, header, sizeof(header));
    mock->rx[9] = ack;
    mock->rx[10] = (uint8_t)(sum >> 8U);
    mock->rx[11] = (uint8_t)sum;
    mock->rx_pos = 0U;
    mock->rx_length = sizeof(mock->rx);
}

/* Poll the public process API with a bounded test loop. */
static as608_progress_t run(as608_t *ctx, mock_t *mock) {
    for (unsigned int i = 0U; i < 100U; ++i) {
        const unsigned int io = mock->io_calls;
        const as608_progress_t progress = as608_process(ctx);
        assert(mock->io_calls - io <= 1U);

        if (progress != AS608_PENDING) {
            return progress;
        }
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Verify rejected admission preserves the entire context. */
static void assert_rejected(as608_t *ctx, uint16_t start, uint16_t count, as608_status_t expected) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_delete_templates_start(ctx, start, count) == expected);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify exact big-endian fields, range boundaries and raw ACKs. */
static void test_results(void) {
    const uint16_t ranges[][2] = {
        {0U, 1U}, {299U, 1U}, {0U, 300U}, {0x1234U, 0x0102U},
        {UINT16_MAX, 1U}, {1U, UINT16_MAX}, {0U, UINT16_MAX}
    };
    const uint8_t confirmations[] = {
        AS608_ACK_OK, AS608_ACK_PACKET_ERROR, AS608_ACK_DELETE_ERROR, 0x42U
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);

    for (size_t i = 0U; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        const uint16_t start = ranges[i][0];
        const uint16_t count = ranges[i][1];
        uint8_t command[] = {
            0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
            0x07U, 0x0CU, (uint8_t)(start >> 8U), (uint8_t)start,
            (uint8_t)(count >> 8U), (uint8_t)count, 0U, 0U
        };
        uint16_t sum = 0U;

        for (size_t n = 6U; n < 14U; ++n) {
            sum = (uint16_t)(sum + command[n]);
        }

        command[14] = (uint8_t)(sum >> 8U);
        command[15] = (uint8_t)sum;

        for (size_t a = 0U; a < sizeof(confirmations); ++a) {
            mock.tx_length = 0U;
            mock.io_calls = 0U;
            mock.clock_calls = 0U;
            prepare_ack(&mock, confirmations[a]);

            assert(as608_delete_templates_start(&ctx, start, count) == AS608_STATUS_OK);
            assert(ctx.state == AS608_STATE_BUSY);
            assert(mock.io_calls == 0U && mock.clock_calls == 1U);
            assert(!as608_get_result(&ctx, &result));
            assert_rejected(&ctx, 0U, 1U, AS608_STATUS_BUSY);
            assert(run(&ctx, &mock) == AS608_COMPLETE);
            assert(as608_get_result(&ctx, &result));
            assert(result.status == AS608_STATUS_OK && result.confirmation == confirmations[a]);
            assert(mock.tx_length == sizeof(command));
            assert(memcmp(mock.tx, command, sizeof(command)) == 0);
            assert_rejected(&ctx, UINT16_MAX, 2U, AS608_STATUS_INVALID_ARG);
        }
    }
}

/* Verify invalid ranges and transport faults never trigger a retry. */
static void test_faults(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    assert(as608_delete_templates_start(NULL, 0U, 1U) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, 0U, 1U, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, 0U, 0U, AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, UINT16_MAX, 0U, AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, UINT16_MAX, 2U, AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, 2U, UINT16_MAX, AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);

    prepare_ack(&mock, AS608_ACK_OK);
    mock.rx[11] ^= 1U;
    assert(as608_delete_templates_start(&ctx, 0U, 1U) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_CHECKSUM_ERROR);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(mock.tx_length == 16U);
    assert_rejected(&ctx, 0U, 1U, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(mock.tx_length == 16U);

    /* The fixture represents external physical recovery. */
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    mock.tx_length = 0U;
    mock.rx_length = 0U;
    mock.rx_pos = 0U;
    assert(as608_delete_templates_start(&ctx, 0U, 1U) == AS608_STATUS_OK);

    for (unsigned int i = 0U; i < 40U; ++i) {
        assert(as608_process(&ctx) == AS608_PENDING);
    }

    assert(mock.tx_length == 16U);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert_rejected(&ctx, 0U, 1U, AS608_STATUS_RECOVERY_REQUIRED);
    assert(mock.tx_length == 16U);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    mock.tx_length = 0U;
    assert(as608_delete_templates_start(&ctx, 0U, 1U) == AS608_STATUS_OK);

    for (unsigned int i = 0U; i < 40U; ++i) {
        assert(as608_process(&ctx) == AS608_PENDING);
    }

    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_RECOVERY_REQUIRED);
    assert_rejected(&ctx, 0U, 1U, AS608_STATUS_RECOVERY_REQUIRED);
    assert(mock.tx_length == 16U);
}

int main(void) {
    test_results();
    test_faults();

    puts("as608 API 22 delete_templates: PASS");
    return 0;
}
