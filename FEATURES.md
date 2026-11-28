# AS608 capabilities and compatibility

This reference describes the implemented **host driver**, not a claim of
universal AS608 firmware support. The source of truth for signatures,
ownership and individual preconditions is [as608.h](src/as608.h).
The [implementation](src/as608.c) and dedicated host tests establish the
implemented wire profiles.

## Coverage

| Category | Count |
| --- | ---: |
| Public lifecycle APIs | 5 |
| Direct command/profile start APIs | 36 |
| Host composite job APIs | 5 |
| Total public APIs | 46 |
| Wire opcodes implemented | 31 |
| General opcode implementations | 30 |
| Experimental opcode implementations | 1 |

All 46 APIs are declared, defined and covered by host tests. One opcode
can expose multiple APIs (e.g. UpChar, DownChar, WriteReg and ReadINFpage).
Host composite jobs introduce no additional wire opcodes. "Implemented"
means the stated host protocol profile is present and exercised by tests;
target support remains unverified for the current cooperative core.

## Lifecycle API

| API | Responsibility |
| --- | --- |
| `as608_init()` | Initialize fresh zeroed context; copy configuration without UART I/O. |
| `as608_process()` | Advance one bounded cooperative step. |
| `as608_get_result()` | Copy available terminal result without advancing the job. |
| `as608_abort()` | Logically abort a busy job and quarantine. |
| `as608_confirm_reset()` | Acknowledge external recovery; no physical reset. |

## Wire command inventory

The table is ordered by opcode (0x01–0x1F). All rows are host-implemented;
0x1A is experimental. Some commands are reserved or variant-specific in
the AS60x manuals and are not confirmed on all AS608 modules.

| Opcode | Manual command | Public start API | Implemented scope |
| --- | --- | --- | --- |
| 0x01 | `PS_GetImage` | `as608_capture_image_start()` | Acquire into ImageBuffer. |
| 0x02 | `PS_GenChar` | `as608_generate_character_start()` | Extract features into CharBuffer1 or 2. |
| 0x03 | `PS_Match` | `as608_match_buffers_start()` | Compare buffers; return firmware-defined score. |
| 0x04 | `PS_Search` | `as608_search_start()` | Search a nonempty PageID range; return match. |
| 0x05 | `PS_RegModel` | `as608_create_model_start()` | Merge character buffers into a template. |
| 0x06 | `PS_StoreChar` | `as608_store_template_start()` | Write a selected character buffer to flash. |
| 0x07 | `PS_LoadChar` | `as608_load_template_start()` | Load a stored template into a character buffer. |
| 0x08 | `PS_UpChar` | `as608_upload_template_start()`<br>`as608_upload_feature_start()` | Receive 512/768-byte templates or exact 256-byte features. |
| 0x09 | `PS_DownChar` | `as608_download_template_start()`<br>`as608_download_feature_start()` | Transmit supported template/feature sizes; drain-only profile. |
| 0x0A | `PS_UpImage` | `as608_upload_image_start()` | Exact-length packed image stream to a sink. |
| 0x0B | `PS_DownImage` | `as608_download_image_start()` | Exact-length image source; drain-only profile. |
| 0x0C | `PS_DeletChar` | `as608_delete_templates_start()` | Delete a contiguous, nonempty PageID range. |
| 0x0D | `PS_Empty` | `as608_empty_database_start()` | Erase the fingerprint database. |
| 0x0E | `PS_WriteReg` | `as608_set_baud_rate_start()`<br>`as608_set_security_level_start()`<br>`as608_set_packet_size_start()` | Registers 4, 5 and 6 only. |
| 0x0F | `PS_ReadSysPara` | `as608_read_parameters_start()` | Decode the 16-byte parameter record. |
| 0x10 | `PS_Enroll` | `as608_enroll_start()` | Sensor-managed single-scan enrollment; writes flash. |
| 0x11 | `PS_Identify` | `as608_identify_start()` | Sensor-managed identification; firmware may update templates. |
| 0x12 | `PS_SetPwd` | `as608_set_password_start()` | Set 32-bit handshake password. |
| 0x13 | `PS_VfyPwd` | `as608_verify_password_start()` | Verify 32-bit handshake password. |
| 0x14 | `PS_GetRandomCode` | `as608_get_random_code_start()` | Return a raw 32-bit value, not a cryptographic guarantee. |
| 0x15 | `PS_SetChipAddr` | `as608_set_device_address_start()` | Initial provisioning only; exact new-address ACK. |
| 0x16 | `PS_ReadINFpage` | `as608_read_information_page_start()`<br>`as608_read_device_info_start()` | Exact 512-byte page or selective 32-byte descriptors. |
| 0x17 | `PS_Port_Control` | `as608_control_other_port_start()` | Control the interface other than the active one. |
| 0x18 | `PS_WriteNotepad` | `as608_write_notepad_start()` | Replace a complete 32-byte page. |
| 0x19 | `PS_ReadNotepad` | `as608_read_notepad_start()` | Read a complete 32-byte page. |
| 0x1A | `PS_BurnCode` | `as608_burn_code_start()` | Experimental, destructive, explicitly authorized profile only. |
| 0x1B | `PS_HighSpeedSearch` | `as608_high_speed_search_start()` | Range search using the high-speed command. |
| 0x1C | `PS_GenBinImage` | `as608_generate_binary_image_start()` | Modes 0–2; output format requires target validation. |
| 0x1D | `PS_ValidTempleteNum` | `as608_get_template_count_start()` | Read valid-template count. |
| 0x1E | `PS_UserGPIOCommand` | `as608_write_user_gpio_start()` | Reserved/variant-specific; selectors and echo 0/1. |
| 0x1F | `PS_ReadIndexTable` | `as608_read_index_table_start()` | Reserved/variant-specific; raw 32-byte bitmap, pages 0–3. |

