/** @file test_as608_upload_feature.c @brief API 27: fixed-size feature upload tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[1200U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_pos;
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
} mock_t;

/* Accept one TX byte into the mock wire buffer. */
static int32_t write_byte(void *user, const uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U) && (mock->tx_length < sizeof(mock->tx)));
    ++mock->io_calls;
    mock->tx[mock->tx_length++] = *data;
    return 1;
}

/* Return one available RX byte or report no data. */
static int32_t read_byte(void *user, uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U));
    ++mock->io_calls;

    if (mock->rx_pos == mock->rx_length) {
        return 0;
    }

    *data = mock->rx[mock->rx_pos++];
    return 1;
}

/* Report the mock transmitter idle. */
static int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    return 1;
}

/* Return the mock time and count clock accesses. */
static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

/* Initialize the device with the mock UART port. */
static void setup(as608_t *ctx, mock_t *mock) {
    const as608_config_t config = {
        .port = {
            .user = mock, .try_write = write_byte, .try_read = read_byte,
            .tx_idle = tx_idle, .clock_ms = clock_ms
        },
        .device_address = AS608_DEFAULT_ADDRESS,
        .timeout_ms = 100U
    };

    *ctx = (as608_t){0};
    assert(as608_init(ctx, &config) == AS608_STATUS_OK);
}

/* Generate deterministic data for wire and output checks. */
static uint8_t pattern(size_t offset) {
    return (uint8_t)(offset * 13U + 7U);
}

/* Append an independently encoded packet to the mock RX stream. */
static void append_frame(mock_t *mock, uint8_t kind, const uint8_t *payload, size_t length) {
    const uint16_t wire_length = (uint16_t)(length + 2U);
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind,
        (uint8_t)(wire_length >> 8U), (uint8_t)wire_length
    };

    assert(length <= 256U);
    assert(mock->rx_length + 11U + length <= sizeof(mock->rx));
    uint8_t *frame = mock->rx + mock->rx_length;
    (void)memcpy(frame, header, sizeof(header));
    (void)memcpy(frame + sizeof(header), payload, length);

    uint16_t sum = 0U;
    for (size_t i = 6U; i < 9U + length; ++i) {
        sum = (uint16_t)(sum + frame[i]);
    }

    frame[9U + length] = (uint8_t)(sum >> 8U);
    frame[10U + length] = (uint8_t)sum;
    mock->rx_length += 11U + length;
}

/* Append a one-byte confirmation ACK. */
static void append_ack(mock_t *mock, uint8_t ack) {
    append_frame(mock, AS608_PACKET_ACK, &ack, 1U);
}

/* Append a patterned DATA or END_DATA fragment. */
static void append_data(mock_t *mock, uint8_t kind, size_t offset, size_t length) {
    uint8_t data[256U];

    assert(length <= sizeof(data));
    for (size_t i = 0U; i < length; ++i) {
        data[i] = pattern(offset + i);
    }

    append_frame(mock, kind, data, length);
}

/* Advance one poll and verify its callback bounds. */
static as608_progress_t step(as608_t *ctx, mock_t *mock) {
    const unsigned int io = mock->io_calls;
    const unsigned int clocks = mock->clock_calls;
    const as608_progress_t progress = as608_process(ctx);
    assert(mock->io_calls - io <= 1U);
    assert(mock->clock_calls - clocks <= 1U);
    return progress;
}

