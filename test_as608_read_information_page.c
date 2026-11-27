/** @file test_as608_read_information_page.c @brief API 10: information-page stream tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 16U
#define AS608_TEST_RX_CAPACITY 800U
#define AS608_TEST_DRAIN 0
#include "tests/common/mock_wire.h"

static uint8_t pattern(size_t offset) {
    return (uint8_t)(offset * 13U + 7U);
}

static void append_data(mock_t *mock, uint8_t kind, size_t offset, size_t length) {
    uint8_t data[256U];

    assert(length <= sizeof(data));
    for (size_t i = 0U; i < length; ++i) {
        data[i] = pattern(offset + i);
    }

    test_append_frame(mock, kind, data, length);
}

static as608_progress_t run(as608_t *ctx, mock_t *mock) {
    for (unsigned int i = 0U; i < 1500U; ++i) {
        const as608_progress_t progress = test_step(ctx, mock);

        if (progress != AS608_PENDING) {
            return progress;
        }
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

static void assert_output(const uint8_t *out, size_t prefix) {
    for (size_t i = 0U; i < AS608_INFORMATION_PAGE_BYTES; ++i) {
        assert(out[i] == ((i < prefix) ? pattern(i) : 0xA5U));
    }
}

static void test_success(size_t chunk) {
    static const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x01U, 0x00U, 0x03U, 0x16U, 0x00U, 0x1AU
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t out[AS608_INFORMATION_PAGE_BYTES + 2U];

    setup(&ctx, &mock);
    (void)memset(out, 0xA5, sizeof(out));
    test_append_ack(&mock, AS608_ACK_OK);

    for (size_t offset = 0U; offset < AS608_INFORMATION_PAGE_BYTES; offset += chunk) {
        const uint8_t kind = (offset + chunk == AS608_INFORMATION_PAGE_BYTES) ?
            AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        append_data(&mock, kind, offset, chunk);
    }

    assert(as608_read_information_page_start(&ctx, out + 1U, sizeof(out) - 1U) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert_output(out + 1U, 0U);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert_output(out + 1U, AS608_INFORMATION_PAGE_BYTES);
    assert(out[0] == 0xA5U && out[sizeof(out) - 1U] == 0xA5U);
    assert(mock.tx_length == sizeof(command));
    assert(memcmp(mock.tx, command, sizeof(command)) == 0);
}

enum fault_case {
    EARLY_END,
    MISSING_END,
    EXCESS_DATA,
    EMPTY_DATA,
    WRONG_KIND,
    BAD_CHECKSUM,
    TIMEOUT_AFTER_DATA
};

static void test_stream_fault(enum fault_case fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t out[AS608_INFORMATION_PAGE_BYTES];
    as608_status_t expected = AS608_STATUS_UNEXPECTED_REPLY;

    setup(&ctx, &mock);
    (void)memset(out, 0xA5, sizeof(out));
    test_append_ack(&mock, AS608_ACK_OK);
    append_data(&mock, AS608_PACKET_DATA, 0U, 256U);

    switch (fault) {
        case EARLY_END: {
            append_data(&mock, AS608_PACKET_END_DATA, 256U, 128U);
            break;
        }
        case MISSING_END: {
            append_data(&mock, AS608_PACKET_DATA, 256U, 256U);
            break;
        }
        case EXCESS_DATA: {
            /* Leave only 128 bytes of room for the last packet. */
            append_data(&mock, AS608_PACKET_DATA, 256U, 128U);
            append_data(&mock, AS608_PACKET_END_DATA, 384U, 256U);
            break;
        }
        case EMPTY_DATA: {
            append_data(&mock, AS608_PACKET_DATA, 256U, 0U);
            break;
        }
        case WRONG_KIND: {
            test_append_ack(&mock, AS608_ACK_OK);
            break;
        }
        case BAD_CHECKSUM: {
            append_data(&mock, AS608_PACKET_END_DATA, 256U, 256U);
            mock.rx[mock.rx_length - 1U] ^= 1U;
            expected = AS608_STATUS_CHECKSUM_ERROR;
            break;
        }
        case TIMEOUT_AFTER_DATA: {
            expected = AS608_STATUS_TIMEOUT;
            break;
        }
    }

    assert(as608_read_information_page_start(&ctx, out, sizeof(out)) == AS608_STATUS_OK);
    if (fault == TIMEOUT_AFTER_DATA) {
        for (unsigned int i = 0U; (i < 500U) && (ctx.stream_offset == 0U); ++i) {
            assert(test_step(&ctx, &mock) == AS608_PENDING);
        }
        assert(ctx.stream_offset == 256U);
        mock.now = 100U;
    }

    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert_output(out, (fault == EXCESS_DATA) ? 384U : 256U);
}

static void test_admission_and_negative_ack(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t out[AS608_INFORMATION_PAGE_BYTES];
    (void)memset(out, 0xA5, sizeof(out));

    assert(as608_read_information_page_start(NULL, out, sizeof(out)) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_information_page_start(&ctx, out, sizeof(out)) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_read_information_page_start(&ctx, NULL, sizeof(out)) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_information_page_start(&ctx, out, sizeof(out) - 1U) == AS608_STATUS_INVALID_ARG);
    test_append_ack(&mock, AS608_ACK_UPLOAD_ERROR);
    assert(as608_read_information_page_start(&ctx, out, sizeof(out)) == AS608_STATUS_OK);
    assert(as608_read_information_page_start(&ctx, out, sizeof(out)) == AS608_STATUS_BUSY);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_UPLOAD_ERROR);
    assert_output(out, 0U);

    assert(as608_read_information_page_start(&ctx, out, sizeof(out)) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(as608_read_information_page_start(&ctx, out, sizeof(out)) == AS608_STATUS_RECOVERY_REQUIRED);
    assert_output(out, 0U);
}

int main(void) {
    test_success(32U);
    test_success(64U);
    test_success(128U);
    test_success(256U);
    test_stream_fault(EARLY_END);
    test_stream_fault(MISSING_END);
    test_stream_fault(EXCESS_DATA);
    test_stream_fault(EMPTY_DATA);
    test_stream_fault(WRONG_KIND);
    test_stream_fault(BAD_CHECKSUM);
    test_stream_fault(TIMEOUT_AFTER_DATA);
    test_admission_and_negative_ack();

    puts("as608 API 10 read_information_page: PASS");
    return 0;
}