## Host composite jobs

All phases share a single original deadline. No implicit retry,
sensor-side rollback or database reservation is provided.

| Public start API | Sequence | Scope and conditions |
| --- | --- | --- |
| `as608_read_template_start()` | LoadChar + UpChar | Load from flash and receive a 512/768-byte opaque template. |
| `as608_write_template_start()` | ReadSysPara + DownChar + drain + StoreChar | Requires verified no-final-ACK firmware; no occupancy check or readback. |
| `as608_enroll_two_scan_start()` | GetImage + GenChar(1) + removal + GetImage + GenChar(2) + RegModel + StoreChar(2) | Paced placement/removal polls; observed NO_FINGER before scan two; explicit PageID. |
| `as608_identify_range_start()` | GetImage + GenChar + Search | One attempt under one deadline; no automatic finger polling or retry. |
| `as608_find_free_slot_start()` | ReadSysPara + ReadIndexTable | First observed free slot, capacity 1–1024; no reservation. |

## Supported data and configuration profiles

| Property | Host profile | Validation caveat |
| --- | --- | --- |
| Character buffer IDs | 1, 2 | Sensor owns buffer contents. |
| Wire payload storage | Up to 256 bytes per frame | Codec's theoretical length bound is not sensor support. |
| Outbound template/feature chunks | 32/64/128/256 bytes | Read actual packet-size code before download. |
| DownImage fragments | 64/128/256 bytes | Exact total must be divisible by actual chunk size. |
| Template size | 512 or 768 bytes | 512 documented; 768 historical observation. Format compatibility not guaranteed. |
| Ordinary feature size | 256 bytes | Target profile not verified. |
| Information page | 512 bytes | Full transfer verified by host codec and length checks. |
| Device descriptors | 4 × 8 raw bytes | Offsets 28, 36, 44, 52; last inferred. Not NUL-terminated or unique serial numbers. |
| Index bitmap | 32 bytes/page; page 0–3 | 256 slots per page; target support and bit ordering unverified. |
| Free-slot lookup capacity | 1–1024 | Capacity outside range is a protocol error, not "full". |
| Notepad | 16 pages × 32 bytes | Manual prose conflicts; verify capacity and persistence. |
| Baud rate | 9600–115200 in steps of 9600 | WriteReg register 4; verify target rate and ACK timing. |
| Security level | 1–5 | WriteReg register 5; sensor controls thresholds. |
| Packet size code | 0–3 = 32/64/128/256 bytes | WriteReg register 6; manual ReadSysPara prose is inconsistent for code 1. |
| Binary image mode | 0, 1, 2 | Binary, thinned, thinned with minutiae; format unverified. |
| User GPIO | Selector 0/1, echo 0/1 | Mapping and availability variant-specific. |
| Clock and timeout | uint32_t milliseconds; 1..0x7FFFFFFF ms | Whole-job deadline, modulo wrap; scheduler must poll before full wrap hides elapsed time. |

