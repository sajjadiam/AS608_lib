/** @file test_as608_high_speed_search.c @brief API 26: high-speed search wire and range tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define AS608_TEST_TX_CAPACITY 17U
#define AS608_TEST_RX_CAPACITY 17U
#define AS608_TEST_DRAIN 0
#include "tests/common/mock_uart.h"

/* Construct an independent ACK or malformed reply. */
static void prepare_reply(mock_t *mock, uint8_t kind, uint8_t ack, uint16_t page, uint16_t score, size_t length) {
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind, 0x00U, (uint8_t)(length + 2U)
    };
    const uint8_t payload[] = {
        ack, (uint8_t)(page >> 8U), (uint8_t)page,
        (uint8_t)(score >> 8U), (uint8_t)score, 0U
    };

    assert((length >= 1U) && (length <= sizeof(payload)));
    (void)memcpy(mock->rx, header, sizeof(header));
    (void)memcpy(mock->rx + sizeof(header), payload, length);

    uint16_t sum = 0U;
    for (size_t i = 6U; i < 9U + length; ++i) {
        sum = (uint16_t)(sum + mock->rx[i]);
    }

    mock->rx[9U + length] = (uint8_t)(sum >> 8U);
    mock->rx[10U + length] = (uint8_t)sum;
    mock->rx_length = 11U + length;
    mock->rx_pos = 0U;
}

/* Verify pending polls leave the caller's match unchanged. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, const as608_search_t *search) {
    const as608_match_t before = search->match;

    for (unsigned int i = 0U; i < 100U; ++i) {
        const unsigned int io = mock->io_calls;
        const as608_progress_t progress = as608_process(ctx);
        assert(mock->io_calls - io <= 1U);

        if (progress != AS608_PENDING) {
            return progress;
        }

        assert(search->match.page_id == before.page_id);
        assert(search->match.score == before.score);
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Verify exact command bytes, both range boundaries and 16-bit scores. */
static void test_success(void) {
    static const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
        0x08U, 0x1BU, 0x02U, 0x01U, 0x00U, 0x01U, 0x2CU, 0x00U, 0x54U
    };
    const uint16_t pages[] = {256U, 555U};
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_search_t search = {AS608_CHAR_BUFFER_2, 256U, 300U, {0xA55AU, 0x5AA5U}};

    setup(&ctx, &mock);

    for (size_t i = 0U; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        mock.tx_length = 0U;
        mock.io_calls = 0U;
        mock.clock_calls = 0U;
        prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, pages[i], 0x1234U, 5U);

        assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_OK);
        assert(mock.io_calls == 0U && mock.clock_calls == 1U);
        assert(run(&ctx, &mock, &search) == AS608_COMPLETE);
        assert(as608_get_result(&ctx, &result));
        assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
        assert(search.match.page_id == pages[i] && search.match.score == 0x1234U);
        assert(search.buffer == AS608_CHAR_BUFFER_2 && search.start_page == 256U && search.page_count == 300U);
        assert(mock.tx_length == sizeof(command));
        assert(memcmp(mock.tx, command, sizeof(command)) == 0);
    }
}

/* Test negative ACKs and malformed or out-of-range successful replies. */
static void test_reply(uint8_t kind, uint8_t ack, uint16_t page, size_t length, bool corrupt, as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_search_t search = {AS608_CHAR_BUFFER_1, 10U, 20U, {123U, 456U}};
    as608_result_t result;

    setup(&ctx, &mock);
    prepare_reply(&mock, kind, ack, page, 0U, length);

    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }

    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &search) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);
    assert(ctx.state == ((expected == AS608_STATUS_OK) ? AS608_STATE_DONE : AS608_STATE_DESYNCHRONIZED));

    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack);
    }

    assert(search.match.page_id == 123U && search.match.score == 456U);
}

/* Test invalid requests, full-width PageIDs, busy rejection, abort and timeout. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_search_t search = {AS608_CHAR_BUFFER_1, UINT16_MAX, 1U, {123U, 456U}};
    as608_search_t other = {AS608_CHAR_BUFFER_2, 0U, 1U, {1U, 2U}};
    as608_result_t result;

    assert(as608_high_speed_search_start(NULL, &search) == AS608_STATUS_INVALID_ARG);
    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_high_speed_search_start(&ctx, NULL) == AS608_STATUS_INVALID_ARG);
    search.page_count = 0U;
    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_INVALID_ARG);
    search.page_count = 2U;
    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_INVALID_ARG);
    search.page_count = 1U;
    search.buffer = (as608_char_buffer_t)3;
    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_INVALID_ARG);
    search.buffer = AS608_CHAR_BUFFER_1;
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);

    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, UINT16_MAX, 42U, 5U);
    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_OK);
    assert(as608_high_speed_search_start(&ctx, &other) == AS608_STATUS_BUSY);
    assert(ctx.reply_output == &search);
    assert(run(&ctx, &mock, &search) == AS608_COMPLETE);
    assert(search.match.page_id == UINT16_MAX && search.match.score == 42U);
    assert(other.match.page_id == 1U && other.match.score == 2U);

    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(search.match.page_id == UINT16_MAX && search.match.score == 42U);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_high_speed_search_start(&ctx, &search) == AS608_STATUS_OK);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert(search.match.page_id == UINT16_MAX && search.match.score == 42U);
}

int main(void) {
    test_success();
    test_reply(AS608_PACKET_ACK, AS608_ACK_NOT_FOUND, 0U, 5U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_NOT_FOUND, 0U, 1U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 0U, 1U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, 0xA5U, 0U, 5U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_NOT_FOUND, 0U, 3U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 9U, 5U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 30U, 5U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 10U, 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 10U, 4U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 10U, 6U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_DATA, AS608_ACK_OK, 10U, 5U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 10U, 5U, true, AS608_STATUS_CHECKSUM_ERROR);
    test_admission();

    puts("as608 API 26 high_speed_search: PASS");
    return 0;
}
