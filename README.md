# AS608 UART driver

Portable ISO C11 cooperative **non-blocking** driver for AS60x fingerprint
modules. No heap, HAL, RTOS, or UART wait loops in the core. One context per
physical sensor. Historical hardware validation: two AS608_QCCF modules on
STM32F103C8T6, USART1/USART2, 57,600 baud, 8N2; successful 768-byte template
transfer. The unified non-blocking port still needs hardware validation.

## Incremental API rebuild — active stage 14

The five lifecycle APIs and nine command APIs (0x13, 0x0F, 0x1D, 0x1F, 0x16, 0x01, 0x02, 0x03, 0x05)
are currently compiled from as608.c. Process supports simple,
system-parameter, 16-bit result and index-table ACK phases, plus the
512-byte information-page stream. The previous implementation is preserved as line
comments below the active sections, excluding migrated definitions. Each subsequent
API will be restored in header order, with its required static helpers
above it and explicit section boundaries. The process dispatcher (API 02)
is placed after active command sections so reply handlers need no forward
declarations. Shared lifecycle helpers and named command/step enums are
at the top of as608.c. Add new opcodes to the single command enum.
Every active static function has a short English purpose comment immediately
above its definition; follow this convention for newly added helpers.
The packet and transaction layers are unchanged.

This is intentionally an incomplete, non-deployable command layer:
the other declarations in as608.h cannot currently be linked. The
command/async regression sources are retained but not built until their
required APIs are restored. CI still builds the independent packet and
transaction tests and the API 01–14 tests. Previous command examples and
coverage below describe the historical baseline, not this active stage.

## Flat project structure

```text
AS608_lib/
├── as608.h                       Public command API, types and context
├── as608.c                       Command FSM and public job lifecycle
├── as608_transaction.h/.c         Internal non-blocking packet transport
├── as608_port.h                  Non-blocking UART port contract
├── as608_types.h                 Shared status and transport constants
├── as608_packet.h/.c              Internal packet codec and checksum
├── test_as608_init.c             Active API 01 initialization tests
├── test_as608_process.c          Active API 02 progression tests
├── test_as608_get_result.c       Active API 03 result tests
├── test_as608_abort.c            Active API 04 abort tests
├── test_as608_confirm_reset.c    Active API 05 recovery tests
├── test_as608_verify_password.c  Active API 06 password tests
├── test_as608_read_parameters.c  Active API 07 parameter tests
├── test_as608_get_template_count.c Active API 08 count tests
├── test_as608_read_index_table.c Active API 09 bitmap tests
├── test_as608_read_information_page.c Active API 10 stream tests
├── test_as608_capture_image.c    Active API 11 capture tests
├── test_as608_generate_character.c Active API 12 feature tests
├── test_as608_match_buffers.c    Active API 13 comparison tests
├── test_as608_create_model.c     Active API 14 model creation tests
├── test_as608_transaction.c      Independent transport tests
├── test_as608_packet.c           Packet tests
├── test_as608_commands.c         Full command and template regression tests
├── test_as608_async.c            Cooperative scheduling tests
├── test_as608_async_faults.c      Partial I/O and fault tests
├── FEATURES.md                   Command inventory and implementation roadmap
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
              v
       as608_transaction.h/.c
        TX / RX / drain / deadline
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
The command layer owns command sequencing, ACK semantics and job lifecycle.
The transaction layer owns partial packet I/O, checksum validation and the
single job deadline; it returns SENT, RECEIVED, DRAINED or FAULT events.
There is one non-blocking engine and one port contract, not parallel engines.
A job owns its context until completion or quarantine.

### Header dependencies and ownership

Arrows denote direct project-header includes (standard C headers omitted):

```text
as608.h
  ├── as608_types.h
  └── as608_transaction.h
        ├── as608_types.h
        ├── as608_packet.h ──> as608_types.h
        └── as608_port.h   ──> as608_types.h
```

No lower layer includes as608.h. The packet codec does not include the
transaction or port headers. The command context as608_t embeds an
as608_transaction_t by value. The transport context owns its single copy
of as608_config_t, the deadline, wire phase, and packet buffers. The
command context owns only command/job state and caller buffer references.
Transport functions accept as608_transaction_t*, never as608_t*.
Headers expose storage layouts to permit static allocation in ISO C;
their internal fields and transport entry points are not public application API.

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

Add `as608.c`, `as608_transaction.c` and `as608_packet.c` to the Keil project and include the root.
Do not link host test files (each contains a main). Supply the platform port;
STM32 HAL IT/DMA integration is not yet provided.

```sh
mkdir -p build
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c test_as608_packet.c -o build/test_packet
./build/test_packet
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c test_as608_transaction.c -o build/test_transaction
./build/test_transaction
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_init.c -o build/test_init
./build/test_init
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_process.c -o build/test_process
./build/test_process
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_get_result.c -o build/test_get_result
./build/test_get_result
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_abort.c -o build/test_abort
./build/test_abort
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_confirm_reset.c -o build/test_confirm_reset
./build/test_confirm_reset
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_verify_password.c -o build/test_verify_password
./build/test_verify_password
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_read_parameters.c -o build/test_read_parameters
./build/test_read_parameters
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_get_template_count.c -o build/test_get_template_count
./build/test_get_template_count
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_read_index_table.c -o build/test_read_index_table
./build/test_read_index_table
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_read_information_page.c -o build/test_read_information_page
./build/test_read_information_page
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_capture_image.c -o build/test_capture_image
./build/test_capture_image
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_generate_character.c -o build/test_generate_character
./build/test_generate_character
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_match_buffers.c -o build/test_match_buffers
./build/test_match_buffers
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_create_model.c -o build/test_create_model
./build/test_create_model
# Full command/async regression tests are temporarily inactive.
```

## Full command API design

See [FEATURES.md](FEATURES.md) for the 31 documented AS60x commands and
their implementation status. The normal header exposes the implemented
non-blocking API. The additional, proposed contracts for the 15 missing
opcodes and selected composite operations are grouped under
`AS608_DECLARE_PLANNED_API` in `as608.h`. Defining the macro before inclusion
only reveals declarations for review; it does not provide implementations,
and linking a call to a planned function will fail until that function
is implemented. The existing public functions and context layout are unchanged.

Image and firmware data use caller-owned stream descriptors in the planned
API instead of full-image allocations. Unknown firmware extensions (e.g.
sleep or LED commands), platform adapters and multi-device backup/migration
orchestration are tracked separately; no unverified wire commands or
hardware-specific APIs are promised by the core.

## Migration

This is an intentional breaking API change. Replace old synchronous calls and
`as608_async_*` names with `as608_*_start()`, `as608_process()`,
`as608_get_result()`; use the single `as608_t` and `as608_config_t`.
Historical source and hardware baseline are available in Git history.
