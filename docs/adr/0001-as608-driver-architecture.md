# ADR-0001: Unified cooperative AS608 driver

Status: Accepted architecture; host CI and target validation tracked separately.

## Decision

One C11 non-blocking engine: command sequencing in as608.c, generic packet
transport and deadline in as608_transaction.c, one public
header as608.h, one UART callback contract in as608_port.h, one independent
packet codec. Transport exposes packet-level events and has no command or
ACK interpretation. Command layer owns job lifecycle and quarantine on faults.
No production waiting loops or parallel synchronous API. The
former implementation remains available only in Git history. Tests drive the
same production engine and may use bounded test-only polling helpers.

## Layer ownership and dependencies

as608_types.h is the common dependency for host status and transport
constants. Packet and port headers do not include the command header.
as608_transaction_t owns configuration, deadline, packet storage and wire
state; as608_t embeds it by value and owns command-specific state.
All internal transport calls operate on as608_transaction_t. The config
is stored once; no heap, global context or back-pointer is introduced.
The codec and transaction tests link independently of the command layer.
Storage is visible in headers for static allocation, not for application
mutation. The public command function signatures remain unchanged.

## Protocol and safety

Use existing AS60x command/ACK formats, big-endian wire fields, 0xEF01 start
and additive 16-bit checksum. A job has one wrap-safe deadline. UART I/O is
partial and non-blocking; zero progress is not a fault. The port copies queued
TX bytes and reports loss/errors. TX drain includes physical UART completion.

On malformed or late replies, timeout, I/O failure or abort, quarantine.
Physical reset, boot wait, TX cancellation, full RX/TX purge and error
cleanup are platform responsibilities; only after those guarantees may
as608_confirm_reset release quarantine. Flash operations may have uncertain
outcomes after timeout. No implicit retry or recovery.

Supported opaque template sizes are 512 and 768 bytes. Hardware evidence:
two AS608_QCCF devices on STM32F103C8T6 transferred a 768-byte template
(six 128-byte data packets) and both recognized the same finger using the
historical driver. This does not establish cross-firmware compatibility
or validate the new hardware port.

Reference: AS60x fingerprint SOC user manual V1.0, packet and command
format sections. Reference hardware configuration: 57600 baud, 8N2 on the
tested modules. See prior Git revisions for the original detailed ADR.

## Remaining validation

STM32 UART IT/DMA adapter, physical recovery, complete target enrollment
and identification, and independent two-UART 768-byte transfer.
