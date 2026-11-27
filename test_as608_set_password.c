/** @file test_as608_set_password.c @brief API 37: PS_SetPwd wire and lifecycle tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 16U
#define AS608_TEST_RX_CAPACITY 12U
#define AS608_TEST_DRAIN 0
#include "tests/common/mock_wire.h"

/* Poll within the cooperative I/O budget. */
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
static void assert_rejected(as608_t *ctx, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_set_password_start(ctx, 0U) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify independent wire vectors, full-width values and raw ACKs. */
static void test_wire(void) {
    const uint32_t passwords[] = {0U, UINT32_C(0x12345678), UINT32_MAX, UINT32_C(0x80000001)};
    const uint8_t frames[][16] = {
        {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 7U, 0x12U, 0U, 0U, 0U, 0U, 0U, 0x1AU},
        {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 7U, 0x12U, 0x12U, 0x34U, 0x56U, 0x78U, 1U, 0x2EU},
        {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 7U, 0x12U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 4U, 0x16U},
        {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 7U, 0x12U, 0x80U, 0U, 0U, 1U, 0U, 0x9BU}
    };
    const uint8_t confirmations[] = {AS608_ACK_OK, AS608_ACK_PACKET_ERROR, AS608_ACK_FLASH_ERROR, AS608_ACK_PASSWORD_ERROR, 0x42U};
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    setup(&ctx, &mock);

    for (size_t p = 0U; p < sizeof(passwords) / sizeof(passwords[0]); ++p) {
        for (size_t i = 0U; i < sizeof(confirmations); ++i) {
            mock.tx_length = 0U;
            mock.io_calls = 0U;
            mock.clock_calls = 0U;
            test_prepare_ack(&mock, confirmations[i]);
            assert(as608_set_password_start(&ctx, passwords[p]) == AS608_STATUS_OK);
            assert(mock.io_calls == 0U && mock.clock_calls == 1U);
            assert(!as608_get_result(&ctx, &result));
            assert_rejected(&ctx, AS608_STATUS_BUSY);
            assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_BUSY);
            assert(run(&ctx, &mock) == AS608_COMPLETE);
            assert(as608_get_result(&ctx, &result));
            assert(result.status == AS608_STATUS_OK && result.confirmation == confirmations[i]);
            assert(mock.tx_length == sizeof(frames[p]));
            assert(memcmp(mock.tx, frames[p], sizeof(frames[p])) == 0);
            const unsigned int io = mock.io_calls;
            assert(as608_process(&ctx) == AS608_COMPLETE && mock.io_calls == io);
        }
    }

    /* Verification is a separate job, not an automatic second command. */
    mock.tx_length = 0U;
    test_prepare_ack(&mock, AS608_ACK_OK);
    assert(as608_verify_password_start(&ctx, UINT32_C(0x80000001)) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(mock.tx_length == 16U && mock.tx[9] == 0x13U && mock.tx[15] == 0x9CU);
    assert(memcmp(mock.tx + 10U, frames[3] + 10U, 4U) == 0);
}

/* Verify lost or malformed replies after transmission never trigger a retry. */
static void test_fault(unsigned int fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_status_t expected = AS608_STATUS_UNEXPECTED_REPLY;
    setup(&ctx, &mock);
    if (fault < 3U) {
        test_prepare_ack(&mock, AS608_ACK_OK);
        if (fault == 0U) {
            mock.rx[11] ^= 1U;
            expected = AS608_STATUS_CHECKSUM_ERROR;
        } else if (fault == 1U) {
            mock.rx[6] = AS608_PACKET_DATA;
            mock.rx[11] = (uint8_t)(AS608_PACKET_DATA + 3U);
        } else {
            mock.rx[8] = 4U;
        }
    }
    assert(as608_set_password_start(&ctx, UINT32_MAX) == AS608_STATUS_OK);
    if (fault >= 3U) {
        for (unsigned int i = 0U; i < 100U && mock.tx_length < 16U; ++i) {
            assert(as608_process(&ctx) == AS608_PENDING);
        }
        assert(mock.tx_length == 16U);
        if (fault == 3U) {
            mock.now = 100U;
            expected = AS608_STATUS_TIMEOUT;
        } else {
            assert(as608_abort(&ctx) == AS608_STATUS_OK);
            expected = AS608_STATUS_RECOVERY_REQUIRED;
        }
    }
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(mock.tx_length == 16U);
    assert_rejected(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED && mock.tx_length == 16U);
}

/* Verify initialization and external recovery admission rules. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    assert(as608_set_password_start(NULL, 0U) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_set_password_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_set_password_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U);
}

int main(void) {
    test_wire();
    for (unsigned int i = 0U; i < 5U; ++i) {
        test_fault(i);
    }
    test_admission();
    puts("as608 API 37 set_password: PASS");
    return 0;
}
