# AS608 UART driver

Portable ISO C11 cooperative **non-blocking** driver for AS60x fingerprint
modules. No heap, HAL, RTOS, or UART wait loops in the core. One context per
physical sensor. Historical hardware validation: two AS608_QCCF modules on
STM32F103C8T6, USART1/USART2, 57,600 baud, 8N2; successful 768-byte template
transfer. The unified non-blocking port still needs hardware validation.

## Completed API rebuild — 46 APIs

Post-rebuild maintenance: stage 58, CI validation matrix.

The five lifecycle APIs and forty-one command/job APIs covering thirty-one opcodes (BurnCode experimental) (0x13, 0x0F, 0x1D, 0x1F, 0x16, 0x01, 0x02, 0x03, 0x05, 0x04, 0x07, 0x08, 0x09, 0x06, 0x0C, 0x0D, 0x10, 0x11, 0x1B, 0x0A, 0x0B, 0x1C, 0x0E, 0x17, 0x12, 0x14, 0x15, 0x18, 0x19, 0x1E, 0x1A)
are currently compiled from as608.c. Process supports simple,
system-parameter, 16-bit result, search and index-table ACK phases, plus the
512-byte information-page, 512/768-byte template upload/download and 256-byte feature upload/download and exact-length image upload/download streams.
The composite read, write and two-scan enrollment APIs chain their commands under a single job deadline.
Enrollment polls only for placement/removal, requires observed NO_FINGER before scan two,
and stores the merged template from CharBuffer2 at the caller's explicit PageID.
No duplicate detection, occupancy check or automatic flash retry is performed.
Range identification chains GetImage, GenChar and ordinary Search under
one deadline, using the requested buffer and range. No automatic finger
polling or retry; a negative ACK terminates the attempt. Match output is
committed only after a validated in-range Search success ACK.
Free-slot discovery reads actual capacity (1..1024), then scans up to four
32-byte index pages directly from verified receive frames. It ignores
padding bits, stops at the lowest free PageID, and never reserves a slot.
LSB-to-lowest-ID mapping is inferred from the related R503 manual; verify
on the actual AS608 before using the result for enrollment.
All baseline APIs are restored; the obsolete commented reference is removed
and remains available in Git history (commit 53425e4). Keep API sections in
header order, with required static helpers above their first use and explicit
section boundaries. The process dispatcher (API 02)
is placed after active command sections so reply handlers need no forward
declarations. Shared lifecycle helpers and named command/step enums are
at the top of as608.c. Add new opcodes to the single command enum.
Every active static function has a short English purpose comment immediately
above its definition; follow this convention for newly added helpers.
The packet header now documents structural versus semantic validation,
wire length and checksum bounds, buffer preconditions and independent
C11/C++11 header checks. Codec signatures and behavior are unchanged;
checksum matching reuses the existing big-endian reader. Packet tests
exhaust all 256 identifiers, all 65536 wire lengths for each of four
kinds, all 65536 checksum trailers, and validate an independent streaming sum.
API 39 adds an exact per-receive address
to the transaction layer without weakening normal address validation.
The port contract requires bounded, non-reentrant callbacks, ordered
prefix acceptance/consumption, port-owned DMA/IT buffers, latched link
faults and a coherent monotonic clock. TX acceptance differs from physical
TX completion: STM32F1 USART TC, not TXE or DMA TC alone, must be checked
with empty software queues and no outstanding transfers (ST RM0008 Rev 21).
Independent C11/C++11 port-header checks and mock partial-I/O, queue and
fault tests exercise these contracts. Actual ISR/DMA synchronization and
physical UART recovery remain platform integration responsibilities.
The transaction header documents phase admission, one original deadline,
borrowed RX views, fixed 256-byte payload storage and phase-local exact RX
addresses. SENT denotes port acceptance, RECEIVED denotes a structurally
validated frame, and DRAINED denotes physical TX completion; none alone
implies sensor command success. Event numbers and context layout are unchanged.
Independent C11/C++11 header checks and transport bounds/address tests
exercise these contracts. Stage 53 rejects invalid full-width TX kinds
and active-phase replacement, checks full-width callback counts before
narrowing, and guards TX/RX counters before subtraction or pointer arithmetic.
Inconsistent RX metadata faults before UART I/O. Deadline precedence and
callback budgets remain unchanged. Host tests inject invalid counters and
oversized callback results; actual narrow-size_t target execution remains
unverified.

