/** @file test_as608_enroll_two_scan.c @brief API 44: paced two-capture enrollment tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 512U
#define AS608_TEST_RX_CAPACITY 256U
#define AS608_TEST_DRAIN 1
#include "tests/common/mock_wire.h"

/* Advance until the entire expected command is on the wire. */
static size_t expect_command(as608_t *ctx, mock_t *mock, size_t pos, const uint8_t *bytes, size_t length) {
    const size_t end = pos + length + 11U;
    for (unsigned int i = 0U; i < 100U && mock->tx_length < end; ++i) {
        assert(test_step(ctx, mock) == AS608_PENDING);
    }
    assert(mock->tx_length == end);
    return test_check_frame(mock, pos, AS608_PACKET_COMMAND, bytes, length);
}

/* Consume one queued ACK without allowing an extra command to transmit. */
static as608_progress_t reply(as608_t *ctx, mock_t *mock, uint8_t ack) {
    test_append_ack(mock, ack);
    as608_progress_t progress = AS608_PENDING;
    for (unsigned int i = 0U; i < 100U && mock->rx_pos < mock->rx_length; ++i) {
        progress = test_step(ctx, mock);
        if (progress != AS608_PENDING) {
            break;
        }
    }
    assert(mock->rx_pos == mock->rx_length);
    return progress;
}

/* Verify a pending interval performs no UART I/O and is wrap-safe. */
static void wait_interval(as608_t *ctx, mock_t *mock, uint32_t interval) {
    const unsigned int io = mock->io_calls;
    assert(test_step(ctx, mock) == AS608_PENDING); /* Arm at current tick. */
    mock->now += interval - 1U;
    for (unsigned int i = 0U; i < 4U; ++i) {
        assert(test_step(ctx, mock) == AS608_PENDING);
    }
    assert(mock->io_calls == io);
    ++mock->now;
    assert(test_step(ctx, mock) == AS608_PENDING); /* Queue, do not transmit. */
    assert(mock->io_calls == io);
}

static const uint8_t capture[] = {0x01U};
static const uint8_t gen1[] = {0x02U, 1U};
static const uint8_t gen2[] = {0x02U, 2U};
static const uint8_t merge[] = {0x05U};
static const uint8_t store[] = {0x06U, 2U, 0x12U, 0x34U};

/* Verify finger placement/removal polling and the exact successful sequence. */
static void test_success(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_enroll_request_t request = {0x1234U, 5U};
    setup(&ctx, &mock);
    mock.now = UINT32_MAX - 2U;
    assert(as608_enroll_two_scan_start(&ctx, &request) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    const uint32_t started = ctx.transaction.started_ms;
    request.page_id = 0U;
    request.poll_interval_ms = 99U;
    size_t pos = expect_command(&ctx, &mock, 0U, capture, sizeof(capture));
    assert(reply(&ctx, &mock, AS608_ACK_NO_FINGER) == AS608_PENDING);
    wait_interval(&ctx, &mock, 5U);
    pos = expect_command(&ctx, &mock, pos, capture, sizeof(capture));
    assert(reply(&ctx, &mock, AS608_ACK_OK) == AS608_PENDING);
    pos = expect_command(&ctx, &mock, pos, gen1, sizeof(gen1));
    assert(reply(&ctx, &mock, AS608_ACK_OK) == AS608_PENDING);
    pos = expect_command(&ctx, &mock, pos, capture, sizeof(capture));
    for (unsigned int i = 0U; i < 2U; ++i) {
        assert(reply(&ctx, &mock, AS608_ACK_OK) == AS608_PENDING);
        wait_interval(&ctx, &mock, 5U);
        pos = expect_command(&ctx, &mock, pos, capture, sizeof(capture));
    }
    assert(reply(&ctx, &mock, AS608_ACK_NO_FINGER) == AS608_PENDING);
    pos = expect_command(&ctx, &mock, pos, capture, sizeof(capture));
    assert(reply(&ctx, &mock, AS608_ACK_NO_FINGER) == AS608_PENDING);
    wait_interval(&ctx, &mock, 5U);
    pos = expect_command(&ctx, &mock, pos, capture, sizeof(capture));
    assert(reply(&ctx, &mock, AS608_ACK_OK) == AS608_PENDING);
    pos = expect_command(&ctx, &mock, pos, gen2, sizeof(gen2));
    assert(reply(&ctx, &mock, AS608_ACK_OK) == AS608_PENDING);
    pos = expect_command(&ctx, &mock, pos, merge, sizeof(merge));
    assert(reply(&ctx, &mock, AS608_ACK_OK) == AS608_PENDING);
    pos = expect_command(&ctx, &mock, pos, store, sizeof(store));
    assert(!as608_get_result(&ctx, &result));
    assert(reply(&ctx, &mock, AS608_ACK_OK) == AS608_COMPLETE);
    assert(ctx.transaction.started_ms == started);
    assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(ctx.state == AS608_STATE_DONE && mock.tx_length == pos);
    const unsigned int io = mock.io_calls;
    assert(test_step(&ctx, &mock) == AS608_COMPLETE && mock.io_calls == io);
    assert(as608_capture_image_start(&ctx) == AS608_STATUS_OK);
}

/* Verify every phase stops on a terminal sensor error without retries. */
static void test_negative(unsigned int phase, uint8_t ack) {
    const uint8_t *const commands[] = {capture, gen1, capture, capture, gen2, merge, store};
    const size_t lengths[] = {1U, 2U, 1U, 1U, 2U, 1U, 4U};
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    const as608_enroll_request_t request = {0x1234U, 5U};
    setup(&ctx, &mock);
    assert(as608_enroll_two_scan_start(&ctx, &request) == AS608_STATUS_OK);
    size_t pos = 0U;
    for (unsigned int i = 0U; i <= phase; ++i) {
        pos = expect_command(&ctx, &mock, pos, commands[i], lengths[i]);
        if (i < phase) {
            assert(reply(&ctx, &mock, i == 2U ? AS608_ACK_NO_FINGER : AS608_ACK_OK) == AS608_PENDING);
        }
    }
    assert(reply(&ctx, &mock, ack) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_OK && result.confirmation == ack);
    assert(ctx.state == AS608_STATE_DONE && mock.tx_length == pos);
    assert(test_step(&ctx, &mock) == AS608_COMPLETE && mock.tx_length == pos);
}

/* Verify a rejected start does not modify the active context. */
static void assert_rejected(as608_t *ctx, const as608_enroll_request_t *request, as608_status_t expected) {
    unsigned char saved[sizeof(*ctx)];
    (void)memcpy(saved, ctx, sizeof(saved));
    assert(as608_enroll_two_scan_start(ctx, request) == expected);
    assert(memcmp(saved, ctx, sizeof(saved)) == 0);
}

/* Verify validation, busy admission and deadline expiration before another poll. */
static void test_wait_timeout(bool arm) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_enroll_request_t request = {0U, 5U};
    assert(as608_enroll_two_scan_start(NULL, &request) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, &request, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, NULL, AS608_STATUS_INVALID_ARG);
    request.poll_interval_ms = 0U;
    assert_rejected(&ctx, &request, AS608_STATUS_INVALID_ARG);
    request.poll_interval_ms = UINT32_MAX;
    assert_rejected(&ctx, &request, AS608_STATUS_INVALID_ARG);
    request.poll_interval_ms = 5U;
    mock.now = UINT32_MAX - 49U;
    assert(as608_enroll_two_scan_start(&ctx, &request) == AS608_STATUS_OK);
    assert_rejected(&ctx, &request, AS608_STATUS_BUSY);
    const size_t pos = expect_command(&ctx, &mock, 0U, capture, sizeof(capture));
    assert(reply(&ctx, &mock, AS608_ACK_NO_FINGER) == AS608_PENDING);
    if (arm) {
        assert(test_step(&ctx, &mock) == AS608_PENDING);
    }
    mock.now = 50U;
    const unsigned int io = mock.io_calls;
    assert(test_step(&ctx, &mock) == AS608_FAILED && mock.io_calls == io);
    assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_TIMEOUT);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED && mock.tx_length == pos);
    assert_rejected(&ctx, &request, AS608_STATUS_RECOVERY_REQUIRED);
}

