/** @file test_as608_write_template.c @brief API 21: composite write sequencing and fault tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[1100U];
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
    for (unsigned int i = 0U; i < 1600U; ++i) {
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

/* Verify exact frames, drain gating and the original deadline through StoreChar. */
static void test_success(size_t length, uint16_t code, uint16_t page, uint8_t ack) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[AS608_TEMPLATE_MAX_BYTES];
    const uint8_t params[] = {0x0FU};
    const uint8_t down[] = {0x09U, 0x01U};
    const uint8_t store[] = {0x06U, 0x01U, (uint8_t)(page >> 8U), (uint8_t)page};

    setup(&ctx, &mock);
    for (size_t i = 0U; i < sizeof(data); ++i) {
        data[i] = pattern(i);
    }

    mock.now = 10U;
    parameters(&mock, AS608_ACK_OK, code, 17U);
    assert(as608_write_template_start(&ctx, page, data, length) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);

    for (unsigned int i = 0U; i < 100U; ++i) {
        assert(step(&ctx, &mock) == AS608_PENDING);
    }

    size_t pos = check_frame(&mock, 0U, AS608_PACKET_COMMAND, params, sizeof(params));
    pos = check_frame(&mock, pos, AS608_PACKET_COMMAND, down, sizeof(down));
    assert(pos == mock.tx_length && mock.drain_calls == 0U);

    mock.now = 60U;
    append_ack(&mock, AS608_ACK_OK);
    assert(run(&ctx, &mock, true) == AS608_PENDING);

    const size_t chunk = (size_t)32U << code;
    for (size_t offset = 0U; offset < length; offset += chunk) {
        const uint8_t kind = (offset + chunk == length) ? AS608_PACKET_END_DATA : AS608_PACKET_DATA;
        pos = check_frame(&mock, pos, kind, data + offset, chunk);
    }

    assert(pos == mock.tx_length);
    for (unsigned int i = 0U; i < 10U; ++i) {
        assert(step(&ctx, &mock) == AS608_PENDING);
        assert(mock.tx_length == pos);
    }

    mock.idle = 1;
    for (unsigned int i = 0U; i < 50U; ++i) {
        assert(step(&ctx, &mock) == AS608_PENDING);
    }

    assert(check_frame(&mock, pos, AS608_PACKET_COMMAND, store, sizeof(store)) == mock.tx_length);
    assert(ctx.state == AS608_STATE_BUSY && !as608_get_result(&ctx, &result));
    assert(ctx.transaction.started_ms == 10U);
    append_ack(&mock, ack);
    assert(run(&ctx, &mock, false) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == ack);
    assert(mock.tx_length == pos + 15U);
    for (size_t i = 0U; i < sizeof(data); ++i) {
        assert(data[i] == pattern(i));
    }
}

/* Verify negative or malformed preliminary replies prevent flash writes. */
static void test_reject(uint8_t ack, uint16_t code, uint8_t down, as608_status_t expected, size_t sent) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[512U] = {0};

    setup(&ctx, &mock);
    parameters(&mock, ack, code, (ack == AS608_ACK_OK) ? 17U : 1U);
    append_ack(&mock, down);
    assert(as608_write_template_start(&ctx, 1U, data, sizeof(data)) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, false) == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);
    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ((ack != AS608_ACK_OK) ? ack : down));
    }

    assert(mock.tx_length == sent && mock.drain_calls == 0U);
}

/* Verify drain failure prevents StoreChar and final-ACK failure is quarantined. */
static void test_fault(as608_status_t expected, bool after_store) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[512U] = {0};

    setup(&ctx, &mock);
    parameters(&mock, AS608_ACK_OK, 2U, 17U);
    append_ack(&mock, AS608_ACK_OK);
    assert(as608_write_template_start(&ctx, 1U, data, sizeof(data)) == AS608_STATUS_OK);
    mock.now = 99U;
    assert(run(&ctx, &mock, true) == AS608_PENDING);
    const size_t sent = mock.tx_length;

    if (after_store) {
        mock.idle = 1;
        for (unsigned int i = 0U; i < 50U; ++i) {
            assert(step(&ctx, &mock) == AS608_PENDING);
        }

        assert(mock.tx_length == sent + 15U);
    }

    if (expected == AS608_STATUS_RECOVERY_REQUIRED) {
        assert(as608_abort(&ctx) == AS608_STATUS_OK);
    } else if (expected == AS608_STATUS_TIMEOUT) {
        mock.now = 100U;
    } else if (expected == AS608_STATUS_IO_ERROR) {
        mock.idle = -1;
    } else {
        append_ack(&mock, AS608_ACK_OK);
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }

    assert(run(&ctx, &mock, false) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected && ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(mock.tx_length == sent + (after_store ? 15U : 0U));
    assert(as608_write_template_start(&ctx, 1U, data, sizeof(data)) == AS608_STATUS_RECOVERY_REQUIRED);
}

