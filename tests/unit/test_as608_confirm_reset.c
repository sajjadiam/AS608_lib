/** @file test_as608_confirm_reset.c @brief API 05: recovery acknowledgment. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
} mock_t;

static int32_t write_byte(void *user, const uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U));
    ++mock->io_calls;
    return 1;
}

static int32_t read_byte(void *user, uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U));
    ++mock->io_calls;
    return 0;
}

static int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    return 0;
}

static uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

static as608_config_t config_for(mock_t *mock) {
    return (as608_config_t){
        .port = {
            .user = mock, .try_write = write_byte, .try_read = read_byte,
            .tx_idle = tx_idle, .clock_ms = clock_ms
        },
        .device_address = UINT32_C(0x12345678),
        .timeout_ms = 100U
    };
}

/* A stream callback must never run during logical recovery. */
static bool unexpected_sink(void *user, const uint8_t *data, size_t length) {
    (void)user;
    (void)data;
    (void)length;
    assert(0 && "recovery must not call stream sink");
    return false;
}

/* A stream callback must never run during logical recovery. */
static int32_t unexpected_source(void *user, uint8_t *data, size_t length) {
    (void)user;
    (void)data;
    (void)length;
    assert(0 && "recovery must not call stream source");
    return -1;
}

/* Admit a normal job using the public API. */
static void arm(as608_t *ctx) {
    assert(as608_capture_image_start(ctx) == AS608_STATUS_OK);
}

static void assert_unchanged(as608_t *ctx, as608_status_t expected) {
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));

    assert(as608_confirm_reset(ctx) == expected);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

static void test_invalid_states(void) {
    as608_t ctx = {0};
    mock_t mock = {0};
    const as608_config_t config = config_for(&mock);

    assert(as608_confirm_reset(NULL) == AS608_STATUS_INVALID_ARG);
    assert_unchanged(&ctx, AS608_STATUS_NOT_INITIALIZED);
    assert(as608_init(&ctx, &config) == AS608_STATUS_OK);
    assert_unchanged(&ctx, AS608_STATUS_INVALID_ARG);

    ctx.state = AS608_STATE_DONE;
    ctx.result = (as608_result_t){AS608_STATUS_OK, AS608_ACK_OK};
    assert_unchanged(&ctx, AS608_STATUS_INVALID_ARG);

    ctx.state = AS608_STATE_BUSY;
    assert_unchanged(&ctx, AS608_STATUS_BUSY);
    assert(mock.io_calls == 0U && mock.clock_calls == 0U);
}

static void assert_recovered(as608_t *ctx, const as608_config_t *config, mock_t *mock) {
    as608_result_t result = {AS608_STATUS_BUSY, 0xA5U};
    const unsigned int io_calls = mock->io_calls;
    const unsigned int clock_calls = mock->clock_calls;

    /* The mock stands for a fresh physical link. No real reset is tested. */
    assert(as608_confirm_reset(ctx) == AS608_STATUS_OK);
    assert(ctx->state == AS608_STATE_IDLE);
    assert(ctx->step == 0U);
    assert(ctx->page_id == 0U && ctx->chunk_size == 0U);
    assert(!ctx->store_after_download);
    assert(!ctx->input && ctx->input_length == 0U && ctx->input_offset == 0U);
    assert(!ctx->stream_data && ctx->stream_capacity == 0U && ctx->stream_offset == 0U);
    assert(!ctx->stream_length && !ctx->reply_output);
    assert(!ctx->stream_write && !ctx->stream_read && !ctx->stream_user);
    assert(ctx->transaction.started_ms == 0U);
    assert(ctx->transaction.wire_phase == 0U);
    assert(ctx->transaction.tx_len == 0U && ctx->transaction.tx_pos == 0U);
    assert(ctx->transaction.rx_header_pos == 0U);
    assert(ctx->transaction.rx_body_len == 0U && ctx->transaction.rx_body_pos == 0U);
    assert(ctx->transaction.rx_payload_len == 0U && ctx->transaction.rx_limit == 0U);
    assert(ctx->transaction.config.device_address == config->device_address);
    assert(ctx->transaction.config.timeout_ms == config->timeout_ms);
    assert(ctx->transaction.config.port.user == config->port.user);
    assert(ctx->transaction.config.port.try_write == config->port.try_write);
    assert(ctx->transaction.config.port.try_read == config->port.try_read);
    assert(ctx->transaction.config.port.tx_idle == config->port.tx_idle);
    assert(ctx->transaction.config.port.clock_ms == config->port.clock_ms);

    assert(!as608_get_result(ctx, &result));
    assert(result.status == AS608_STATUS_BUSY && result.confirmation == 0xA5U);
    assert(as608_process(ctx) == AS608_FAILED);
    assert_unchanged(ctx, AS608_STATUS_INVALID_ARG);
    assert(mock->io_calls == io_calls && mock->clock_calls == clock_calls);
}

static void test_abort_recovery(void) {
    mock_t mock = {.now = 17U};
    const as608_config_t config = config_for(&mock);
    as608_t ctx = {0};
    uint8_t buffer[1U] = {0};
    size_t length = 0U;

    assert(as608_init(&ctx, &config) == AS608_STATUS_OK);
    arm(&ctx);
    assert(as608_process(&ctx) == AS608_PENDING);
    ctx.page_id = 5U;
    ctx.chunk_size = 128U;
    ctx.store_after_download = true;
    ctx.input = buffer;
    ctx.input_length = sizeof(buffer);
    ctx.input_offset = 1U;
    ctx.stream_data = buffer;
    ctx.stream_capacity = sizeof(buffer);
    ctx.stream_offset = 1U;
    ctx.stream_length = &length;
    ctx.reply_output = buffer;
    /* White-box fixture: recovery must clear every borrowed reference. */
    ctx.stream_write = unexpected_sink;
    ctx.stream_read = unexpected_source;
    ctx.stream_user = &mock;

    assert(as608_abort(&ctx) == AS608_STATUS_OK);
    assert_recovered(&ctx, &config, &mock);
}

static void test_timeout_recovery(void) {
    mock_t mock = {0};
    const as608_config_t config = config_for(&mock);
    as608_t ctx = {0};
    as608_result_t result;

    assert(as608_init(&ctx, &config) == AS608_STATUS_OK);
    arm(&ctx);
    mock.now = config.timeout_ms;
    assert(as608_process(&ctx) == AS608_FAILED);
    assert(as608_get_result(&ctx, &result));
    assert(result.status == AS608_STATUS_TIMEOUT);
    assert_recovered(&ctx, &config, &mock);

    /* A fresh internally admitted job can progress after recovery. */
    arm(&ctx);
    assert(as608_process(&ctx) == AS608_PENDING);
    assert(ctx.transaction.tx_pos == 1U);
}

int main(void) {
    test_invalid_states();
    test_abort_recovery();
    test_timeout_recovery();

    puts("as608 API 05 confirm_reset: PASS");
    return 0;
}
