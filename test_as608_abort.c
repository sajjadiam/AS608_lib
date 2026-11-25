/** @file test_as608_abort.c @brief API 04: logical abort and quarantine. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned int writes;
    unsigned int reads;
    unsigned int idle_calls;
    unsigned int clock_calls;
} mock_t;

static int32_t write_byte(void *user, const uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U));
    ++mock->writes;
    return 1;
}

static int32_t read_byte(void *user, uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U));
    ++mock->reads;
    return 0;
}

static int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->idle_calls;
    return 0;
}

static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return 0U;
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

/* Internal fixture until public start APIs are restored. */
static void arm(as608_t *ctx) {
    const uint8_t command = 0x01U;
    const as608_transaction_frame_t frame = {
        AS608_PACKET_COMMAND, &command, 1U
    };

    assert(as608_transaction_send(&ctx->transaction, &frame) == AS608_STATUS_OK);
    as608_transaction_begin(&ctx->transaction);
    ctx->step = 1U; /* STEP_SIMPLE_ACK */
    ctx->state = AS608_STATE_BUSY;
    ctx->result.status = AS608_STATUS_NOT_INITIALIZED;
}

static void assert_unchanged(as608_t *ctx, as608_status_t expected) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_abort(ctx) == expected);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

static void test_invalid_states(void) {
    as608_t ctx = {0};

    assert(as608_abort(NULL) == AS608_STATUS_INVALID_ARG);
    assert_unchanged(&ctx, AS608_STATUS_INVALID_ARG);

    ctx.state = AS608_STATE_IDLE;
    assert_unchanged(&ctx, AS608_STATUS_INVALID_ARG);

    ctx.state = AS608_STATE_DONE;
    ctx.result = (as608_result_t){AS608_STATUS_OK, AS608_ACK_OK};
    assert_unchanged(&ctx, AS608_STATUS_INVALID_ARG);

    ctx.state = AS608_STATE_DESYNCHRONIZED;
    ctx.result = (as608_result_t){AS608_STATUS_TIMEOUT, 0U};
    assert_unchanged(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
}

static void test_abort_in_flight(unsigned int steps) {
    mock_t mock = {0};
    as608_t ctx;
    as608_result_t result;
    unsigned char transport_before[sizeof(ctx.transaction)];

    setup(&ctx, &mock);
    arm(&ctx);

    for (unsigned int i = 0U; i < steps; ++i) {
        assert(as608_process(&ctx) == AS608_PENDING);
    }

    const mock_t before = mock;
    (void)memcpy(transport_before, &ctx.transaction, sizeof(transport_before));

    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert(ctx.state == AS608_STATE_DESYNCHRONIZED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_RECOVERY_REQUIRED);
    assert(result.confirmation == 0U);

    assert_unchanged(&ctx, AS608_STATUS_RECOVERY_REQUIRED);
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(memcmp(transport_before, &ctx.transaction, sizeof(transport_before)) == 0);

    assert(mock.writes == before.writes);
    assert(mock.reads == before.reads);
    assert(mock.idle_calls == before.idle_calls);
    assert(mock.clock_calls == before.clock_calls);
}

int main(void) {
    test_invalid_states();
    test_abort_in_flight(0U);  /* Before first TX byte. */
    test_abort_in_flight(1U);  /* Partial TX accepted by port. */
    test_abort_in_flight(13U); /* Command sent; waiting for reply. */

    puts("as608 API 04 abort: PASS");
    return 0;
}
