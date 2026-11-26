# AS608 Feature Checklist

Baseline: [Core e9218ec](https://github.com/sajjadiam/AS608_lib/commit/e9218ecd362b254b4e8d1de9e199825fbf42a173) — 2026-11-25.

This checklist tracks the host library against the Synochip AS60x Communication Manual V1.0 (April 2010), checked against the current [public API](as608.h) and [implementation](as608.c).

- Checked: implemented in the non-blocking core, within the stated scope.
- Unchecked: missing, incomplete, or awaiting the indicated validation.
- Command coverage: **31/31 opcode implementations** (30 general command implementations + one explicitly authorized experimental BurnCode profile). Target burn procedure is unverified. UpChar and DownChar are implemented with limited file-size support.
- Implementation is not equivalent to hardware validation or universal firmware compatibility.
- API design: all 31 documented opcodes have contracts in [as608.h](as608.h). All wire opcodes now have host implementations; BurnCode remains experimental and requires target verification. All 46 planned APIs are implemented and exposed without a feature macro; target validation remains separate.

## Active rebuild status (current source)

- [x] API 01: `as608_init()` — active implementation and dedicated tests.
- [x] API 02: `as608_process()` — active bounded simple-ACK path and dedicated tests; additional command phases restored with their start APIs.
- [x] API 03: `as608_get_result()` — terminal result snapshot and dedicated tests.
- [x] API 04: `as608_abort()` — logical abort and quarantine, dedicated tests.
- [x] API 05: `as608_confirm_reset()` — external recovery acknowledgment and dedicated tests.
- [x] API 06: `as608_verify_password_start()` — PS_VfyPwd (0x13), active implementation and wire/lifecycle tests.
- [x] API 07: `as608_read_parameters_start()` — PS_ReadSysPara (0x0F), decoded 16-byte parameter data and dedicated tests.
- [x] API 08: `as608_get_template_count_start()` — PS_ValidTempleteNum (0x1D), decoded 16-bit valid-template count and dedicated tests.
- [x] API 09: `as608_read_index_table_start()` — PS_ReadIndexTable (0x1F), raw 32-byte bitmap and dedicated tests; firmware support unverified.
- [x] API 10: `as608_read_information_page_start()` — PS_ReadINFpage (0x16), exact 512-byte stream and dedicated tests.
- [x] API 11: `as608_capture_image_start()` — PS_GetImage (0x01), sensor-side image acquisition and dedicated tests.
- [x] API 12: `as608_generate_character_start()` — PS_GenChar (0x02), validated destination buffer and dedicated tests.
- [x] API 13: `as608_match_buffers_start()` — PS_Match (0x03), 16-bit score on successful 1:1 comparison and dedicated tests.
- [x] API 14: `as608_create_model_start()` — PS_RegModel (0x05), model generation in both character buffers and dedicated tests.
- [x] API 15: `as608_search_start()` — PS_Search (0x04), validated search range, PageID and score, dedicated tests.
- [x] API 16: `as608_load_template_start()` — PS_LoadChar (0x07), validated destination buffer and page-addressed command tests.
- [x] API 17: `as608_upload_template_start()` — PS_UpChar (0x08), 512/768-byte template streaming and dedicated tests.
- [x] API 18: `as608_download_template_start()` — PS_DownChar (0x09), negotiated chunk size and physical TX drain; final ACK behavior requires firmware validation.
- [x] API 19: `as608_store_template_start()` — PS_StoreChar (0x06), page-addressed flash storage and dedicated tests.
- [x] API 20: `as608_read_template_start()` — composite LoadChar + UpChar, one deadline and dedicated tests.
- [x] API 21: `as608_write_template_start()` — composite ReadSysPara + DownChar + StoreChar, shared deadline and drain gating; requires verified no-final-ACK firmware.
- [x] API 22: `as608_delete_templates_start()` — PS_DeletChar (0x0C), validated nonempty range and dedicated tests.
- [x] API 23: `as608_empty_database_start()` — PS_Empty (0x0D), complete database deletion and dedicated tests.
- [x] API 24: `as608_enroll_start()` — PS_Enroll (0x10), sensor-managed enrollment and returned PageID; dedicated host tests, hardware support unverified.
- [x] API 25: `as608_identify_start()` — PS_Identify (0x11), automatic acquisition and database match; dedicated host tests, hardware support unverified.
- [x] API 26: `as608_high_speed_search_start()` — PS_HighSpeedSearch (0x1B), shared range search and match ACK handling; dedicated host tests, hardware support unverified.
- [x] API 27: `as608_upload_feature_start()` — PS_UpChar (0x08), exact 256-byte feature stream; dedicated host tests, hardware support unverified.
- [x] API 28: `as608_download_feature_start()` — PS_DownChar (0x09), exact 256-byte feature transmission and TX drain; dedicated host tests, hardware support unverified.
- [x] API 29: `as608_upload_image_start()` — PS_UpImage (0x0A), exact-length callback stream without image-sized storage; dedicated host tests, hardware support unverified.
- [x] API 30: `as608_download_image_start()` — PS_DownImage (0x0B), callback source with bounded polling, negotiated 64/128/256-byte fragments and TX drain; hardware support unverified.
- [x] API 31: `as608_generate_binary_image_start()` — PS_GenBinImage (0x1C), validated modes 0–2 and simple ACK; dedicated host tests, hardware support unverified.
- [x] API 32: `as608_set_baud_rate_start()` — PS_WriteReg (0x0E), register 4, AS608 multipliers 1–12; dedicated host tests, UART synchronization and hardware support unverified.
- [x] API 33: `as608_set_security_level_start()` — PS_WriteReg (0x0E), register 5, levels 1–5; dedicated host tests, hardware support unverified.
- [x] API 34: `as608_set_packet_size_start()` — PS_WriteReg (0x0E), register 6, size codes 0–3; dedicated host tests, hardware support unverified.
- [x] API 35: `as608_control_other_port_start()` — PS_Port_Control (0x17), boolean control of the other interface; dedicated host tests, hardware support unverified.
- [x] API 36: `as608_read_device_info_start()` — PS_ReadINFpage (0x16), selective 32-byte staging and atomic descriptor output; dedicated host tests, target layout unverified.
- [x] API 37: `as608_set_password_start()` — PS_SetPwd (0x12), shared big-endian password command encoding; dedicated host tests, persistence unverified.
- [x] API 38: `as608_get_random_code_start()` — PS_GetRandomCode (0x14), validated 32-bit result ACK; dedicated host tests, hardware support and entropy unverified.
- [x] API 39: `as608_set_device_address_start()` — PS_SetChipAddr (0x15), strict initial provisioning, exact new-address ACK and commit on success; dedicated host tests, hardware transition unverified.
- [x] API 40: `as608_write_notepad_start()` — PS_WriteNotepad (0x18), full-page snapshot and validated ACK; dedicated host tests, hardware capacity and persistence unverified.
- [x] API 41: `as608_read_notepad_start()` — PS_ReadNotepad (0x19), shared 32-byte ACK handling and atomic output; dedicated host tests, hardware capacity unverified.
- [x] API 42: `as608_write_user_gpio_start()` — PS_UserGPIOCommand (0x1E), Chinese manual reserved command, strict 0/1 selector and echoed state; dedicated host tests, physical mapping and support unverified.
- [x] API 43: `as608_burn_code_start()` — PS_BurnCode (0x1A), explicit experimental ACK-each profile, recovery gate and dedicated host tests; target validation pending.
- [x] API 44: `as608_enroll_two_scan_start()` — host-managed two-capture workflow, paced finger removal/placement, shared deadline, final StoreChar ACK; dedicated host tests.
- [x] API 45: `as608_identify_range_start()` — host GetImage + GenChar + Search, one deadline and one attempt, shared range validation; dedicated host tests.
- [x] API 46: `as608_find_free_slot_start()` — ReadSysPara + capacity-bounded ReadIndexTable scan, first free PageID, one deadline and dedicated host tests; target bitmap ordering unverified.
- [x] All 46 planned APIs are implemented (BurnCode experimental).

Active wire-command coverage: **31/31 including experimental BurnCode** (PS_VfyPwd 0x13, PS_ReadSysPara 0x0F, PS_ValidTempleteNum 0x1D, PS_ReadIndexTable 0x1F, PS_ReadINFpage 0x16, PS_GetImage 0x01, PS_GenChar 0x02, PS_Match 0x03, PS_RegModel 0x05, PS_Search 0x04, PS_LoadChar 0x07, PS_UpChar 0x08, PS_DownChar 0x09, PS_StoreChar 0x06, PS_DeletChar 0x0C, PS_Empty 0x0D, PS_Enroll 0x10, PS_Identify 0x11, PS_HighSpeedSearch 0x1B, PS_UpImage 0x0A, PS_DownImage 0x0B, PS_GenBinImage 0x1C, PS_WriteReg 0x0E, PS_Port_Control 0x17, PS_SetPwd 0x12, PS_GetRandomCode 0x14, PS_SetChipAddr 0x15, PS_WriteNotepad 0x18, PS_ReadNotepad 0x19, PS_UserGPIOCommand 0x1E, PS_BurnCode 0x1A).
All 23 baseline APIs are restored and APIs 24–46 are implemented (BurnCode is experimental). The independent, dedicated and full
command/async regression suites are enabled in CI. No wire opcode remains declaration-only; all three host composite APIs are implemented. Current firmware validation remains
outstanding; historical hardware results are not new validation.

## 1. Fingerprint acquisition and recognition

- [x] `0x01 PS_GetImage` — Capture fingerprint image into ImageBuffer; `as608_capture_image_start()`.
- [x] `0x02 PS_GenChar` — Extract features into CharBuffer1 or CharBuffer2; `as608_generate_character_start()`.
- [x] `0x03 PS_Match` — 1:1 comparison of character buffers and match score; `as608_match_buffers_start()`.
- [x] `0x04 PS_Search` — 1:N database range search, PageID and score; `as608_search_start()`.
- [x] `0x05 PS_RegModel` — Merge fingerprint features into a template; `as608_create_model_start()`.
- [x] `0x10 PS_Enroll` — Sensor-side automatic single-scan enrollment, free-slot selection and PageID; `as608_enroll_start()`. Firmware support unverified.
- [x] `0x11 PS_Identify` — Automatic acquisition and identification; may update incomplete templates on a sufficiently strong match; `as608_identify_start()`. Firmware support unverified.
- [x] `0x1B PS_HighSpeedSearch` — High-speed database range search; `as608_high_speed_search_start()`. Firmware support unverified.

## 2. Template and database management

- [x] `0x06 PS_StoreChar` — Store a character buffer at flash PageID; `as608_store_template_start()`.
- [x] `0x07 PS_LoadChar` — Load a stored template into a character buffer; `as608_load_template_start()`.
- [x] `0x08 PS_UpChar` — Sensor-to-host 512/768-byte template upload and separate 256-byte feature upload; `as608_upload_template_start()`, `as608_upload_feature_start()`.
- [x] `0x09 PS_DownChar` — Host-to-sensor 512/768-byte template download and separate 256-byte feature download; `as608_download_template_start()`, `as608_download_feature_start()`.
- [x] `0x0C PS_DeletChar` — Delete a consecutive range of templates; `as608_delete_templates_start()`.
- [x] `0x0D PS_Empty` — Erase the fingerprint database; `as608_empty_database_start()`.
- [x] `0x1D PS_ValidTempleteNum` — Count valid templates; `as608_get_template_count_start()`.
- [x] `0x1F PS_ReadIndexTable` — Read 32-byte occupancy bitmap per 256 slots, index pages 0–3; `as608_read_index_table_start()`. Manual marks this command reserved in the detailed description; verify on target firmware.

Additional template capabilities:

- [x] Both character buffer IDs.
- [x] Opaque 512/768-byte templates with actual received length.
- [x] Negotiated outbound packet size: 32/64/128/256 bytes.
- [x] Upload ordinary 256-byte feature files separately from templates: `as608_upload_feature_start()`; firmware support unverified.
- [x] Download ordinary 256-byte feature files separately from templates: `as608_download_feature_start()`; firmware support and final-ACK behavior unverified.
- [ ] Verify other file lengths per firmware; do not assume arbitrary lengths.
- [ ] Verify DownChar post-stream ACK behavior per firmware. Standalone download currently completes at physical TX drain; composite write also checks StoreChar ACK. The manual's workflow on printed p. 31 shows an additional ACK after data.

## 3. Image transfer and processing

- [x] `0x0A PS_UpImage` — Upload ImageBuffer to host through an exact-length stream sink; `as608_upload_image_start()`. Hardware support unverified.
- [x] `0x0B PS_DownImage` — Stream packed image into ImageBuffer; `as608_download_image_start()`. Hardware support and trailing ACK behavior unverified.
- [x] `0x1C PS_GenBinImage` — Generate binary, thinned or minutiae-marked thinned image; `as608_generate_binary_image_start()`. Hardware support unverified.
- [x] Stream image uploads without storing a complete image in MCU RAM.
- [x] Stream image downloads without storing a complete image in MCU RAM.
- [ ] Encode/decode packed UART image pixels (two 4-bit pixels per byte).
- [ ] Verify image dimensions and transfer lengths per sensor.
- [x] Support GenBinImage modes 0, 1 and 2 and separate image upload at the API level.
- [ ] Verify GenBinImage output format and subsequent upload on target hardware.
- [ ] Optional host-side PGM export and image/feature-quality diagnostics.

## 4. System configuration and information

- [x] `0x0E PS_WriteReg` — Baud multiplier (register 4), security level (register 5) and packet size (register 6); `as608_set_baud_rate_start()`, `as608_set_security_level_start()`, `as608_set_packet_size_start()`. Hardware readback and persistence unverified.
- [x] `0x0F PS_ReadSysPara` — Read 16-byte system parameters; `as608_read_parameters_start()`.
- [x] `0x16 PS_ReadINFpage` — Read 512-byte flash information page; `as608_read_information_page_start()`.
- [x] `0x17 PS_Port_Control` — Enable/disable the other communication interface (UART/USB); `as608_control_other_port_start()`. Hardware support and port state unverified.
- [x] Read status register, sensor type, capacity, security level, address, packet-size code and baud multiplier.
- [ ] Typed status flags: busy, pass, password verified, valid image.
- [x] Set baud rate (register 4), multiples of 9600 from 9600 to 115200; host UART reconfiguration remains caller-owned.
- [ ] Verify target rate support, ACK timing, flash persistence and host UART synchronization on hardware.
- [x] Set security level 1–5 (register 5); hardware readback, persistence and matching behavior unverified.
- [x] Set packet-size code 0–3 (register 6); download jobs still read the actual setting. DownImage retains its 64/128/256-byte restriction.
- [x] Parse product, firmware, manufacturer and sensor descriptors from the full validated page: `as608_read_device_info_start()`; 32-byte staging, raw wire order.
- [ ] Verify page origin, descriptor offsets and byte order on target; SensorName word address 0x21A is inferred from the four-word sequential layout.
- [ ] Read-back and persistence verification for configuration writes.

## 5. Password, address and random number

- [x] `0x12 PS_SetPwd` — Change 32-bit handshake password; `as608_set_password_start()`. Verify separately; hardware persistence unverified.
- [x] `0x13 PS_VfyPwd` — Verify 32-bit handshake password; `as608_verify_password_start()`.
- [x] `0x14 PS_GetRandomCode` — Read 32-bit random code; `as608_get_random_code_start()`. No entropy or cryptographic guarantee.
- [x] `0x15 PS_SetChipAddr` — Initial address provisioning; `as608_set_device_address_start()`. No arbitrary reassignment; hardware transition unverified.
- [x] Configurable host-side address and strict received-address validation.
- [x] Handle documented default-TX/new-RX transition with exact address checking; any uncertain reply quarantines.
- [ ] Verify address transition and flash persistence on target hardware.
- [x] Separate password-change and verification APIs; no automatic retry or credential logging by the core.
- [ ] Verify password change and persistence on hardware; secure application-owned credential handling.
- [ ] Characterize RNG guarantees before cryptographic use.

Handshake passwords and additive checksums are not encryption or authenticated transport.

## 6. User flash, GPIO and firmware maintenance

- [x] `0x18 PS_WriteNotepad` — Replace 32-byte user flash page; `as608_write_notepad_start()`. Hardware persistence unverified.
- [x] `0x19 PS_ReadNotepad` — Read 32-byte user flash page; `as608_read_notepad_start()`. Output committed only on a valid success ACK.
- [x] `0x1A PS_BurnCode` — Experimental explicit ACK-each profile only; `as608_burn_code_start()`. No claim of verified target programming.
- [x] `0x1E PS_UserGPIOCommand` — User GPIO output control per Chinese manual command 30; `as608_write_user_gpio_start()`. Reserved/variant-specific; hardware support unverified.
- [x] Notepad read/write APIs validate pages 0–15 and 32-byte payloads.
- [ ] Verify actual notepad capacity, readback and persistence on hardware.
- [ ] Verify GPIO mapping and availability on the physical module.
- [ ] Firmware-update support only with verified vendor procedure, compatible image and recovery plan.

## 7. Firmware-specific extensions (not part of the 31-command count)

The following require a matching firmware manual and target verification.
The AS60x V1.0 command list does not establish their opcodes or availability on our AS608_QCCF modules.

- [ ] Software reset command.
- [ ] Sleep/standby and wake-on-finger or touch-event integration.
- [ ] LED/illumination control, including Aura LED where available.
- [ ] Dedicated serial-number, firmware-version or product-information commands beyond ReadINFpage.
- [ ] Additional vendor-specific enrollment, recognition and diagnostic commands.

## 8. Composite host-library operations (not additional opcodes)

- [x] Read stored template: LoadChar + UpChar; `as608_read_template_start()`.
- [x] Write stored template: ReadSysPara + DownChar + StoreChar; `as608_write_template_start()`.
- [x] Two-scan enrollment FSM with paced finger placement/removal, GenChar, RegModel and StoreChar: `as608_enroll_two_scan_start()`; verify target behavior.
- [x] Capture + GenChar + Search identification FSM: `as608_identify_range_start()`; one attempt under the original job deadline.
- [x] Free-slot discovery using index table and database capacity: `as608_find_free_slot_start()`; capacity 1..1024, LSB-first, no reservation.
- [ ] Full database backup/restore with per-template outcomes.
- [ ] Cross-sensor migration workflow with compatibility checks and partial-failure reporting.
- [ ] Optional duplicate-finger detection before enrollment.

All three planned composite APIs are implemented; verify bitmap bit ordering, capacity and exclusive database ownership on target.

## 9. Core architecture and transport

- [x] Single ISO C11 cooperative non-blocking core.
- [x] Independent command, transaction, packet and port layers.
- [x] Independent transaction context and common low-level types.
- [x] Caller-owned static allocation; no heap.
- [x] Multiple independent sensor contexts.
- [x] Packet header, address, kind, length and additive-checksum validation.
- [x] Partial TX/RX, zero-progress handling and bounded process calls.
- [x] Single wrap-safe operation-wide deadline.
- [x] Physical TX drain callback contract.
- [x] Separate host status and raw sensor ACK.
- [x] Abort, quarantine and external recovery acknowledgment.
- [ ] STM32F103 interrupt/DMA UART adapter with owned TX queue, RX ring and error propagation.
- [ ] Non-blocking physical reset/power-cycle recovery implementation.
- [ ] USB transport adapter for supported module variants.
- [ ] Target concurrency, reactive-sensor mocks, sanitizer and fuzz coverage.

## 10. Validation and release

- [x] [GCC/Clang host CI](https://github.com/sajjadiam/AS608_lib/actions/runs/45580355200) for baseline core.
- [x] Independent packet and transaction builds/tests.
- [x] Host command, template and fault regression tests.
- [x] Historical hardware evidence: 768-byte transfer and recognition on two AS608_QCCF modules using the former driver.
- [ ] Current non-blocking core hardware validation on STM32F103C8T6.
- [ ] Two independent USART1/USART2 sessions under load.
- [ ] Repeat enrollment, recognition and 512/768-byte transfers as supported.
- [ ] Verify supported commands, packet sizes, timings and feature lengths for each firmware.
- [ ] Target fault injection: late ACK, timeout, UART errors, overflow and physical recovery.
- [ ] Document RAM consumption, service latency and firmware compatibility matrix.

## Recommended development order

1. STM32F103 port and current-core hardware validation.
2. HighSpeedSearch, system settings, password and address configuration.
3. Automatic enrollment/identification and host composite workflows.
4. Ordinary feature files, image streaming, GenBinImage and notepad.
5. Optional USB, GPIO, power and maintenance functions after variant verification.

## References

1. Synochip, *AS60x fingerprint recognition SOC Communication Manual V1.0* (AS60x 指纹识别 SOC 通讯手册), April 2010. Printed pp. 2–7 (configuration, buffers), pp. 8–10 (31 commands), pp. 11–28 (wire protocol and commands), pp. 29–34 (workflows).
2. Synochip, *AS608 Processor Datasheet*, v1.00, September 2015, sections 4 and 7. Processor peripherals are not necessarily accessible through module UART commands.
3. Repository: [as608.h](as608.h), [as608.c](as608.c), [as608_transaction.c](as608_transaction.c), [as608_port.h](as608_port.h).

Manual caveats: several entries are reserved or legacy PS1802-specific. Notepad total sizes conflict in the prose (16 × 32-byte page layout gives 512 bytes); ReadSysPara contains a conflicting packet-size code 1 value (WriteReg specifies 64 bytes). Verify these on target. No claim is made that every AS608 module implements every AS60x command.

## Post-rebuild maintenance

- [x] Stage 47: remove unused free-slot test helper; GCC/Clang regression baseline restored.
- [x] Stage 48: common-type contract review, explicit status values, C11/C++11 requirements and independent compile-time tests. No new wire commands or runtime state.

- [x] Stage 49: packet-header contract review; wire versus storage bounds, zero-length framing, exact address validation, checksum and buffer preconditions, independent C11/C++11 compile-time checks. No codec behavior or layout changes.
- [x] Stage 50: packet codec review, shared big-endian checksum reader, consistent control-flow braces, exhaustive wire length/kind/checksum tests and independent streaming oracle. No public signature or wire-behavior changes.
- [x] Stage 51: UART port contract review; borrowed-buffer lifetime, partial I/O, ISR/DMA ownership, latched errors, physical TX drain, coherent clock and independent header/transport tests. No callback signature or runtime core change. Target port validation pending.
- [x] Stage 52: transaction-header contract review; explicit stable event values, phase and borrowed-view preconditions, fixed storage bounds, exact RX address scope, deadline/callback contract and independent header/transport tests. No runtime core or layout change. Review full-width callback counts and TX kind validation in stage 53.
- [x] Stage 53: transaction implementation hardening; full-width TX kind and callback validation, busy send rejection, pre-arithmetic counter guards, RX metadata checks, consistent static comments and regression/fault tests. Context layout and public API unchanged. Narrow-size_t target and hardware validation pending.
