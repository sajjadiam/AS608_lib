/** @file test_as608_read_device_info.c @brief API 36: selective information-page stream tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 16U
#define AS608_TEST_RX_CAPACITY 2048U
#define AS608_TEST_DRAIN 0
#include "tests/common/mock_wire.h"

/* Generate deterministic raw bytes including zeros and high bits. */
static uint8_t pattern(size_t offset) {
    return (uint8_t)(offset * 13U + 7U);
}

/* Append a patterned data fragment. */
static void append_data(mock_t *mock, uint8_t kind, size_t offset, size_t length) {
    uint8_t data[256U];

    assert(length <= sizeof(data));
    for (size_t i = 0U; i < length; ++i) {
        data[i] = pattern(offset + i);
    }

    test_append_frame(mock, kind, data, length);
}

/* Poll until a terminal result within a bounded loop. */
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

/* Append a complete page with configurable fragment boundaries. */
static void append_page(mock_t *mock, size_t chunk) {
    test_append_ack(mock, AS608_ACK_OK);
    for (size_t offset = 0U; offset < AS608_INFORMATION_PAGE_BYTES;) {
        const size_t remaining = AS608_INFORMATION_PAGE_BYTES - offset;
        const size_t count = (remaining < chunk) ? remaining : chunk;
        append_data(mock, count == remaining ? AS608_PACKET_END_DATA : AS608_PACKET_DATA, offset, count);
        offset += count;
    }
}

/* Verify raw byte order and all four independently specified offsets. */
static void assert_info(const as608_device_info_t *info) {
    for (size_t i = 0U; i < 8U; ++i) {
        assert(info->product_model[i] == pattern(28U + i));
        assert(info->software_version[i] == pattern(36U + i));
        assert(info->manufacturer[i] == pattern(44U + i));
        assert(info->sensor_name[i] == pattern(52U + i));
    }
}