`as608_parameters_t` exposes the status register, sensor type, template
capacity, security level, address, packet-size code and baud multiplier.
`as608_sensor_status_flag_t` names the busy, match-passed,
password-verified and image-valid status masks. Sensor type is not a
guaranteed QCCF/QYCF identifier.

## Command-specific constraints

### Recognition and database

- Sensor-managed Enroll (0x10) selects a slot and writes flash; a lost
  result may leave an enrolled template at an unknown PageID. Identify
  (0x11) may update incomplete templates on firmware that implements
  this behavior. No automatic retry is safe after uncertain effects.
- Host two-scan enrollment uses an explicit PageID and requires
  observed finger removal between scans. It does not detect duplicates
  or check occupancy before StoreChar.
- Search scores are firmware-defined, not probabilities. The caller
  selects a valid range based on actual database capacity.
- Free-slot discovery scans capacity-limited occupancy bytes and stops
  at the lowest zero bit. LSB-to-lowest-ID mapping follows the related
  R503 protocol; the Chinese AS60x manual does not establish bit order.
  Verify polarity, ordering, zero-based IDs and capacity on target.
  `found=false` is a valid full-database result, not a host fault.
  The result never reserves a slot; ensure exclusive database ownership
  through the subsequent write.
- Delete, Empty, StoreChar and enrollment may have irreversible or
  uncertain flash effects. No readback, power-loss atomicity or rollback
  is promised.

### Template, feature and image transfer

- UpChar exposes separate template and ordinary-feature APIs; a successful
  template receive commits the actual 512/768-byte length. Partial buffer
  writes are possible on fault. The caller verifies format compatibility.
- DownChar and DownImage use a readiness ACK followed by DATA/END_DATA
  and physical TX drain. **No final ACK is consumed.** The manual's
  DownChar command description (English guide pp. 16–17) and workflow
  (p. 31) differ on this point. Verify no-final-ACK behavior before
  using any download or subsequent command.
- Composite template write waits for StoreChar ACK after TX drain.
  A trailing DownChar ACK could be mistaken for that ACK. Do not use
  the composite job on unverified firmware.
- Image upload delivers validated fragments to a non-blocking sink;
  the sink must accept/copy the complete fragment or fail. No sink
  backpressure/retry is implemented.
- Image download uses an exact-length source. Return the requested
  byte count, zero without advancing source state, or a negative error.
  Partial positive returns are rejected. The source and sink must not
  retain borrowed pointers. The driver does not unpack pixel nibbles,
  infer dimensions or allocate an entire image.

### Configuration, password and address

- Baud writes receive ACK at the old rate under the implemented profile;
  the application owns synchronization at the new rate. Verify ACK
  timing, actual settings and persistence by readback on target.
- SetPwd and VfyPwd are distinct; verify the new password separately.
  Neither password nor checksum provides encrypted/authenticated transport.
- GetRandomCode yields a raw uint32_t value with no entropy, uniqueness
  or cryptographic assurance.
- SetChipAddr is initial provisioning only: TX at default 0xFFFFFFFF,
  RX at the exact requested non-default address. Only ACK_OK commits
  the local address. Any other reply, timeout or abort quarantines
  without local commit. The actual sensor address may nevertheless
  have changed. Verify externally and recover with compatible settings.
- Port control affects the interface other than the active one. On
  UART it controls USB; on USB it controls UART. No host-port
  reconfiguration or persistent behavior is implied.
- Device descriptors are raw fixed-width bytes. SensorName offset 52
  (word address 0x21A) is inferred from sequential layout; verify page
  origin, byte order and fields on the target.

### User flash, GPIO and maintenance

- Notepad writes replace the entire 32-byte page; validate target
  capacity and read back separately. No persistence/atomicity guarantee.
- UserGPIO follows reserved command 30 in the Chinese AS60x manual.
  The echoed state is not a voltage measurement. Verify physical mapping.
- BurnCode is an explicitly authorized **experimental** ACK-each
  profile: readiness 00, intermediate F0 and terminal 00. Fragments
  are 64/128/256 bytes, with total length divisible by fragment size.
  The generic manuals do not establish this complete handshake or
  firmware image format. The host tests exercise a synthetic profile,
  not an actual update. A valid terminal burn response requires external
  recovery before another job. Do not enable without matching vendor
  documentation, verified compatible image and a recovery procedure.
  No firmware image, flash verification or automatic retry is supplied.

## Implemented engineering properties

