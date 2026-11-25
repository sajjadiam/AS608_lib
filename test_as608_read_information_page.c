/** @file test_as608_read_information_page.c @brief API 10: information-page stream tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[800U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_pos;
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
} mock_t;

static int32_t write_byte(void *user, const uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U) && (mock->tx_length < sizeof(mock->tx)));
    ++mock->io_calls;
    mock->tx[mock->tx_length++] = *data;
    return 1;
}

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

static int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    return 1;
}

static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

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

static uint8_t pattern(size_t offset) {
    return (uint8_t)(offset * 13U + 7U);
}

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

static void append_ack(mock_t *mock, uint8_t ack) {
    append_frame(mock, AS608_PACKET_ACK, &ack, 1U);
}

static void append_data(mock_t *mock, uint8_t kind, size_t offset, size_t length) {
    uint8_t data[256U];

    assert(length <= sizeof(data));
    for (size_t i = 0U; i < length; ++i) {
        data[i] = pattern(offset + i);
    }

    append_frame(mock, kind, data, length);
}

static as608_progress_t step(as608_t *ctx, mock_t *mock) {
    const unsigned int io = mock->io_calls;
    const unsigned int clocks = mock->clock_calls;
    const as608_progress_t progress = as608_process(ctx);
    assert(mock->io_calls - io <= 1U);
    assert(mock->clock_calls - clocks <= 1U);
    return progress;
}

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
    append_ack(&mock, AS608_ACK_OK);

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
    append_ack(&mock, AS608_ACK_OK);
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
            append_ack(&mock, AS608_ACK_OK);
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
            assert(step(&ctx, &mock) == AS608_PENDING);
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
    append_ack(&mock, AS608_ACK_UPLOAD_ERROR);
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
