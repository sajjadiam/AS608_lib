/**
 * @file as608_port_stm32f103.c
 * @brief Single-instance STM32F103 reference test port (LL + CMSIS).
 *
 * Application owns clocks, GPIO, UART framing/baud, NVIC and sensor reset.
 * Use USART1 (or override AS608_TEST_UART), asynchronous 8-bit no-parity
 * full duplex, no flow control or DMA. HAL_GetTick() must remain monotonic.
 * Call init/stop and all core APIs from one serialized thread, not an ISR.
 * Route the selected USART IRQ only to as608_test_port_irq_handler().
 * Do not share this USART with HAL UART I/O, DMA or other interrupt handlers.
 *
 * Before init: enable the UART clock, configure USART, disable its NVIC
 * IRQ and peripheral interrupt sources, establish a fresh sensor link,
 * ensure TC is set and no stale RX/error flags remain. Enable NVIC after
 * successful init. Never use init to bypass physical recovery.
 * After stop/fault: disable NVIC, reset/reconfigure UART and recover sensor,
 * discard old traffic only as part of physical recovery, clear NVIC pending,
 * then init and (if core quarantined) call as608_confirm_reset().
 * Stop only disables servicing; it does NOT abort the hardware shifter.
 *
 * Add these declarations to the application's existing header (e.g. main.h):
 *   bool as608_test_port_init(as608_port_t *port);
 *   void as608_test_port_stop(void);
 *   uint32_t as608_test_port_get_errors(void);
 *   void as608_test_port_irq_handler(void);
 */
#include "as608_port.h"
#include "stm32f1xx_ll_usart.h"
#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <string.h>

/* =========================================================================
 * Test configuration and private state
 * ========================================================================= */

#ifndef AS608_TEST_UART
#define AS608_TEST_UART USART1
#endif

#define RX_BUFFER_SIZE 512U
#define TX_BUFFER_SIZE 128U
#define UART_ERROR_MASK (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)
#define RX_OVERFLOW (UINT32_C(1) << 16)

_Static_assert(RX_BUFFER_SIZE > 1U &&
               (RX_BUFFER_SIZE & (RX_BUFFER_SIZE - 1U)) == 0U &&
               RX_BUFFER_SIZE <= UINT16_MAX, "Invalid RX ring size");
_Static_assert(TX_BUFFER_SIZE > 1U &&
               (TX_BUFFER_SIZE & (TX_BUFFER_SIZE - 1U)) == 0U &&
               TX_BUFFER_SIZE <= UINT16_MAX, "Invalid TX ring size");

