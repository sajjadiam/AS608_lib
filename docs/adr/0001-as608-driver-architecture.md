# ADR-0001: AS608 embedded C driver architecture

- **Status:** Proposed — baseline design awaiting public-header review
- **Date:** 2026-11-11
- **Confirmed hardware target:** Synochip AS608_QCCF (QFN32), based on user's direct observation of the IC marking and pin count on a previously purchased module. This is hardware identification, not firmware identification.
- **Protocol family:** AS60x UART packet protocol (module-firmware compatibility to be validated).

## Context and verified protocol facts

The ATK-AS608 manual documents UART communication (8N1, default 57600 baud on that board), a default four-byte device address of 0xFFFFFFFF, two feature buffers, and packet categories command (0x01), data (0x02), acknowledgement (0x07), and end-data (0x08). Packets begin with 0xEF01. The two-byte packet length covers payload plus the two checksum bytes; the 16-bit checksum is the sum of packet identifier, length bytes, and payload bytes. All multibyte wire fields are transmitted most-significant byte first. Acknowledgement payload begins with a one-byte confirmation code. These are protocol facts, not assumptions about every marketed AS608 board.

References:
- ATK-AS608 module user manual V1.00 (2016-05-12), sections 2.2–2.4: https://it.scribd.com/document/773294313/AS608-Fingerprint-Sensor-User-Manual
- AS60x fingerprint SOC user manual (source-hosted PDF): https://github.com/siyouluo/FingerprintLock/blob/V2.0/datasheet/AS608datasheet/AS60x%E6%8C%87%E7%BA%B9%E8%AF%86%E5%88%ABSOC%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8CV10.pdf
- Reference implementation for command coverage (not normative): https://github.com/adafruit/Adafruit-Fingerprint-Sensor-Library

## Hardware baseline and compatibility limits

- 2026-11-11: User inspected a physical AS608 module and confirmed the IC marking `AS608_QCCF` with 32 pins. Treat QCCF as the verified chip variant of the reference sample; do not infer the chip variant of every third-party AS608 board.
- Synochip's AS608 Processor Datasheet v1.00 (2015-09), Table 2-1: `AS608_QCCF` is QFN32; `AS608_QYCF` is QFN60. Both list 512 KB flash and 128 KB SRAM, but their peripheral availability differs.
- The chip package does **not** identify the connected fingerprint image sensor, installed firmware, or fingerprint-template algorithm/version. `PS_ReadSysPara` reports a `SensorType` code in the AS60x communication manual; it is **not** an AS608_QCCF/QYCF package identifier. Product/software descriptors may be available via `PS_ReadINFpage` on supported firmware.
- The AS60x manual describes 256-byte feature files and 512-byte templates, and defines `PS_LoadChar`, `PS_UpChar`, `PS_DownChar` and `PS_StoreChar`. This specifies a transfer mechanism, **not** proof of cross-firmware or cross-sensor template compatibility. Do not hardcode cross-device sync as guaranteed. Opaque template transfer and matching tests on both physical modules must establish interoperability before enabling sync.
- Image transfer is not a default replacement for an incompatible template: the manual specifies 4-bit-per-pixel image transfer over UART, and it provides no guarantee of cross-firmware/sensor reconstruction or recognition.
- Correct the earlier assumption that all AS608-like modules have identical UART framing: the older AS60x manual states default 57600 baud, 8 data bits, **2 stop bits**, no parity; module-specific electrical and UART settings must be independently confirmed. This driver's transport interface must not enforce 8N1.

## Decisions

