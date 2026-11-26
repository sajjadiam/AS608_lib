# AS608 Feature Checklist

Baseline: [Core e9218ec](https://github.com/sajjadiam/AS608_lib/commit/e9218ecd362b254b4e8d1de9e199825fbf42a173) — 2026-11-25.

This checklist tracks the host library against the Synochip AS60x Communication Manual V1.0 (April 2010), checked against the current [public API](as608.h) and [implementation](as608.c).

- Checked: implemented in the non-blocking core, within the stated scope.
- Unchecked: missing, incomplete, or awaiting the indicated validation.
- Command coverage: **16/31 (51.6%)**; 15 commands remain. UpChar and DownChar are implemented with limited file-size support.
- Implementation is not equivalent to hardware validation or universal firmware compatibility.
- API design: all 31 documented opcodes have contracts in [as608.h](as608.h). The 15 missing commands are declarations only under `AS608_DECLARE_PLANNED_API`; they remain unchecked until implemented and tested.

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
- [ ] API 22: `as608_delete_templates_start()`.
- [ ] Remaining command APIs, restored individually in header order.

Active wire-command coverage: **14/31** (PS_VfyPwd 0x13, PS_ReadSysPara 0x0F, PS_ValidTempleteNum 0x1D, PS_ReadIndexTable 0x1F, PS_ReadINFpage 0x16, PS_GetImage 0x01, PS_GenChar 0x02, PS_Match 0x03, PS_RegModel 0x05, PS_Search 0x04, PS_LoadChar 0x07, PS_UpChar 0x08, PS_DownChar 0x09, PS_StoreChar 0x06).
The 16/31 command coverage and checked items below describe the preserved
pre-rebuild baseline (commit 53425e4), not current linkability except for
APIs explicitly restored above.
No historical test result is asserted for the new active stage.

## 1. Fingerprint acquisition and recognition

- [x] `0x01 PS_GetImage` — Capture fingerprint image into ImageBuffer; `as608_capture_image_start()`.
- [x] `0x02 PS_GenChar` — Extract features into CharBuffer1 or CharBuffer2; `as608_generate_character_start()`.
- [x] `0x03 PS_Match` — 1:1 comparison of character buffers and match score; `as608_match_buffers_start()`.
- [x] `0x04 PS_Search` — 1:N database range search, PageID and score; `as608_search_start()`.
- [x] `0x05 PS_RegModel` — Merge fingerprint features into a template; `as608_create_model_start()`.
- [ ] `0x10 PS_Enroll` — Sensor-side automatic single-scan enrollment, free-slot selection and PageID. Planned: `as608_enroll_start()`.
- [ ] `0x11 PS_Identify` — Automatic acquisition and identification; may update incomplete templates on a sufficiently strong match. Planned: `as608_identify_start()`.
- [ ] `0x1B PS_HighSpeedSearch` — High-speed database range search. Planned: `as608_high_speed_search_start()`.

## 2. Template and database management

- [x] `0x06 PS_StoreChar` — Store a character buffer at flash PageID; `as608_store_template_start()`.
- [x] `0x07 PS_LoadChar` — Load a stored template into a character buffer; `as608_load_template_start()`.
- [x] `0x08 PS_UpChar` — Sensor-to-host template upload, 512/768 bytes only; `as608_upload_template_start()`.
- [x] `0x09 PS_DownChar` — Host-to-sensor template download, 512/768 bytes only; `as608_download_template_start()`.
- [x] `0x0C PS_DeletChar` — Delete a consecutive range of templates; `as608_delete_templates_start()`.
- [x] `0x0D PS_Empty` — Erase the fingerprint database; `as608_empty_database_start()`.
- [x] `0x1D PS_ValidTempleteNum` — Count valid templates; `as608_get_template_count_start()`.
- [x] `0x1F PS_ReadIndexTable` — Read 32-byte occupancy bitmap per 256 slots, index pages 0–3; `as608_read_index_table_start()`. Manual marks this command reserved in the detailed description; verify on target firmware.

Additional template capabilities:

- [x] Both character buffer IDs.
- [x] Opaque 512/768-byte templates with actual received length.
- [x] Negotiated outbound packet size: 32/64/128/256 bytes.
- [ ] Support ordinary 256-byte feature files described by the manual, separately from templates. Planned: `as608_upload_feature_start()`, `as608_download_feature_start()`.
- [ ] Verify other file lengths per firmware; do not assume arbitrary lengths.
- [ ] Verify DownChar post-stream ACK behavior per firmware. Standalone download currently completes at physical TX drain; composite write also checks StoreChar ACK. The manual's workflow on printed p. 31 shows an additional ACK after data.

## 3. Image transfer and processing

- [ ] `0x0A PS_UpImage` — Upload ImageBuffer to host. Planned: `as608_upload_image_start()`.
- [ ] `0x0B PS_DownImage` — Download image into ImageBuffer. Planned: `as608_download_image_start()`.
- [ ] `0x1C PS_GenBinImage` — Generate binary, thinned or minutiae-marked thinned image. Planned: `as608_generate_binary_image_start()`.
- [ ] Stream large images without storing a complete image in MCU RAM.
- [ ] Encode/decode packed UART image pixels (two 4-bit pixels per byte).
- [ ] Verify image dimensions and transfer lengths per sensor.
- [ ] Support GenBinImage modes 0, 1 and 2 and upload results.
- [ ] Optional host-side PGM export and image/feature-quality diagnostics.

## 4. System configuration and information

- [ ] `0x0E PS_WriteReg` — Write baud multiplier, security level or packet-size register. Planned: `as608_set_baud_rate_start()`, `as608_set_security_level_start()`, `as608_set_packet_size_start()`.
- [x] `0x0F PS_ReadSysPara` — Read 16-byte system parameters; `as608_read_parameters_start()`.
- [x] `0x16 PS_ReadINFpage` — Read 512-byte flash information page; `as608_read_information_page_start()`.
- [ ] `0x17 PS_Port_Control` — Enable/disable the other communication interface (UART/USB). Planned: `as608_control_other_port_start()`.
- [x] Read status register, sensor type, capacity, security level, address, packet-size code and baud multiplier.
- [ ] Typed status flags: busy, pass, password verified, valid image.
- [ ] Set baud rate (register 4) and synchronize host UART reconfiguration after ACK.
- [ ] Set security level 1–5 (register 5).
- [ ] Set packet-size code 0–3 (register 6).
- [ ] Parse product, firmware, manufacturer and sensor descriptors from the information page where verified. Planned: `as608_read_device_info_start()`.
- [ ] Read-back and persistence verification for configuration writes.

## 5. Password, address and random number

- [ ] `0x12 PS_SetPwd` — Change 32-bit handshake password. Planned: `as608_set_password_start()`.
- [x] `0x13 PS_VfyPwd` — Verify 32-bit handshake password; `as608_verify_password_start()`.
- [ ] `0x14 PS_GetRandomCode` — Read 32-bit random code. Planned: `as608_get_random_code_start()`.
- [ ] `0x15 PS_SetChipAddr` — Set device address. Planned: `as608_set_device_address_start()`.
- [x] Configurable host-side address and strict received-address validation.
- [ ] Handle address-change reply transition according to verified firmware behavior.
- [ ] Password-change and verification workflow without logging credentials.
- [ ] Characterize RNG guarantees before cryptographic use.

Handshake passwords and additive checksums are not encryption or authenticated transport.

## 6. User flash, GPIO and firmware maintenance

- [ ] `0x18 PS_WriteNotepad` — Write 32-byte user flash page. Planned: `as608_write_notepad_start()`.
- [ ] `0x19 PS_ReadNotepad` — Read 32-byte user flash page. Planned: `as608_read_notepad_start()`.
- [ ] `0x1A PS_BurnCode` — Firmware/information-page programming; high-risk, variant-specific. Planned: `as608_burn_code_start()`.
- [ ] `0x1E PS_UserGPIOCommand` — User GPIO output control; reserved/variant-specific. Planned: `as608_write_user_gpio_start()`.
- [ ] Notepad pages 0–15; verify actual capacity.
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
- [ ] Complete two-scan enrollment FSM with finger removal, GenChar, RegModel and StoreChar. Planned: `as608_enroll_two_scan_start()`.
- [ ] Complete capture + GenChar + Search identification FSM. Planned: `as608_identify_range_start()`.
- [ ] Free-slot discovery using index table and database capacity. Planned: `as608_find_free_slot_start()`.
- [ ] Full database backup/restore with per-template outcomes.
- [ ] Cross-sensor migration workflow with compatibility checks and partial-failure reporting.
- [ ] Optional duplicate-finger detection before enrollment.

Primitive commands for most of these workflows already exist; dedicated composite APIs do not.

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