/* Poll with a bounded iteration count. */
static as608_progress_t run(as608_t *ctx, mock_t *mock) {
    for (unsigned int i = 0U; i < 1500U; ++i) {
        const as608_progress_t progress = step(ctx, mock);

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

/* Verify exact wire request, fragment sizes and guard bytes. */
static void test_success(size_t chunk, as608_char_buffer_t buffer) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    struct {
        uint8_t before;
        as608_feature_file_t feature;
        uint8_t after;
    } out;
    const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x01U, 0x00U, 0x04U, 0x08U, (uint8_t)buffer, 0x00U,
        (uint8_t)(0x0DU + (uint8_t)buffer)
    };

    setup(&ctx, &mock);
    (void)memset(&out, 0xA5, sizeof(out));
    append_ack(&mock, AS608_ACK_OK);

    for (size_t offset = 0U; offset < AS608_FEATURE_256_BYTES; offset += chunk) {
        const uint8_t kind = (offset + chunk == AS608_FEATURE_256_BYTES) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        append_data(&mock, kind, offset, chunk);
    }

    assert(as608_upload_feature_start(&ctx, buffer, &out.feature) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert_output(out.feature.data, sizeof(out.feature.data), 0U);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert_output(out.feature.data, sizeof(out.feature.data), AS608_FEATURE_256_BYTES);
    assert(out.before == 0xA5U && out.after == 0xA5U);
    assert(mock.tx_length == sizeof(command));
    assert(memcmp(mock.tx, command, sizeof(command)) == 0);
}

enum fault_case {
    EARLY_END,
    MISSING_END,
    EXCESS_DATA,
    TEMPLATE_STREAM,
    EMPTY_DATA,
    WRONG_KIND,
    BAD_CHECKSUM,
    TIMEOUT_AFTER_DATA,
    ABORT_AFTER_DATA
};

/* Verify rejected fragments do not overwrite the verified prefix or guards. */
static void test_fault(enum fault_case fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    struct {
        uint8_t before;
        as608_feature_file_t feature;
        uint8_t after;
    } out;
    as608_status_t expected = AS608_STATUS_UNEXPECTED_REPLY;
    size_t prefix = 128U;

    setup(&ctx, &mock);
    (void)memset(&out, 0xA5, sizeof(out));
    append_ack(&mock, AS608_ACK_OK);

    if (fault != TEMPLATE_STREAM) {
        append_data(&mock, AS608_PACKET_DATA, 0U, 128U);
    }

    switch (fault) {
        case EARLY_END: {
            append_data(&mock, AS608_PACKET_END_DATA, 128U, 64U);
            break;
        }
        case MISSING_END: {
            append_data(&mock, AS608_PACKET_DATA, 128U, 128U);
            break;
        }
        case EXCESS_DATA: {
            append_data(&mock, AS608_PACKET_END_DATA, 128U, 129U);
            break;
        }
        case TEMPLATE_STREAM: {
            append_data(&mock, AS608_PACKET_DATA, 0U, 256U);
            append_data(&mock, AS608_PACKET_END_DATA, 256U, 256U);
            prefix = 0U;
            break;
        }
        case EMPTY_DATA: {
            append_data(&mock, AS608_PACKET_END_DATA, 128U, 0U);
            break;
        }
        case WRONG_KIND: {
            append_ack(&mock, AS608_ACK_OK);
            break;
        }
        case BAD_CHECKSUM: {
            append_data(&mock, AS608_PACKET_END_DATA, 128U, 128U);
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

    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_1, &out.feature) == AS608_STATUS_OK);

    if ((fault == TIMEOUT_AFTER_DATA) || (fault == ABORT_AFTER_DATA)) {
        for (unsigned int i = 0U; (i < 500U) && (ctx.stream_offset == 0U); ++i) {
            assert(step(&ctx, &mock) == AS608_PENDING);
        }

        assert(ctx.stream_offset == 128U);
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
    assert_output(out.feature.data, sizeof(out.feature.data), prefix);
    assert(out.before == 0xA5U && out.after == 0xA5U);
}

/* Verify admission and negative readiness ACK preserve the destination. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_feature_file_t out;
    as608_feature_file_t other;

    (void)memset(&out, 0xA5, sizeof(out));
    (void)memset(&other, 0xA5, sizeof(other));
    assert(as608_upload_feature_start(NULL, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_1, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_feature_start(&ctx, (as608_char_buffer_t)3, &out) == AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    append_ack(&mock, AS608_ACK_UPLOAD_ERROR);
    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_OK);
    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_2, &other) == AS608_STATUS_BUSY);
    assert(ctx.stream_data == out.data);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_UPLOAD_ERROR);
    assert_output(out.data, sizeof(out.data), 0U);
    assert_output(other.data, sizeof(other.data), 0U);

    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_1, &out) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
}

/* Verify a previous variable-length stream cannot leave a stale length output. */
static void test_sequential_jobs(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    uint8_t data[AS608_TEMPLATE_512_BYTES];
    as608_template_buffer_t templ = {data, sizeof(data), 0U};
    as608_feature_file_t feature;

    setup(&ctx, &mock);
    append_ack(&mock, AS608_ACK_OK);
    append_data(&mock, AS608_PACKET_DATA, 0U, 256U);
    append_data(&mock, AS608_PACKET_END_DATA, 256U, 256U);
    assert(as608_upload_template_start(&ctx, AS608_CHAR_BUFFER_1, &templ) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(templ.length == AS608_TEMPLATE_512_BYTES);

    mock.tx_length = 0U;
    append_ack(&mock, AS608_ACK_OK);
    append_data(&mock, AS608_PACKET_END_DATA, 0U, 256U);
    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_2, &feature) == AS608_STATUS_OK);
    assert(ctx.stream_length == NULL);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(templ.length == AS608_TEMPLATE_512_BYTES);
    assert_output(feature.data, sizeof(feature.data), AS608_FEATURE_256_BYTES);
}

int main(void) {
    const size_t chunks[] = {32U, 64U, 128U, 256U};

    for (size_t i = 0U; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        test_success(chunks[i], AS608_CHAR_BUFFER_1);
        test_success(chunks[i], AS608_CHAR_BUFFER_2);
    }

    test_fault(EARLY_END);
    test_fault(MISSING_END);
    test_fault(EXCESS_DATA);
    test_fault(TEMPLATE_STREAM);
    test_fault(EMPTY_DATA);
    test_fault(WRONG_KIND);
    test_fault(BAD_CHECKSUM);
    test_fault(TIMEOUT_AFTER_DATA);
    test_fault(ABORT_AFTER_DATA);
    test_admission();
    test_sequential_jobs();

    puts("as608 API 27 upload_feature: PASS");
    return 0;
}
