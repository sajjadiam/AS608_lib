/** @file test_as608_verify_password.c @brief API 06: PS_VfyPwd wire and lifecycle tests. */
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

static void assert_rejected(as608_t *ctx, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_verify_password_start(ctx, 0U) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

static void test_command_and_reuse(void) {
    static const uint8_t nonzero_frame[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
        0x07U, 0x13U, 0x12U, 0x34U, 0x56U, 0x78U, 0x01U, 0x2FU
    };
    static const uint8_t zero_frame[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
        0x07U, 0x13U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1BU
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);
    assert(as608_verify_password_start(&ctx, UINT32_C(0x12345678)) == AS608_STATUS_OK);
    assert(ctx.state == AS608_STATE_BUSY);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert(!as608_get_result(&ctx, &result));
    assert_rejected(&ctx, AS608_STATUS_BUSY);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);

    prepare_ack(&mock, AS608_ACK_PASSWORD_ERROR);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(mock.tx_length == sizeof(nonzero_frame));
    assert(memcmp(mock.tx, nonzero_frame, sizeof(nonzero_frame)) == 0);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK);
    assert(result.confirmation == AS608_ACK_PASSWORD_ERROR);

    mock.tx_length = 0U;
    prepare_ack(&mock, AS608_ACK_OK);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(!as608_get_result(&ctx, &result));
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(mock.tx_length == sizeof(zero_frame));
    assert(memcmp(mock.tx, zero_frame, sizeof(zero_frame)) == 0);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
}

static void test_admission_and_timeout(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    assert(as608_verify_password_start(NULL, 0U) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_OK);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert_rejected(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
    assert(mock.io_calls == 0U);

    /* Test fixture represents external physical recovery. */
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
}

int main(void) {
    test_command_and_reuse();
    test_admission_and_timeout();

    puts("as608 API 06 verify_password: PASS");
    return 0;
}
