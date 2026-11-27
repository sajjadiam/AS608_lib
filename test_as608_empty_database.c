/** @file test_as608_empty_database.c @brief API 23: database clear wire and lifecycle tests. */
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

/* Verify a rejected database clear preserves the entire context. */
static void assert_rejected(as608_t *ctx, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_empty_database_start(ctx) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Exercise sensor confirmations and successive jobs from DONE. */
static void test_empty_results(void) {
    static const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x01U, 0x00U, 0x03U, 0x0DU, 0x00U, 0x11U
    };
    const uint8_t confirmations[] = {
        AS608_ACK_EMPTY_ERROR, AS608_ACK_PACKET_ERROR,
        AS608_ACK_OK, 0x42U
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);

    for (size_t i = 0U; i < sizeof(confirmations); ++i) {
        mock.tx_length = 0U;
        mock.io_calls = 0U;
        mock.clock_calls = 0U;
        test_prepare_ack(&mock, confirmations[i]);

        assert(as608_empty_database_start(&ctx) == AS608_STATUS_OK);
        assert(ctx.state == AS608_STATE_BUSY);
        assert(mock.io_calls == 0U && mock.clock_calls == 1U);
        assert(!as608_get_result(&ctx, &result));
        assert_rejected(&ctx, AS608_STATUS_BUSY);
        assert(run(&ctx, &mock) == AS608_COMPLETE);
        assert(as608_get_result(&ctx, &result));
        assert(result.status == AS608_STATUS_OK);
        assert(result.confirmation == confirmations[i]);
        assert(mock.tx_length == sizeof(command));
        assert(memcmp(mock.tx, command, sizeof(command)) == 0);
        const unsigned int io = mock.io_calls;
        assert(as608_process(&ctx) == AS608_COMPLETE);
        assert(mock.io_calls == io);
    }
}

/* Verify communication faults remain distinct from sensor rejections. */
static void test_faults(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    assert(as608_empty_database_start(NULL) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    test_prepare_ack(&mock, AS608_ACK_OK);
    mock.rx[11] ^= 1U;
    assert(as608_empty_database_start(&ctx) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_CHECKSUM_ERROR);
    assert_rejected(&ctx, AS608_STATUS_RECOVERY_REQUIRED);

    /* The fixture represents external physical recovery. */
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    mock.tx_length = 0U;
    mock.rx_length = 0U;
    mock.rx_pos = 0U;
    assert(as608_empty_database_start(&ctx) == AS608_STATUS_OK);
    for (unsigned int i = 0U; i < 40U; ++i) {
        assert(as608_process(&ctx) == AS608_PENDING);
    }

    assert(mock.tx_length == 12U);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert_rejected(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(mock.tx_length == 12U);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    mock.tx_length = 0U;
    assert(as608_empty_database_start(&ctx) == AS608_STATUS_OK);

    for (unsigned int i = 0U; i < 40U; ++i) {
        assert(as608_process(&ctx) == AS608_PENDING);
    }

    assert(mock.tx_length == 12U);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(mock.tx_length == 12U);
}

int main(void) {
    test_empty_results();
    test_faults();

    puts("as608 API 23 empty_database: PASS");
    return 0;
}
