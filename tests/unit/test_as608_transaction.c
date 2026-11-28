/** @file test_as608_transaction.c @brief Independent transport tests. */
#include "as608_transaction.h"

#ifdef AS608_H
#error "Transaction must not depend on the public command header"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[267U];
    uint8_t rx[267U];
    size_t tx_length;
    size_t rx_length;
    size_t rx_offset;
    size_t calls;
    size_t clock_calls;
    uint32_t now;
    int32_t idle;
    bool stall;
    size_t burst;
    int32_t write_fault;
    int32_t read_fault;
} mock_t;

/* Accept an ordered prefix into the mock port-owned TX queue. */
static int32_t write_byte(void *user, const uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->calls;
    if (m->write_fault != 0) return m->write_fault;
    if (m->stall) return 0;
    assert(length != 0U && m->tx_length < sizeof(m->tx));
    const size_t cap = m->burst != 0U ? m->burst : 1U;
    const size_t count = length < cap ? length : cap;
    assert(count <= sizeof(m->tx) - m->tx_length);
    (void)memcpy(m->tx + m->tx_length, data, count);
    m->tx_length += count;
    return (int32_t)count;
}

/* Consume available mock RX bytes in order. */
static int32_t read_byte(void *user, uint8_t *data, size_t length)
{
    mock_t *m = user;
    ++m->calls;
    assert(length != 0U);
    if (m->read_fault != 0) return m->read_fault;
    const size_t available = m->rx_length - m->rx_offset;
    const size_t cap = m->burst != 0U ? m->burst : 1U;
    size_t count = length < cap ? length : cap;
    if (count > available) count = available;
    if (count == 0U) return 0;
    (void)memcpy(data, m->rx + m->rx_offset, count);
    m->rx_offset += count;
    return (int32_t)count;
}

/* Return the simulated physical TX completion state. */
static int32_t idle(void *user)
{
    mock_t *m = user;
    ++m->calls;
    return m->idle;
}

/* Return the mock monotonic clock. */
static uint32_t clock_ms(void *user)
{
    mock_t *m = user;
    ++m->clock_calls;
    return m->now;
}

/* Initialize a fresh transaction with all mandatory callbacks. */
static void setup(as608_transaction_t *ctx, mock_t *mock)
{
    const as608_config_t config = {
        .port = { .user = mock, .try_write = write_byte,
                  .try_read = read_byte, .tx_idle = idle, .clock_ms = clock_ms },
        .device_address = AS608_DEFAULT_ADDRESS, .timeout_ms = 100U
    };
    assert(as608_transaction_config_valid(&config));
    as608_transaction_init(ctx, &config);
    as608_transaction_begin(ctx);
}

/* Poll once and enforce the UART callback budget. */
static as608_transaction_result_t step(as608_transaction_t *ctx, mock_t *mock)
{
    const size_t calls = mock->calls;
    const size_t clock_calls = mock->clock_calls;
    const as608_transaction_result_t result = as608_transaction_process(ctx);
    assert(mock->calls - calls <= 1U);
    assert(mock->clock_calls - clock_calls == 1U);
    assert((result.event == AS608_TXN_FAULT) == (result.status != AS608_STATUS_OK));
    return result;
}

/* Poll until a terminal transaction event. */
static as608_transaction_result_t run(as608_transaction_t *ctx, mock_t *mock)
{
    for (size_t i = 0U; i < 300U; ++i) {
        const as608_transaction_result_t result = step(ctx, mock);
        if (result.event != AS608_TXN_PENDING) return result;
    }
    assert(0 && "transport stalled");
    return (as608_transaction_result_t){AS608_TXN_FAULT, AS608_STATUS_TIMEOUT};
}

/* Load an independently encoded ACK into the mock RX queue. */
static void prepare_rx(mock_t *mock)
{
    /* Fixed independent wire vector: ACK with one arbitrary payload byte.
     * Sum = 0x07 + 0x00 + 0x03 + 0x42 = 0x004C. */
    const uint8_t bytes[] = {
        0xEFU, 0x01U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0x07U, 0x00U, 0x03U, 0x42U, 0x00U, 0x4CU
    };
    (void)memcpy(mock->rx, bytes, sizeof(bytes));
    mock->rx_length = sizeof(bytes);
    mock->rx_offset = 0U;
}

