# AS608 UART driver

Portable ISO C11 cooperative **non-blocking** driver for AS60x fingerprint
modules. No heap, HAL, RTOS, or UART wait loops in the core. One context per
physical sensor. Historical hardware validation: two AS608_QCCF modules on
STM32F103C8T6, USART1/USART2, 57,600 baud, 8N2; successful 768-byte template
transfer. The unified non-blocking port still needs hardware validation.

## Completed API rebuild — 46 APIs

Post-rebuild maintenance: stage 48, common-type contract review.

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
The packet layer is unchanged. API 39 adds an exact per-receive address
to the transaction layer without weakening normal address validation.

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
├── test_as608_search.c           Active API 15 search tests
├── test_as608_load_template.c    Active API 16 template load tests
├── test_as608_upload_template.c  Active API 17 template upload tests
├── test_as608_download_template.c Active API 18 template download tests
├── test_as608_store_template.c   Active API 19 flash store tests
├── test_as608_read_template.c    Active API 20 composite read tests
├── test_as608_write_template.c   Active API 21 composite write tests
├── test_as608_delete_templates.c Active API 22 range deletion tests
├── test_as608_empty_database.c  Active API 23 database clear tests
├── test_as608_enroll.c          Active API 24 automatic enrollment tests
├── test_as608_identify.c        Active API 25 automatic identification tests
├── test_as608_high_speed_search.c Active API 26 high-speed search tests
├── test_as608_upload_feature.c  Active API 27 feature upload tests
├── test_as608_download_feature.c Active API 28 feature download tests
├── test_as608_upload_image.c    Active API 29 image upload tests
├── test_as608_download_image.c  Active API 30 image download tests
├── test_as608_generate_binary_image.c Active API 31 image processing tests
├── test_as608_set_baud_rate.c    Active API 32 baud register tests
├── test_as608_set_security_level.c Active API 33 security register tests
├── test_as608_set_packet_size.c  Active API 34 packet-size register tests
├── test_as608_control_other_port.c Active API 35 port-control tests
├── test_as608_read_device_info.c Active API 36 descriptor stream tests
├── test_as608_set_password.c    Active API 37 password-change tests
├── test_as608_get_random_code.c Active API 38 random-code tests
├── test_as608_set_device_address.c Active API 39 provisioning tests
├── test_as608_write_notepad.c   Active API 40 notepad write tests
├── test_as608_read_notepad.c    Active API 41 notepad read tests
├── test_as608_write_user_gpio.c Active API 42 GPIO echo tests
├── test_as608_burn_code.c       Active API 43 experimental burn tests
├── test_as608_enroll_two_scan.c Active API 44 enrollment FSM tests
├── test_as608_identify_range.c  Active API 45 identification FSM tests
├── test_as608_find_free_slot.c  Active API 46 free-slot bitmap tests
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
on success. A standalone DownChar completes on physical TX drain after
its readiness ACK, not on final sensor confirmation. The manual's pp. 16–17
specify readiness; p. 31 shows another ACK after END_DATA. Verify the target
sends no trailing ACK before further commands to avoid desynchronization.
The combined write is active and waits for StoreChar ACK after physical TX
drain. It requires verified no-final-ACK firmware: otherwise a trailing
DownChar ACK could be mistaken for the StoreChar reply. No readback or retry.
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
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_search.c -o build/test_search
./build/test_search
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_load_template.c -o build/test_load_template
./build/test_load_template
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_upload_template.c -o build/test_upload_template
./build/test_upload_template
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_download_template.c -o build/test_download_template
./build/test_download_template
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_store_template.c -o build/test_store_template
./build/test_store_template
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_read_template.c -o build/test_read_template
./build/test_read_template
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_write_template.c -o build/test_write_template
./build/test_write_template
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_delete_templates.c -o build/test_delete_templates
./build/test_delete_templates
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_empty_database.c -o build/test_empty_database
./build/test_empty_database
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_commands.c -o build/test_commands
./build/test_commands
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_async.c -o build/test_async
./build/test_async
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. as608_packet.c as608_transaction.c as608.c test_as608_async_faults.c -o build/test_async_faults
./build/test_async_faults
```

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
