/** @file test_as608_init.c @brief API 01: initialization contract tests. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int32_t unexpected_write(void *user, const uint8_t *data, size_t length)
{
    (void)user;
    (void)data;
    (void)length;
    assert(0 && "init must not write");
    return -1;
}

static int32_t unexpected_read(void *user, uint8_t *data, size_t length)
{
    (void)user;
    (void)data;
    (void)length;
    assert(0 && "init must not read");
    return -1;
}

static int32_t unexpected_idle(void *user)
{
    (void)user;
    assert(0 && "init must not query TX");
    return -1;
}

static uint32_t unexpected_clock(void *user)
{
    (void)user;
    assert(0 && "init must not read clock");
    return 0U;
}

static as608_config_t valid_config(void)
{
    return (as608_config_t){
        .port = {
            .try_write = unexpected_write,
            .try_read = unexpected_read,
            .tx_idle = unexpected_idle,
            .clock_ms = unexpected_clock
        },
        .device_address = AS608_DEFAULT_ADDRESS,
        .timeout_ms = 100U
    };
}

static void assert_unchanged(as608_t *ctx, const as608_config_t *config,
                             as608_status_t expected)
{
    unsigned char before[sizeof(*ctx)];
    (void)memcpy(before, ctx, sizeof(before));
    assert(as608_init(ctx, config) == expected);
    assert(memcmp(before, ctx, sizeof(before)) == 0);
}

static void test_success(void)
{
    as608_t ctx = {0};
    as608_config_t config = valid_config();
    int user = 0;
    config.port.user = &user;

    assert(as608_init(&ctx, &config) == AS608_STATUS_OK);
    assert(ctx.state == AS608_STATE_IDLE);
    assert(ctx.transaction.config.port.user == &user);
    assert(ctx.transaction.config.port.try_write == unexpected_write);
    assert(ctx.transaction.config.port.try_read == unexpected_read);
    assert(ctx.transaction.config.port.tx_idle == unexpected_idle);
    assert(ctx.transaction.config.port.clock_ms == unexpected_clock);
    assert(ctx.transaction.config.device_address == AS608_DEFAULT_ADDRESS);
    assert(ctx.transaction.config.timeout_ms == 100U);
    assert(ctx.transaction.started_ms == 0U);
    assert(ctx.transaction.tx_len == 0U);
    assert(ctx.transaction.rx_header_pos == 0U);
    assert(ctx.step == 0U);
    assert(!ctx.input && !ctx.stream_data && !ctx.stream_length && !ctx.reply_output);
    assert(!ctx.stream_write && !ctx.stream_read && !ctx.stream_user);
    config.timeout_ms = 1U;
    assert(ctx.transaction.config.timeout_ms == 100U);
    assert_unchanged(&ctx, &config, AS608_STATUS_INVALID_ARG);
}

static void test_invalid_arguments(void)
{
    as608_t ctx = {0};
    const as608_config_t good = valid_config();
    as608_config_t bad = good;

    assert(as608_init(NULL, &good) == AS608_STATUS_INVALID_ARG);
    assert_unchanged(&ctx, NULL, AS608_STATUS_INVALID_ARG);
    bad.port.try_write = NULL;
    assert_unchanged(&ctx, &bad, AS608_STATUS_INVALID_ARG);
    bad = good;
    bad.port.try_read = NULL;
    assert_unchanged(&ctx, &bad, AS608_STATUS_INVALID_ARG);
    bad = good;
    bad.port.tx_idle = NULL;
    assert_unchanged(&ctx, &bad, AS608_STATUS_INVALID_ARG);
    bad = good;
    bad.port.clock_ms = NULL;
    assert_unchanged(&ctx, &bad, AS608_STATUS_INVALID_ARG);
    bad = good;
    bad.timeout_ms = 0U;
    assert_unchanged(&ctx, &bad, AS608_STATUS_INVALID_ARG);
    bad.timeout_ms = AS608_MAX_TIMEOUT_MS + UINT32_C(1);
    assert_unchanged(&ctx, &bad, AS608_STATUS_INVALID_ARG);
}

static void test_state_guards(void)
{
    as608_t ctx = {0};
    const as608_config_t config = valid_config();

    ctx.state = AS608_STATE_BUSY;
    assert_unchanged(&ctx, &config, AS608_STATUS_BUSY);
    ctx.state = AS608_STATE_DESYNCHRONIZED;
    assert_unchanged(&ctx, &config, AS608_STATUS_RECOVERY_REQUIRED);
    ctx.state = AS608_STATE_DONE;
    assert_unchanged(&ctx, &config, AS608_STATUS_INVALID_ARG);
}

static void test_alias_and_timeout_bounds(void)
{
    as608_t ctx = {0};
    ctx.transaction.config = valid_config();
    ctx.transaction.config.timeout_ms = AS608_MAX_TIMEOUT_MS;
    assert(as608_init(&ctx, &ctx.transaction.config) == AS608_STATUS_OK);
    assert(ctx.transaction.config.timeout_ms == AS608_MAX_TIMEOUT_MS);
    assert(ctx.transaction.config.port.clock_ms == unexpected_clock);

    as608_t other = {0};
    as608_config_t config = valid_config();
    config.timeout_ms = 1U;
    assert(as608_init(&other, &config) == AS608_STATUS_OK);
    assert(other.transaction.config.timeout_ms == 1U);
    assert(ctx.transaction.config.timeout_ms == AS608_MAX_TIMEOUT_MS);
}

int main(void)
{
    test_success();
    test_invalid_arguments();
    test_state_guards();
    test_alias_and_timeout_bounds();
    puts("as608 API 01 init: PASS");
    return 0;
}
