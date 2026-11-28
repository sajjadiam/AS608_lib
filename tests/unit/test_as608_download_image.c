/** @file test_as608_download_image.c @brief API 30: image source and drain tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 100000U
#define AS608_TEST_RX_CAPACITY 64U
#define AS608_TEST_DRAIN 1
#include "tests/common/mock_wire.h"

/* Generate deterministic data for wire and output checks. */
static uint8_t pattern(size_t offset) {
    return (uint8_t)(offset * 13U + offset / 251U + 7U);
}

/* Append a parameter ACK with the negotiated packet-size field. */
static void parameters(mock_t *mock, uint8_t ack, uint16_t code, size_t length) {
    uint8_t payload[17U] = {0};
    payload[0] = ack;
    payload[13] = (uint8_t)(code >> 8U);
    payload[14] = (uint8_t)code;
    assert(length <= sizeof(payload));
    test_append_frame(mock, AS608_PACKET_ACK, payload, length);
}

/* Poll to a terminal event or the first drain callback. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, bool stop_at_drain) {
    for (unsigned int i = 0U; i < 120000U; ++i) {
        const as608_progress_t progress = test_step(ctx, mock);

        if ((progress != AS608_PENDING) || (stop_at_drain && (mock->drain_calls > 0U))) {
            return progress;
        }
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
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
        assert(test_step(&ctx, &mock) == AS608_PENDING);
    }

    size_t pos = test_check_frame(&mock, 0U, AS608_PACKET_COMMAND, params, sizeof(params));
    pos = test_check_frame(&mock, pos, AS608_PACKET_COMMAND, down, sizeof(down));
    assert(pos == mock.tx_length && state.calls == 0U);
    test_append_ack(&mock, AS608_ACK_OK);

    for (unsigned int i = 0U; (i < 100U) && (state.calls < 3U); ++i) {
        const size_t calls = state.calls;
        assert(test_step(&ctx, &mock) == AS608_PENDING);
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
        pos = test_check_frame(&mock, pos, kind, data, chunk);
    }

    assert(pos == mock.tx_length && ctx.input_offset == total && state.offset == total);
    assert(state.calls == total / chunk + 3U && !ctx.store_after_download);
    assert(test_step(&ctx, &mock) == AS608_PENDING);
    mock.idle = 1;
    assert(test_step(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(test_step(&ctx, &mock) == AS608_COMPLETE && state.calls == total / chunk + 3U);
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
    test_append_ack(&mock, down);
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
    test_append_ack(&mock, AS608_ACK_OK);
    assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_OK);

    for (unsigned int i = 0U; (i < 100U) && (state.calls == 0U); ++i) {
        assert(test_step(&ctx, &mock) == AS608_PENDING);
    }

    assert(state.offset == 64U && ctx.input_offset == 64U);
    state.failure = failure;
    state.waits = (failure == 0) ? 100U : 0U;

    for (unsigned int i = 0U; (i < 200U) && (state.calls == 1U); ++i) {
        const as608_progress_t progress = test_step(&ctx, &mock);
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

    assert(test_step(&ctx, &mock) == AS608_FAILED);
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
    test_append_ack(&mock, AS608_ACK_OK);
    mock.idle = 1;
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_OK);
    assert(ctx.stream_read == NULL && ctx.stream_user == NULL);
    assert(run(&ctx, &mock, false) == AS608_COMPLETE);
    assert(state.calls == 0U && ctx.input_offset == sizeof(feature.data));
}

/* Reject impossible source chunk sizes before invoking the callback. */
static void test_source_metadata(void) {
    const uint16_t invalid[] = {0U, 32U, 63U, 65U, 127U, 255U, 257U, UINT16_MAX};
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        mock_t mock = {0};
        as608_t ctx = {0};
        as608_result_t result;
        source_t state = {256U, 0U, 0U, 1U, 0};
        const as608_stream_source_t source = {&state, produce, state.length};
        setup(&ctx, &mock);
        parameters(&mock, AS608_ACK_OK, 1U, 17U);
        test_append_ack(&mock, AS608_ACK_OK);
        assert(as608_download_image_start(&ctx, &source) == AS608_STATUS_OK);

        for (unsigned int poll = 0U; poll < 100U && state.calls == 0U; ++poll) {
            assert(test_step(&ctx, &mock) == AS608_PENDING);
        }

        assert(state.calls == 1U && state.offset == 0U && ctx.input_offset == 0U);
        ctx.chunk_size = invalid[i]; /* White-box fault injection only. */
        const unsigned int io = mock.io_calls;
        const size_t sent = mock.tx_length;
        assert(test_step(&ctx, &mock) == AS608_FAILED);
        assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_UNEXPECTED_REPLY);
        assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
        assert(state.calls == 1U && mock.io_calls == io && mock.tx_length == sent);
    }
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
    test_source_fault(INT32_MIN, false);
    test_source_fault(INT32_MAX, false);
    test_source_fault(65536, false);
    test_source_fault(65537, false);
    test_source_fault(65600, false); /* Would narrow to 64 on a 16-bit size_t. */
    test_source_fault(0, false);
    test_source_fault(0, true);
    test_source_metadata();
    test_admission();

    puts("as608 API 30 download_image: PASS");
    return 0;
}