/* Verify uncertain StoreChar completion is quarantined, never retried. */
static void test_store_fault(unsigned int fault) {
    const uint8_t *const commands[] = {capture, gen1, capture, capture, gen2, merge, store};
    const size_t lengths[] = {1U, 2U, 1U, 1U, 2U, 1U, 4U};
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    const as608_enroll_request_t request = {0x1234U, 5U};
    setup(&ctx, &mock);
    assert(as608_enroll_two_scan_start(&ctx, &request) == AS608_STATUS_OK);
    size_t pos = 0U;
    for (unsigned int i = 0U; i < 7U; ++i) {
        pos = expect_command(&ctx, &mock, pos, commands[i], lengths[i]);
        if (i < 6U) {
            mock.now += 10U; /* A later phase must not extend the original deadline. */
            assert(reply(&ctx, &mock, i == 2U ? AS608_ACK_NO_FINGER : AS608_ACK_OK) == AS608_PENDING);
        }
    }
    as608_status_t expected = AS608_STATUS_TIMEOUT;
    if (fault == 0U) {
        mock.now = 100U;
    } else if (fault == 1U) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
        expected = AS608_STATUS_RECOVERY_REQUIRED;
    } else {
        const uint8_t ack = AS608_ACK_OK;
        test_append_frame(&mock, fault == 2U ? AS608_PACKET_DATA : AS608_PACKET_ACK, &ack, 1U);
        if (fault == 3U) {
            mock.rx[mock.rx_length - 1U] ^= 1U;
        }
        expected = fault == 2U ? AS608_STATUS_UNEXPECTED_REPLY : AS608_STATUS_CHECKSUM_ERROR;
    }
    as608_progress_t progress = AS608_PENDING;
    for (unsigned int i = 0U; i < 100U && progress == AS608_PENDING; ++i) {
        progress = test_step(&ctx, &mock);
    }
    assert(progress == AS608_FAILED);
    assert(as608_get_result(&ctx, &result) && result.status == expected);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED && mock.tx_length == pos);
    assert_rejected(&ctx, &request, AS608_STATUS_RECOVERY_REQUIRED);
    assert(test_step(&ctx, &mock) == AS608_FAILED && mock.tx_length == pos);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_enroll_two_scan_start(&ctx, &request) == AS608_STATUS_OK);
}

int main(void) {
    test_success();
    const uint8_t errors[] = {AS608_ACK_IMAGE_ERROR, AS608_ACK_IMAGE_MESSY,
        AS608_ACK_PACKET_ERROR, AS608_ACK_IMAGE_ERROR, AS608_ACK_FEATURE_TOO_FEW,
        AS608_ACK_MERGE_ERROR, AS608_ACK_FLASH_ERROR};
    for (unsigned int i = 0U; i < sizeof(errors); ++i) {
        test_negative(i, errors[i]);
    }
    test_negative(1U, AS608_ACK_NO_FINGER);
    test_negative(5U, 0x42U);
    test_wait_timeout(false);
    test_wait_timeout(true);
    for (unsigned int fault = 0U; fault < 4U; ++fault) {
        test_store_fault(fault);
    }
    puts("as608 API 44 enroll_two_scan: PASS");
    return 0;
}
