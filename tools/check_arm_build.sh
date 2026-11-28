#!/usr/bin/env bash
# Verify the reference Cortex-M3 build; the linked ELF is NOT bootable firmware.
set -euo pipefail
export LC_ALL=C

build="${1:-build/arm}"
archive="$build/libas608.a"
report="$build/audit"
mkdir -p "$report"
test -f "$archive"
test -f "$build/compile_commands.json"

{
    arm-none-eabi-gcc --version
    arm-none-eabi-gcc -dumpmachine
    arm-none-eabi-ld --version
    cmake --version
} | tee "$report/toolchain.txt"
test "$(arm-none-eabi-gcc -dumpmachine)" = arm-none-eabi

arm-none-eabi-ar t "$archive" | tee "$report/archive-members.txt"
test "$(wc -l < "$report/archive-members.txt")" -eq 3
for member in as608.c.o as608_packet.c.o as608_transaction.c.o; do
    grep -Fxq "$member" "$report/archive-members.txt"
done

arm-none-eabi-readelf -h -A "$archive" | tee "$report/archive-elf.txt"
# Check attributes of every archive member, not only the first object.
for pattern in 'Class:.*ELF32' 'Machine:.*ARM' 'Data:.*little endian' 'Tag_CPU_arch: v7$' 'Tag_CPU_arch_profile: Microcontroller' 'Tag_THUMB_ISA_use: Thumb-2'; do
    test "$(grep -Ec "$pattern" "$report/archive-elf.txt")" -eq 3
done

# Use the independent public/lower-layer C header contracts on target ABI.
for source in types packet_header port_header transaction_header header; do
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -mfloat-abi=soft \
        -std=c11 -ffreestanding -Wall -Wextra -Werror -pedantic -Isrc \
        -fsyntax-only "tests/unit/test_as608_${source}.c"
done

arm-none-eabi-nm -u "$archive" | tee "$report/archive-undefined.txt"
arm-none-eabi-nm -g --defined-only "$archive" > "$report/archive-symbols.txt"

# Force every production object into an ELF and resolve all its references
# using only the target C library and compiler runtime. No board startup,
# HAL, RTOS, test mocks, or application-supplied implementations are linked.
# The arbitrary entry is for link verification only. Never flash this ELF.
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -nostdlib \
    -Wl,--no-undefined,-e,as608_process "-Wl,-Map,$report/link-check.map" \
    -Wl,--whole-archive "$archive" -Wl,--no-whole-archive \
    -Wl,--start-group -lc -lgcc -Wl,--end-group \
    -o "$report/link-check.elf"
arm-none-eabi-nm -u "$report/link-check.elf" | tee "$report/link-undefined.txt"
test ! -s "$report/link-undefined.txt"
arm-none-eabi-readelf -h -A "$report/link-check.elf" > "$report/link-elf.txt"
echo "PASS: Cortex-M3 archive, five C header contracts and full-core link."
