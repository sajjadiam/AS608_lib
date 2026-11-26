/** @file test_as608_set_device_address.c @brief API 39: address transition tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[16U];
    uint8_t rx[12U];
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

/* Initialize a fresh device at its default address. */
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

/* Build an independent ACK at the supplied address. */
static void prepare_ack(mock_t *mock, uint32_t address, uint8_t ack) {
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U, 0x00U, 0x03U
    };
    const uint16_t sum = (uint16_t)(0x07U + 0x03U + ack);

    (void)memcpy(mock->rx, header, sizeof(header));
    mock->rx[2] = (uint8_t)(address >> 24U);
    mock->rx[3] = (uint8_t)(address >> 16U);
    mock->rx[4] = (uint8_t)(address >> 8U);
    mock->rx[5] = (uint8_t)address;
    mock->rx[9] = ack;
    mock->rx[10] = (uint8_t)(sum >> 8U);
    mock->rx[11] = (uint8_t)sum;
    mock->rx_pos = 0U;
    mock->rx_length = sizeof(mock->rx);
}

/* Poll within the cooperative I/O budget. */
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

/* Verify rejected admission preserves every context byte. */
static void assert_rejected(as608_t *ctx, uint32_t address, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_set_device_address_start(ctx, address) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify default TX, exact new RX, delayed commit and subsequent commands. */
static void test_success(uint32_t address, const uint8_t *command) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    setup(&ctx, &mock);
    prepare_ack(&mock, address, AS608_ACK_OK);
    assert(as608_set_device_address_start(&ctx, address) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U && mock.clock_calls == 1U);
    assert(ctx.transaction.config.device_address == AS608_DEFAULT_ADDRESS);
    assert_rejected(&ctx, UINT32_C(0x11223344), AS608_STATUS_BUSY);
    as608_progress_t progress = AS608_PENDING;
    for (unsigned int i = 0U; i < 100U && progress == AS608_PENDING; ++i) {
        progress = as608_process(&ctx);
        if (progress == AS608_PENDING) {
            assert(ctx.transaction.config.device_address == AS608_DEFAULT_ADDRESS);
            assert(!as608_get_result(&ctx, &result));
        }
    }
    assert(progress == AS608_COMPLETE);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_OK && result.confirmation == AS608_ACK_OK);
    assert(ctx.transaction.config.device_address == address);
    assert(mock.tx_length == 16U && memcmp(mock.tx, command, 16U) == 0);
    assert_rejected(&ctx, UINT32_C(0x11223344), AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, AS608_DEFAULT_ADDRESS, AS608_STATUS_INVALID_ARG);

    mock.tx_length = 0U;
    prepare_ack(&mock, address, AS608_ACK_OK);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_COMPLETE);
    assert(mock.tx_length == 16U && mock.tx[9] == 0x13U);
    assert(memcmp(mock.tx + 2U, command + 10U, 4U) == 0);
    assert(as608_get_result(&ctx, &result) && result.confirmation == AS608_ACK_OK);

    /* The normal receive path must still reject default-address replies. */
    mock.tx_length = 0U;
    prepare_ack(&mock, AS608_DEFAULT_ADDRESS, AS608_ACK_OK);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result) && result.status == AS608_STATUS_FRAME_ERROR);
    assert(ctx.transaction.config.device_address == address);
}

/* Verify malformed, negative and lost ACKs never publish the new address. */
static void test_fault(unsigned int fault) {
    const uint32_t address = UINT32_C(0x12345678);
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;
    as608_status_t expected = AS608_STATUS_UNEXPECTED_REPLY;
    setup(&ctx, &mock);

    if (fault < 7U) {
        prepare_ack(&mock, address, AS608_ACK_OK);
        switch (fault) {
            case 0U: mock.rx[11] ^= 1U; expected = AS608_STATUS_CHECKSUM_ERROR; break;
            case 1U: mock.rx[6] = AS608_PACKET_DATA; mock.rx[11] = 5U; break;
            case 2U: mock.rx[8] = 4U; break;
            case 3U: prepare_ack(&mock, address, AS608_ACK_PACKET_ERROR); break;
            case 4U: prepare_ack(&mock, AS608_DEFAULT_ADDRESS, AS608_ACK_OK); expected = AS608_STATUS_FRAME_ERROR; break;
            case 5U: prepare_ack(&mock, AS608_DEFAULT_ADDRESS, AS608_ACK_PACKET_ERROR); expected = AS608_STATUS_FRAME_ERROR; break;
            case 6U: prepare_ack(&mock, address ^ 1U, AS608_ACK_OK); expected = AS608_STATUS_FRAME_ERROR; break;
        }
    }
    assert(as608_set_device_address_start(&ctx, address) == AS608_STATUS_OK);
    if (fault >= 7U) {
        for (unsigned int i = 0U; i < 100U && mock.tx_length < 16U; ++i) {
            assert(as608_process(&ctx) == AS608_PENDING);
        }
        assert(mock.tx_length == 16U);
        if (fault == 7U) {
            mock.now = 100U;
            expected = AS608_STATUS_TIMEOUT;
        } else {
            assert(as608_abort(&ctx) == AS608_STATUS_OK);
            expected = AS608_STATUS_RECOVERY_REQUIRED;
        }
    }
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result) && result.status == expected);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(ctx.transaction.config.device_address == AS608_DEFAULT_ADDRESS);
    assert(mock.tx_length == 16U);
    assert_rejected(&ctx, address, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_verify_password_start(&ctx, 0U) == AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED && mock.tx_length == 16U);
}

/* Verify admission errors and recovery do not change the configured address. */
static void test_admission(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    assert(as608_set_device_address_start(NULL, 0U) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, 0U, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, AS608_DEFAULT_ADDRESS, AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
    assert(as608_set_device_address_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, 0U, AS608_STATUS_RECOVERY_REQUIRED);
    /* The fixture represents a physically recovered default-address sensor. */
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(ctx.transaction.config.device_address == AS608_DEFAULT_ADDRESS);
    assert(as608_set_device_address_start(&ctx, 0U) == AS608_STATUS_OK);
    assert(mock.io_calls == 0U);
}

int main(void) {
    const uint8_t normal[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 7U, 0x15U, 0x12U, 0x34U, 0x56U, 0x78U, 1U, 0x31U};
    const uint8_t zero[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 1U, 0U, 7U, 0x15U, 0U, 0U, 0U, 0U, 0U, 0x1DU};
    test_success(UINT32_C(0x12345678), normal);
    test_success(0U, zero);
    for (unsigned int i = 0U; i < 9U; ++i) {
        test_fault(i);
    }
    test_admission();
    puts("as608 API 39 set_device_address: PASS");
    return 0;
}
