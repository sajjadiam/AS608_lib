/** @file test_as608_burn_code.c @brief API 43: explicit experimental burn handshake tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[1024U];
    uint8_t rx[128U];
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
    size_t offset;
    size_t calls;
    bool wait;
    int32_t error;
} source_t;

/* Supply deterministic complete fragments, or simulate a stall/fault. */
static int32_t produce(void *user, uint8_t *data, size_t length) {
    source_t *source = user;
    ++source->calls;
    if (source->error != 0) {
        return source->error;
    }
    if (source->wait) {
        return 0;
    }
    for (size_t i = 0U; i < length; ++i) {
        data[i] = pattern(source->offset + i);
    }
    source->offset += length;
    return (int32_t)length;
}

/* Construct an explicitly authorized synthetic test profile. */
static as608_firmware_request_t request_for(source_t *source, as608_packet_size_t size) {
    const as608_firmware_request_t request = {
        AS608_BURN_FULL_FIRMWARE, {source, produce, 512U},
        size, AS608_BURN_PROFILE_ACK_EACH, true
    };
    return request;
}

/* Poll a bounded interval while no terminal ACK is available. */
static void pending(as608_t *ctx, mock_t *mock, unsigned int count) {
    for (unsigned int i = 0U; i < count; ++i) {
        assert(step(ctx, mock) == AS608_PENDING);
    }
}

/* Verify every fragment is gated by its predecessor's ACK. */
static void test_success(as608_packet_size_t size, as608_burn_mode_t mode) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    source_t source = {0};
    as608_firmware_request_t request = request_for(&source, size);
    request.mode = mode;
    setup(&ctx, &mock);
    assert(as608_burn_code_start(&ctx, &request) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U && source.calls == 0U);
    request = (as608_firmware_request_t){0};
    pending(&ctx, &mock, 80U);
    const uint8_t command[] = {0x1AU, (uint8_t)mode};
    size_t pos = check_frame(&mock, 0U, AS608_PACKET_COMMAND, command, sizeof(command));
    assert(pos == mock.tx_length && source.calls == 0U);
    source.wait = true;
    append_ack(&mock, AS608_ACK_OK);
    pending(&ctx, &mock, 80U);
    assert(source.calls > 0U && source.offset == 0U && mock.tx_length == pos);
    source.wait = false;
    const size_t chunk = (size_t)32U << (uint8_t)size;
    uint8_t data[256U];
    for (size_t offset = 0U; offset < 512U; offset += chunk) {
        pending(&ctx, &mock, 300U);
        for (size_t i = 0U; i < chunk; ++i) {
            data[i] = pattern(offset + i);
        }
        const bool end = offset + chunk == 512U;
        pos = check_frame(&mock, pos, end ? AS608_PACKET_END_DATA : AS608_PACKET_DATA, data, chunk);
        assert(pos == mock.tx_length && source.offset == offset + chunk);
        assert(!as608_get_result(&ctx, &result));
        const size_t calls = source.calls;
        pending(&ctx, &mock, 10U);
        assert(source.calls == calls && mock.tx_length == pos);
        append_ack(&mock, end ? AS608_ACK_OK : AS608_ACK_DATA_ACCEPTED);
    }
    assert(run(&ctx, &mock, false) == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(ctx.state == AS608_STATE_DONE_RECOVERY_REQUIRED && mock.drain_calls == 0U);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_abort(&ctx) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(step(&ctx, &mock) == AS608_COMPLETE && mock.tx_length == pos);
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(!as608_get_result(&ctx, &result));
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_OK);
}

