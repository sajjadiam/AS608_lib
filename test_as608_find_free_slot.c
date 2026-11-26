/** @file test_as608_find_free_slot.c @brief API 46: capacity-bounded bitmap search tests. */
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

/* Build a ReadSysPara ACK with independently encoded capacity. */
static void params(mock_t *mock, uint8_t ack, uint16_t capacity, size_t length) {
    const uint8_t payload[18U] = {
        ack, 0U, 9U, 0U, 2U, (uint8_t)(capacity >> 8U), (uint8_t)capacity,
        0U, 3U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0U, 2U, 0U, 6U, 0U
    };
    assert(length <= sizeof(payload));
    append_frame(mock, AS608_PACKET_ACK, payload, length);
}

/* Build an independent bitmap, with padding bits clear beyond capacity. */
static void bitmap(mock_t *mock, uint16_t capacity, uint8_t page, uint16_t free_id) {
    uint8_t payload[33U] = {AS608_ACK_OK};
    const unsigned int base = (unsigned int)page * 256U;
    for (unsigned int i = 0U; i < 256U; ++i) {
        const unsigned int id = base + i;
        if ((id < capacity) && (id != free_id)) {
            payload[1U + i / 8U] |= (uint8_t)(1U << (i % 8U));
        }
    }
    append_frame(mock, AS608_PACKET_ACK, payload, sizeof(payload));
}

/* Poll with a strict bound and verify output is untouched while pending. */
static as608_progress_t run(as608_t *ctx, mock_t *mock, const as608_free_slot_t *slot) {
    unsigned char before[sizeof(*slot)];
    (void)memcpy(before, slot, sizeof(before));
    for (unsigned int i = 0U; i < 600U; ++i) {
        const as608_progress_t progress = step(ctx, mock);
        if (progress != AS608_PENDING) {
            return progress;
        }
        assert(memcmp(before, slot, sizeof(before)) == 0);
    }
    assert(0 && "job stalled");
    return AS608_FAILED;
}

/* Independently verify only the required index pages were requested. */
static void assert_wire(const mock_t *mock, unsigned int pages) {
    const uint8_t read_params[] = {0x0FU};
    size_t pos = check_frame(mock, 0U, AS608_PACKET_COMMAND, read_params, sizeof(read_params));
    for (unsigned int i = 0U; i < pages; ++i) {
        const uint8_t query[] = {0x1FU, (uint8_t)i};
        pos = check_frame(mock, pos, AS608_PACKET_COMMAND, query, sizeof(query));
    }
    assert(pos == mock->tx_length);
}

/* Verify exact first-free ID, early stop, full database and padded tail. */
static void test_success(uint16_t capacity, uint16_t free_id) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    struct {
        uint8_t before;
        as608_free_slot_t slot;
        uint8_t after;
    } out = {0xA5U, {false, 0xA55AU}, 0x5AU};
    const bool found = free_id < capacity;
    out.slot.found = !found;
    setup(&ctx, &mock);
    params(&mock, AS608_ACK_OK, capacity, 17U);
    const unsigned int pages = found ? (unsigned int)free_id / 256U + 1U :
        ((unsigned int)capacity + 255U) / 256U;
    for (unsigned int page = 0U; page < pages; ++page) {
        bitmap(&mock, capacity, (uint8_t)page, free_id);
    }
    assert(as608_find_free_slot_start(&ctx, &out.slot) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert(!as608_get_result(&ctx, &result));
    assert(run(&ctx, &mock, &out.slot) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_OK &&
           result.confirmation == AS608_ACK_OK);
    assert(out.slot.found == found && out.slot.page_id == (found ? free_id : 0U));
    assert(out.before == 0xA5U && out.after == 0x5AU);
    assert(mock.rx_pos == mock.rx_length);
    assert_wire(&mock, pages);
    const unsigned int io = mock.io_calls;
    assert(step(&ctx, &mock) == AS608_COMPLETE && mock.io_calls == io);
}

/* Verify a byte containing several zeros returns its lowest clear bit. */
static void test_multiple_free(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_free_slot_t slot = {false, 999U};
    uint8_t payload[33U];
    (void)memset(payload, 0xFF, sizeof(payload));
    payload[0] = AS608_ACK_OK;
    payload[1] = 0xD7U; /* Bits 3 and 5 clear. */
    payload[2] = 0U;
    setup(&ctx, &mock);
    params(&mock, AS608_ACK_OK, 300U, 17U);
    append_frame(&mock, AS608_PACKET_ACK, payload, sizeof(payload));
    assert(as608_find_free_slot_start(&ctx, &slot) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &slot) == AS608_COMPLETE);
    assert(slot.found && slot.page_id == 3U);
    assert_wire(&mock, 1U);
}

/* Reject unsupported capacities rather than claiming a full database. */
static void test_capacity(uint16_t capacity) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_free_slot_t slot = {true, 999U};
    as608_result_t result;
    setup(&ctx, &mock);
    params(&mock, AS608_ACK_OK, capacity, 17U);
    assert(as608_find_free_slot_start(&ctx, &slot) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &slot) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_UNEXPECTED_REPLY);
    assert(slot.found && slot.page_id == 999U && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert_wire(&mock, 0U);
}

/* Verify negative/malformed ACKs at parameters or any index page. */
static void test_reply(unsigned int phase, uint8_t kind, uint8_t ack, size_t length,
                       bool corrupt, as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_free_slot_t slot = {true, 999U};
    as608_result_t result;
    uint8_t payload[34U] = {0};
    payload[0] = ack;
    if (phase > 0U) {
        params(&mock, AS608_ACK_OK, 1024U, 17U);
        for (unsigned int i = 1U; i < phase; ++i) {
            bitmap(&mock, 1024U, (uint8_t)(i - 1U), UINT16_MAX);
        }
    }
    append_frame(&mock, kind, payload, length);
    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }
    setup(&ctx, &mock);
    assert(as608_find_free_slot_start(&ctx, &slot) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &slot) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result) && result.status == expected);
    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack && ctx.state == AS608_STATE_DONE);
    } else {
        assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    }
    assert(slot.found && slot.page_id == 999U);
    assert_wire(&mock, phase);
}

