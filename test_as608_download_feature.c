/** @file test_as608_download_feature.c @brief API 28: feature download wire and drain tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 1100U
#define AS608_TEST_RX_CAPACITY 64U
#define AS608_TEST_DRAIN 1
#include "tests/common/mock_wire.h"

/* Generate deterministic data for wire and output checks. */
static uint8_t pattern(size_t offset) {
    return (uint8_t)(offset * 13U + 7U);
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
    for (unsigned int i = 0U; i < 1600U; ++i) {
        const as608_progress_t progress = test_step(ctx, mock);

        if ((progress != AS608_PENDING) || (stop_at_drain && (mock->drain_calls > 0U))) {
            return progress;
        }
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Verify readiness gating, every fragment and physical drain completion. */
static void test_success(uint16_t code, as608_char_buffer_t buffer) {
    const size_t length = AS608_FEATURE_256_BYTES;
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_feature_file_t feature;
    const uint8_t params[] = {0x0FU};
    const uint8_t down[] = {0x09U, (uint8_t)buffer};

    setup(&ctx, &mock);
    for (size_t i = 0U; i < sizeof(feature.data); ++i) {
        feature.data[i] = pattern(i);
    }

    parameters(&mock, AS608_ACK_OK, code, 17U);
    assert(as608_download_feature_start(&ctx, buffer, &feature) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);

    for (unsigned int i = 0U; i < 100U; ++i) {
        assert(test_step(&ctx, &mock) == AS608_PENDING);
    }

    size_t pos = test_check_frame(&mock, 0U, AS608_PACKET_COMMAND, params, sizeof(params));
    pos = test_check_frame(&mock, pos, AS608_PACKET_COMMAND, down, sizeof(down));
    assert(pos == mock.tx_length && mock.drain_calls == 0U);

    test_append_ack(&mock, AS608_ACK_OK);
    assert(run(&ctx, &mock, true) == AS608_PENDING);
    assert(ctx.state == AS608_STATE_BUSY && !as608_get_result(&ctx, &result));

    const size_t chunk = (size_t)32U << code;
    for (size_t offset = 0U; offset < length; offset += chunk) {
        const uint8_t kind = (offset + chunk == length) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        pos = test_check_frame(&mock, pos, kind, feature.data + offset, chunk);
    }

    assert(pos == mock.tx_length && ctx.input_offset == length);
    assert(ctx.input_length == AS608_FEATURE_256_BYTES && !ctx.store_after_download);
    assert(test_step(&ctx, &mock) == AS608_PENDING);
    assert(mock.rx_pos == mock.rx_length);
    mock.idle = 1;
    assert(test_step(&ctx, &mock) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    for (size_t i = 0U; i < sizeof(feature.data); ++i) {
        assert(feature.data[i] == pattern(i));
    }
}

/* Verify negative ACKs and invalid size codes stop before data. */
static void test_reply(uint8_t ack, uint16_t code, size_t length, uint8_t down, as608_status_t status, uint8_t confirmation, size_t sent) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_feature_file_t feature = {0};

    setup(&ctx, &mock);
    parameters(&mock, ack, code, length);
    test_append_ack(&mock, down);
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, false) == ((status == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == status);
    if (status == AS608_STATUS_OK) {
        assert(result.confirmation == confirmation);
    }

    assert(mock.tx_length == sent && mock.drain_calls == 0U);
}

/* Verify admission checks and terminal faults during TX drain. */
static void test_drain_fault(as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_feature_file_t feature = {0};

    assert(as608_download_feature_start(NULL, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_INVALID_ARG);
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_download_template_start(&ctx, AS608_CHAR_BUFFER_1, feature.data, sizeof(feature.data)) == AS608_STATUS_INVALID_ARG);
    assert(as608_write_template_start(&ctx, 0U, feature.data, sizeof(feature.data)) == AS608_STATUS_INVALID_ARG);
    assert(as608_download_feature_start(&ctx, (as608_char_buffer_t)3, &feature) == AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);

    parameters(&mock, AS608_ACK_OK, 2U, 17U);
    test_append_ack(&mock, AS608_ACK_OK);
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_OK);
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_2, &feature) == AS608_STATUS_BUSY);
    assert(run(&ctx, &mock, true) == AS608_PENDING);

    if (expected == AS608_STATUS_RECOVERY_REQUIRED) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
    } else if (expected == AS608_STATUS_TIMEOUT) {
        mock.now = 100U;
    } else {
        mock.idle = -1;
    }

    assert(test_step(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(as608_download_feature_start(&ctx, AS608_CHAR_BUFFER_1, &feature) == AS608_STATUS_RECOVERY_REQUIRED);
}

int main(void) {
    for (uint16_t code = 0U; code <= 3U; ++code) {
        test_success(code, AS608_CHAR_BUFFER_1);
        test_success(code, AS608_CHAR_BUFFER_2);
    }

    test_reply(AS608_ACK_PACKET_ERROR, 2U, 1U, AS608_ACK_OK, AS608_STATUS_OK, AS608_ACK_PACKET_ERROR, 12U);
    test_reply(AS608_ACK_OK, 4U, 17U, AS608_ACK_OK, AS608_STATUS_UNEXPECTED_REPLY, 0U, 12U);
    test_reply(AS608_ACK_OK, 2U, 1U, AS608_ACK_OK, AS608_STATUS_UNEXPECTED_REPLY, 0U, 12U);
    test_reply(AS608_ACK_OK, 2U, 17U, AS608_ACK_TRANSFER_REJECTED, AS608_STATUS_OK, AS608_ACK_TRANSFER_REJECTED, 25U);
    test_drain_fault(AS608_STATUS_TIMEOUT);
    test_drain_fault(AS608_STATUS_IO_ERROR);
    test_drain_fault(AS608_STATUS_RECOVERY_REQUIRED);

    puts("as608 API 28 download_feature: PASS");
    return 0;
}