The public header groups related records, fixes lifecycle/progress values,
and documents the distinction between admission, completion, result
availability and sensor success (including drain-only downloads). All 46
signatures and context members remain unchanged. A standalone C11/C++11
contract test checks public constants, signatures and links all 46 symbols
against C11-compiled objects. The obsolete planned-macro CI check is removed.
Stage 55 centralizes ACK shape checks without changing phase-specific
confirmation semantics, uses generic stream setup, clears stale input and
stream data references on admission, and validates exact fragment sizes.
Stream source results are compared at full int32_t width before use.
The dispatcher reuses its ACK limit to reject impossible SENT events.
The original public signatures, context layout, wire profiles and job
ordering are unchanged. Wide callback values and corrupt chunk metadata
are exercised by host regression tests.
All 41 dedicated command/job test translation units now use shared
one-byte UART fixtures in tests/common/mock_uart.h, with original local
buffer capacities and drain profiles. Identical independent frame helpers
are in mock_wire.h; specialized lifecycle, packet, transaction and partial-I/O
regressions retain their fixtures. Test cases and production sources are unchanged.
All production files now live together in src/. Host tests live in tests/unit,
tests/integration and tests/common. CMake/CTest replaces the former manual
host build commands; the core-only build does not require C++.

All 23 baseline APIs and APIs 24–46 are now implemented (BurnCode experimental). CI builds independent packet and
transaction tests, API 01–46 dedicated tests, and full command/async/fault
regressions. All 46 planned APIs are linkable; BurnCode remains experimental. The common-type
header has independent freestanding C11 and C++11 compile-time contract tests;
status numeric values are unchanged. Target hardware
validation and the DownChar final-ACK compatibility check remain outstanding.

Baud-rate changes use PS_WriteReg register 4, with the AS608 profile
9600..115200 bps in 9600-bps increments. Receive the ACK at the old rate;
the application owns UART synchronization at the new rate and readback.
Do not retry automatically after an uncertain register update.
Security-level changes use register 5 with levels 1 through 5; the
sensor owns its matching threshold. ACK_OK does not establish readback.
Packet-size changes use register 6 (codes 0–3 for 32/64/128/256-byte
payloads). Download jobs independently read the actual setting; image
download continues to reject code 0.
Port control (0x17) targets the interface other than the active one:
UART controls USB, and USB controls UART. Verify target support; the
host transport is not reconfigured and port persistence is not assumed.
Device-info reads validate the complete 512-byte information page but
stage only the 32 descriptor bytes at offsets 28–59. Output is committed
only on valid END_DATA. SensorName offset 52 is inferred from the
manual's sequential four-word layout; verify on target firmware.
SetPwd (0x12) changes the handshake password; verify separately with VfyPwd.
Do not retry after uncertain completion. Passwords are plaintext on the
wire and may remain in transport buffers; do not log raw frames or pages.
GetRandomCode (0x14) returns a raw 32-bit value only after a valid ACK.
No entropy, uniqueness or cryptographic randomness is guaranteed.
SetChipAddr (0x15) is initial provisioning only: default-address TX,
exact new-address ACK, local commit only on ACK_OK. All other replies,
timeout or abort quarantine the link without changing local configuration.
Verify the actual address externally after uncertain completion; no retry.
The manual indicates that reassignment requires clearing flash.
WriteNotepad (0x18) snapshots the page index and all 32 bytes at admission.
The documented range is 0..15. Every write replaces the full flash page;
no automatic retry or power-loss atomicity is promised. Verify on target.
ReadNotepad (0x19) shares the fixed 32-byte result ACK handler with
ReadIndexTable. Only a complete valid ACK_OK changes page->data;
the page index is never modified. Readback is a separate job.
UserGPIO (0x1E) follows the reserved command 30 in the Chinese AS60x
manual. Only selectors 0/1 and echoed states 0/1 are supported. Verify
firmware and physical pin mapping before use; an echo is not a pin
voltage measurement. No automatic probing or retry.

## Project structure