1. **C99, portable core.** The library shall not include vendor HAL, RTOS, or MCU register headers. A thin application-provided port layer supplies byte transmit, receive and monotonic millisecond time.
2. **Caller-owned memory, no heap.** The application owns one device context per AS608 instance; the core never calls malloc/free. No global mutable device state. Streamed image/template transfers are deferred until their memory contract is reviewed.
3. **Blocking command API for v0.1.** Each command is a synchronous transaction with one total timeout budget measured using a wrap-safe monotonic clock. The I/O interface permits partial transfers, zero bytes on timeout, and negative values on transport failure. Calls may block, but should not busy-spin unnecessarily in platform ports. An asynchronous API may be added independently later; it is not claimed to exist in v0.1.
4. **Layer split.** Public API (include/as608.h), platform-independent transport contract (include/as608_port.h), command implementation (src/as608.c), private packet contract (src/as608_packet.h), parser/serializer (src/as608_packet.c), and optional high-level workflow state machines (src/as608_workflow.c). Five files suffice for the first low-level implementation; workflow source is added only with genuine multi-step behavior.
5. **Three distinct error domains.** Input misuse, transport/timeouts and malformed frames are host-side errors; device confirmation codes are preserved losslessly and must not be mistaken for UART errors. An ACK with NO_FINGER or NOT_FOUND is a valid transaction carrying a nonzero device confirmation code.
6. **Strict parsing.** Validate start code, expected address, packet identifier, length bounds and 16-bit checksum before exposing returned data. Use explicit big-endian encoding/decoding, never wire-struct casting or packed layout assumptions. Unexpected/truncated responses fail cleanly. Receive synchronization/recovery policy must be unit-tested.
7. **Timing and concurrency.** No implicit delays, RTOS calls or IRQ disabling in the core. The application serializes transactions on a single sensor/UART; the public synchronous functions are not thread-safe on the same device context. Timeouts shall apply to an entire transaction rather than reset for every received byte.
8. **Feature scope in v0.1.** Verify password (explicit, not silently assumed), read system parameters, image capture, image-to-character conversion for CharBuffer1/2, combine model, search by library range, store, delete, empty database, and read template count. Model/image upload, LED variants, persistent setting changes and automatic enrollment are deferred until their board/version behavior is verified.
9. **Runtime capability discovery.** Do not hardcode advertised capacity (300 on the ATK-AS608 reference board), baud rate, packet size, or fingerprint library bounds for every board. Read parameters when available. The caller configures the UART prior to init; init itself does not probe hardware.
10. **Security and destructive operations.** Sensor password is an access-control parameter, not cryptographic protection. Avoid retaining it in device context; pass it only to explicit verification. The library shall not automatically erase templates, overwrite IDs, change password/address, or reconfigure persistent device settings.
11. **Validation.** Host-side deterministic tests for serializer vectors, checksums, endian conversion, split reads/writes, malformed/oversized lengths, lost bytes, slow partial transfers, timeout (including 32-bit tick wrap), unexpected device address, confirmation-code propagation, and search-range arithmetic. Hardware integration tests are separate.

## Public API implications

- A caller-owned as608_t and a compact as608_config_t with a transport callback table, 32-bit device address and transaction timeout.
- Functions return an as608_result_t containing host status and raw device confirmation code. Outputs are only valid on success.
- The initial synchronous contract does not guarantee reentrancy or ISR-safe operation.
- Buffer identifiers are explicitly limited to CharBuffer1 and CharBuffer2; template positions and ranges are 16-bit and validated with widened arithmetic.
- The public header is **not committed by this ADR**; its proposed signatures will be reviewed separately before implementation.

## Alternatives and tradeoffs

- **Single source/header:** slightly lower initial effort but couples framing, I/O and domain commands and makes tests harder.
- **HAL-specific library:** faster for one MCU but fails portability objective.
- **Heap-allocated opaque handle:** hides implementation size but introduces allocation/lifetime decisions unsuited to smaller MCUs.
- **Fully asynchronous from day one:** useful for high concurrency, but adds state, cancellation and race complexity before the basic packet contract has tests.

## Open questions before accepting this ADR

- Confirm the actual *module* revision, IO voltage, power requirements and UART line settings from its own documentation or measurement; the reference IC is identified as AS608_QCCF.
- Agree on callback return-value conventions, total timeout API and whether all public structs remain caller-owned.
- Define behavior after incomplete response (port flush/resync strategy) and initial maximum ACK size.
- Agree on the first milestone: the small blocking command set above, not image/template streaming. Template sync is a possible next milestone contingent on an interoperability test and an explicit streaming API design.