static uint8_t rx_buffer[RX_BUFFER_SIZE];
static uint8_t tx_buffer[TX_BUFFER_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static volatile uint32_t errors;
static volatile bool active;

/* =========================================================================
 * Synchronization and ring helpers
 * ========================================================================= */

/* Preserve the previous interrupt state; publish memory before restoring it. */
static uint32_t irq_save(void) {
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return state;
}

/* Restore the caller's interrupt mask, including an already masked state. */
static void irq_restore(uint32_t state) {
    __DMB();
    __set_PRIMASK(state);
}

/* Advance a power-of-two ring index without division. */
static uint16_t ring_advance(uint16_t index, size_t count, size_t capacity) {
    return (uint16_t)(((size_t)index + count) & (capacity - 1U));
}

/* Compute the occupied count using unsigned modulo arithmetic. */
static size_t ring_used(uint16_t head, uint16_t tail, size_t capacity) {
    return ((size_t)head - (size_t)tail) & (capacity - 1U);
}

/* Bound a transfer to one contiguous memory span. */
static size_t ring_span(uint16_t index, size_t count, size_t capacity) {
    const size_t contiguous = capacity - (size_t)index;
    return count < contiguous ? count : contiguous;
}

/* =========================================================================
 * Fault handling
 * ========================================================================= */

/* Disable port-owned interrupt sources without altering the UART shifter. */
static void disable_sources(void) {
    LL_USART_DisableIT_TXE(AS608_TEST_UART);
    LL_USART_DisableIT_RXNE(AS608_TEST_UART);
    LL_USART_DisableIT_ERROR(AS608_TEST_UART);
    LL_USART_DisableIT_PE(AS608_TEST_UART);
}

/* Latch the failure and prevent further queue or IRQ progress. */
static void latch_fault(uint32_t flags) {
    errors |= flags;
    disable_sources();
}

/* Called with interrupts masked; report pending hardware errors as well. */
static bool port_ready(void) {
    if (!active || errors != 0U) {
        return false;
    }

    const uint32_t flags = LL_USART_ReadReg(AS608_TEST_UART, SR) & UART_ERROR_MASK;
    if (flags != 0U) {
        latch_fault(flags);
        return false;
    }

    return true;
}

/* =========================================================================
 * TX callback
 * ========================================================================= */

/* Copy an ordered prefix into port-owned storage before publishing it. */
static int32_t try_write(void *user, const uint8_t *data, size_t length) {
    (void)user;
    uint32_t state = irq_save();
    if (!port_ready()) {
        irq_restore(state);
        return -1;
    }

    const uint16_t head = tx_head;
    const size_t free_bytes = TX_BUFFER_SIZE - 1U -
                              ring_used(head, tx_tail, TX_BUFFER_SIZE);
    const size_t count = length < free_bytes ? length : free_bytes;
    irq_restore(state);

    if (count != 0U) {
        const size_t first = ring_span(head, count, TX_BUFFER_SIZE);
        (void)memcpy(tx_buffer + head, data, first);
        if (first < count) {
            (void)memcpy(tx_buffer, data + first, count - first);
        }
    }

    /* ISR may consume old bytes, but cannot see unpublished bytes. */
    state = irq_save();
    if (!port_ready()) {
        irq_restore(state);
        return -1;
    }

    if (count != 0U) {
        tx_head = ring_advance(head, count, TX_BUFFER_SIZE);
        LL_USART_EnableIT_TXE(AS608_TEST_UART);
    }
    irq_restore(state);
    return (int32_t)count;
}

/* =========================================================================
 * RX callback
 * ========================================================================= */

/* Consume a prefix; keep its slots reserved until the copy completes. */
static int32_t try_read(void *user, uint8_t *data, size_t length) {
    (void)user;
    uint32_t state = irq_save();
    if (!port_ready()) {
        irq_restore(state);
        return -1;
    }

    const uint16_t tail = rx_tail;
    const size_t available = ring_used(rx_head, tail, RX_BUFFER_SIZE);
    const size_t count = length < available ? length : available;
    irq_restore(state);

    if (count != 0U) {
        const size_t first = ring_span(tail, count, RX_BUFFER_SIZE);
        (void)memcpy(data, rx_buffer + tail, first);
        if (first < count) {
            (void)memcpy(data + first, rx_buffer, count - first);
        }
    }

    /* ISR cannot overwrite occupied slots; overflow instead latches a fault. */
    state = irq_save();
    if (!port_ready()) {
        irq_restore(state);
        return -1;
    }

    rx_tail = ring_advance(tail, count, RX_BUFFER_SIZE);
    irq_restore(state);
    return (int32_t)count;
}

/* =========================================================================
 * Completion and clock callbacks
 * ========================================================================= */

/* Report physical completion only after the last stop bit, not merely TXE. */
static int32_t tx_idle(void *user) {
    (void)user;
    const uint32_t state = irq_save();
    int32_t result = -1;

    if (port_ready()) {
        result = (tx_head == tx_tail &&
                  LL_USART_IsActiveFlag_TC(AS608_TEST_UART)) ? 1 : 0;
    }

    irq_restore(state);
    return result;
}

/* Use the application's existing millisecond timebase. */
static uint32_t clock_ms(void *user) {
    (void)user;
    return HAL_GetTick();
}

/* =========================================================================
 * Initialization, stop and diagnostics
 * ========================================================================= */

bool as608_test_port_init(as608_port_t *port) {
    if (!port) {
        return false;
    }

    const uint32_t state = irq_save();
    const uint32_t cr1 = LL_USART_ReadReg(AS608_TEST_UART, CR1);
    const uint32_t cr2 = LL_USART_ReadReg(AS608_TEST_UART, CR2);
    const uint32_t cr3 = LL_USART_ReadReg(AS608_TEST_UART, CR3);
    const uint32_t sr = LL_USART_ReadReg(AS608_TEST_UART, SR);
    const uint32_t required = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    const uint32_t forbidden1 = USART_CR1_M | USART_CR1_PCE |
        USART_CR1_RXNEIE | USART_CR1_TXEIE | USART_CR1_TCIE |
        USART_CR1_PEIE | USART_CR1_IDLEIE;
    const uint32_t forbidden2 = USART_CR2_CLKEN | USART_CR2_LINEN |
        USART_CR2_LBDIE;
    const uint32_t forbidden3 = USART_CR3_DMAR | USART_CR3_DMAT |
        USART_CR3_RTSE | USART_CR3_CTSE | USART_CR3_CTSIE |
        USART_CR3_HDSEL | USART_CR3_SCEN | USART_CR3_IREN | USART_CR3_EIE;

    if (active || (cr1 & required) != required || (cr1 & forbidden1) != 0U ||
        (cr2 & forbidden2) != 0U || (cr3 & forbidden3) != 0U ||
        (sr & (UART_ERROR_MASK | USART_SR_RXNE)) != 0U ||
        (sr & USART_SR_TC) == 0U) {
        irq_restore(state);
        return false;
    }

    rx_head = 0U;
    rx_tail = 0U;
    tx_head = 0U;
    tx_tail = 0U;
    errors = 0U;

    port->user = NULL;
    port->try_write = try_write;
    port->try_read = try_read;
    port->tx_idle = tx_idle;
    port->clock_ms = clock_ms;

    active = true;
    LL_USART_EnableIT_ERROR(AS608_TEST_UART);
    LL_USART_EnableIT_RXNE(AS608_TEST_UART);
    irq_restore(state);
    return true;
}

void as608_test_port_stop(void) {
    const uint32_t state = irq_save();
    disable_sources();
    active = false;
    irq_restore(state);
}

/* Raw STM32 SR error bits (PE/FE/NE/ORE); bit 16 = RX ring overflow. */
uint32_t as608_test_port_get_errors(void) {
    const uint32_t state = irq_save();
    const uint32_t result = errors;
    irq_restore(state);
    return result;
}

/* =========================================================================
 * USART interrupt dispatcher
 * ========================================================================= */

void as608_test_port_irq_handler(void) {
    if (!active || errors != 0U) {
        return;
    }

    /* One SR snapshot; a single DR read consumes RX and clears RX errors. */
    const uint32_t sr = LL_USART_ReadReg(AS608_TEST_UART, SR);
    if ((sr & UART_ERROR_MASK) != 0U) {
        (void)LL_USART_ReceiveData8(AS608_TEST_UART);
        latch_fault(sr & UART_ERROR_MASK);
        return;
    }

    if ((sr & USART_SR_RXNE) != 0U) {
        const uint8_t byte = LL_USART_ReceiveData8(AS608_TEST_UART);
        const uint16_t head = rx_head;
        const uint16_t next = ring_advance(head, 1U, RX_BUFFER_SIZE);

        if (next == rx_tail) {
            latch_fault(RX_OVERFLOW);
            return;
        }

        rx_buffer[head] = byte;
        __DMB();
        rx_head = next;
    }

    if ((sr & USART_SR_TXE) != 0U &&
        LL_USART_IsEnabledIT_TXE(AS608_TEST_UART)) {
        const uint16_t tail = tx_tail;
        if (tail != tx_head) {
            /* SR read followed by DR write clears TC on STM32F1. */
            LL_USART_TransmitData8(AS608_TEST_UART, tx_buffer[tail]);
            tx_tail = ring_advance(tail, 1U, TX_BUFFER_SIZE);
        }

        if (tx_tail == tx_head) {
            LL_USART_DisableIT_TXE(AS608_TEST_UART);
        }
    }
}
