/** @file test_as608_get_result.c @brief API 03: terminal result contract. */
#include "as608.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_unavailable(const as608_t *ctx) {
    as608_result_t output = {AS608_STATUS_BUSY, 0xA5U};

    assert(!as608_get_result(ctx, &output));
    assert(output.status == AS608_STATUS_BUSY);
    assert(output.confirmation == 0xA5U);
}

static void test_unavailable(void) {
    as608_t ctx = {0};

    assert_unavailable(NULL);
    assert_unavailable(&ctx);

    ctx.state = AS608_STATE_IDLE;
    assert_unavailable(&ctx);

    ctx.state = AS608_STATE_BUSY;
    assert_unavailable(&ctx);

    ctx.state = AS608_STATE_DONE;
    assert(!as608_get_result(&ctx, NULL));
    assert(!as608_get_result(NULL, NULL));
}

static void test_terminal(as608_state_t state, as608_status_t status, uint8_t ack) {
    as608_t ctx = {0};
    as608_result_t output = {AS608_STATUS_NOT_INITIALIZED, 0xFFU};
    unsigned char before[sizeof(ctx)];

    ctx.state = state;
    ctx.result.status = status;
    ctx.result.confirmation = ack;
    (void)memcpy(before, &ctx, sizeof(before));

    assert(as608_get_result(&ctx, &output));
    assert(output.status == status);
    assert(output.confirmation == ack);
    assert(memcmp(before, &ctx, sizeof(before)) == 0);

    output = (as608_result_t){AS608_STATUS_BUSY, 0xA5U};
    assert(as608_get_result(&ctx, &output));
    assert(output.status == status);
    assert(output.confirmation == ack);
    assert(memcmp(before, &ctx, sizeof(before)) == 0);
}

int main(void) {
    test_unavailable();
    test_terminal(AS608_STATE_DONE, AS608_STATUS_OK, AS608_ACK_OK);
    test_terminal(AS608_STATE_DONE, AS608_STATUS_OK, AS608_ACK_NO_FINGER);
    test_terminal(AS608_STATE_DONE, AS608_STATUS_OK, 0x42U);
    test_terminal(AS608_STATE_DESYNCHRONIZED, AS608_STATUS_TIMEOUT, 0U);
    test_terminal(AS608_STATE_DESYNCHRONIZED, AS608_STATUS_CHECKSUM_ERROR, 0U);
    test_terminal(AS608_STATE_DESYNCHRONIZED, AS608_STATUS_IO_ERROR, 0U);

    puts("as608 API 03 get_result: PASS");
    return 0;
}