/* Verify no output is published while the job is pending. */
static void test_success(size_t chunk) {
    const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x01U, 0x00U, 0x03U, 0x16U, 0x00U, 0x1AU
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    struct {
        uint8_t before;
        as608_device_info_t info;
        uint8_t after;
    } out;
    unsigned char saved[sizeof(out)];

    setup(&ctx, &mock);
    (void)memset(&out, 0xA5, sizeof(out));
    (void)memcpy(saved, &out, sizeof(saved));
    append_page(&mock, chunk);

    assert(as608_read_device_info_start(&ctx, &out.info) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert(memcmp(saved, &out, sizeof(saved)) == 0);
    as608_progress_t progress = AS608_PENDING;
    for (unsigned int i = 0U; (i < 1500U) && (progress == AS608_PENDING); ++i) {
        progress = test_step(&ctx, &mock);
        if (progress == AS608_PENDING) {
            assert(memcmp(saved, &out, sizeof(saved)) == 0);
        }
    }
    assert(progress == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert_info(&out.info);
    assert(out.before == 0xA5U && out.after == 0xA5U);
    assert(mock.rx_pos == mock.rx_length && ctx.stream_offset == 512U);
    assert(mock.tx_length == sizeof(command) && memcmp(mock.tx, command, sizeof(command)) == 0);
}

enum fault_case {
    EARLY_END, MISSING_END, EXCESS_DATA, EMPTY_DATA, WRONG_KIND,
    BAD_CHECKSUM, TIMEOUT_AFTER_DATA, ABORT_AFTER_DATA
};

/* Verify the output remains untouched even after all descriptors arrived. */
static void test_fault(enum fault_case fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_device_info_t info;
    unsigned char saved[sizeof(info)];
    as608_status_t expected = AS608_STATUS_UNEXPECTED_REPLY;

    setup(&ctx, &mock);
    (void)memset(&info, 0xA5, sizeof(info));
    (void)memcpy(saved, &info, sizeof(saved));
    test_append_ack(&mock, AS608_ACK_OK);
    append_data(&mock, AS608_PACKET_DATA, 0U, 256U);
    switch (fault) {
        case EARLY_END: append_data(&mock, AS608_PACKET_END_DATA, 256U, 128U); break;
        case MISSING_END: append_data(&mock, AS608_PACKET_DATA, 256U, 256U); break;
        case EXCESS_DATA:
            append_data(&mock, AS608_PACKET_DATA, 256U, 128U);
            append_data(&mock, AS608_PACKET_END_DATA, 384U, 129U);
            break;
        case EMPTY_DATA: append_data(&mock, AS608_PACKET_END_DATA, 256U, 0U); break;
        case WRONG_KIND: test_append_ack(&mock, AS608_ACK_OK); break;
        case BAD_CHECKSUM:
            append_data(&mock, AS608_PACKET_END_DATA, 256U, 256U);
            mock.rx[mock.rx_length - 1U] ^= 1U;
            expected = AS608_STATUS_CHECKSUM_ERROR;
            break;
        case TIMEOUT_AFTER_DATA: expected = AS608_STATUS_TIMEOUT; break;
        case ABORT_AFTER_DATA: expected = AS608_STATUS_RECOVERY_REQUIRED; break;
    }
    assert(as608_read_device_info_start(&ctx, &info) == AS608_STATUS_OK);
    if ((fault == TIMEOUT_AFTER_DATA) || (fault == ABORT_AFTER_DATA)) {
        for (unsigned int i = 0U; (i < 500U) && (ctx.stream_offset == 0U); ++i) {
            assert(test_step(&ctx, &mock) == AS608_PENDING);
        }
        assert(ctx.stream_offset == 256U);
        assert(memcmp(saved, &info, sizeof(saved)) == 0);
        if (fault == ABORT_AFTER_DATA) {
            assert(as608_abort(&ctx) == AS608_STATUS_OK);
        } else {
            mock.now = 100U;
        }
    }
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(memcmp(saved, &info, sizeof(saved)) == 0);
    assert(as608_read_device_info_start(&ctx, &info) == AS608_STATUS_RECOVERY_REQUIRED);
}

/* Verify admission, negative ACK and alternating raw/decoded page jobs. */
static void test_admission_and_sequence(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_device_info_t info, other;
    unsigned char saved[sizeof(info)], competing[sizeof(other)];
    uint8_t page[AS608_INFORMATION_PAGE_BYTES];

    (void)memset(&info, 0xA5, sizeof(info));
    (void)memset(&other, 0x5A, sizeof(other));
    (void)memcpy(saved, &info, sizeof(saved));
    (void)memcpy(competing, &other, sizeof(competing));
    assert(as608_read_device_info_start(NULL, &info) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_device_info_start(&ctx, &info) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_read_device_info_start(&ctx, NULL) == AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);

    test_append_ack(&mock, AS608_ACK_UPLOAD_ERROR);
    assert(as608_read_device_info_start(&ctx, &info) == AS608_STATUS_OK);
    assert(as608_read_device_info_start(&ctx, &other) == AS608_STATUS_BUSY);
    assert(ctx.reply_output == &info);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_UPLOAD_ERROR);
    assert(memcmp(saved, &info, sizeof(saved)) == 0);
    assert(memcmp(competing, &other, sizeof(competing)) == 0);

    for (unsigned int i = 0U; i < 2U; ++i) {
        mock.tx_length = 0U;
        mock.rx_pos = 0U;
        mock.rx_length = 0U;
        append_page(&mock, 64U);
        assert(as608_read_information_page_start(&ctx, page, sizeof(page)) == AS608_STATUS_OK);
        assert(run(&ctx, &mock) == AS608_COMPLETE);
        for (size_t j = 0U; j < sizeof(page); ++j) {
            assert(page[j] == pattern(j));
        }

        mock.tx_length = 0U;
        mock.rx_pos = 0U;
        mock.rx_length = 0U;
        append_page(&mock, 31U);
        (void)memset(&info, 0xA5, sizeof(info));
        assert(as608_read_device_info_start(&ctx, &info) == AS608_STATUS_OK);
        assert(run(&ctx, &mock) == AS608_COMPLETE);
        assert(as608_get_result(&ctx, &result));
        assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
        assert_info(&info);
        assert(memcmp(competing, &other, sizeof(competing)) == 0);
    }
}

int main(void) {
    const size_t chunks[] = {17U, 31U, 32U, 64U, 128U, 256U};
    for (size_t i = 0U; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        test_success(chunks[i]);
    }
    test_fault(EARLY_END);
    test_fault(MISSING_END);
    test_fault(EXCESS_DATA);
    test_fault(EMPTY_DATA);
    test_fault(WRONG_KIND);
    test_fault(BAD_CHECKSUM);
    test_fault(TIMEOUT_AFTER_DATA);
    test_fault(ABORT_AFTER_DATA);
    test_admission_and_sequence();

    puts("as608 API 36 read_device_info: PASS");
    return 0;
}
