/** @file test_as608_download_image.c @brief API 30: image source and drain tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[100000U];
    uint8_t rx[64U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_pos;
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
    int32_t idle;
    unsigned int drain_calls;
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

/* Report physical TX drain and count drain polls. */
static int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    ++mock->drain_calls;
    return mock->idle;
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

/* Append a parameter ACK with the negotiated packet-size field. */
static void parameters(mock_t *mock, uint8_t ack, uint16_t code, size_t length) {
    uint8_t payload[17U] = {0};
    payload[0] = ack;
    payload[13] = (uint8_t)(code >> 8U);
    payload[14] = (uint8_t)code;
    assert(length <= sizeof(payload));
    append_frame(mock, AS608_PACKET_ACK, payload, length);
}

/* Advance one poll and verify bounded callback work. */
static as608_progress_t step(as608_t *ctx, mock_t *mock) {
    const unsigned int io = mock->io_calls;
    const unsigned int clocks = mock->clock_calls;
    const as608_progress_t progress = as608_process(ctx);
    assert(mock->io_calls - io <= 1U);
    assert(mock->clock_calls - clocks <= 1U);
    return progress;
}

/* Poll to a terminal event or the first drain callback. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, bool stop_at_drain) {
    for (unsigned int i = 0U; i < 120000U; ++i) {
        const as608_progress_t progress = step(ctx, mock);

        if ((progress != AS608_PENDING) || (stop_at_drain && (mock->drain_calls > 0U))) {
            return progress;
        }
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Independently verify the outgoing packet bytes and checksum. */
static size_t check_frame(const mock_t *mock, size_t pos, uint8_t kind, const uint8_t *data, size_t length) {
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind,
        (uint8_t)((length + 2U) >> 8U), (uint8_t)(length + 2U)
    };

    assert(pos + length + 11U <= mock->tx_length);
    assert(memcmp(mock->tx + pos, header, sizeof(header)) == 0);
    assert(memcmp(mock->tx + pos + 9U, data, length) == 0);

    uint16_t sum = 0U;
    for (size_t i = 6U; i < length + 9U; ++i) {
        sum = (uint16_t)(sum + mock->tx[pos + i]);
    }

    assert(mock->tx[pos + length + 9U] == (uint8_t)(sum >> 8U));
    assert(mock->tx[pos + length + 10U] == (uint8_t)sum);
    return pos + length + 11U;
}

typedef struct {
    size_t length;
    size_t offset;
    size_t calls;
    unsigned int waits;
    int32_t failure;
} source_t;

/* Provide exact fragments, optional temporary stalls or an invalid return. */
static int32_t produce(void *user, uint8_t *data, size_t length) {
    source_t *source = user;
    assert(data && (length >= 64U) && (length <= 256U));
    assert(source->offset <= source->length && length <= source->length - source->offset);
    ++source->calls;

    if (source->waits != 0U) {
        --source->waits;
        return 0;
    }

    if (source->failure != 0) {
        return source->failure;
    }

    for (size_t i = 0U; i < length; ++i) {
        data[i] = pattern(source->offset + i);
    }

    source->offset += length;
    return (int32_t)length;
}

/* Verify independent packet checksums, source stalls and physical drain. */
static void test_success(size_t total, uint16_t code) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    source_t state = {total, 0U, 0U, 3U, 0};
    as608_stream_source_t source = {&state, produce, total};
    const uint8_t params[] = {0x0FU};
    const uint8_t down[] = {0x0BU};

    setup(&ctx, &mock);
    parameters(&mock, AS608_ACK_OK, code, 17U);
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U && state.calls == 0U);
    source = (as608_stream_source_t){0};

    for (unsigned int i = 0U; i < 100U; ++i) {
        assert(step(&ctx, &mock) == AS608_PENDING);
    }

    size_t pos = check_frame(&mock, 0U, AS608_PACKET_COMMAND, params, sizeof(params));
    pos = check_frame(&mock, pos, AS608_PACKET_COMMAND, down, sizeof(down));
    assert(pos == mock.tx_length && state.calls == 0U);
    append_ack(&mock, AS608_ACK_OK);

    for (unsigned int i = 0U; (i < 100U) && (state.calls < 3U); ++i) {
        const size_t calls = state.calls;
        assert(step(&ctx, &mock) == AS608_PENDING);
        assert(state.calls - calls <= 1U);
        assert(state.offset == 0U && ctx.input_offset == 0U && mock.tx_length == pos);
    }

    assert(state.calls == 3U);
    assert(run(&ctx, &mock, true) == AS608_PENDING);
    assert(ctx.state == AS608_STATE_BUSY && !as608_get_result(&ctx, &result));
    const size_t chunk = (size_t)32U << code;
    uint8_t data[256U];

    for (size_t offset = 0U; offset < total; offset += chunk) {
        for (size_t i = 0U; i < chunk; ++i) {
            data[i] = pattern(offset + i);
        }

        const uint8_t kind = (offset + chunk == total) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        pos = check_frame(&mock, pos, kind, data, chunk);
    }

    assert(pos == mock.tx_length && ctx.input_offset == total && state.offset == total);
    assert(state.calls == total / chunk + 3U && !ctx.store_after_download);
    assert(step(&ctx, &mock) == AS608_PENDING);
    mock.idle = 1;
    assert(step(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(step(&ctx, &mock) == AS608_COMPLETE && state.calls == total / chunk + 3U);
}

/* Verify incompatible sizes and negative ACKs never call the source. */
static void test_reply(size_t total, uint8_t ack, uint16_t code, size_t length, uint8_t down,
                       as608_status_t expected, uint8_t confirmation, size_t sent) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    source_t state = {total, 0U, 0U, 0U, 0};
    const as608_stream_source_t source = {&state, produce, total};

    setup(&ctx, &mock);
    parameters(&mock, ack, code, length);
    append_ack(&mock, down);
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, false) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);
    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == confirmation);
    }

    assert(mock.tx_length == sent && mock.drain_calls == 0U && state.calls == 0U);
}