/* Verify rejected admission never changes context storage. */
static void assert_rejected(as608_t *ctx, as608_free_slot_t *slot, as608_status_t expected) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_find_free_slot_start(ctx, slot) == expected);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify timeout/abort in any phase, with one wrap-safe deadline. */
static void test_fault(unsigned int phase, bool abort_job) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_free_slot_t slot = {true, 999U}, other = {false, 777U};
    const uint8_t read_params[] = {0x0FU};
    setup(&ctx, &mock);
    mock.now = UINT32_MAX - 49U;
    const uint32_t started = mock.now;
    assert(as608_find_free_slot_start(&ctx, &slot) == AS608_STATUS_OK);
    assert_rejected(&ctx, &other, AS608_STATUS_BUSY);
    size_t pos = expect_command(&ctx, &mock, 0U, read_params, sizeof(read_params));
    for (unsigned int i = 0U; i < phase; ++i) {
        mock.now += 15U;
        if (i == 0U) {
            params(&mock, AS608_ACK_OK, 1024U, 17U);
        } else {
            bitmap(&mock, 1024U, (uint8_t)(i - 1U), UINT16_MAX);
        }
        const uint8_t query[] = {0x1FU, (uint8_t)i};
        pos = expect_command(&ctx, &mock, pos, query, sizeof(query));
        assert(ctx.transaction.started_ms == started);
        assert(slot.found && slot.page_id == 999U);
    }
    if (abort_job) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
    } else {
        mock.now = 50U;
    }
    assert(run(&ctx, &mock, &slot) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result) &&
           result.status == (abort_job ? AS608_STATUS_RECOVERY_REQUIRED : AS608_STATUS_TIMEOUT));
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED && mock.tx_length == pos);
    assert(slot.found && slot.page_id == 999U && !other.found && other.page_id == 777U);
    assert_rejected(&ctx, &slot, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_find_free_slot_start(&ctx, &slot) == AS608_STATUS_OK);
}

/* Verify initialization and consecutive jobs do not reuse old capacity/index. */
static void test_sequence(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_free_slot_t slot = {false, 999U};
    as608_parameters_t p = {0};
    uint8_t raw[32U] = {0};
    assert(as608_find_free_slot_start(NULL, &slot) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, &slot, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, NULL, AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    params(&mock, AS608_ACK_OK, 1024U, 17U);
    for (uint8_t i = 0U; i < 4U; ++i) {
        bitmap(&mock, 1024U, i, 1023U);
    }
    assert(as608_find_free_slot_start(&ctx, &slot) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &slot) == AS608_COMPLETE);
    assert(slot.found && slot.page_id == 1023U);
    mock.tx_length = mock.rx_length = mock.rx_pos = 0U;
    params(&mock, AS608_ACK_OK, 9U, 17U);
    assert(as608_read_parameters_start(&ctx, &p) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &slot) == AS608_COMPLETE && p.template_capacity == 9U);
    mock.tx_length = mock.rx_length = mock.rx_pos = 0U;
    bitmap(&mock, 9U, 0U, UINT16_MAX);
    assert(as608_read_index_table_start(&ctx, 0U, raw, sizeof(raw)) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &slot) == AS608_COMPLETE && raw[0] == 0xFFU && raw[1] == 1U);
    mock.tx_length = mock.rx_length = mock.rx_pos = 0U;
    params(&mock, AS608_ACK_OK, 9U, 17U);
    bitmap(&mock, 9U, 0U, UINT16_MAX);
    assert(as608_find_free_slot_start(&ctx, &slot) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, &slot) == AS608_COMPLETE && !slot.found && slot.page_id == 0U);
    assert_wire(&mock, 1U);
}

int main(void) {
    for (uint16_t id = 0U; id < 1024U; ++id) {
        test_success(1024U, id);
    }
    const uint16_t capacities[] = {1U, 7U, 8U, 9U, 255U, 256U, 257U, 300U,
        511U, 512U, 513U, 767U, 768U, 769U, 1023U, 1024U};
    for (size_t i = 0U; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
        test_success(capacities[i], UINT16_MAX);
        test_success(capacities[i], (uint16_t)(capacities[i] - 1U));
    }
    test_multiple_free();
    test_capacity(0U);
    test_capacity(1025U);
    test_capacity(UINT16_MAX);
    for (unsigned int phase = 0U; phase <= 4U; ++phase) {
        const size_t length = phase == 0U ? 17U : 33U;
        test_reply(phase, AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 1U, false, AS608_STATUS_OK);
        test_reply(phase, AS608_PACKET_ACK, 0x42U, length, false, AS608_STATUS_OK);
        test_reply(phase, AS608_PACKET_ACK, AS608_ACK_OK, 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
        test_reply(phase, AS608_PACKET_ACK, AS608_ACK_OK, length - 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
        test_reply(phase, AS608_PACKET_ACK, AS608_ACK_OK, length + 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
        test_reply(phase, AS608_PACKET_DATA, AS608_ACK_OK, length, false, AS608_STATUS_UNEXPECTED_REPLY);
        test_reply(phase, AS608_PACKET_ACK, AS608_ACK_OK, length, true, AS608_STATUS_CHECKSUM_ERROR);
        test_fault(phase, false);
        test_fault(phase, true);
    }
    test_sequence();
    puts("as608 API 46 find_free_slot: PASS");
    return 0;
}
