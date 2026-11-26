/** @file test_as608_identify_range.c @brief API 45: composite identification tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[512U];
    uint8_t rx[256U];
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

/* Advance one poll and verify bounded callback work. */
static as608_progress_t step(as608_t *ctx, mock_t *mock) {
    const unsigned int io = mock->io_calls;
    const unsigned int clocks = mock->clock_calls;
    const as608_progress_t progress = as608_process(ctx);
    assert(mock->io_calls - io <= 1U);
    assert(mock->clock_calls - clocks <= 1U);
    return progress;
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

/* Advance until the entire expected command is on the wire. */
static size_t expect_command(as608_t *ctx, mock_t *mock, size_t pos, const uint8_t *bytes, size_t length) {
    const size_t end = pos + length + 11U;
    for (unsigned int i = 0U; i < 100U && mock->tx_length < end; ++i) {
        assert(step(ctx, mock) == AS608_PENDING);
    }
    assert(mock->tx_length == end);
    return check_frame(mock, pos, AS608_PACKET_COMMAND, bytes, length);
}

/* Poll until terminal, verifying output is not published while pending. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, const as608_search_t *search) {
    const as608_match_t before = search->match;
    for (unsigned int i = 0U; i < 200U; ++i) {
        const as608_progress_t progress = step(ctx, mock);
        if (progress != AS608_PENDING) {
            return progress;
        }
        assert(search->match.page_id == before.page_id && search->match.score == before.score);
    }
    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Append an independently encoded five-byte Search ACK. */
static void match_ack(mock_t *mock, uint8_t ack, uint16_t page, uint16_t score, size_t length) {
    const uint8_t payload[] = {ack, (uint8_t)(page >> 8U), (uint8_t)page,
                               (uint8_t)(score >> 8U), (uint8_t)score, 0U};
    assert(length <= sizeof(payload));
    append_frame(mock, AS608_PACKET_ACK, payload, length);
}

/* Verify all three outgoing command payloads and checksum fields. */
static void assert_wire(const mock_t *mock, const as608_search_t *search, unsigned int stages) {
    const uint8_t capture[] = {0x01U};
    const uint8_t generate[] = {0x02U, (uint8_t)search->buffer};
    const uint8_t query[] = {0x04U, (uint8_t)search->buffer,
        (uint8_t)(search->start_page >> 8U), (uint8_t)search->start_page,
        (uint8_t)(search->page_count >> 8U), (uint8_t)search->page_count};
    size_t pos = check_frame(mock, 0U, AS608_PACKET_COMMAND, capture, sizeof(capture));
    if (stages > 1U) {
        pos = check_frame(mock, pos, AS608_PACKET_COMMAND, generate, sizeof(generate));
    }
    if (stages > 2U) {
        pos = check_frame(mock, pos, AS608_PACKET_COMMAND, query, sizeof(query));
    }
    assert(pos == mock->tx_length);
}

/* Verify both buffers, both range boundaries, and full-width PageID. */
static void test_success(as608_char_buffer_t buffer, uint16_t start, uint16_t count, uint16_t page) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_search_t search = {buffer, start, count, {0xA55AU, 0x5AA5U}};
    setup(&ctx, &mock);
    append_ack(&mock, AS608_ACK_OK);
    append_ack(&mock, AS608_ACK_OK);
    match_ack(&mock, AS608_ACK_OK, page, UINT16_MAX, 5U);
    assert(as608_identify_range_start(&ctx, &search) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert(!as608_get_result(&ctx, &result));
    assert(run(&ctx, &mock, &search) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(search.match.page_id == page && search.match.score == UINT16_MAX);
    assert(search.buffer == buffer && search.start_page == start && search.page_count == count);
    assert_wire(&mock, &search, 3U);
    const unsigned int io = mock.io_calls;
    assert(step(&ctx, &mock) == AS608_COMPLETE && mock.io_calls == io);

    /* The standalone Search must still operate using the shared encoder. */
    mock.tx_length = 0U;
    mock.rx_length = 0U;
    mock.rx_pos = 0U;
    match_ack(&mock, AS608_ACK_OK, page, 0U, 5U);
    assert(as608_search_start(&ctx, &search) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &search) == AS608_COMPLETE);
    assert(search.match.page_id == page && search.match.score == 0U);
    const uint8_t query[] = {0x04U, (uint8_t)buffer, (uint8_t)(start >> 8U),
        (uint8_t)start, (uint8_t)(count >> 8U), (uint8_t)count};
    assert(check_frame(&mock, 0U, AS608_PACKET_COMMAND, query, sizeof(query)) == mock.tx_length);
}

/* Verify negative ACKs and malformed results terminate without publishing. */
static void test_reply(unsigned int phase, uint8_t kind, uint8_t ack, uint16_t page,
                       size_t length, bool corrupt, as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_search_t search = {AS608_CHAR_BUFFER_1, 10U, 20U, {123U, 456U}};
    const uint8_t payload[] = {ack, (uint8_t)(page >> 8U), (uint8_t)page, 0x12U, 0x34U, 0U};
    setup(&ctx, &mock);
    for (unsigned int i = 0U; i < phase; ++i) {
        append_ack(&mock, AS608_ACK_OK);
    }
    append_frame(&mock, kind, payload, length);
    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }
    assert(as608_identify_range_start(&ctx, &search) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &search) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result) && result.status == expected);
    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack && ctx.state == AS608_STATE_DONE);
    } else {
        assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    }
    assert(search.match.page_id == 123U && search.match.score == 456U);
    assert_wire(&mock, &search, phase + 1U);
}