/* Verify explicit authorization and profile are required before any I/O. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    source_t source = {0};
    as608_firmware_request_t r = request_for(&source, AS608_PACKET_SIZE_64);
    assert(as608_burn_code_start(NULL, &r) == AS608_STATUS_INVALID_ARG);
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    unsigned char before[sizeof(ctx)];
    (void)memcpy(before, &ctx, sizeof(before));
    assert(as608_burn_code_start(&ctx, NULL) == AS608_STATUS_INVALID_ARG);
    r.authorized = false;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.authorized = true;
    r.profile = AS608_BURN_PROFILE_DISABLED;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.profile = (as608_burn_profile_t)2;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.profile = AS608_BURN_PROFILE_ACK_EACH;
    r.mode = (as608_burn_mode_t)256;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.mode = AS608_BURN_INFORMATION_PAGE;
    r.packet_size = AS608_PACKET_SIZE_32;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.packet_size = (as608_packet_size_t)256;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.packet_size = AS608_PACKET_SIZE_64;
    r.source.length = 0U;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.source.length = 513U;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    r.source.length = 512U;
    r.source.read = NULL;
    assert(as608_burn_code_start(&ctx, &r) == AS608_STATUS_INVALID_ARG);
    assert(memcmp(before, &ctx, sizeof(before)) == 0);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U && source.calls == 0U);
}

/* Exercise terminal sensor errors and incorrect phase confirmations. */
static void test_ack(uint8_t ack, unsigned int phase, bool valid_negative) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    source_t source = {0};
    as608_firmware_request_t request = request_for(&source, AS608_PACKET_SIZE_256);
    setup(&ctx, &mock);
    assert(as608_burn_code_start(&ctx, &request) == AS608_STATUS_OK);
    assert(as608_burn_code_start(&ctx, &request) == AS608_STATUS_BUSY);
    if (phase > 0U) {
        append_ack(&mock, AS608_ACK_OK);
        pending(&ctx, &mock, 350U);
    }
    if (phase > 1U) {
        append_ack(&mock, AS608_ACK_DATA_ACCEPTED);
        pending(&ctx, &mock, 350U);
    }
    if (phase == 0U) {
        pending(&ctx, &mock, 80U);
        assert(mock.tx_length == 13U && source.calls == 0U);
    }
    const size_t sent = mock.tx_length;
    const size_t calls = source.calls;
    append_ack(&mock, ack);
    assert(run(&ctx, &mock, false) == (valid_negative ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == (valid_negative ? AS608_STATUS_OK : AS608_STATUS_UNEXPECTED_REPLY));
    if (valid_negative) {
        assert(result.confirmation == ack && ctx.state == AS608_STATE_DONE_RECOVERY_REQUIRED);
    } else {
        assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    }
    assert(source.calls == calls && mock.tx_length == sent);
    assert(as608_burn_code_start(&ctx, &request) == AS608_STATUS_RECOVERY_REQUIRED);
}

/* Test source stalls, invalid returns, missing final ACK and abort. */
static void test_fault(unsigned int fault) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    source_t source = {0};
    as608_firmware_request_t request = request_for(&source, AS608_PACKET_SIZE_256);
    setup(&ctx, &mock);
    mock.now = UINT32_MAX - 49U;
    assert(as608_burn_code_start(&ctx, &request) == AS608_STATUS_OK);
    as608_status_t expected = AS608_STATUS_TIMEOUT;
    if (fault == 0U) {
        source.wait = true;
        append_ack(&mock, AS608_ACK_OK);
        pending(&ctx, &mock, 100U);
        mock.now = 50U;
    } else if ((fault <= 3U) || (fault >= 7U)) {
        const int32_t invalid[] = {INT32_MIN, INT32_MAX, 65536, 65792};
        source.error = (fault >= 7U) ? invalid[fault - 7U] : (fault == 1U ? -1 : fault == 2U ? 1 : 257);
        append_ack(&mock, AS608_ACK_OK);
        expected = AS608_STATUS_IO_ERROR;
    } else {
        append_ack(&mock, AS608_ACK_OK);
        pending(&ctx, &mock, 350U);
        append_ack(&mock, AS608_ACK_DATA_ACCEPTED);
        pending(&ctx, &mock, 350U);
        assert(source.offset == 512U);
        if (fault == 4U) {
            mock.now = 50U;
        } else if (fault == 5U) {
            assert(as608_abort(&ctx) == AS608_STATUS_OK);
            expected = AS608_STATUS_RECOVERY_REQUIRED;
        } else {
            append_ack(&mock, AS608_ACK_OK);
            mock.rx[mock.rx_length - 1U] ^= 1U;
            expected = AS608_STATUS_CHECKSUM_ERROR;
        }
    }
    assert(run(&ctx, &mock, false) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result) && result.status == expected);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    const size_t sent = mock.tx_length;
    const size_t calls = source.calls;
    assert(step(&ctx, &mock) == AS608_FAILED);
    assert(mock.tx_length == sent && source.calls == calls);
    assert(as608_burn_code_start(&ctx, &request) == AS608_STATUS_RECOVERY_REQUIRED);
}

int main(void) {
    test_admission();
    for (unsigned int code = 1U; code <= 3U; ++code) {
        test_success((as608_packet_size_t)code, AS608_BURN_INFORMATION_PAGE);
        test_success((as608_packet_size_t)code, AS608_BURN_FULL_FIRMWARE);
    }
    test_ack(AS608_ACK_TRANSFER_REJECTED, 0U, true);
    test_ack(AS608_ACK_COMMAND_ACCEPTED, 0U, false);
    test_ack(AS608_ACK_OK, 1U, false);
    test_ack(AS608_ACK_DATA_ACCEPTED, 2U, false);
    for (uint8_t code = 0xF2U; code <= 0xF6U; ++code) {
        test_ack(code, 1U, true);
        test_ack(code, 2U, true);
    }
    for (unsigned int fault = 0U; fault < 11U; ++fault) {
        test_fault(fault);
    }
    puts("as608 API 43 burn_code experimental profile: PASS");
    return 0;
}