/* Verify source failure and waiting preserve the original deadline and offset. */
static void test_source_fault(int32_t failure, bool abort_job) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    source_t state = {256U, 0U, 0U, 0U, 0};
    const as608_stream_source_t source = {&state, produce, state.length};

    setup(&ctx, &mock);
    mock.now = UINT32_MAX - 49U;
    parameters(&mock, AS608_ACK_OK, 1U, 17U);
    append_ack(&mock, AS608_ACK_OK);
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_OK);

    for (unsigned int i = 0U; (i < 100U) && (state.calls == 0U); ++i) {
        assert(step(&ctx, &mock) == AS608_PENDING);
    }

    assert(state.offset == 64U && ctx.input_offset == 64U);
    state.failure = failure;
    state.waits = (failure == 0) ? 100U : 0U;

    for (unsigned int i = 0U; (i < 200U) && (state.calls == 1U); ++i) {
        const as608_progress_t progress = step(&ctx, &mock);
        assert(progress == ((state.calls == 2U && failure != 0) ? AS608_FAILED : AS608_PENDING));
    }

    assert(state.calls == 2U && ctx.input_offset == 64U);
    const size_t sent = mock.tx_length;
    if (failure == 0) {
        if (abort_job) {
            assert(as608_abort(&ctx) == AS608_STATUS_OK);
        } else {
            mock.now = 50U;
        }
    }

    assert(step(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    const as608_status_t expected = (failure != 0) ? AS608_STATUS_IO_ERROR :
        (abort_job ? AS608_STATUS_RECOVERY_REQUIRED : AS608_STATUS_TIMEOUT);
    assert(result.status == expected && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(state.calls == 2U && state.offset == 64U && mock.tx_length == sent);
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_RECOVERY_REQUIRED);
}

/* Verify admission and clearing source callbacks between different jobs. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    source_t state = {256U, 0U, 0U, 0U, 0};
    as608_stream_source_t source = {&state, produce, state.length};
    const as608_stream_source_t invalid = {&state, NULL, 256U};
    as608_feature_file_t feature = {0};

    assert(as608_download_image_start(NULL, &source) == AS608_STATUS_INVALID_ARG);
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_download_image_start(&ctx, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_download_image_start(&ctx, &invalid) == AS608_STATUS_INVALID_ARG);
    source.length = 0U;
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_INVALID_ARG);
    source.length = 65U;
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_INVALID_ARG);
    source.length = 256U;
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    parameters(&mock, AS608_ACK_PACKET_ERROR, 1U, 1U);
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_OK);
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_BUSY);
    assert(run(&ctx, &mock, false) == AS608_COMPLETE);
    assert(state.calls == 0U);

    mock.tx_length = 0U;
    parameters(&mock, AS608_ACK_OK, 0U, 17U);
    append_ack(&mock, AS608_ACK_OK);
    mock.idle = 1;
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_OK);
    assert(ctx.stream_read == NULL && ctx.stream_user == NULL);
    assert(run(&ctx, &mock, false) == AS608_COMPLETE);
    assert(state.calls == 0U && ctx.input_offset == sizeof(feature.data));
}

int main(void) {
    for (uint16_t code = 1U; code <= 3U; ++code) {
        test_success(256U, code);
        test_success(36864U, code);
    }
    test_success(70144U, 1U);
    test_reply(256U, AS608_ACK_OK, 0U, 17U, AS608_ACK_OK, AS608_STATUS_UNEXPECTED_REPLY, 0U, 12U);
    test_reply(256U, AS608_ACK_OK, 4U, 17U, AS608_ACK_OK, AS608_STATUS_UNEXPECTED_REPLY, 0U, 12U);
    test_reply(192U, AS608_ACK_OK, 2U, 17U, AS608_ACK_OK, AS608_STATUS_UNEXPECTED_REPLY, 0U, 12U);
    test_reply(256U, AS608_ACK_OK, 2U, 1U, AS608_ACK_OK, AS608_STATUS_UNEXPECTED_REPLY, 0U, 12U);
    test_reply(256U, AS608_ACK_PACKET_ERROR, 2U, 1U, AS608_ACK_OK, AS608_STATUS_OK, AS608_ACK_PACKET_ERROR, 12U);
    test_reply(256U, AS608_ACK_OK, 2U, 17U, AS608_ACK_TRANSFER_REJECTED, AS608_STATUS_OK, AS608_ACK_TRANSFER_REJECTED, 24U);
    test_source_fault(-1, false);
    test_source_fault(1, false);
    test_source_fault(65, false);
    test_source_fault(0, false);
    test_source_fault(0, true);
    test_admission();

    puts("as608 API 30 download_image: PASS");
    return 0;
}
