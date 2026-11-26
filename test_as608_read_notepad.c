/** @file test_as608_read_notepad.c @brief API 41: notepad read wire and output tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[64U];
    uint8_t rx[45U];
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

/* Return the mock clock and count accesses. */
static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

/* Initialize a fresh device context. */
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

/* Generate raw page bytes with a nonuniform pattern. */
static uint8_t pattern(uint8_t page, size_t offset) {
    return (uint8_t)(page * 19U + offset * 37U);
}

/* Build an independent reply with an exact length and additive checksum. */
static void prepare_reply(mock_t *mock, uint8_t kind, uint8_t ack, uint8_t page, size_t length) {
    const uint8_t header[] = {
        0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind, 0U, (uint8_t)(length + 2U)
    };
    assert(length <= 34U);
    (void)memcpy(mock->rx, header, sizeof(header));
    if (length != 0U) {
        mock->rx[9] = ack;
        for (size_t i = 1U; i < length; ++i) {
            mock->rx[9U + i] = pattern(page, i - 1U);
        }
    }
    uint16_t sum = 0U;
    for (size_t i = 6U; i < 9U + length; ++i) {
        sum = (uint16_t)(sum + mock->rx[i]);
    }
    mock->rx[9U + length] = (uint8_t)(sum >> 8U);
    mock->rx[10U + length] = (uint8_t)sum;
    mock->rx_length = 11U + length;
    mock->rx_pos = 0U;
}

/* Poll with bounded I/O and verify caller storage is untouched while pending. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, const void *out, const void *before, size_t size) {
    for (unsigned int i = 0U; i < 150U; ++i) {
        const unsigned int io = mock->io_calls;
        const as608_progress_t progress = as608_process(ctx);
        assert(mock->io_calls - io <= 1U);
        if (progress != AS608_PENDING) {
            return progress;
        }
        assert(memcmp(out, before, size) == 0);
    }
    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Verify exact commands and all 16 page indices, including guard bytes. */
static void test_success(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    setup(&ctx, &mock);

    for (uint8_t n = 0U; n < 16U; ++n) {
        struct {
            uint8_t before;
            as608_notepad_page_t page;
            uint8_t after;
        } out;
        unsigned char saved[sizeof(out)];
        const uint8_t command[] = {
            0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 4U, 0x19U, n, 0U, (uint8_t)(0x1EU + n)
        };
        (void)memset(&out, 0xA5, sizeof(out));
        out.page.page = n;
        (void)memcpy(saved, &out, sizeof(saved));
        mock.tx_length = 0U;
        mock.io_calls = 0U;
        mock.clock_calls = 0U;
        prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, n, 33U);
        assert(as608_read_notepad_start(&ctx, &out.page) == AS608_STATUS_OK);
        assert(ctx.reply_output == out.page.data);
        assert(mock.io_calls == 0U && mock.clock_calls == 1U);
        assert(memcmp(saved, &out, sizeof(saved)) == 0);
        assert(!as608_get_result(&ctx, &result));
        assert(run(&ctx, &mock, &out, saved, sizeof(out)) == AS608_COMPLETE);
        assert(as608_get_result(&ctx, &result));
        assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
        assert(out.page.page == n && out.before == 0xA5U && out.after == 0xA5U);
        for (size_t i = 0U; i < sizeof(out.page.data); ++i) {
            assert(out.page.data[i] == pattern(n, i));
        }
        assert(mock.tx_length == sizeof(command) && memcmp(mock.tx, command, sizeof(command)) == 0);
    }
}

/* Verify negative ACKs and malformed packets cannot change the page. */
static void test_reply(uint8_t kind, uint8_t ack, size_t length, bool corrupt, as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_notepad_page_t out = {15U, {0}};
    unsigned char saved[sizeof(out)];
    as608_result_t result;
    setup(&ctx, &mock);
    (void)memset(out.data, 0xA5, sizeof(out.data));
    (void)memcpy(saved, &out, sizeof(saved));
    prepare_reply(&mock, kind, ack, out.page, length);
    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }
    assert(as608_read_notepad_start(&ctx, &out) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &out, saved, sizeof(out)) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result) && result.status == expected);
    assert(ctx.state == ((expected == AS608_STATUS_OK) ? AS608_STATE_DONE : AS608_STATE_DESYNCHRONIZED));
    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack);
    }
    assert(memcmp(saved, &out, sizeof(saved)) == 0);
}