/* Check send, receive and drain events. */
static void test_packet_events(void)
{
    mock_t mock = {0};
    as608_transaction_t ctx;
    setup(&ctx, &mock);
    uint8_t data = 0x42U;
    const as608_transaction_frame_t frame = {AS608_PACKET_DATA, &data, 1U};
    assert(as608_transaction_send(&ctx, &frame) == AS608_STATUS_OK);
    data = 0U; /* Send must own a copy. */
    mock.stall = true;
    assert(step(&ctx, &mock).event == AS608_TXN_PENDING);
    assert(mock.tx_length == 0U);
    mock.stall = false;
    assert(run(&ctx, &mock).event == AS608_TXN_SENT);
    assert(mock.tx_length == 12U && mock.tx[9] == 0x42U);
    assert(mock.tx[10] == 0U && mock.tx[11] == 0x47U);

    as608_transaction_receive(&ctx, 1U);
    assert(step(&ctx, &mock).event == AS608_TXN_PENDING);
    prepare_rx(&mock);
    assert(run(&ctx, &mock).event == AS608_TXN_RECEIVED);
    const as608_transaction_frame_t reply = as608_transaction_received(&ctx);
    assert(reply.kind == AS608_PACKET_ACK && reply.length == 1U);
    assert(reply.data[0] == 0x42U); /* No command/ACK interpretation. */
    as608_transaction_drain(&ctx);
    assert(step(&ctx, &mock).event == AS608_TXN_PENDING);
    mock.idle = 1;
    assert(step(&ctx, &mock).event == AS608_TXN_DRAINED);
}

/* Check malformed packets and drain failures. */
static void test_faults(void)
{
    mock_t mock = {0};
    as608_transaction_t ctx;
    setup(&ctx, &mock);
    prepare_rx(&mock);
    mock.rx[11] ^= 1U;
    as608_transaction_receive(&ctx, 1U);
    as608_transaction_result_t result = run(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT &&
           result.status == AS608_STATUS_CHECKSUM_ERROR);

    prepare_rx(&mock);
    as608_transaction_receive(&ctx, 0U);
    result = run(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT &&
           result.status == AS608_STATUS_UNEXPECTED_REPLY);
    assert(mock.rx_offset == 9U);

    as608_transaction_drain(&ctx);
    mock.idle = -1;
    result = step(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
}

/* Check one wrap-safe deadline across phases. */
static void test_shared_deadline(void)
{
    mock_t mock = {.now = UINT32_MAX - 20U};
    as608_transaction_t ctx;
    setup(&ctx, &mock);
    const uint8_t data = 1U;
    const as608_transaction_frame_t frame = {AS608_PACKET_COMMAND, &data, 1U};
    assert(as608_transaction_send(&ctx, &frame) == AS608_STATUS_OK);
    mock.now += 80U;
    assert(run(&ctx, &mock).event == AS608_TXN_SENT);
    as608_transaction_receive(&ctx, 1U);
    mock.now += 20U;
    const size_t calls = mock.calls;
    const as608_transaction_result_t result = step(&ctx, &mock);
    assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_TIMEOUT);
    assert(mock.calls == calls);
}

/* Verify timeout bounds and required callbacks. */
static void test_config_validation(void)
{
    mock_t mock = {0};
    as608_transaction_t txn;
    setup(&txn, &mock);
    as608_config_t config = txn.config;
    config.timeout_ms = 0U;
    assert(!as608_transaction_config_valid(&config));
    config.timeout_ms = AS608_MAX_TIMEOUT_MS + 1U;
    assert(!as608_transaction_config_valid(&config));
    config.timeout_ms = 1U;
    config.port.user = NULL;
    assert(as608_transaction_config_valid(&config));
    config.port.try_read = NULL;
    assert(!as608_transaction_config_valid(&config));
    config.port.try_read = read_byte;
    config.port.try_write = NULL;
    assert(!as608_transaction_config_valid(&config));
    config.port.try_write = write_byte;
    config.port.tx_idle = NULL;
    assert(!as608_transaction_config_valid(&config));
    config.port.tx_idle = idle;
    config.port.clock_ms = NULL;
    assert(!as608_transaction_config_valid(&config));
    assert(!as608_transaction_config_valid(NULL));
    assert(mock.calls == 0U);
}

