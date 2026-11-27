/** @file mock_uart.h @brief Shared one-byte UART test fixture; host tests only. */
#ifndef AS608_TEST_MOCK_UART_H
#define AS608_TEST_MOCK_UART_H

#include "as608.h"
#include <assert.h>
#include <string.h>

#if !defined(AS608_TEST_TX_CAPACITY) || !defined(AS608_TEST_RX_CAPACITY) || !defined(AS608_TEST_DRAIN)
#error "Select the test-local buffer capacities and drain profile before including mock_uart.h"
#endif

typedef struct {
    uint8_t tx[AS608_TEST_TX_CAPACITY];
    uint8_t rx[AS608_TEST_RX_CAPACITY];
    size_t tx_length;
    size_t rx_length;
    size_t rx_pos;
    unsigned int io_calls;
    unsigned int clock_calls;
    uint32_t now;
#if AS608_TEST_DRAIN
    int32_t idle;
    unsigned int drain_calls;
#endif
} mock_t;

/* Accept one TX byte into the mock wire buffer. */
static inline int32_t write_byte(void *user, const uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U) && (mock->tx_length < sizeof(mock->tx)));
    ++mock->io_calls;
    mock->tx[mock->tx_length++] = *data;
    return 1;
}

/* Consume one available RX byte without waiting. */
static inline int32_t read_byte(void *user, uint8_t *data, size_t length) {
    mock_t *mock = user;
    assert(data && (length > 0U));
    ++mock->io_calls;

    if (mock->rx_pos == mock->rx_length) {
        return 0;
    }

    *data = mock->rx[mock->rx_pos++];
    return 1;
}

/* Return physical completion in the selected mock profile. */
#if AS608_TEST_DRAIN
static inline int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    ++mock->drain_calls;
    return mock->idle;
}
#else
static inline int32_t tx_idle(void *user) {
    mock_t *mock = user;
    ++mock->io_calls;
    return 1;
}
#endif

/* Read the simulated clock and count accesses. */
static inline uint32_t clock_ms(void *user) {
    mock_t *mock = user;
    ++mock->clock_calls;
    return mock->now;
}

/* Initialize a fresh device with the mock port. */
static inline void setup(as608_t *ctx, mock_t *mock) {
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

#undef AS608_TEST_TX_CAPACITY
#undef AS608_TEST_RX_CAPACITY
#undef AS608_TEST_DRAIN
#endif /* AS608_TEST_MOCK_UART_H */
