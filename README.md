# AS608 UART Driver

Portable **ISO C11** driver for AS60x-protocol fingerprint modules, validated on two
**AS608_QCCF** modules connected to an STM32F103C8T6 through USART1 and USART2
(57,600 baud, 8N2 on the tested hardware). No heap allocation or STM32 HAL
dependency in the core.

> **Status:** The original blocking API is the hardware-validated baseline.
> The additive cooperative async engine is experimental. The async STM32
> interrupt/DMA port and target validation have **not** been completed.

## Project file tree (flat layout)

```text
AS608_lib/
├── as608.h                       Public blocking API and shared types
├── as608_port.h                  Blocking UART/clock/recovery callback contract
├── as608.c                       Blocking AS60x commands
├── as608_transaction.h           Internal blocking transaction interface
├── as608_transaction.c           Blocking framed TX/RX, timeout and recovery
├── as608_packet.h                Internal packet framing interface
├── as608_packet.c                Shared header/checksum codec (no UART I/O)
├── as608_async.h                 Cooperative async API and nonblocking port
├── as608_async.c                 Async command/packet/stream state machine
├── test_as608_packet.c           Packet framing tests
├── test_as608_transaction.c      Blocking transaction/error tests
├── test_as608_commands.c         Blocking commands and 512/768 B tests
├── test_as608_async.c            Async ACK/stream/timeout/two-device tests
├── README.md                     This file, build steps and architecture
├── docs/
│   └── adr/
│       └── 0001-as608-driver-architecture.md
└── .github/
    └── workflows/
        └── c11-host-tests.yml    GitHub Actions: GCC + Clang
```

**Flat** means all C and H files, including test sources, live in the
repository root. The `docs/` and `.github/workflows/` directories are retained
because they hold documentation and a GitHub Actions workflow (which GitHub
only recognizes in that location). There are no production stub headers.

## Architecture and dependency graph

```text
                   Application (STM32 / RTOS / Linux)
                   /                           \
                  v                             v
        as608.h + as608.c              as608_async.h + as608_async.c
             BLOCKING                      COOPERATIVE ASYNC
                  |                             |
                  v                             | async port callbacks
  as608_transaction.h/.c                       v
                  |                    Platform-owned UART TX/RX queues
                  |                             |
                  +--------------+--------------+
                                 |
                       as608_packet.h/.c
                    Shared packet codec/checksum
```

The two engines **share the packet codec and AS60x type definitions**, but have
different port contracts. Use only **one engine on a given physical sensor/UART
at a time**. They must never race for the same sensor or consume each other's
ACKs. Two independent sensors can be processed with two async contexts.

The packet codec supports `0xEF01` start, 32-bit address, frame kind,
16-bit payload+checksum length and the additive 16-bit checksum. The
transaction layer is responsible for deadlines and quarantine, while the
command layer interprets ACK codes and maintains template operations.

### Blocking control flow

```text
application → as608_read_template(...)
            → PS_LoadChar → ACK → PS_UpChar → ACK
            → DATA ... END_DATA → result
```

The blocking port may wait for bytes. It was exercised on real sensors;
retaining it is intentional while the async port is being validated.

### Cooperative async control flow

```text
start(...) ────→ BUSY
                 |
                 v
          process(): one nonblocking port I/O attempt
                 |
                 +─ TX frame partial / RX header partial / RX payload partial
                 |                       |
                 |                       v
                 |                  checksum verify
                 |                       |
                 |        next command or DATA ... END_DATA
                 |                       |
                 +── PENDING             +── DONE / DESYNCHRONIZED
```

`as608_async_process()` must run frequently; it invokes at most **one**
`try_read` or `try_write` callback per invocation and does not wait for UART.
The port's `try_write` must **copy** all accepted bytes into its own ordered
TX queue; returning a count while borrowing a DMA pointer that can change is
not allowed. `try_read` consumes bytes from an RX queue; UART errors, overflow
or dropped bytes must surface as a port error. Partial transfers and zero
progress are expected, not faults.

Timeout covers the **whole operation**, including every packet. Unexpected
reply, checksum error, timeout, I/O failure or abort quarantines the context:
a verified physical sensor reset and UART RX/TX queue cleanup are required
before any fresh operation. Reinitializing software alone is not recovery.

### Async API example

```c
static as608_async_t sensor;
static uint8_t raw_template[AS608_TEMPLATE_MAX_BYTES];
static as608_template_buffer_t rx = {
    .data = raw_template, .capacity = sizeof(raw_template), .length = 0U
};

/* Initialize once with platform nonblocking callbacks in cfg. */
as608_async_init(&sensor, &cfg);
as608_async_read_template_start(&sensor, 1U, &rx);

for (;;) {
    const as608_async_progress_t progress = as608_async_process(&sensor);
    if (progress == AS608_ASYNC_COMPLETE) {
        as608_result_t result;
        if (as608_async_get_result(&sensor, &result) &&
            result.status == AS608_STATUS_OK &&
            result.confirmation == AS608_ACK_OK) {
            /* rx.length is 512 or 768 bytes (on supported firmware). */
        }
        break;
    }
    if (progress == AS608_ASYNC_FAILED) {
        /* Quarantined on transport/protocol failure: arrange recovery. */
        break;
    }
    /* Do unrelated work; do NOT busy-loop on process() in production. */
}
```

Do not copy this loop verbatim into the production scheduling design; call
`as608_async_process` from the application scheduler or main superloop,
interleaved with other work. Keep `raw_template` valid throughout the transfer.

## Host build and test (from repository root)

```sh
mkdir -p build
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  as608_packet.c test_as608_packet.c -o build/test_packet
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  as608_packet.c as608_transaction.c test_as608_transaction.c \
  -o build/test_transaction
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  as608_packet.c as608_transaction.c as608.c test_as608_commands.c \
  -o build/test_commands
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  as608_packet.c as608_async.c test_as608_async.c -o build/test_async
./build/test_packet
./build/test_transaction
./build/test_commands
./build/test_async
```

For Keil, add the production `.c` files from the root and set the root as
the include search path. **Do not add `test_as608_*.c` to the target build**:
those contain host test `main()` functions. Use the existing blocking
functions or the new async entry points; async requires a platform-specific
nonblocking port, not the old blocking UART callbacks.

## Template sizes and compatibility

The referenced AS60x manual describes **512 bytes**, while the tested
AS608_QCCF firmware emitted **768 bytes** (six 128-byte data fragments,
final fragment marked END_DATA). Importing this opaque 768-byte model into
the second tested module and matching the original finger succeeded.

`AS608_TEMPLATE_MAX_BYTES` is the allocation bound; the actual number of
bytes is returned in `as608_template_buffer_t.length` after successful upload.
Never send the buffer's capacity in place of the actual length. Compatibility
is **not** guaranteed across arbitrary AS608 firmware variants.

## Remaining work

- STM32F103 HAL IT/DMA nonblocking UART port: TX queue ownership, RX ring,
  overflow detection, UART error propagation and physical recovery.
- More async commands: Search/Match/Enrollment/Delete and other command set.
- Target integration and real-sensor regression including 768-byte transfer,
  independently scheduled USART1/USART2, late ACK, reset and power cycling.
- Confirm host CI results before calling async validated for production.

Protocol and architectural decisions: [ADR-0001](docs/adr/0001-as608-driver-architecture.md).
