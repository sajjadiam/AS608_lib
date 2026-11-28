# AS608 UART driver

[![C11 host tests](https://github.com/sajjadiam/AS608_lib/actions/workflows/c11-host-tests.yml/badge.svg)](https://github.com/sajjadiam/AS608_lib/actions/workflows/c11-host-tests.yml)

Portable ISO C11 cooperative, non-blocking driver for AS60x fingerprint
modules, with C++11-compatible public declarations.

- 46 public APIs: 5 lifecycle functions and 41 command/job start functions.
- 31 wire-opcode implementations within the documented profiles, including
  one explicitly experimental firmware-maintenance command.
- One caller-owned context per sensor; no heap, HAL or RTOS dependency.
- Partial UART I/O, a whole-job deadline, explicit fault quarantine and
  external recovery.
- Opaque 512/768-byte templates, separate 256-byte feature files, and
  exact-length callback-based image streams.

**Validation status:** host tests, GCC/Clang Debug and Release, Clang
ASan/UBSan and GCC static analysis have passed. The current cooperative
core has not been validated on the target hardware. Historical evidence
from the former driver: two AS608_QCCF modules on STM32F103C8T6, USART1/2,
57,600 baud, 8N2, successfully transferred a 768-byte template and
recognized the same finger. This is not evidence of universal firmware
compatibility or current-port correctness.

See [FEATURES.md](FEATURES.md) for the complete API inventory, command
profiles and target-validation gaps. The API contract is [src/as608.h](src/as608.h).

## Build

Requirements: ISO C11 compiler; CMake 3.20+ for the CMake build.
Host tests also require a C++11 compiler (GCC/G++ or Clang/Clang++).

```sh
cmake -S . -B build/host -DAS608_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 2
ctest --test-dir build/host --parallel 2 --output-on-failure
```

For the C-only static library:

```sh
cmake -S . -B build/core -DAS608_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel 2
```

A standalone native configuration enables host tests by default.
Cross builds and subdirectory consumers default to tests disabled.
For the reference Cortex-M3 cross-build audit, use the supplied
`cmake/toolchains/arm-cortex-m3-gcc.cmake` with ARM GNU Toolchain and
target C library (newlib). Application firmware should use its own
toolchain configuration, startup code and linker script.

```sh
cmake -S . -B build/arm \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-cortex-m3-gcc.cmake \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DAS608_BUILD_TESTS=OFF
cmake --build build/arm --parallel 2
bash tools/check_arm_build.sh build/arm
```

The audit ELF is for link validation only; it is not bootable firmware.
See [target build verification](docs/TESTING.md#cortex-m3-cross-build).

### Add to an application

```cmake
add_subdirectory(path/to/AS608_lib)
target_link_libraries(my_firmware PRIVATE as608::as608)
```

For STM32CubeIDE, Keil or another non-CMake build, compile
`src/as608.c`, `src/as608_transaction.c`, and `src/as608_packet.c`.
Add `src/` to the include path, select C11 or later, and include
`as608.h` from the application. Do not compile the host test files
into firmware. A working platform adapter must be supplied by the
application; no STM32 HAL IT/DMA adapter is included yet.

## Quick start

### 1. Provide a port

Supply all four callbacks in `as608_config_t.port`. The optional user
pointer refers to application-owned resources that must remain valid.

| Callback | Contract |
| --- | --- |
| `try_write` | Accept an ordered prefix into port-owned storage or hardware. Return accepted count, zero for backpressure, negative on I/O failure. |
| `try_read` | Consume an ordered RX prefix. Return count, zero if empty, negative on failure. |
| `tx_idle` | Exactly 1 after all queued data and the last UART stop bit have been transmitted, 0 while pending, negative on fault. |
| `clock_ms` | Coherent monotonic millisecond tick modulo 2^32. |

All callbacks are bounded, non-blocking and non-reentrant. Do not retain
borrowed buffers for asynchronous DMA/IT. Keep RX active during TX,
report lost bytes and UART/DMA errors, and latch link faults until
recovery. On STM32F1, TXE or DMA completion alone does not establish
physical TX completion; check USART TC together with pending queues
and transfers. See [as608_port.h](src/as608_port.h).

### 2. Initialize, admit, poll and interpret

The following application-side fragment uses an already configured port
and an authenticated sensor where authentication is required. Call the
initialization function once on a fresh physical link. The application's
scheduler calls the poll function while performing other work.
Implement the two application notification functions for your use case.

```c
#include "as608.h"

static as608_t sensor;
static uint8_t model[AS608_TEMPLATE_MAX_BYTES];
static as608_template_buffer_t output = {model, sizeof(model), 0U};
static bool active;

/* Application-owned notifications; consume borrowed arguments immediately. */
void app_template_ready(const uint8_t *data, size_t length);
void app_job_finished(as608_progress_t progress, const as608_result_t *result);

as608_status_t app_init(const as608_config_t *config) {
    return as608_init(&sensor, config);
}

as608_status_t app_read_template(uint16_t page_id) {
    const as608_status_t status =
        as608_read_template_start(&sensor, page_id, &output);

    if (status == AS608_STATUS_OK) {
        active = true;
    }
    return status;
}

void app_poll(void) {
    if (!active) {
        return;
    }

    const as608_progress_t progress = as608_process(&sensor);
    if (progress == AS608_PENDING) {
        return;
    }
    active = false;

    as608_result_t result;
    const bool available = as608_get_result(&sensor, &result);
    if (available &&
        result.status == AS608_STATUS_OK &&
        result.confirmation == AS608_ACK_OK) {
        app_template_ready(output.data, output.length);
    }
    app_job_finished(progress, available ? &result : NULL);
}
```

The example is an application fragment, not a complete UART adapter.
Serialize calls, including notification-driven application actions.
Check the returned admission status; a rejected start does not complete
or replace an active job. Verify the device address and baud settings
and use `as608_verify_password_start()` if required before template access.

### 3. Understand completion

| Signal | Meaning |
| --- | --- |
| `start() == AS608_STATUS_OK` | Admitted locally; sensor result not yet available. |
| `AS608_PENDING` | Job active; continue scheduling. |
| `AS608_COMPLETE` | Terminal result, possibly a negative sensor ACK. |
| `AS608_FAILED` | No active job or host fault; check result availability. |
| `as608_get_result() == true` | A result was copied; not a success flag. |

Check host status first, then the raw sensor confirmation. The
confirmation is valid only when host status is OK. For ordinary
commands, both fields must indicate success before using outputs.
For drain-only downloads, even both OK fields mean readiness ACK
plus physical transmission completion, not verified sensor acceptance.

Keep borrowed input immutable and output storage valid through termination
or successful abort. A failed transfer may leave partial bytes or stream
sink side effects; committed lengths and scalar outputs follow their
individual API contracts. Do not read or modify private context members
to schedule jobs. Use one context per physical byte stream and serialize
all its calls. The context layout is not a stable binary ABI.

## Faults and recovery

A valid ordinary negative sensor ACK is not a host communication fault.
Timeout, malformed replies, I/O failure and logical abort quarantine
the link. Address provisioning and experimental BurnCode have stricter
terminal rules.

For a quarantined device:

1. Stop/cancel outstanding TX, including DMA/IT.
2. Perform target-appropriate physical recovery and wait for readiness
   without blocking unrelated application work.
3. Purge stale RX/TX and clear latched errors. Establish a fresh link
   with compatible address and UART settings.
4. Call `as608_confirm_reset()`, then authenticate again if needed.

`as608_abort()` does not cancel bytes already accepted by the port.
`as608_confirm_reset()` does not reset hardware or verify communication;
it trusts the application and preserves the configured port, address
and timeout. If an uncertain address change altered the actual address,
verify it externally and initialize a fresh zeroed context after recovery.

Never automatically retry an uncertain flash write, enrollment, erase,
configuration change or firmware transfer. Sensor-side effects may
already have occurred.

## Important firmware constraints

- **DownChar/DownImage:** the implemented profile expects no final ACK
  after END_DATA. A trailing ACK can desynchronize subsequent jobs,
  including being mistaken for a StoreChar ACK in composite writes.
  Verify this profile on target before enabling these operations.
- **Templates and features:** 512/768-byte templates and 256-byte ordinary
  feature files are separate opaque formats; byte count alone does not
  establish cross-firmware compatibility.
- **Image streaming:** the application supplies the verified packed byte
  count and pixel format. The driver does not decode images or allocate
  an image-sized buffer.
- **Free-slot discovery:** target bitmap bit order is inferred, not
  verified; a returned free PageID is not reserved.
- **BurnCode:** experimental and destructive; do not use without a
  verified vendor procedure, compatible image and recovery plan.
- Passwords and fingerprint data travel in plaintext and may remain in
  driver/port memory. The additive checksum is not authentication or
  encryption. Do not log raw frames or context memory. Secure erasure,
  access control and biometric-data handling are application duties.

See [FEATURES.md](FEATURES.md) for all command-specific restrictions.

## Architecture and layout

```text
Application
    -> as608 (command jobs and lifecycle)
        -> as608_transaction (partial I/O, deadline and wire state)
            -> as608_packet (codec and checksum)
            -> as608_port (platform callback contract)
```

```text
src/             All 3 C sources and 5 headers
tests/unit/      Dedicated API, packet, transaction and header tests
tests/integration/  Multi-command, asynchronous and fault regressions
tests/common/    Shared test-only mock UART and wire helpers
docs/adr/        Architecture decisions
CMakeLists.txt   Static library and host build
```

See [ADR-0001](docs/adr/0001-as608-driver-architecture.md) for design rationale.
Engineering documentation:

- [Architecture and state machines](docs/ARCHITECTURE.md)
- [Wire protocol and profiles](docs/PROTOCOL.md)
- [Platform porting and UART design](docs/PORTING.md)
- [Error handling and recovery](docs/ERROR_HANDLING.md)
- [Testing and target validation](docs/TESTING.md)

## Validation

CI runs 53 executable tests in each of five configurations:
GCC Debug/Release, Clang Debug/Release and Clang ASan+UBSan. Nine
independent compile-only header checks run in each configuration,
including freestanding C11 and C++11 contracts. A separate GCC analyzer
job checks the three production translation units. Assertions stay
enabled in Release tests. CI syntax-checks the actual README C fragment.
Standalone C-only archive builds are verified
with both compilers. Reports are retained as workflow artifacts.

```sh
ctest --test-dir build/host -L unit --output-on-failure
ctest --test-dir build/host -L integration --output-on-failure
python3 tests/check_inventory.py build/host
```

To reproduce the sanitizer and analyzer builds:

```sh
cmake -S . -B build/sanitized -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
    -DAS608_BUILD_TESTS=ON -DAS608_ENABLE_SANITIZERS=ON
cmake --build build/sanitized --parallel 2
ctest --test-dir build/sanitized --parallel 2 --output-on-failure

cmake -S . -B build/analyzer -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_BUILD_TYPE=Debug -DAS608_BUILD_TESTS=OFF \
    '-DCMAKE_C_FLAGS=-Wall -Wextra -Werror -pedantic -fanalyzer'
cmake --build build/analyzer --parallel 2
```

These checks are not proof of absence of UB or target timing faults.
The reference ARM build and memory baseline have passed: 7241 bytes
of allocated Flash sections, no writable static core sections and 712
bytes per context (GCC 13.2.1, Cortex-M3, -Os). See the target audit
for scope and evidence. Final firmware memory fit, stack, platform
adapters and current-core hardware tests remain outstanding. See [validation checklist](FEATURES.md#remaining-validation).

## Migration from the former driver

The unified cooperative API replaces the old synchronous and
`as608_async_*` interfaces. Use `as608_*_start()`, `as608_process()`
and `as608_get_result()` with the current `as608_t` and
`as608_config_t`. The obsolete `AS608_DECLARE_PLANNED_API` macro is
not needed. Rebuild all consumers of the context. Source files now
live in `src/`; development history is retained in Git.
