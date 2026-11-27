/** @file test_as608_write_user_gpio.c @brief API 42: GPIO command and echoed state tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 16U
#define AS608_TEST_RX_CAPACITY 17U
#define AS608_TEST_DRAIN 0
#include "tests/common/mock_uart.h"

/* Build independent ACK bytes, including malformed lengths. */
static void prepare_reply(mock_t *mock, uint8_t kind, uint8_t ack, uint8_t level, size_t length) {
    const uint8_t header[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind, 0U, (uint8_t)(length + 2U)};
    const uint8_t payload[] = {ack, level, 0U};
    assert(length <= sizeof(payload));
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

/* Poll with bounded I/O while checking that the request remains unchanged. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, const as608_gpio_request_t *request,
                            const unsigned char *before) {
    for (unsigned int i = 0U; i < 100U; ++i) {
        const unsigned int io = mock->io_calls;
        const as608_progress_t progress = as608_process(ctx);
        assert(mock->io_calls - io <= 1U);
        if (progress != AS608_PENDING) {
            return progress;
        }
        assert(memcmp(request, before, sizeof(*request)) == 0);
    }
    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Verify all pin, requested-level and echoed-level combinations. */
static void test_success(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    setup(&ctx, &mock);
    for (uint8_t pin = 0U; pin < 2U; ++pin) {
        for (uint8_t level = 0U; level < 2U; ++level) {
            for (uint8_t echo = 0U; echo < 2U; ++echo) {
                as608_gpio_request_t request = {(as608_user_gpio_t)pin, level != 0U, echo == 0U};
                unsigned char before[sizeof(request)];
                (void)memcpy(before, &request, sizeof(before));
                const uint8_t command[] = {
                    0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                    1U, 0U, 5U, 0x1EU, pin, level, 0U, (uint8_t)(0x24U + pin + level)
                };
                mock.tx_length = 0U;
                mock.io_calls = 0U;
                mock.clock_calls = 0U;
                prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, echo, 2U);
                assert(as608_write_user_gpio_start(&ctx, &request) == AS608_STATUS_OK);
                assert(ctx.reply_output == &request.actual_level);
                assert(mock.io_calls == 0U && mock.clock_calls == 1U);
                assert(!as608_get_result(&ctx, &result));
                assert(memcmp(before, &request, sizeof(before)) == 0);
                assert(run(&ctx, &mock, &request, before) == AS608_COMPLETE);
                assert(as608_get_result(&ctx, &result));
                assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
                assert(request.pin == (as608_user_gpio_t)pin && request.level == (level != 0U));
                assert(request.actual_level == (echo != 0U));
                assert(mock.tx_length == sizeof(command) && memcmp(mock.tx, command, sizeof(command)) == 0);
            }
        }
    }
}

/* Verify negative and malformed replies preserve the entire request. */
static void test_reply(uint8_t kind, uint8_t ack, uint8_t level, size_t length,
                       bool corrupt, as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_gpio_request_t request = {AS608_USER_GPIO_1, true, false};
    unsigned char before[sizeof(request)];
    as608_result_t result;
    (void)memcpy(before, &request, sizeof(before));
    setup(&ctx, &mock);
    prepare_reply(&mock, kind, ack, level, length);
    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }
    assert(as608_write_user_gpio_start(&ctx, &request) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &request, before) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result) && result.status == expected);
    assert(ctx.state == ((expected == AS608_STATUS_OK) ? AS608_STATE_DONE : AS608_STATE_DESYNCHRONIZED));
    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack);
    }
    assert(memcmp(before, &request, sizeof(before)) == 0);
}

/* Verify rejected admission leaves the complete context unchanged. */
static void assert_rejected(as608_t *ctx, as608_gpio_request_t *request, as608_status_t expected) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_write_user_gpio_start(ctx, request) == expected);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify admission, partial reply timeout and abort without retry. */
static void test_fault(bool abort_job) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_gpio_request_t request = {AS608_USER_GPIO_0, true, false};
    as608_gpio_request_t other = {AS608_USER_GPIO_1, false, true};
    unsigned char before[sizeof(request)];
    (void)memcpy(before, &request, sizeof(before));
    assert(as608_write_user_gpio_start(NULL, &request) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, &request, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, NULL, AS608_STATUS_INVALID_ARG);
    const as608_user_gpio_t invalid[] = {(as608_user_gpio_t)-1, (as608_user_gpio_t)2,
                                         (as608_user_gpio_t)256, (as608_user_gpio_t)257};
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        as608_gpio_request_t bad = {invalid[i], false, false};
        assert_rejected(&ctx, &bad, AS608_STATUS_INVALID_ARG);
    }
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, 1U, 2U);
    --mock.rx_length;
    assert(as608_write_user_gpio_start(&ctx, &request) == AS608_STATUS_OK);
    assert_rejected(&ctx, &other, AS608_STATUS_BUSY);
    for (unsigned int i = 0U; i < 100U && mock.rx_pos < mock.rx_length; ++i) {
        assert(as608_process(&ctx) == AS608_PENDING);
        assert(memcmp(before, &request, sizeof(before)) == 0);
    }
    assert(mock.rx_pos == mock.rx_length && mock.tx_length == 14U);
    if (abort_job) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
    } else {
        mock.now = 100U;
    }
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == (abort_job ? AS608_STATUS_RECOVERY_REQUIRED : AS608_STATUS_TIMEOUT));
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(memcmp(before, &request, sizeof(before)) == 0 && other.actual_level);
    assert_rejected(&ctx, &request, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED && mock.tx_length == 14U);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_write_user_gpio_start(&ctx, &request) == AS608_STATUS_OK);
}

int main(void) {
    test_success();
    const uint8_t negative[] = {AS608_ACK_PACKET_ERROR, AS608_ACK_INVALID_REGISTER, 0x42U};
    for (size_t i = 0U; i < sizeof(negative); ++i) {
        test_reply(AS608_PACKET_ACK, negative[i], 1U, 1U, false, AS608_STATUS_OK);
        test_reply(AS608_PACKET_ACK, negative[i], 0xFFU, 2U, false, AS608_STATUS_OK);
    }
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 1U, 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 1U, 3U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 2U, 2U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 255U, 2U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_DATA, AS608_ACK_OK, 1U, 2U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 1U, 2U, true, AS608_STATUS_CHECKSUM_ERROR);
    test_fault(false);
    test_fault(true);
    puts("as608 API 42 write_user_gpio: PASS");
    return 0;
}