/* Verify rejected admission preserves the whole context. */
static void assert_rejected(as608_t *ctx, as608_search_t *search, as608_status_t expected) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_identify_range_start(ctx, search) == expected);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify the original wrap-safe deadline and abort across command boundaries. */
static void test_fault(unsigned int phase, bool abort_job) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_search_t search = {AS608_CHAR_BUFFER_2, 0U, 1U, {123U, 456U}};
    as608_search_t other = {AS608_CHAR_BUFFER_1, 1U, 1U, {7U, 8U}};
    setup(&ctx, &mock);
    mock.now = UINT32_MAX - 49U;
    const uint32_t started = mock.now;
    assert(as608_identify_range_start(&ctx, &search) == AS608_STATUS_OK);
    assert_rejected(&ctx, &other, AS608_STATUS_BUSY);
    const uint8_t capture[] = {0x01U};
    const uint8_t generate[] = {0x02U, 2U};
    const uint8_t query[] = {0x04U, 2U, 0U, 0U, 0U, 1U};
    const uint8_t *const commands[] = {capture, generate, query};
    const size_t lengths[] = {sizeof(capture), sizeof(generate), sizeof(query)};
    size_t pos = 0U;
    for (unsigned int i = 0U; i <= phase; ++i) {
        pos = expect_command(&ctx, &mock, pos, commands[i], lengths[i]);
        assert(ctx.transaction.started_ms == started);
        if (i < phase) {
            mock.now += 30U;
            append_ack(&mock, AS608_ACK_OK);
        }
    }
    if (abort_job) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
    } else {
        mock.now = 50U;
    }
    assert(run(&ctx, &mock, &search) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == (abort_job ? AS608_STATUS_RECOVERY_REQUIRED : AS608_STATUS_TIMEOUT));
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED && mock.tx_length == pos);
    assert(search.match.page_id == 123U && search.match.score == 456U);
    assert(other.match.page_id == 7U && other.match.score == 8U);
    assert_rejected(&ctx, &search, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_identify_range_start(&ctx, &search) == AS608_STATUS_OK);
}

/* Verify all request fields are validated before command admission. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_search_t search = {AS608_CHAR_BUFFER_1, UINT16_MAX, 1U, {123U, 456U}};
    assert(as608_identify_range_start(NULL, &search) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, &search, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, NULL, AS608_STATUS_INVALID_ARG);
    search.page_count = 0U;
    assert_rejected(&ctx, &search, AS608_STATUS_INVALID_ARG);
    search.page_count = 2U;
    assert_rejected(&ctx, &search, AS608_STATUS_INVALID_ARG);
    search.page_count = 1U;
    search.buffer = (as608_char_buffer_t)257;
    assert_rejected(&ctx, &search, AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    assert(search.match.page_id == 123U && search.match.score == 456U);
}

int main(void) {
    test_admission();
    test_success(AS608_CHAR_BUFFER_1, 10U, 20U, 10U);
    test_success(AS608_CHAR_BUFFER_2, 256U, 300U, 555U);
    test_success(AS608_CHAR_BUFFER_2, UINT16_MAX, 1U, UINT16_MAX);
    test_success(AS608_CHAR_BUFFER_1, 0U, 1U, 0U);
    test_reply(0U, AS608_PACKET_ACK, AS608_ACK_NO_FINGER, 0U, 1U, false, AS608_STATUS_OK);
    test_reply(0U, AS608_PACKET_ACK, AS608_ACK_IMAGE_ERROR, 0U, 1U, false, AS608_STATUS_OK);
    test_reply(1U, AS608_PACKET_ACK, AS608_ACK_IMAGE_MESSY, 0U, 1U, false, AS608_STATUS_OK);
    test_reply(2U, AS608_PACKET_ACK, AS608_ACK_NOT_FOUND, 0U, 1U, false, AS608_STATUS_OK);
    test_reply(2U, AS608_PACKET_ACK, AS608_ACK_NOT_FOUND, 0U, 5U, false, AS608_STATUS_OK);
    test_reply(2U, AS608_PACKET_ACK, 0x42U, 0U, 1U, false, AS608_STATUS_OK);
    for (unsigned int phase = 0U; phase < 3U; ++phase) {
        const size_t length = phase == 2U ? 5U : 1U;
        test_reply(phase, AS608_PACKET_DATA, AS608_ACK_OK, 10U, length, false, AS608_STATUS_UNEXPECTED_REPLY);
        test_reply(phase, AS608_PACKET_ACK, AS608_ACK_OK, 10U, length, true, AS608_STATUS_CHECKSUM_ERROR);
        test_reply(phase, AS608_PACKET_ACK, AS608_ACK_OK, 10U, length + 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
        test_fault(phase, false);
        test_fault(phase, true);
    }
    test_reply(2U, AS608_PACKET_ACK, AS608_ACK_OK, 10U, 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(2U, AS608_PACKET_ACK, AS608_ACK_OK, 9U, 5U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(2U, AS608_PACKET_ACK, AS608_ACK_OK, 30U, 5U, false, AS608_STATUS_UNEXPECTED_REPLY);
    puts("as608 API 45 identify_range: PASS");
    return 0;
}
