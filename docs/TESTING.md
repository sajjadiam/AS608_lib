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
