/** @file test_as608_control_other_port.c @brief API 35: other-port control wire and admission tests. */
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

/* Verify rejected admission preserves the complete context. */
static void assert_rejected(as608_t *ctx, bool enabled, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_control_other_port_start(ctx, enabled) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify both control codes, exact command bytes and raw confirmations. */
static void test_port_results(void) {
    const bool states[] = {false, true};
    const uint8_t confirmations[] = {
        AS608_ACK_OK, AS608_ACK_PACKET_ERROR, AS608_ACK_PORT_ERROR, 0x42U
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);

    for (size_t n = 0U; n < sizeof(states) / sizeof(states[0]); ++n) {
        const bool enabled = states[n];
        const uint8_t command[] = {
            0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
            0x01U, 0x00U, 0x04U, 0x17U, enabled ? 1U : 0U,
            0x00U, enabled ? 0x1DU : 0x1CU
        };

        for (size_t i = 0U; i < sizeof(confirmations) / sizeof(confirmations[0]); ++i) {
            mock.tx_length = 0U;
            mock.io_calls = 0U;
            mock.clock_calls = 0U;
            prepare_ack(&mock, confirmations[i]);

            assert(as608_control_other_port_start(&ctx, enabled) == AS608_STATUS_OK);
            assert(ctx.state == AS608_STATE_BUSY);
            assert(mock.io_calls == 0U && mock.clock_calls == 1U);
            assert(!as608_get_result(&ctx, &result));
            assert_rejected(&ctx, !enabled, AS608_STATUS_BUSY);
            assert(run(&ctx, &mock) == AS608_COMPLETE);
            assert(as608_get_result(&ctx, &result));
            assert(result.status == AS608_STATUS_OK && result.confirmation == confirmations[i]);
            assert(mock.tx_length == sizeof(command));
            assert(memcmp(mock.tx, command, sizeof(command)) == 0);

            const unsigned int io = mock.io_calls;
            const unsigned int clocks = mock.clock_calls;
            assert(as608_process(&ctx) == AS608_COMPLETE);
            assert(mock.io_calls == io && mock.clock_calls == clocks);
        }
    }
}

/* Verify initialization, recovery and terminal transport faults. */
static void test_admission_and_faults(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    assert(as608_control_other_port_start(NULL, true) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, false, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);

    prepare_ack(&mock, AS608_ACK_OK);
    mock.rx[11] ^= 1U;
    assert(as608_control_other_port_start(&ctx, true) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_CHECKSUM_ERROR && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert_rejected(&ctx, false, AS608_STATUS_RECOVERY_REQUIRED);

    /* The fixture represents external physical recovery. */
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    mock.tx_length = 0U;
    mock.rx_length = 0U;
    mock.rx_pos = 0U;
    assert(as608_control_other_port_start(&ctx, false) == AS608_STATUS_OK);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert_rejected(&ctx, true, AS608_STATUS_RECOVERY_REQUIRED);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_control_other_port_start(&ctx, true) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, false, AS608_STATUS_RECOVERY_REQUIRED);
}

/* Reject a verified packet with the wrong kind or an oversized ACK header. */
static void test_malformed_reply(bool wrong_kind) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);
    prepare_ack(&mock, AS608_ACK_OK);

    if (wrong_kind) {
        mock.rx[6] = AS608_PACKET_DATA;
        mock.rx[11] = (uint8_t)(AS608_PACKET_DATA + 3U);
    } else {
        mock.rx[8] = 4U;
    }

    assert(as608_control_other_port_start(&ctx, true) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_UNEXPECTED_REPLY && ctx.state == AS608_STATE_DESYNCHRONIZED);
}

int main(void) {
    test_port_results();
    test_admission_and_faults();
    test_malformed_reply(true);
    test_malformed_reply(false);

    puts("as608 API 35 control_other_port: PASS");
    return 0;
}