/* Verify accepted TX is distinct from drain and ordered across frames. */
static void test_port_queue(void) {
    mock_t mock = {.burst = 5U};
    as608_transaction_t txn;
    setup(&txn, &mock);
    uint8_t payload = 0x42U;
    const as608_transaction_frame_t frame = {AS608_PACKET_DATA, &payload, 1U};
    const uint8_t first[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                              2U, 0U, 3U, 0x42U, 0U, 0x47U};
    const uint8_t second[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                               2U, 0U, 3U, 0x99U, 0U, 0x9EU};
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
    assert(step(&txn, &mock).event == AS608_TXN_PENDING);
    assert(mock.tx_length == 5U);
    mock.stall = true;
    assert(step(&txn, &mock).event == AS608_TXN_PENDING);
    assert(mock.tx_length == 5U);
    mock.stall = false;
    assert(run(&txn, &mock).event == AS608_TXN_SENT);
    assert(mock.idle == 0 && mock.tx_length == sizeof(first));

    payload = 0x99U;
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
    assert(run(&txn, &mock).event == AS608_TXN_SENT);
    assert(mock.tx_length == sizeof(first) + sizeof(second));
    assert(memcmp(mock.tx, first, sizeof(first)) == 0);
    assert(memcmp(mock.tx + sizeof(first), second, sizeof(second)) == 0);

    /* A fast RX reply may already be queued before explicit TX drain. */
    prepare_rx(&mock);
    as608_transaction_receive(&txn, 1U);
    assert(run(&txn, &mock).event == AS608_TXN_RECEIVED);
    assert(as608_transaction_received(&txn).data[0] == 0x42U);
    as608_transaction_drain(&txn);
    assert(step(&txn, &mock).event == AS608_TXN_PENDING);
    mock.idle = 1;
    assert(step(&txn, &mock).event == AS608_TXN_DRAINED);
}

/* Verify all UART phases reject negative or overreported callback results. */
static void test_port_faults(void) {
    const int32_t faults[] = {INT32_MIN, -1, 268, 65536, 65537, 131073, INT32_MAX};
    const uint8_t payload = 1U;
    const as608_transaction_frame_t frame = {AS608_PACKET_COMMAND, &payload, 1U};
    for (size_t i = 0U; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        mock_t mock = {.burst = 9U};
        as608_transaction_t txn;
        setup(&txn, &mock);
        assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
        mock.write_fault = faults[i];
        as608_transaction_result_t result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
        assert(mock.tx_length == 0U);
        mock.write_fault = 0;

        prepare_rx(&mock);
        as608_transaction_receive(&txn, 1U);
        mock.read_fault = faults[i];
        result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
        assert(mock.rx_offset == 0U);
        mock.read_fault = 0;

        as608_transaction_receive(&txn, 1U);
        assert(step(&txn, &mock).event == AS608_TXN_PENDING);
        assert(mock.rx_offset == 9U);
        mock.read_fault = faults[i];
        result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
        assert(mock.rx_offset == 9U);
        mock.read_fault = 0;

        as608_transaction_drain(&txn);
        mock.idle = faults[i];
        result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
    }
    mock_t mock = {.idle = 2};
    as608_transaction_t txn;
    setup(&txn, &mock);
    as608_transaction_drain(&txn);
    const as608_transaction_result_t result = step(&txn, &mock);
    assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_IO_ERROR);
}

