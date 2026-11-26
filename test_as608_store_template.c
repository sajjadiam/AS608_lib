/** @file test_as608_store_template.c @brief API 19: store template wire and lifecycle tests. */
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

/* Return the mock clock and count clock accesses. */
static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

/* Initialize a device with the mock port. */
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

/* Construct an independent one-byte ACK frame. */
static void prepare_ack(mock_t *mock, uint8_t ack) {
    const uint8_t header[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U, 0x00U, 0x03U
    };
    const uint16_t sum = (uint16_t)(0x07U + 0x03U + ack);

    (void)memcpy(mock->rx, header, sizeof(header));
    mock->rx[9] = ack;
    mock->rx[10] = (uint8_t)(sum >> 8U);
    mock->rx[11] = (uint8_t)sum;
    mock->rx_pos = 0U;
    mock->rx_length = sizeof(mock->rx);
}

/* Poll the public process API with a bounded test loop. */
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

/* Verify rejected admission preserves the entire context. */
static void assert_rejected(as608_t *ctx, as608_char_buffer_t buffer, uint16_t page, as608_status_t status) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_store_template_start(ctx, buffer, page) == status);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

/* Verify both buffer IDs, full-width PageIDs and raw confirmations. */
static void test_store_results(void) {
    const as608_char_buffer_t buffers[] = {AS608_CHAR_BUFFER_1, AS608_CHAR_BUFFER_2};
    const uint16_t pages[] = {0U, 1U, 299U, 0x1234U, UINT16_MAX};
    const uint8_t confirmations[] = {
        AS608_ACK_OK, AS608_ACK_PACKET_ERROR, AS608_ACK_INVALID_PAGE,
        AS608_ACK_FLASH_ERROR, 0x42U
    };
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    setup(&ctx, &mock);

    for (size_t b = 0U; b < sizeof(buffers) / sizeof(buffers[0]); ++b) {
        for (size_t i = 0U; i < sizeof(pages) / sizeof(pages[0]); ++i) {
            const uint8_t hi = (uint8_t)(pages[i] >> 8U);
            const uint8_t lo = (uint8_t)pages[i];
            const uint16_t sum = (uint16_t)(0x01U + 0x06U + 0x06U + (uint8_t)buffers[b] + hi + lo);
            const uint8_t command[] = {
                0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x01U, 0x00U,
                0x06U, 0x06U, (uint8_t)buffers[b], hi, lo, (uint8_t)(sum >> 8U), (uint8_t)sum
            };

            for (size_t a = 0U; a < sizeof(confirmations); ++a) {
                mock.tx_length = 0U;
                mock.io_calls = 0U;
                mock.clock_calls = 0U;
                prepare_ack(&mock, confirmations[a]);

                assert(as608_store_template_start(&ctx, buffers[b], pages[i]) == AS608_STATUS_OK);
                assert(ctx.state == AS608_STATE_BUSY);
                assert(mock.io_calls == 0U && mock.clock_calls == 1U);
                assert(!as608_get_result(&ctx, &result));
                assert_rejected(&ctx, AS608_CHAR_BUFFER_2, 1U, AS608_STATUS_BUSY);
                assert(run(&ctx, &mock) == AS608_COMPLETE);
                assert(as608_get_result(&ctx, &result));
                assert(result.status == AS608_STATUS_OK && result.confirmation == confirmations[a]);
                assert(mock.tx_length == sizeof(command));
                assert(memcmp(mock.tx, command, sizeof(command)) == 0);
                assert_rejected(&ctx, (as608_char_buffer_t)0, 1U, AS608_STATUS_INVALID_ARG);
            }
        }
    }
}

/* Verify invalid buffer rejection and transport fault handling. */
static void test_faults(void) {
    mock_t mock = {0};
    as608_t ctx = {0};
    as608_result_t result;

    assert(as608_store_template_start(NULL, AS608_CHAR_BUFFER_1, 0U) == AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, AS608_CHAR_BUFFER_1, 0U, AS608_STATUS_NOT_INITIALIZED);
    setup(&ctx, &mock);
    assert_rejected(&ctx, (as608_char_buffer_t)0, 0U, AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, (as608_char_buffer_t)3, 0U, AS608_STATUS_INVALID_ARG);
    assert_rejected(&ctx, (as608_char_buffer_t)255, 0U, AS608_STATUS_INVALID_ARG);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);

    prepare_ack(&mock, AS608_ACK_OK);
    mock.rx[11] ^= 1U;
    assert(as608_store_template_start(&ctx, AS608_CHAR_BUFFER_1, 0U) == AS608_STATUS_OK);
    assert(run(&ctx, &mock) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_CHECKSUM_ERROR);
    assert_rejected(&ctx, AS608_CHAR_BUFFER_1, 0U, AS608_STATUS_RECOVERY_REQUIRED);

    /* The fixture represents external physical recovery. */
    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    mock.tx_length = 0U;
    mock.rx_length = 0U;
    mock.rx_pos = 0U;
    assert(as608_store_template_start(&ctx, AS608_CHAR_BUFFER_2, 0U) == AS608_STATUS_OK);
    mock.now = 100U;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert_rejected(&ctx, AS608_CHAR_BUFFER_2, 0U, AS608_STATUS_RECOVERY_REQUIRED);

    assert(as608_confirm_reset(&ctx) == AS608_STATUS_OK);
    assert(as608_store_template_start(&ctx, AS608_CHAR_BUFFER_1, 0U) == AS608_STATUS_OK);
    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_rejected(&ctx, AS608_CHAR_BUFFER_1, 0U, AS608_STATUS_RECOVERY_REQUIRED);
}

int main(void) {
    test_store_results();
    test_faults();

    puts("as608 API 19 store_template: PASS");
    return 0;
}
