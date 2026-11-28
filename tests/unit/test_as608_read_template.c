/** @file test_as608_read_template.c @brief API 20: composite read sequencing and deadline tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 28U
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

/* Verify both commands, ACK gating, stream completion and a shared deadline. */
static void test_success(size_t total, size_t chunk) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[AS608_TEMPLATE_MAX_BYTES + 2U];
    as608_template_buffer_t out = {data + 1U, AS608_TEMPLATE_MAX_BYTES, 123U};
    const uint8_t load[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
        0x06U, 0x07U, 0x01U, 0x12U, 0x34U, 0x00U, 0x55U
    };
    const uint8_t up[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x01U, 0x00U, 0x04U, 0x08U, 0x01U, 0x00U, 0x0EU
    };

    setup(&ctx, &mock);
    (void)memset(data, 0xA5, sizeof(data));
    mock.now = 10U;
    assert(as608_read_template_start(&ctx, 0x1234U, &out) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);

    for (unsigned int i = 0U; i < 50U; ++i) {
        assert(test_step(&ctx, &mock) == AS608_PENDING);
    }

    assert(mock.tx_length == sizeof(load));
    assert(memcmp(mock.tx, load, sizeof(load)) == 0);
    assert(out.length == 123U);
    assert_output(out.data, out.capacity, 0U);

    mock.now = 60U;
    test_append_ack(&mock, AS608_ACK_OK);
    for (unsigned int i = 0U; i < 50U; ++i) {
        assert(test_step(&ctx, &mock) == AS608_PENDING);
    }

    assert(mock.tx_length == sizeof(load) + sizeof(up));
    assert(memcmp(mock.tx + sizeof(load), up, sizeof(up)) == 0);
    assert(ctx.transaction.started_ms == 10U);
    assert(!as608_get_result(&ctx, &result));
    assert(out.length == 123U);
    assert_output(out.data, out.capacity, 0U);

    test_append_ack(&mock, AS608_ACK_OK);
    for (size_t offset = 0U; offset < total; offset += chunk) {
        const uint8_t kind = (offset + chunk == total) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        append_data(&mock, kind, offset, chunk);
    }

    as608_progress_t progress = AS608_PENDING;
    for (unsigned int i = 0U; (i < 1500U) && (progress == AS608_PENDING); ++i) {
        assert(out.length == 123U);
        progress = test_step(&ctx, &mock);
    }

    assert(progress == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(out.length == total);
    assert_output(out.data, out.capacity, total);
    assert(data[0] == 0xA5U && data[sizeof(data) - 1U] == 0xA5U);
    assert(mock.tx_length == sizeof(load) + sizeof(up));
}

/* Verify a rejected or malformed ACK never advances the wrong phase. */
static void test_ack(bool upload, uint8_t ack, bool corrupt, bool malformed) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[AS608_TEMPLATE_MAX_BYTES];
    as608_template_buffer_t out = {data, sizeof(data), 123U};

    setup(&ctx, &mock);
    (void)memset(data, 0xA5, sizeof(data));

    if (upload) {
        test_append_ack(&mock, AS608_ACK_OK);
    }

    if (malformed) {
        test_append_frame(&mock, AS608_PACKET_DATA, &ack, 1U);
    } else {
        test_append_ack(&mock, ack);
    }

    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }

    assert(as608_read_template_start(&ctx, 1U, &out) == AS608_STATUS_OK);
    const as608_status_t expected = corrupt ? AS608_STATUS_CHECKSUM_ERROR :
        (malformed ? AS608_STATUS_UNEXPECTED_REPLY : AS608_STATUS_OK);
    assert(run(&ctx, &mock) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);

    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack);
    }

    assert(mock.tx_length == (upload ? 28U : 15U));
    assert(out.length == 123U);
    assert_output(data, sizeof(data), 0U);
}

/* Verify a second command cannot extend the deadline and partial data is not committed. */
static void test_stream_fault(bool timeout, bool abort_job) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[AS608_TEMPLATE_MAX_BYTES];
    as608_template_buffer_t out = {data, sizeof(data), 123U};

    setup(&ctx, &mock);
    (void)memset(data, 0xA5, sizeof(data));
    test_append_ack(&mock, AS608_ACK_OK);
    test_append_ack(&mock, AS608_ACK_OK);
    append_data(&mock, AS608_PACKET_DATA, 0U, 256U);
    assert(as608_read_template_start(&ctx, 1U, &out) == AS608_STATUS_OK);
    mock.now = 99U;

    for (unsigned int i = 0U; (i < 500U) && (ctx.stream_offset == 0U); ++i) {
        assert(test_step(&ctx, &mock) == AS608_PENDING);
    }

    assert(ctx.stream_offset == 256U && ctx.transaction.started_ms == 0U);
    as608_status_t expected = AS608_STATUS_CHECKSUM_ERROR;

    if (abort_job) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
        expected = AS608_STATUS_RECOVERY_REQUIRED;
    } else if (timeout) {
        mock.now = 100U;
        expected = AS608_STATUS_TIMEOUT;
    } else {
        append_data(&mock, AS608_PACKET_END_DATA, 256U, 256U);
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }

    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(out.length == 123U);
    assert_output(data, sizeof(data), 256U);
    assert(as608_read_template_start(&ctx, 1U, &out) == AS608_STATUS_RECOVERY_REQUIRED);
}

/* Verify invalid descriptors and busy admission preserve the current job. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    uint8_t data[512U];
    as608_template_buffer_t out = {data, sizeof(data), 123U};
    as608_template_buffer_t bad = {NULL, sizeof(data), 456U};

    assert(as608_read_template_start(NULL, 1U, &out) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_template_start(&ctx, 1U, &out) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_read_template_start(&ctx, 1U, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_template_start(&ctx, 1U, &bad) == AS608_STATUS_INVALID_ARG);
    out.capacity = 511U;
    assert(as608_read_template_start(&ctx, 1U, &out) == AS608_STATUS_INVALID_ARG);
    out.capacity = sizeof(data);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    assert(as608_read_template_start(&ctx, 1U, &out) == AS608_STATUS_OK);

    unsigned char before[sizeof(ctx)];
    (void)memcpy(before, &ctx, sizeof(before));
    assert(as608_read_template_start(&ctx, 2U, &out) == AS608_STATUS_BUSY);
    assert(memcmp(before, &ctx, sizeof(before)) == 0);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(out.length == 123U);
}

int main(void) {
    const size_t chunks[] = {32U, 64U, 128U, 256U};

    for (size_t i = 0U; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        test_success(512U, chunks[i]);
        test_success(768U, chunks[i]);
    }

    test_ack(false, AS608_ACK_TEMPLATE_ERROR, false, false);
    test_ack(false, AS608_ACK_INVALID_PAGE, false, false);
    test_ack(true, AS608_ACK_UPLOAD_ERROR, false, false);
    test_ack(false, AS608_ACK_OK, true, false);
    test_ack(true, AS608_ACK_OK, true, false);
    test_ack(false, AS608_ACK_OK, false, true);
    test_ack(true, AS608_ACK_OK, false, true);
    test_stream_fault(true, false);
    test_stream_fault(false, false);
    test_stream_fault(false, true);
    test_admission();

    puts("as608 API 20 read_template: PASS");
    return 0;
}