/* Verify the explicit receive address is phase-local, never a wildcard. */
static void test_receive_address(void) {
    mock_t mock = {.burst = 12U};
    as608_transaction_t txn;
    setup(&txn, &mock);
    prepare_rx(&mock);
    mock.rx[2] = 0x12U;
    mock.rx[3] = 0x34U;
    mock.rx[4] = 0x56U;
    mock.rx[5] = 0x78U;
    const uint32_t started = txn.started_ms;
    as608_transaction_receive_at(&txn, 1U, UINT32_C(0x12345678));
    assert(mock.calls == 0U && mock.clock_calls == 1U);
    assert(txn.config.device_address == AS608_DEFAULT_ADDRESS && txn.started_ms == started);
    assert(run(&txn, &mock).event == AS608_TXN_RECEIVED);
    const as608_transaction_frame_t view = as608_transaction_received(&txn);
    assert(view.data == txn.rx_body && view.length == 1U && view.data[0] == 0x42U);

    mock.rx_offset = 0U;
    as608_transaction_receive(&txn, 1U);
    as608_transaction_result_t result = run(&txn, &mock);
    assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_FRAME_ERROR);
    assert(mock.rx_offset == 9U);

    /* This fixture starts a fresh simulated link after the fault. */
    as608_transaction_init(&txn, &txn.config);
    assert(txn.config.device_address == AS608_DEFAULT_ADDRESS);
    prepare_rx(&mock);
    as608_transaction_begin(&txn);
    as608_transaction_receive(&txn, 1U);
    assert(run(&txn, &mock).event == AS608_TXN_RECEIVED);
}

/* Exercise the storage bound independently of the command-specific limit. */
static void test_receive_bounds(void) {
    const uint16_t lengths[] = {0U, 256U, 257U};
    const size_t limits[] = {0U, 255U, 256U, 512U};
    for (size_t i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        for (size_t j = 0U; j < sizeof(limits) / sizeof(limits[0]); ++j) {
            mock_t mock = {.burst = 17U};
            as608_transaction_t txn;
            setup(&txn, &mock);
            const uint16_t len = lengths[i];
            const uint16_t wire_len = (uint16_t)(len + 2U);
            const uint8_t header[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                      8U, (uint8_t)(wire_len >> 8U), (uint8_t)wire_len};
            (void)memcpy(mock.rx, header, sizeof(header));
            mock.rx_length = sizeof(header);
            if (len <= 256U) {
                uint32_t sum = 8U + header[7] + header[8];
                for (size_t n = 0U; n < len; ++n) {
                    mock.rx[9U + n] = (uint8_t)(n * 19U);
                    sum += mock.rx[9U + n];
                }
                mock.rx[9U + len] = (uint8_t)(sum >> 8U);
                mock.rx[10U + len] = (uint8_t)sum;
                mock.rx_length = 11U + len;
            }
            as608_transaction_receive(&txn, limits[j]);
            const as608_transaction_result_t result = run(&txn, &mock);
            if (len > 256U || len > limits[j]) {
                assert(result.event == AS608_TXN_FAULT);
                assert(result.status == (len > 256U ? AS608_STATUS_FRAME_ERROR :
                                                     AS608_STATUS_UNEXPECTED_REPLY));
                assert(mock.rx_offset == 9U);
            } else {
                assert(result.event == AS608_TXN_RECEIVED);
                const as608_transaction_frame_t view = as608_transaction_received(&txn);
                assert(view.kind == AS608_PACKET_END_DATA && view.length == len);
                assert(memcmp(view.data, mock.rx + 9U, len) == 0);
                assert(mock.rx_offset == mock.rx_length);
            }
        }
    }
}

/* Verify preparation copies a maximum frame and rejects invalid lengths. */
static void test_send_bounds(void) {
    mock_t mock = {.burst = 23U};
    as608_transaction_t txn;
    setup(&txn, &mock);
    uint8_t bytes[256U];
    (void)memset(bytes, 0xA5, sizeof(bytes));
    as608_transaction_frame_t frame = {AS608_PACKET_END_DATA, bytes, 257U};
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_INVALID_ARG);
    frame.length = 1U;
    frame.data = NULL;
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_INVALID_ARG);
    frame.length = sizeof(bytes);
    frame.data = bytes;
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
    assert(mock.calls == 0U && mock.clock_calls == 1U);
    (void)memset(bytes, 0U, sizeof(bytes));
    assert(run(&txn, &mock).event == AS608_TXN_SENT);
    assert(mock.tx_length == 267U && mock.tx[6] == 8U);
    assert(mock.tx[7] == 1U && mock.tx[8] == 2U);
    for (size_t i = 9U; i < 265U; ++i) {
        assert(mock.tx[i] == 0xA5U);
    }
    assert(mock.tx[265] == 0xA5U && mock.tx[266] == 0x0BU);

    mock.tx_length = 0U;
    frame.data = NULL;
    frame.length = 0U;
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
    assert(run(&txn, &mock).event == AS608_TXN_SENT);
    assert(mock.tx_length == 11U && mock.tx[8] == 2U && mock.tx[10] == 10U);
}

