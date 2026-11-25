# AS608 UART driver

Portable ISO C11 cooperative **non-blocking** driver for AS60x fingerprint
modules. No heap, HAL, RTOS, or UART wait loops in the core. One context per
physical sensor. Historical hardware validation: two AS608_QCCF modules on
STM32F103C8T6, USART1/USART2, 57,600 baud, 8N2; successful 768-byte template
transfer. The unified non-blocking port still needs hardware validation.

## Flat project structure

```text
AS608_lib/
├── as608.h                       Public command API, types and context
├── as608.c                       Single non-blocking command/transaction FSM
├── as608_port.h                  Non-blocking UART port contract
├── as608_packet.h/.c              Internal packet codec and checksum
├── test_as608_packet.c           Packet tests
├── test_as608_commands.c         Full command and template regression tests
├── test_as608_async.c            Cooperative scheduling tests
├── test_as608_async_faults.c      Partial I/O and fault tests
├── README.md
├── docs/adr/0001-as608-driver-architecture.md
└── .github/workflows/c11-host-tests.yml
```

## Architecture and dependency graph

```text
Application (main loop / task)
              |
              v
         as608.h / as608.c
        start / process / result
              |
      +-------+-------------------+
      |                           |
      v                           v
as608_packet.h/.c           as608_port.h (contract)
encode/decode/checksum            |
                                  v
                          Platform UART adapter
                          TX queue / RX ring / clock
                                  |
                                  v
                           UART / AS608 sensor
```

The packet codec is pure software; the port does not depend on it.
There is one command/transaction engine and one port contract, not parallel
engines. A job owns its context until completion or quarantine.

```text
start -> BUSY -> TX -> RX_HEADER -> RX_BODY -> verify -> next phase
                    ^                                 |
                    +---------------------------------+
       Final download DATA -> TX_DRAIN -> finish or STORE command
       Valid final response -> DONE
       Timeout / malformed reply / I/O error / abort -> DESYNCHRONIZED
```

## API and platform port

Initialize a zeroed `as608_t` with `as608_config_t`. Supply
`port.try_write`, `port.try_read`, `port.tx_idle`, `port.clock_ms` and
`port.user`. Each start call returns admission status, not the sensor ACK.
Each process call performs at most one transport callback (plus a clock read).
The application must schedule process while BUSY; no function waits for UART.

- try_write copies accepted bytes into an owned, ordered TX queue.
- try_read consumes available RX bytes; zero is normal backpressure/empty.
- tx_idle returns 1 only after the queue is empty and physical UART TX
  completes (TC, not merely DMA completion); 0 while pending, negative on error.
- clock_ms is monotonic modulo 2^32. Timeout covers the whole job.
- Report UART errors and dropped bytes through the port. Do not silently flush.

All previous sensor commands now use `as608_*_start()`: password, parameters,
count, index, information page, capture, character generation, match, model,
search, load, upload, download, store, read/write template, delete, empty.
For example:

```c
static as608_t sensor;
static uint8_t model[AS608_TEMPLATE_MAX_BYTES];
static as608_template_buffer_t output = {model, sizeof(model), 0U};

/* cfg supplies the application-owned UART callbacks. */
if (as608_init(&sensor, &cfg) == AS608_STATUS_OK) {
    (void)as608_read_template_start(&sensor, 1U, &output);
}

/* Call from the scheduler, interleaved with other work. */
if (sensor.state == AS608_STATE_BUSY) {
    const as608_progress_t progress = as608_process(&sensor);
    if (progress != AS608_PENDING) {
        as608_result_t result;
        if (as608_get_result(&sensor, &result) &&
            result.status == AS608_STATUS_OK &&
            result.confirmation == AS608_ACK_OK) {
            /* output.length is the verified actual byte count. */
        }
    }
}
```

A valid negative sensor ACK completes the job without quarantining the link.
For transport/protocol failure or abort, arrange physical reset/power cycle,
wait for boot without blocking the scheduler, stop/purge old TX/RX and clear
errors. Only when the platform guarantees a fresh physical epoch may it call
`as608_confirm_reset()`. This function does not perform hardware recovery.
Never assume a timed-out flash operation had no effect.

Template sizes supported: 512 and 768 bytes; actual length is returned only
on success. A standalone DownChar completes when TX drains, not on an invented
sensor confirmation. A combined write additionally waits for StoreChar ACK.
Template interoperability across firmware variants is not guaranteed.

## Build and test

Add `as608.c` and `as608_packet.c` to the Keil project and include the root.
Do not link host test files (each contains a main). Supply the platform port;
STM32 HAL IT/DMA integration is not yet provided.

```sh
mkdir -p build
for suite in packet commands async async_faults; do
  cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608.c test_as608_${suite}.c -o build/test_${suite}
  ./build/test_${suite}
done
```

## Migration

This is an intentional breaking API change. Replace old synchronous calls and
`as608_async_*` names with `as608_*_start()`, `as608_process()`,
`as608_get_result()`; use the single `as608_t` and `as608_config_t`.
Historical source and hardware baseline are available in Git history.
