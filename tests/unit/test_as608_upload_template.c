/** @file test_as608_upload_template.c @brief API 17: template upload stream tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 16U
#define AS608_TEST_RX_CAPACITY 1200U
#define AS608_TEST_DRAIN 0
#include "tests/common/mock_wire.h"

/* Generate deterministic data for wire and output checks. */
static uint8_t pattern(size_t offset) {
    return (uint8_t)(offset * 13U + 7U);
}

/* Append a patterned DATA or END_DATA fragment. */
static void append_data(mock_t *mock, uint8_t kind, size_t offset, size_t length) {
    uint8_t data[256U];

    assert(length <= sizeof(data));
    for (size_t i = 0U; i < length; ++i) {
        data[i] = pattern(offset + i);
    }

    test_append_frame(mock, kind, data, length);
}

/* Poll with a bounded iteration count. */
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

/* Verify only the expected prefix was written. */
static void assert_output(const uint8_t *out, size_t capacity, size_t prefix) {
    for (size_t i = 0U; i < capacity; ++i) {
        assert(out[i] == ((i < prefix) ? pattern(i) : 0xA5U));
    }
}

/* Verify the wire request, both supported sizes and guard bytes. */
static void test_success(size_t total, size_t chunk, size_t capacity, as608_char_buffer_t buffer) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[AS608_TEMPLATE_MAX_BYTES + 2U];
    as608_template_buffer_t out = {data + 1U, capacity, 123U};
    const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x01U, 0x00U, 0x04U, 0x08U, (uint8_t)buffer, 0x00U,
        (uint8_t)(0x0DU + (uint8_t)buffer)
    };

    setup(&ctx, &mock);
    (void)memset(data, 0xA5, sizeof(data));
    test_append_ack(&mock, AS608_ACK_OK);

    for (size_t offset = 0U; offset < total; offset += chunk) {
        const uint8_t kind = (offset + chunk == total) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        append_data(&mock, kind, offset, chunk);
    }

    assert(as608_upload_template_start(&ctx, buffer, &out) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert_output(out.data, capacity, 0U);

    as608_progress_t progress = AS608_PENDING;
    for (unsigned int i = 0U; (i < 1500U) && (progress == AS608_PENDING); ++i) {
        assert(out.length == 123U);
        progress = test_step(&ctx, &mock);
    }

    assert(progress == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(out.length == total);
    assert_output(out.data, AS608_TEMPLATE_MAX_BYTES, total);
    assert(data[0] == 0xA5U && data[sizeof(data) - 1U] == 0xA5U);
    assert(mock.tx_length == sizeof(command));
    assert(memcmp(mock.tx, command, sizeof(command)) == 0);
}

enum fault_case {
    FEATURE_ONLY,
    EARLY_END,
    MISSING_END,
    EXCESS_DATA,
    EMPTY_DATA,
    WRONG_KIND,
    BAD_CHECKSUM,
    TIMEOUT_AFTER_DATA,
    ABORT_AFTER_DATA
};

/* Verify rejected fragments never commit length or overwrite valid prefixes. */
static void test_fault(enum fault_case fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[AS608_TEMPLATE_MAX_BYTES];
    as608_template_buffer_t out = {data, sizeof(data), 123U};
    as608_status_t expected = AS608_STATUS_UNEXPECTED_REPLY;
    size_t prefix = 256U;

    setup(&ctx, &mock);
    (void)memset(data, 0xA5, sizeof(data));
    test_append_ack(&mock, AS608_ACK_OK);

    if (fault != FEATURE_ONLY) {
        append_data(&mock, AS608_PACKET_DATA, 0U, 256U);
    }

    switch (fault) {
        case FEATURE_ONLY: {
            append_data(&mock, AS608_PACKET_END_DATA, 0U, 256U);
            prefix = 0U;
            break;
        }
        case EARLY_END: {
            append_data(&mock, AS608_PACKET_END_DATA, 256U, 128U);
            break;
        }
        case MISSING_END: {
            out.capacity = 512U;
            append_data(&mock, AS608_PACKET_DATA, 256U, 256U);
            break;
        }
        case EXCESS_DATA: {
            out.capacity = 512U;
            append_data(&mock, AS608_PACKET_DATA, 256U, 128U);
            append_data(&mock, AS608_PACKET_END_DATA, 384U, 256U);
            prefix = 384U;
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
        case ABORT_AFTER_DATA: {
            expected = AS608_STATUS_RECOVERY_REQUIRED;
            break;
        }
    }

    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_OK);

    if ((fault == TIMEOUT_AFTER_DATA) || (fault == ABORT_AFTER_DATA)) {
        for (unsigned int i = 0U; (i < 500U) && (ctx.stream_offset == 0U); ++i) {
            assert(test_step(&ctx, &mock) == AS608_PENDING);
        }

        assert(ctx.stream_offset == 256U);
        if (fault == ABORT_AFTER_DATA) {
            assert(as608_abort(&ctx) == AS608_STATUS_OK);
        } else {
            mock.now = 100U;
        }
    }

    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(out.length == 123U);
    assert_output(data, sizeof(data), prefix);
}

/* Verify admission checks and a negative initial ACK preserve output. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[AS608_TEMPLATE_MAX_BYTES];
    as608_template_buffer_t out = {data, sizeof(data), 123U};
    as608_template_buffer_t invalid = {NULL, sizeof(data), 456U};

    (void)memset(data, 0xA5, sizeof(data));
    assert(as608_upload_template_start(NULL, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_1, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_1, &invalid) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_template_start(&ctx, (as608_char_buffer_t)3, &out) == AS608_STATUS_INVALID_ARG);
    out.capacity = 511U;
    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_INVALID_ARG);
    out.capacity = sizeof(data);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    test_append_ack(&mock, AS608_ACK_UPLOAD_ERROR);
    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_OK);
    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_2, &out) == AS608_STATUS_BUSY);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_UPLOAD_ERROR);
    assert(out.length == 123U);
    assert_output(data, sizeof(data), 0U);
}

int main(void) {
    const size_t chunks[] = {32U, 64U, 128U, 256U};

    for (size_t i = 0U; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        test_success(512U, chunks[i], 512U, AS608_CHAR_BUFFER_1);
        test_success(512U, chunks[i], 600U, AS608_CHAR_BUFFER_2);
        test_success(512U, chunks[i], 768U, AS608_CHAR_BUFFER_1);
        test_success(768U, chunks[i], 768U, AS608_CHAR_BUFFER_2);
    }

    test_fault(FEATURE_ONLY);
    test_fault(EARLY_END);
    test_fault(MISSING_END);
    test_fault(EXCESS_DATA);
    test_fault(EMPTY_DATA);
    test_fault(WRONG_KIND);
    test_fault(BAD_CHECKSUM);
    test_fault(TIMEOUT_AFTER_DATA);
    test_fault(ABORT_AFTER_DATA);
    test_admission();

    puts("as608 API 17 upload_template: PASS");
    return 0;
}