/* Verify direct payload encoding, busy rejection and exact wire equivalence. */
static void test_in_place_send(void) {
    const size_t lengths[] = {0U, 1U, 64U, 128U, 256U};
    assert(as608_transaction_tx_payload(NULL) == NULL);
    for (size_t i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        mock_t direct_mock = {.burst = 17U};
        mock_t copy_mock = {.burst = 17U};
        as608_transaction_t direct;
        as608_transaction_t copy;
        uint8_t bytes[256U];
        setup(&direct, &direct_mock);
        setup(&copy, &copy_mock);
        uint8_t *const payload = as608_transaction_tx_payload(&direct);
        assert(payload == direct.tx_frame + AS608_PACKET_HEADER_BYTES);
        for (size_t j = 0U; j < lengths[i]; ++j) {
            bytes[j] = (uint8_t)(j * 13U + 7U);
            payload[j] = bytes[j];
        }
        const as608_transaction_frame_t direct_frame = {
            AS608_PACKET_END_DATA, payload, lengths[i]
        };
        const as608_transaction_frame_t copy_frame = {
            AS608_PACKET_END_DATA, bytes, lengths[i]
        };
        assert(as608_transaction_send(&direct, &direct_frame) == AS608_STATUS_OK);
        assert(as608_transaction_send(&copy, &copy_frame) == AS608_STATUS_OK);
        assert(as608_transaction_tx_payload(&direct) == NULL);
        assert(as608_transaction_send(&direct, &direct_frame) == AS608_STATUS_BUSY);
        assert(direct_mock.calls == 0U && direct_mock.clock_calls == 1U);
        assert(run(&direct, &direct_mock).event == AS608_TXN_SENT);
        assert(run(&copy, &copy_mock).event == AS608_TXN_SENT);
        assert(direct_mock.tx_length == lengths[i] + AS608_PACKET_OVERHEAD_BYTES);
        assert(direct_mock.tx_length == copy_mock.tx_length);
        assert(memcmp(direct_mock.tx, copy_mock.tx, direct_mock.tx_length) == 0);
        assert(memcmp(direct_mock.tx + AS608_PACKET_HEADER_BYTES,
                      bytes, lengths[i]) == 0);
        assert(as608_transaction_tx_payload(&direct) == payload);
        as608_transaction_receive(&direct, 1U);
        assert(as608_transaction_tx_payload(&direct) == NULL);
        as608_transaction_drain(&copy);
        assert(as608_transaction_tx_payload(&copy) == NULL);
    }
}

/* Reject invalid full-width kinds and active-phase replacement atomically. */
static void test_send_admission(void) {
    mock_t mock = {0};
    as608_transaction_t txn;
    setup(&txn, &mock);
    const uint8_t data = 0x42U;
    as608_transaction_frame_t frame = {AS608_PACKET_COMMAND, &data, 1U};
    unsigned char saved[sizeof(txn)];
    (void)memcpy(saved, &txn, sizeof(saved));
    assert(as608_transaction_send(NULL, &frame) == AS608_STATUS_INVALID_ARG);
    assert(as608_transaction_send(&txn, NULL) == AS608_STATUS_INVALID_ARG);
    /* Wide invalid values whose low byte would otherwise be valid.
     * With short enums, such a value is already truncated by the caller;
     * test only values representable by the enum's actual storage. */
    const unsigned int invalid[] = {0U, 3U, 7U, 9U, 255U, 257U, 258U, 264U, 65537U};
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        frame.kind = (as608_packet_kind_t)invalid[i];
        if ((unsigned int)frame.kind != invalid[i]) {
            continue;
        }
        assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_INVALID_ARG);
        assert(memcmp(saved, &txn, sizeof(saved)) == 0);
    }

    frame.kind = AS608_PACKET_COMMAND;
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
    (void)memcpy(saved, &txn, sizeof(saved));
    assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_BUSY);
    assert(memcmp(saved, &txn, sizeof(saved)) == 0);
    assert(mock.calls == 0U && mock.clock_calls == 1U);
    assert(run(&txn, &mock).event == AS608_TXN_SENT);
}

