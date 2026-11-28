/** @file test_as608_set_security_level.c @brief API 33: security register wire and admission tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 16U
#define AS608_TEST_RX_CAPACITY 12U
#define AS608_TEST_DRAIN 0
#include "tests/common/mock_wire.h"

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
static void assert_rejected(as608_t *ctx, as608_security_level_t level, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_set_security_level_start(ctx, level) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify every supported security level, wire checksum and raw confirmations. */
static void test_register_results(void) {
    const uint8_t confirmations[] = {
        AS608_ACK_OK, AS608_ACK_PACKET_ERROR, AS608_ACK_INVALID_REGISTER,
        AS608_ACK_INVALID_REG_VALUE, 0x42U
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);

    const as608_security_level_t levels[] = {
        AS608_SECURITY_LEVEL_1, AS608_SECURITY_LEVEL_2, AS608_SECURITY_LEVEL_3,
        AS608_SECURITY_LEVEL_4, AS608_SECURITY_LEVEL_5
    };

    for (size_t n = 0U; n < sizeof(levels) / sizeof(levels[0]); ++n) {
        const as608_security_level_t level = levels[n];
        const uint8_t command[] = {
            0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
            0x01U, 0x00U, 0x05U, 0x0EU, 0x05U, (uint8_t)level,
            0x00U, (uint8_t)(0x19U + (uint8_t)level)
        };

        for (size_t i = 0U; i < sizeof(confirmations); ++i) {
            mock.tx_length = 0U;
            mock.io_calls = 0U;
            mock.clock_calls = 0U;
            test_prepare_ack(&mock, confirmations[i]);

            assert(as608_set_security_level_start(&ctx, level) == AS608_STATUS_OK);
            assert(ctx.state == AS608_STATE_BUSY);
            assert(mock.io_calls == 0U && mock.clock_calls == 1U);
            assert(!as608_get_result(&ctx, &result));
            assert_rejected(&ctx, AS608_SECURITY_LEVEL_3, AS608_STATUS_BUSY);
            assert(run(&ctx, &mock) == AS608_COMPLETE);
            assert(as608_get_result(&ctx, &result));
            assert(result.status == AS608_STATUS_OK && result.confirmation == confirmations[i]);
            assert(mock.tx_length == sizeof(command));
            assert(memcmp(mock.tx, command, sizeof(command)) == 0);
            assert_rejected(&ctx, (as608_security_level_t)6, AS608_STATUS_INVALID_ARG);
        }
    }
}

/* Verify invalid levels, recovery and terminal transport faults. */
static void test_admission_and_faults(void) {
    const as608_security_level_t invalid[] = {
        (as608_security_level_t)-1, (as608_security_level_t)0,
        (as608_security_level_t)6, (as608_security_level_t)255,
        (as608_security_level_t)256, (as608_security_level_t)257,
        (as608_security_level_t)261
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    assert(as608_set_security_level_start(NULL, AS608_SECURITY_LEVEL_3) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, AS608_SECURITY_LEVEL_3, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);

    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert_rejected(&ctx, invalid[i], AS608_STATUS_INVALID_ARG);
    }

    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    test_prepare_ack(&mock, AS608_ACK_OK);
    mock.rx[11] ^= 1U;
    assert(as608_set_security_level_start(&ctx, AS608_SECURITY_LEVEL_3) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_CHECKSUM_ERROR && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert_rejected(&ctx, AS608_SECURITY_LEVEL_3, AS608_STATUS_RECOVERY_REQUIRED);

    /* The fixture represents external physical recovery. */
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    mock.tx_length = 0U;
    mock.rx_length = 0U;
    mock.rx_pos = 0U;
    assert(as608_set_security_level_start(&ctx, AS608_SECURITY_LEVEL_5) == AS608_STATUS_OK);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert_rejected(&ctx, AS608_SECURITY_LEVEL_5, AS608_STATUS_RECOVERY_REQUIRED);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_set_security_level_start(&ctx, AS608_SECURITY_LEVEL_1) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, AS608_SECURITY_LEVEL_1, AS608_STATUS_RECOVERY_REQUIRED);
}

/* Reject a verified packet with the wrong kind or an oversized ACK header. */
static void test_malformed_reply(bool wrong_kind) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);
    test_prepare_ack(&mock, AS608_ACK_OK);

    if (wrong_kind) {
        mock.rx[6] = AS608_PACKET_DATA;
        mock.rx[11] = (uint8_t)(AS608_PACKET_DATA + 3U);
    } else {
        mock.rx[8] = 4U;
    }

    assert(as608_set_security_level_start(&ctx, AS608_SECURITY_LEVEL_3) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_UNEXPECTED_REPLY && ctx.state == AS608_STATE_DESYNCHRONIZED);
}

int main(void) {
    test_register_results();
    test_admission_and_faults();
    test_malformed_reply(true);
    test_malformed_reply(false);

    puts("as608 API 33 set_security_level: PASS");
    return 0;
}