```text
AS608_lib/
├── src/
│   ├── as608.c
│   ├── as608.h
│   ├── as608_types.h
│   ├── as608_port.h
│   ├── as608_packet.c
│   ├── as608_packet.h
│   ├── as608_transaction.c
│   └── as608_transaction.h
├── tests/
│   ├── unit/          # API-specific, codec, transport and header contracts
│   ├── integration/   # commands, async and async fault regressions
│   ├── common/        # Mock UART and independent wire fixtures
│   └── CMakeLists.txt
├── docs/
├── .github/workflows/c11-host-tests.yml
├── CMakeLists.txt
├── README.md
└── FEATURES.md
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
on success. A standalone DownChar completes on physical TX drain after
its readiness ACK, not on final sensor confirmation. The manual's pp. 16–17
specify readiness; p. 31 shows another ACK after END_DATA. Verify the target
sends no trailing ACK before further commands to avoid desynchronization.
The combined write is active and waits for StoreChar ACK after physical TX
drain. It requires verified no-final-ACK firmware: otherwise a trailing
DownChar ACK could be mistaken for the StoreChar reply. No readback or retry.
Template interoperability across firmware variants is not guaranteed.

## Build and test

Requires CMake 3.20 or later and an ISO C11 compiler. Native host tests
also need a C++11 compiler (GCC/G++ or Clang/Clang++).

```sh
cmake -S . -B build/host -DAS608_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
ctest --test-dir build/host -L unit
ctest --test-dir build/host -L integration
```

The default native standalone configuration enables tests. Header-only
contracts compile in C11 and C++11 during the build; public C/C++ linkage
and all existing runtime tests are registered with CTest. Assertions
remain enabled in host tests even for Release builds.

### CI validation and sanitizers

The host workflow runs GCC and Clang Debug/Release builds, an additional
Clang ASan+UBSan build, and a separate GCC static-analyzer C-only build.
All executable tests are checked against the source inventory before
running; compile-only header checks are built alongside them. CTest runs
with two parallel workers and publishes JUnit and failure logs. The two
native Release jobs also verify a standalone C-only static library.
No sanitizer instrumentation is enabled for normal embedded builds.

```sh
cmake -S . -B build/sanitized -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DAS608_BUILD_TESTS=ON -DAS608_ENABLE_SANITIZERS=ON
cmake --build build/sanitized --parallel 2
ctest --test-dir build/sanitized --parallel 2 --output-on-failure --no-tests=error
```

For a local GCC analyzer pass:

```sh
cmake -S . -B build/analyzer -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug -DAS608_BUILD_TESTS=OFF '-DCMAKE_C_FLAGS=-Wall -Wextra -Werror -pedantic -fanalyzer'
cmake --build build/analyzer --parallel 2
```

Sanitizers and the analyzer detect classes of defects, not all possible
bugs. They do not establish hardware, timing, stack or firmware correctness.


### Library-only build

```sh
cmake -S . -B build/core -DAS608_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
```

For cross compilation, provide the application's CMake toolchain file
using `-DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake`. Tests default
to OFF in cross builds and when consumed as a subdirectory. No target
startup code, linker script or MCU compiler flags are provided here.

### Integration into an application

```cmake
add_subdirectory(path/to/AS608_lib)
target_link_libraries(my_firmware PRIVATE as608::as608)
```

For Keil or STM32CubeIDE without CMake, compile only
`src/as608.c`, `src/as608_transaction.c`, and `src/as608_packet.c`
and add `src/` to the compiler include path. Application includes remain
`#include "as608.h"`. Do not compile host test sources into firmware.
Supply the platform port; STM32 HAL IT/DMA integration is not yet provided.

## Full command API design

See [FEATURES.md](FEATURES.md) for the 31 documented AS60x commands and
their implementation status. All 46 APIs and related types are now exposed
by the normal header; AS608_DECLARE_PLANNED_API is no longer needed and
has no effect. BurnCode is an experimental opt-in profile, not validated
target firmware support. Existing public function signatures are unchanged.
The context has grown during the rebuild (stream callbacks, descriptor
staging, receive address, enrollment timing and free-slot capacity).
Rebuild all code that allocates or embeds as608_t.

Image upload uses a caller-owned sink descriptor with an exact packed byte
count, copied at admission. Each verified fragment is delivered to a
non-blocking callback; no image-sized driver allocation is required.
Image download uses a copied source descriptor, polls under the original
job deadline, and accepts negotiated 64/128/256-byte fragments. Completion
is physical TX drain after readiness ACK, not verified final acceptance.
BurnCode is an experimental, explicitly authorized profile only: readiness
00, each nonfinal DATA F0, final END_DATA 00. The generic manuals do not
establish this complete sequence or image format. Verify the exact vendor
procedure first. A terminal ACK preserves its raw result but blocks all
new jobs until external recovery; transport faults quarantine the link.
No firmware image, hardware validation, automatic retry or flash verification
is supplied. Unknown firmware extensions (e.g.
sleep or LED commands), platform adapters and multi-device backup/migration
orchestration are tracked separately; no unverified wire commands or
hardware-specific APIs are promised by the core.

## Migration

This is an intentional breaking API change. Replace old synchronous calls and
`as608_async_*` names with `as608_*_start()`, `as608_process()`,
`as608_get_result()`; use the single `as608_t` and `as608_config_t`.
Historical source and hardware baseline are available in Git history.
