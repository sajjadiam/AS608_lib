/** @file test_as608_write_notepad.c @brief API 40: full-page write wire and lifecycle tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 45U
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

/* Verify rejected admission preserves the context and does not queue I/O. */
static void assert_rejected(as608_t *ctx, const as608_notepad_page_t *page, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_write_notepad_start(ctx, page) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Construct independent full-page wire expectations. */
static void expected_frame(uint8_t *frame, const as608_notepad_page_t *page) {
    const uint8_t prefix[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 36U, 0x18U};
    (void)memcpy(frame, prefix, sizeof(prefix));
    frame[10] = page->page;
    (void)memcpy(frame + 11U, page->data, 32U);
    uint16_t sum = 0U;
    for (size_t i = 6U; i < 43U; ++i) {
        sum = (uint16_t)(sum + frame[i]);
    }
    frame[43] = (uint8_t)(sum >> 8U);
    frame[44] = (uint8_t)sum;
}

/* Verify every page, exact bytes, snapshot semantics and raw confirmations. */
static void test_wire(void) {
    const uint8_t confirmations[] = {AS608_ACK_OK, AS608_ACK_PACKET_ERROR,
        AS608_ACK_FLASH_ERROR, AS608_ACK_INVALID_NOTE_PAGE, 0x42U};
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    setup(&ctx, &mock);

    for (uint8_t index = 0U; index < 16U; ++index) {
        for (size_t k = 0U; k < sizeof(confirmations); ++k) {
            as608_notepad_page_t page = {index, {0}};
            uint8_t command[45U];
            for (size_t i = 0U; i < sizeof(page.data); ++i) {
                page.data[i] = (index == 0U) ? 0U : (index == 15U) ? 0xFFU :
                    (uint8_t)(i * 17U + index * 3U);
            }
            expected_frame(command, &page);
            if (index == 0U) {
                assert(command[43] == 0U && command[44] == 0x3DU);
            } else if (index == 15U) {
                assert(command[43] == 0x20U && command[44] == 0x2CU);
            }
            mock.tx_length = 0U;
            mock.io_calls = 0U;
            mock.clock_calls = 0U;
            test_prepare_ack(&mock, confirmations[k]);

            assert(as608_write_notepad_start(&ctx, &page) == AS608_STATUS_OK);
            assert(mock.io_calls == 0U && mock.clock_calls == 1U);
            assert(!as608_get_result(&ctx, &result));
            assert(page.page == index && memcmp(page.data, command + 11U, 32U) == 0);
            assert_rejected(&ctx, &page, AS608_STATUS_BUSY);
            (void)memset(&page, 0xA5, sizeof(page));
            assert(run(&ctx, &mock) == AS608_COMPLETE);
            assert(as608_get_result(&ctx, &result));
            assert(result.status == AS608_STATUS_OK && result.confirmation == confirmations[k]);
            assert(mock.tx_length == sizeof(command) && memcmp(mock.tx, command, sizeof(command)) == 0);
            const unsigned int io = mock.io_calls;
            assert(as608_process(&ctx) == AS608_COMPLETE && mock.io_calls == io);
        }
    }
}

/* Verify malformed or missing ACKs do not cause another flash write. */
static void test_fault(unsigned int fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    const as608_notepad_page_t page = {15U, {0}};
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
    assert(as608_write_notepad_start(&ctx, &page) == AS608_STATUS_OK);
    if (fault >= 3U) {
        for (unsigned int i = 0U; i < 100U && mock.tx_length < 45U; ++i) {
            assert(as608_process(&ctx) == AS608_PENDING);
        }
        assert(mock.tx_length == 45U);
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
    assert_rejected(&ctx, &page, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED && mock.tx_length == 45U);
}

/* Verify invalid pages, initialization and external recovery. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_notepad_page_t page = {0U, {0}};
    assert(as608_write_notepad_start(NULL, &page) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, &page, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, NULL, AS608_STATUS_INVALID_ARG);
    page.page = 16U;
    assert_rejected(&ctx, &page, AS608_STATUS_INVALID_ARG);
    page.page = 255U;
    assert_rejected(&ctx, &page, AS608_STATUS_INVALID_ARG);
    page.page = 15U;
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    assert(as608_write_notepad_start(&ctx, &page) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, &page, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_write_notepad_start(&ctx, &page) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U);
}

int main(void) {
    test_wire();
    for (unsigned int i = 0U; i < 5U; ++i) {
        test_fault(i);
    }
    test_admission();
    puts("as608 API 40 write_notepad: PASS");
    return 0;
}