/* Verify rejected requests preserve the context and job ownership. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    uint8_t data[512U] = {0};

    assert(as608_write_template_start(NULL, 1U, data, sizeof(data)) == AS608_STATUS_INVALID_ARG);
    assert(as608_write_template_start(&ctx, 1U, data, sizeof(data)) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_write_template_start(&ctx, 1U, NULL, sizeof(data)) == AS608_STATUS_INVALID_ARG);
    assert(as608_write_template_start(&ctx, 1U, data, 256U) == AS608_STATUS_INVALID_ARG);
    assert(as608_write_template_start(&ctx, 1U, data, 640U) == AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    assert(as608_write_template_start(&ctx, 1U, data, sizeof(data)) == AS608_STATUS_OK);

    unsigned char before[sizeof(ctx)];
    (void)memcpy(before, &ctx, sizeof(before));
    assert(as608_write_template_start(&ctx, 2U, data, sizeof(data)) == AS608_STATUS_BUSY);
    assert(memcmp(before, &ctx, sizeof(before)) == 0);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
}

/* Verify the composite store flag cannot leak into a later standalone download. */
static void test_reuse(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    uint8_t data[512U] = {0};

    setup(&ctx, &mock);
    mock.idle = 1;
    parameters(&mock, AS608_ACK_OK, 3U, 17U);
    append_ack(&mock, AS608_ACK_OK);
    append_ack(&mock, AS608_ACK_OK);
    assert(as608_write_template_start(&ctx, 1U, data, sizeof(data)) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, false) == AS608_COMPLETE);
    const size_t composite_bytes = mock.tx_length;

    mock.tx_length = 0U;
    mock.rx_length = 0U;
    mock.rx_pos = 0U;
    parameters(&mock, AS608_ACK_OK, 3U, 17U);
    append_ack(&mock, AS608_ACK_OK);
    assert(as608_download_template_start(&ctx, AS608_CHAR_BUFFER_2, data, sizeof(data)) == AS608_STATUS_OK);
    assert(run(&ctx, &mock, false) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(mock.tx_length + 15U == composite_bytes);
}

int main(void) {
    for (uint16_t code = 0U; code <= 3U; ++code) {
        test_success(512U, code, 0x1234U, AS608_ACK_OK);
        test_success(768U, code, UINT16_MAX, AS608_ACK_OK);
    }

    test_success(512U, 2U, 0U, AS608_ACK_FLASH_ERROR);
    test_success(512U, 2U, 299U, AS608_ACK_INVALID_PAGE);
    test_reject(AS608_ACK_PACKET_ERROR, 2U, AS608_ACK_OK, AS608_STATUS_OK, 12U);
    test_reject(AS608_ACK_OK, 4U, AS608_ACK_OK, AS608_STATUS_UNEXPECTED_REPLY, 12U);
    test_reject(AS608_ACK_OK, 2U, AS608_ACK_TRANSFER_REJECTED, AS608_STATUS_OK, 25U);
    test_fault(AS608_STATUS_TIMEOUT, false);
    test_fault(AS608_STATUS_IO_ERROR, false);
    test_fault(AS608_STATUS_RECOVERY_REQUIRED, false);
    test_fault(AS608_STATUS_TIMEOUT, true);
    test_fault(AS608_STATUS_CHECKSUM_ERROR, true);
    test_fault(AS608_STATUS_RECOVERY_REQUIRED, true);
    test_admission();
    test_reuse();

    puts("as608 API 21 write_template: PASS");
    return 0;
}
