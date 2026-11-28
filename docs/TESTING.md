# Verification strategy

[CI workflow](../.github/workflows/c11-host-tests.yml) ·
[Test build](../tests/CMakeLists.txt) · [Remaining validation](../FEATURES.md#remaining-validation)

## Existing evidence

The stage-59 [host CI run](https://github.com/sajjadiam/AS608_lib/actions/runs/46120675380)
passed. 53 CTest executables per runtime configuration; nine compile-only
header targets per configuration. The README C example is additionally
syntax-checked by the workflow (not linked or executed).

| Scope | Coverage |
| --- | --- |
| Codec | Independent vectors, all 256 kinds, all 65536 lengths for each valid kind, all checksum trailers, streaming checksum |
| Transaction | Partial I/O, bounds, checksum, address, deadline, drain, invalid callback counts and metadata |
| Command | Dedicated tests for 46 APIs, wire expectations, state/result contracts |
| Integration | Multi-command, async and fault regressions |
| Header | Standalone C11/C++11 contracts and linkage of 46 public symbols |
| Static analyzer | GCC -fanalyzer on the three production translation units |

Wire fixtures calculate reference values independently of the production
codec. Shared mock UART and wire helpers are in tests/common; specialized
fault fixtures remain separate. Test file names in tests/unit and
tests/integration are checked against the registered CTest inventory.
The compile-only header fixtures are intentionally not CTest executables.

## Reproduce host validation

```sh
cmake -S . -B build/host -DAS608_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 2
python3 tests/check_inventory.py build/host
ctest --test-dir build/host --parallel 2 --output-on-failure --no-tests=error
python3 tests/check_readme_example.py --compiler cc
```

Use a separate build directory per compiler/configuration. CI runs GCC
and Clang Debug/Release, Clang ASan+UBSan, and a separate GCC analyzer
build. Release tests keep assertions enabled; some fixtures perform calls
inside assertions. Sanitizers instrument core and tests, and link runtime
support into each executable. Two Release jobs also verify C-only archives.

See README for exact sanitizer/analyzer commands. JUnit and LastTest.log
artifacts aid diagnosis; failed jobs are not ignored. A passed host run
is evidence of the executed paths only, not proof of correctness for all
inputs, all targets or every firmware variant.

## Cortex-M3 cross build

The separate [ARM workflow](../.github/workflows/arm-cortex-m3.yml)
installs the ARM GNU C compiler, binutils and newlib on Ubuntu 24.04.
Its [reference toolchain](../cmake/toolchains/arm-cortex-m3-gcc.cmake)
targets Cortex-M3, Thumb, soft-float ABI and freestanding ISO C11.
MinSizeRel selects a size-oriented baseline; compile_commands.json
records the effective compiler flags. Compiler/package versions may
change with the runner; use the recorded toolchain version for comparisons.

Follow the README cross-build commands and run
`bash tools/check_arm_build.sh build/arm` from the repository root.
The check verifies all three archive members' ELF32 ARMv7-M/Thumb-2
attributes, independently syntax-checks five C header contracts for the
target ABI, and forces all archive objects into a link-check ELF using
only newlib and libgcc. Unresolved references in the final ELF fail the
check. The archive's undefined-symbol list may legitimately contain
inter-object and C-runtime references.

CI retains the archive, flags, toolchain version, ELF attributes,
symbol lists, linker map and link-check ELF. This ELF has no board
startup/vector table or board memory layout and must not be flashed.
No target execution, application-level memory fit, final firmware size
or hardware correctness is established. Flash, RAM and stack analysis
remain separate engineering tasks.

References: [GCC ARM options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html),
[CMake toolchains](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html),
[CMAKE_TRY_COMPILE_TARGET_TYPE](https://cmake.org/cmake/help/latest/variable/CMAKE_TRY_COMPILE_TARGET_TYPE.html).

## ARM memory reports

Run `python3 tools/report_arm_memory.py build/arm` after the reference
cross build. The separate compile-only probe uses Cortex-M3 reference ABI
flags and encodes sizeof, _Alignof and offsetof in symbol sizes; it is
never included in the production archive or link-check ELF.

The audit artifact includes memory-summary.json (per-member allocated
section totals and field layout/padding), raw objdump section headers,
nm symbol sizes, and size -A output. Flash section sum includes code,
read-only allocated data and initialized writable data; static RAM
section sum includes writable allocated data and zero-initialized data.
The reference build uses -Os, no LTO, and the standard compiler ABI.
Archive sums exclude C-runtime code, final link padding, dead-section
elimination, startup, port resources, application buffers and stack.
Function symbols report out-of-line compiled bodies, not inclusive
call-graph cost or cycle counts. Context costs are separate from global
static sections: one caller-owned context costs sizeof(as608_t), two
cost twice that size; configuration is embedded.

Use the measured JSON for the exact toolchain revision before proposing
layout or code optimizations. STM32F103C8 nominal capacity is 64 KiB
Flash and 20 KiB SRAM (ST product page); the final application link map
and measured stack are required to establish fit.

References: [GNU binutils](https://sourceware.org/binutils/docs/binutils.html),
[STM32F103C8](https://www.st.com/en/microcontrollers-microprocessors/stm32f103c8.html).

### Measured reference baseline

[ARM CI 46123757921](https://github.com/sajjadiam/AS608_lib/actions/runs/46123757921)
passed at commit b9fed6c using ARM GCC 13.2.1, Cortex-M3, Thumb, soft
float, freestanding C11, -Os and no LTO.

| Object | Code | Read-only data | Writable static RAM |
| --- | ---: | ---: | ---: |
| as608.c.obj | 5874 | 261 | 0 |
| as608_packet.c.obj | 214 | 0 | 0 |
| as608_transaction.c.obj | 892 | 0 | 0 |
| Total | 6980 | 261 | 0 |

Flash section sum: 7241 bytes. Context 712 bytes (alignment 4);
transaction 608 (alignment 4), config 28, port 20, result 2.
One/two contexts cost 712/1424 bytes respectively, excluding port,
application and stack. Direct padding is 7 bytes in the command
context and 8 in the embedded transaction. The reference ARM enum
ABI reports size 1 for state, status and packet kind; do not assume
these sizes for other toolchains or alter enum ABI independently.
as608_process has a 2780-byte compiled symbol, including any inlined
code. These results do not justify a layout or dispatcher rewrite
without further evidence.

## Stack usage and direct call graph

The reference GCC toolchain now emits -fstack-usage and
-fcallgraph-info=su sidecar files for each core object. Run:

```sh
python3 tools/report_arm_stack.py build/arm
```

The report preserves the original .su and .ci files, captures compiled
frame sizes and qualifiers, detects recursive known direct-call paths,
and sums frames along each public API's largest known direct path.
Tail calls are conservatively counted as normal calls. Unbounded
dynamic stack records fail the audit. Unknown callees, including
indirect port/stream callbacks and C-runtime functions, are reported
as boundaries, not assumed to cost zero in the total application
budget. The generated stack-summary.json retains all frames, edges,
public paths and unresolved callees.

The largest known path is not a proven maximum stack bound: GCC's
graph may omit backend-generated calls or other edges. The final
budget must include application caller frames, runtime functions,
callbacks, exception stacking, interrupt nesting and measured
target high-water usage. Optimization, compiler version and ABI
can change these numbers. No stack limit is imposed without an
application requirement. See [GCC developer options](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html)
and [GCC instrumentation/options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html).

## Engineering audit still required

| Check | Acceptance evidence |
| --- | --- |
| Cortex-M3 cross build | Recorded compiler version, flags, ELF/archive and diagnostics |
| RAM | sizeof context/config plus actual queues and application buffers, linker map |
| Stack | Compiler stack-usage reports and call-chain analysis plus target high-water measurement; include interrupt nesting |
| Timing | Measured callback and process latency under representative worst-case load |
| UB and concurrency | Review integer ranges, alignment, lifetimes and ISR/DMA ownership; no unresolved high-risk defect |
| Fault containment | Inject invalid/late replies and hardware errors; quarantine until a verified fresh link |

A cross-compiled archive alone is not hardware validation. Static stack
reports alone are not worst-case stack guarantees. No numeric acceptance
budget has been supplied for RAM or latency; set it from the actual
application's memory map, scheduling and interrupt requirements.

## Hardware validation sequence

1. Identify board/firmware, supply/interface ratings, UART setup, toolchain
   and actual port implementation. Record baseline and recovery method.
2. Verify adapter queue ownership, partial I/O, fault latch and physical TC.
3. VfyPwd, ReadSysPara and non-destructive capture/search; compare sanitized
   decoded values with independently verified target observations.
4. On isolated test database, enroll, identify and verify index ordering,
   capacity and template lengths. Observe download post-END_DATA behavior
   before enabling any composite write. Verify template compatibility
   before authorized cross-device transfer.
5. Test simultaneous independent USART sessions under load, queue
   overflow, timeout, abort, late data and recovery.
6. Verify authorized configuration/notepad writes by readback and
   persistence tests. Leave experimental/variant-specific functions
   disabled until separately verified against matching vendor procedures.

Use a dedicated test database, not production biometric records.
Destructive maintenance is not required to establish ordinary driver
correctness. Do not claim BurnCode hardware support based on mocks.

## Evidence record template

For each target test record: source commit, module/firmware identification,
compiler and flags, board/UART configuration, port revision, test case,
preconditions, expected and observed result, timing/memory measurement
method, pass/fail, and sanitized trace or measurement reference. Record
unverified profiles as such. Do not store credentials or biometric payloads.