/* Verify admission rejection never modifies context storage. */
static void assert_rejected(as608_t *ctx, as608_notepad_page_t *page, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_read_notepad_start(ctx, page) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify partial RX timeout and abort leave the entire result unchanged. */
static void test_fault(bool abort_job) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_notepad_page_t page = {7U, {0}};
    unsigned char saved[sizeof(page)];
    (void)memset(page.data, 0xA5, sizeof(page.data));
    (void)memcpy(saved, &page, sizeof(saved));
    setup(&ctx, &mock);
    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, 7U, 33U);
    mock.rx_length -= 1U; /* Withhold the last checksum byte. */
    assert(as608_read_notepad_start(&ctx, &page) == AS608_STATUS_OK);
    for (unsigned int i = 0U; i < 150U && mock.rx_pos < mock.rx_length; ++i) {
        assert(as608_process(&ctx) == AS608_PENDING);
        assert(memcmp(saved, &page, sizeof(saved)) == 0);
    }
    assert(mock.rx_pos == mock.rx_length);
    if (abort_job) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
    } else {
        mock.now = 100U;
    }
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == (abort_job ? AS608_STATUS_RECOVERY_REQUIRED : AS608_STATUS_TIMEOUT));
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(memcmp(saved, &page, sizeof(saved)) == 0);
    assert_rejected(&ctx, &page, AS608_STATUS_RECOVERY_REQUIRED);
}

/* Verify invalid indices, BUSY, recovery and sharing with the index ACK. */
static void test_admission_and_sequence(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_notepad_page_t page = {0U, {0}}, other = {1U, {0}};
    uint8_t bitmap[32U] = {0};
    const uint8_t saved_bitmap[32U] = {0};
    unsigned char saved[sizeof(page)], saved_other[sizeof(other)];
    as608_result_t result;
    assert(as608_read_notepad_start(NULL, &page) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, &page, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, NULL, AS608_STATUS_INVALID_ARG);
    page.page = 16U;
    assert_rejected(&ctx, &page, AS608_STATUS_INVALID_ARG);
    page.page = 255U;
    assert_rejected(&ctx, &page, AS608_STATUS_INVALID_ARG);
    page.page = 0U;
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    (void)memcpy(saved, &page, sizeof(saved));
    (void)memcpy(saved_other, &other, sizeof(saved_other));
    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, 0U, 33U);
    assert(as608_read_notepad_start(&ctx, &page) == AS608_STATUS_OK);
    assert_rejected(&ctx, &other, AS608_STATUS_BUSY);
    assert(as608_read_index_table_start(&ctx, 0U, bitmap, sizeof(bitmap)) == AS608_STATUS_BUSY);
    assert(ctx.reply_output == page.data);
    assert(run(&ctx, &mock, &page, saved, sizeof(page)) == AS608_COMPLETE);
    assert(memcmp(saved_other, &other, sizeof(other)) == 0);
    assert(memcmp(saved_bitmap, bitmap, sizeof(bitmap)) == 0);
    (void)memcpy(saved, &page, sizeof(saved));

    mock.tx_length = 0U;
    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, 3U, 33U);
    assert(as608_read_index_table_start(&ctx, 3U, bitmap, sizeof(bitmap)) == AS608_STATUS_OK);
    assert(ctx.reply_output == bitmap);
    assert(run(&ctx, &mock, bitmap, saved_bitmap, sizeof(bitmap)) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result) && result.confirmation == AS608_ACK_OK);
    for (size_t i = 0U; i < sizeof(bitmap); ++i) {
        assert(bitmap[i] == pattern(3U, i));
    }
    assert(memcmp(saved, &page, sizeof(saved)) == 0);

    mock.tx_length = 0U;
    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, 1U, 33U);
    assert(as608_read_notepad_start(&ctx, &other) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &other, saved_other, sizeof(other)) == AS608_COMPLETE);
    assert(other.page == 1U && memcmp(saved, &page, sizeof(saved)) == 0);
    for (size_t i = 0U; i < sizeof(other.data); ++i) {
        assert(other.data[i] == pattern(1U, i));
        assert(bitmap[i] == pattern(3U, i));
    }
}

int main(void) {
    test_success();
    const uint8_t negative[] = {AS608_ACK_PACKET_ERROR, AS608_ACK_FLASH_ERROR, AS608_ACK_INVALID_NOTE_PAGE, 0x42U};
    for (size_t i = 0U; i < sizeof(negative); ++i) {
        test_reply(AS608_PACKET_ACK, negative[i], 1U, false, AS608_STATUS_OK);
        test_reply(AS608_PACKET_ACK, negative[i], 33U, false, AS608_STATUS_OK);
    }
    for (size_t length = 1U; length < 33U; ++length) {
        test_reply(AS608_PACKET_ACK, AS608_ACK_OK, length, false, AS608_STATUS_UNEXPECTED_REPLY);
    }
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 34U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 2U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_DATA, AS608_ACK_OK, 33U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 33U, true, AS608_STATUS_CHECKSUM_ERROR);
    test_fault(false);
    test_fault(true);
    test_admission_and_sequence();
    puts("as608 API 41 read_notepad: PASS");
    return 0;
}
