/** @file test_as608_verify_password.c @brief API 07: PS_ReadSysPara wire and output tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[29U];
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

/* Independent reply construction: payload length includes confirmation. */
static void prepare_reply(mock_t *mock, uint8_t kind, uint8_t ack, size_t length) {
    static const uint8_t parameters[16U] = {
        0x00U, 0x09U, 0x00U, 0x02U, 0x01U, 0x2CU, 0x00U, 0x03U,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U, 0x02U, 0x00U, 0x06U
    };
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, kind, 0x00U, (uint8_t)(length + 2U)
    };

    assert((length >= 1U) && (length <= 18U));
    (void)memcpy(mock->rx, header, sizeof(header));
    mock->rx[9] = ack;

    for (size_t i = 1U; i < length; ++i) {
        mock->rx[9U + i] = (i <= sizeof(parameters)) ? parameters[i - 1U] : 0U;
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

static as608_progress_t run(as608_t *ctx, mock_t *mock) {
    for (unsigned int i = 0U; i < 100U; ++i) {
        const unsigned int io = mock->io_calls;
        const as608_progress_t progress = as608_process(ctx);
        assert(mock->io_calls - io <= 1U);

        if (progress != AS608_PENDING) {
            return progress;
        }
    }

    assert(0 && "job stalled");
    return AS608_FAILED;
}

static void test_success(void) {
    static const uint8_t command[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
        0x03U, 0x0FU, 0x00U, 0x13U
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_parameters_t out = {0};
    as608_result_t result;
    unsigned char before[sizeof(out)];
    (void)memcpy(before, &out, sizeof(before));

    setup(&ctx, &mock);
    prepare_reply(&mock, AS608_PACKET_ACK, AS608_ACK_OK, 17U);
    assert(as608_read_parameters_start(&ctx, &out) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert(ctx.state == AS608_STATE_BUSY);
    assert(memcmp(before, &out, sizeof(before)) == 0);

    /* After every nonterminal step output remains untouched. */
    for (unsigned int i = 0U; i < 100U; ++i) {
        const as608_progress_t progress = as608_process(&ctx);
        if (progress != AS608_PENDING) {
            assert(progress == AS608_COMPLETE);
            break;
        }
        assert(memcmp(before, &out, sizeof(before)) == 0);
    }

    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(mock.tx_length == sizeof(command));
    assert(memcmp(mock.tx, command, sizeof(command)) == 0);
    assert(out.status_register == 9U);
    assert(out.sensor_type == 2U);
    assert(out.template_capacity == 300U);
    assert(out.security_level == 3U);
    assert(out.device_address == AS608_DEFAULT_ADDRESS);
    assert(out.packet_size_code == 2U);
    assert(out.baud_multiplier == 6U);
    assert(ctx.transaction.config.timeout_ms == 100U);
}

static void test_reply(uint8_t kind, uint8_t ack, size_t length, bool corrupt,
                       as608_status_t expected) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_parameters_t out = {.template_capacity = 0xA55AU};
    as608_result_t result;
    unsigned char before[sizeof(out)];
    (void)memcpy(before, &out, sizeof(before));

    setup(&ctx, &mock);
    prepare_reply(&mock, kind, ack, length);
    if (corrupt) {
        mock.rx[mock.rx_length - 1U] ^= 1U;
    }

    assert(as608_read_parameters_start(&ctx, &out) == AS608_STATUS_OK);
    const as608_progress_t progress = run(&ctx, &mock);
    assert(progress == ((expected == AS608_STATUS_OK) ? AS608_COMPLETE : AS608_FAILED));
    assert(as608_get_result(&ctx, &result));
    assert(result.status == expected);
    if (expected == AS608_STATUS_OK) {
        assert(result.confirmation == ack);
    }
    assert(memcmp(before, &out, sizeof(before)) == 0);
}

static void test_admission_and_timeout(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_parameters_t out = {.template_capacity = 123U};
    as608_result_t result;
    unsigned char before[sizeof(out)];
    (void)memcpy(before, &out, sizeof(before));

    assert(as608_read_parameters_start(NULL, &out) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_parameters_start(&ctx, &out) == AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert(as608_read_parameters_start(&ctx, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_read_parameters_start(&ctx, &out) == AS608_STATUS_OK);
    assert(as608_read_parameters_start(&ctx, &out) == AS608_STATUS_BUSY);
    assert(mock.clock_calls == 1U && mock.io_calls == 0U);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert(as608_read_parameters_start(&ctx, &out) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(memcmp(before, &out, sizeof(before)) == 0);
}

int main(void) {
    test_success();
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 1U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_PACKET_ERROR, 17U, false, AS608_STATUS_OK);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 1U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 16U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 18U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_reply(AS608_PACKET_ACK, AS608_ACK_OK, 17U, true, AS608_STATUS_CHECKSUM_ERROR);
    test_reply(AS608_PACKET_DATA, AS608_ACK_OK, 17U, false, AS608_STATUS_UNEXPECTED_REPLY);
    test_admission_and_timeout();

    puts("as608 API 07 read_parameters: PASS");
    return 0;
}