- [x] ISO C11 core, C++11-compatible declarations and all 46 linkable symbols.
- [x] Independent packet/transaction/command layers and UART contract.
- [x] Caller-owned context; no heap; independent multi-sensor contexts.
- [x] Bounded partial I/O, one whole-job deadline, exact address checks.
- [x] Structural validation, additive checksum, host status and raw ACK separation.
- [x] Logical abort, quarantine and explicit external recovery acknowledgment.
- [x] Shared test fixtures, independent wire vectors, exhaustive packet-domain tests.
- [x] CMake static library, native CTest, C-only build with tests disabled.
- [x] GCC/Clang Debug and Release, ASan/UBSan, GCC analyzer.
- [x] Historical 768-byte transfer and recognition on two AS608_QCCF modules with the former driver.

Host validation baseline: [stage 58 CI](https://github.com/sajjadiam/AS608_lib/actions/runs/46119964847),
commit [880ef52](https://github.com/sajjadiam/AS608_lib/commit/880ef52383a3c72fb9e6b44a63dd9410aff22674).
The workflow ran 53 executable tests per runtime configuration and
nine compile-only header checks per configuration. A successful host
run does not establish target compatibility, timing or memory bounds.

## Remaining validation

- [ ] Verify ARM Cortex-M3 cross-build evidence from the [ARM CI workflow](.github/workflows/arm-cortex-m3.yml).
- [ ] Review Cortex-M3 measured section and ABI layout reports; see [ARM memory reports](docs/TESTING.md#arm-memory-reports).
- [ ] Measure stack, final firmware memory fit and service latency.
- [ ] STM32F103 UART IT/DMA adapter: owned TX queue, RX ring, error propagation and synchronization.
- [ ] Non-blocking physical recovery and current-core target integration.
- [ ] Verify no-final-ACK DownChar/DownImage profile before transfers or composite writes.
- [ ] Verify firmware command availability, descriptor offsets, index ordering, capacity, feature/image sizes and formats.
- [ ] Verify enrollment, identification, templates and independent USART1/USART2 sessions under load.
- [ ] Verify register/password/address/notepad writes, readback and persistence on target.
- [ ] Hardware fault injection: late ACK, timeout, UART/DMA failure, queue overflow and recovery.
- [ ] Characterize target concurrency and timing; extend reactive-sensor mocks and fuzz tests.
- [ ] Record a firmware-specific compatibility matrix; validate or leave experimental features disabled.

## Optional extensions (not implemented)

These are not required for the 31-opcode host scope. Add them only with
a matching firmware contract and a demonstrated use case.

- USB adapter; sleep/wake/touch, software reset and illumination control.
- Other vendor-specific commands and dedicated product/serial diagnostics.
- Packed-pixel conversion, PGM export and host-side image/feature diagnostics.
- Full database backup/restore, cross-sensor migration with partial-failure
  reporting and optional duplicate detection before enrollment.
- Additional template lengths or final-ACK profiles after target verification.

## References

1. Synochip, *AS60x fingerprint recognition SOC Communication Manual V1.0*
   (AS60x 指纹识别 SOC 通讯手册), April 2010. Printed pp. 2–7
   (configuration and buffers), pp. 8–10 (31-command list), pp. 11–28
   (protocol/commands), pp. 29–34 (workflows). Reserved and legacy
   PS1802-specific entries are not proof of AS608 module support.
2. Synochip, *AS60x MCU User's Guide V1.0*, English edition
   ([reference PDF](https://cdn.sparkfun.com/assets/e/c/d/a/1/MCU_SOC_Manual_EN.pdf)).
   The command descriptions and example workflows require profile-specific
   interpretation, especially DownChar completion.
3. Synochip, *AS608 Processor Datasheet*, v1.00, September 2015,
   sections 4 and 7. Processor peripherals do not establish UART
   module-command availability.
4. Related R503 protocol: index bit-order inference only; not direct
   evidence of AS608 behavior.
5. STMicroelectronics, RM0008 Rev.21, USART transmitter and DMA
   transmission sections, for the STM32F1 physical TX-completion contract.
6. Repository: [public API](src/as608.h), [port contract](src/as608_port.h),
   [command implementation](src/as608.c), [transport](src/as608_transaction.c),
   [architecture decision](docs/adr/0001-as608-driver-architecture.md).

See [engineering validation plan](docs/TESTING.md) and [porting contract](docs/PORTING.md).
The development history and former checklists are retained in Git.
