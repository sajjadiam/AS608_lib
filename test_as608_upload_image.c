/** @file test_as608_upload_image.c @brief API 29: image stream and callback tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[100000U];
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
    return (uint8_t)(offset * 13U + offset / 251U + 7U);
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
    for (unsigned int i = 0U; i < 110000U; ++i) {
        const as608_progress_t progress = step(ctx, mock);

        if (progress != AS608_PENDING) {
            return progress;
        }
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

typedef struct {
    size_t length;
    size_t received;
    size_t calls;
    size_t reject_call;
} sink_t;

/* Verify each delivered byte and optionally reject one fragment. */
static bool consume(void *user, const uint8_t *data, size_t length) {
    sink_t *sink = user;
    assert(data && (length > 0U) && (length <= 256U));
    assert(sink->received <= sink->length && length <= sink->length - sink->received);
    ++sink->calls;

    for (size_t i = 0U; i < length; ++i) {
        assert(data[i] == pattern(sink->received + i));
    }

    if (sink->calls == sink->reject_call) {
        return false;
    }

    sink->received += length;
    return true;
}

/* Verify wire bytes, callback ordering, exact size and copied descriptor. */
static void test_success(size_t total, size_t chunk) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    sink_t state = {total, 0U, 0U, 0U};
    as608_stream_sink_t sink = {&state, consume, total};
    const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
        0x03U, 0x0AU, 0x00U, 0x0EU
    };

    setup(&ctx, &mock);
    append_ack(&mock, AS608_ACK_OK);

    for (size_t offset = 0U; offset < total;) {
        const size_t remaining = total - offset;
        const size_t length = (remaining > chunk) ? chunk : remaining;
        const uint8_t kind = (length == remaining) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        append_data(&mock, kind, offset, length);
        offset += length;
    }

    assert(as608_upload_image_start(&ctx, &sink) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U && state.calls == 0U);
    sink = (as608_stream_sink_t){0};
    assert(ctx.stream_data == NULL && ctx.stream_length == NULL);

    as608_progress_t progress = AS608_PENDING;
    for (unsigned int i = 0U; (i < 110000U) && (progress == AS608_PENDING); ++i) {
        const size_t calls = state.calls;
        progress = step(&ctx, &mock);
        assert(state.calls - calls <= 1U);
        if (progress == AS608_PENDING) {
            assert(state.received < total);
        }
    }

    assert(progress == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(state.received == total && state.calls == (total + chunk - 1U) / chunk);
    assert(ctx.stream_offset == total && mock.rx_pos == mock.rx_length);
    assert(mock.tx_length == sizeof(command) && memcmp(mock.tx, command, sizeof(command)) == 0);
    assert(step(&ctx, &mock) == AS608_COMPLETE);
    assert(state.calls == (total + chunk - 1U) / chunk);
}

enum fault_case {
    EARLY_END,
    MISSING_END,
    EXCESS_DATA,
    EMPTY_DATA,
    WRONG_KIND,
    BAD_CHECKSUM,
    SINK_REJECT_DATA,
    SINK_REJECT_END,
    TIMEOUT_AFTER_DATA,
    ABORT_AFTER_DATA
};

/* Verify invalid fragments never reach the sink and callbacks are not retried. */
static void test_fault(enum fault_case fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    sink_t state = {256U, 0U, 0U, 0U};
    const as608_stream_sink_t sink = {&state, consume, state.length};
    as608_status_t expected = AS608_STATUS_UNEXPECTED_REPLY;
    size_t prefix = 128U;
    size_t calls = 1U;

    setup(&ctx, &mock);
    append_ack(&mock, AS608_ACK_OK);
    append_data(&mock, AS608_PACKET_DATA, 0U, 128U);

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
        case SINK_REJECT_DATA: {
            state.reject_call = 1U;
            prefix = 0U;
            expected = AS608_STATUS_IO_ERROR;
            break;
        }
        case SINK_REJECT_END: {
            state.reject_call = 2U;
            append_data(&mock, AS608_PACKET_END_DATA, 128U, 128U);
            calls = 2U;
            expected = AS608_STATUS_IO_ERROR;
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

    assert(as608_upload_image_start(&ctx, &sink) == AS608_STATUS_OK);

    if ((fault == TIMEOUT_AFTER_DATA) || (fault == ABORT_AFTER_DATA)) {
        for (unsigned int i = 0U; (i < 500U) && (state.calls == 0U); ++i) {
            assert(step(&ctx, &mock) == AS608_PENDING);
        }

        assert(state.received == 128U);
        if (fault == ABORT_AFTER_DATA) {
            assert(as608_abort(&ctx) == AS608_STATUS_OK);
        } else {
            mock.now = 100U;
        }
    }

    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(state.received == prefix && state.calls == calls && ctx.stream_offset == prefix);
    assert(step(&ctx, &mock) == AS608_FAILED && state.calls == calls);
    assert(as608_upload_image_start(&ctx, &sink) == AS608_STATUS_RECOVERY_REQUIRED);
}

/* Verify admission, negative ACK and isolation from another stream job. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    sink_t state = {256U, 0U, 0U, 0U};
    sink_t other = {256U, 0U, 0U, 0U};
    const as608_stream_sink_t sink = {&state, consume, state.length};
    const as608_stream_sink_t competing = {&other, consume, other.length};
    const as608_stream_sink_t no_callback = {&state, NULL, 256U};
    const as608_stream_sink_t no_length = {&state, consume, 0U};

    assert(as608_upload_image_start(NULL, &sink) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_image_start(&ctx, &sink) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_upload_image_start(&ctx, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_image_start(&ctx, &no_callback) == AS608_STATUS_INVALID_ARG);
    assert(as608_upload_image_start(&ctx, &no_length) == AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    append_ack(&mock, AS608_ACK_IMAGE_UPLOAD_ERROR);
    assert(as608_upload_image_start(&ctx, &sink) == AS608_STATUS_OK);
    assert(as608_upload_image_start(&ctx, &competing) == AS608_STATUS_BUSY);
    assert(ctx.stream_user == &state);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_IMAGE_UPLOAD_ERROR);
    assert(state.calls == 0U && other.calls == 0U);

    mock.tx_length = 0U;
    append_ack(&mock, AS608_ACK_OK);
    append_data(&mock, AS608_PACKET_END_DATA, 0U, 256U);
    as608_feature_file_t feature;
    assert(as608_upload_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_OK);
    assert(ctx.stream_write == NULL && ctx.stream_user == NULL);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(state.calls == 0U);
    for (size_t i = 0U; i < sizeof(feature.data); ++i) {
        assert(feature.data[i] == pattern(i));
    }
}

int main(void) {
    test_success(1U, 32U);
    test_success(777U, 32U);
    test_success(777U, 64U);
    test_success(777U, 128U);
    test_success(777U, 256U);
    test_success(36864U, 128U);
    test_success(70000U, 32U);

    test_fault(EARLY_END);
    test_fault(MISSING_END);
    test_fault(EXCESS_DATA);
    test_fault(EMPTY_DATA);
    test_fault(WRONG_KIND);
    test_fault(BAD_CHECKSUM);
    test_fault(SINK_REJECT_DATA);
    test_fault(SINK_REJECT_END);
    test_fault(TIMEOUT_AFTER_DATA);
    test_fault(ABORT_AFTER_DATA);
    test_admission();

    puts("as608 API 29 upload_image: PASS");
    return 0;
}