/* Inject inconsistent internal counters before any buffer access. */
static void test_counter_faults(void) {
    const uint8_t data = 0x42U;
    const as608_transaction_frame_t frame = {AS608_PACKET_COMMAND, &data, 1U};

    for (unsigned int fault_id = 0U; fault_id < 15U; ++fault_id) {
        mock_t mock = {.burst = 9U};
        as608_transaction_t txn;
        setup(&txn, &mock);

        if (fault_id < 4U) {
            assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
            switch (fault_id) {
                case 0U: txn.tx_len = 10U; break;
                case 1U: txn.tx_len = SIZE_MAX; break;
                case 2U: txn.tx_pos = txn.tx_len; break;
                default: txn.tx_pos = SIZE_MAX; break;
            }
        } else if (fault_id < 6U) {
            as608_transaction_receive(&txn, 1U);
            txn.rx_header_pos = fault_id == 4U ? sizeof(txn.rx_header) : SIZE_MAX;
        } else {
            prepare_rx(&mock);
            as608_transaction_receive(&txn, 1U);
            assert(step(&txn, &mock).event == AS608_TXN_PENDING);
            assert(mock.rx_offset == sizeof(txn.rx_header));
            switch (fault_id) {
                case 6U: txn.rx_body_len = 0U; break;
                case 7U: txn.rx_body_len = SIZE_MAX; break;
                case 8U: txn.rx_body_pos = txn.rx_body_len; break;
                case 9U: txn.rx_body_pos = SIZE_MAX; break;
                case 10U: txn.rx_payload_len = SIZE_MAX; break;
                case 11U: txn.rx_payload_len = 2U; break;
                case 12U: txn.rx_limit = 0U; break;
                case 13U: txn.rx_kind = AS608_PACKET_COMMAND; break;
                default: txn.rx_body_len = 2U; break;
            }
        }

        const size_t calls = mock.calls;
        const size_t tx_length = mock.tx_length;
        const size_t rx_offset = mock.rx_offset;
        const as608_transaction_result_t result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT &&
               result.status == AS608_STATUS_UNEXPECTED_REPLY);
        assert(mock.calls == calls && mock.tx_length == tx_length &&
               mock.rx_offset == rx_offset);
    }
}

/* Verify deadline precedence in all four active transport phases. */
static void test_deadline_precedence(void) {
    const uint8_t data = 0x42U;
    const as608_transaction_frame_t frame = {AS608_PACKET_COMMAND, &data, 1U};
    for (unsigned int phase = 0U; phase < 4U; ++phase) {
        mock_t mock = {.burst = 9U, .now = UINT32_MAX - 49U};
        as608_transaction_t txn;
        setup(&txn, &mock);
        if (phase == 0U) {
            assert(as608_transaction_send(&txn, &frame) == AS608_STATUS_OK);
        } else if (phase == 3U) {
            as608_transaction_drain(&txn);
        } else {
            prepare_rx(&mock);
            as608_transaction_receive(&txn, 1U);
            if (phase == 2U) {
                assert(step(&txn, &mock).event == AS608_TXN_PENDING);
            }
        }
        mock.now = 50U;
        const size_t calls = mock.calls;
        const as608_transaction_result_t result = step(&txn, &mock);
        assert(result.event == AS608_TXN_FAULT && result.status == AS608_STATUS_TIMEOUT);
        assert(mock.calls == calls);
    }
}

int main(void)
{
    test_config_validation();
    test_packet_events();
    test_faults();
    test_shared_deadline();
    test_port_queue();
    test_port_faults();
    test_receive_address();
    test_receive_bounds();
    test_send_bounds();
    test_in_place_send();
    test_send_admission();
    test_counter_faults();
    test_deadline_precedence();
    puts("as608 transaction tests: PASS");
    return 0;
}
