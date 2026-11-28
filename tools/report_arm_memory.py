"""Measure actual ARM object sections and compiler-selected structure layouts."""

import argparse
import json
from pathlib import Path
import re
import subprocess

GROUPS = ("text", "rodata", "data", "bss", "other_flash", "other_ram")


def run(*args: str) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def sections(raw: str) -> dict[str, dict[str, int]]:
    """Classify allocated sections using objdump flags, not ELF file length."""
    members: dict[str, dict[str, int]] = {}
    current = None
    lines = raw.splitlines()
    for index, line in enumerate(lines):
        header = re.match(r"^(.+?):\s+file format\s+\S+", line)
        if header:
            current = header[1].strip()
            if current in members:
                raise ValueError(f"Duplicate member {current}")
            members[current] = dict.fromkeys(GROUPS, 0)
            continue
        section = re.match(r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+", line)
        if not section or current is None:
            continue
        flags = {flag.strip() for flag in lines[index + 1].split(",")}
        if "ALLOC" not in flags:
            continue
        name, size_hex = section.groups()
        size = int(size_hex, 16)
        if "CODE" in flags:
            group = "text"
        elif "READONLY" in flags:
            group = "rodata" if name.startswith(".rodata") else "other_flash"
        elif "CONTENTS" in flags:
            group = "data"
        else:
            group = "bss" if name.startswith(".bss") else "other_ram"
        members[current][group] += size
    if len(members) != 3 or any(member["text"] == 0 for member in members.values()):
        raise ValueError(f"Unexpected archive section inventory: {members}")
    return members


def layout(raw: str) -> dict:
    values: dict[str, int] = {}
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[0].startswith(("size_", "align_", "offset_", "field_")):
            values[parts[0]] = int(parts[3], 10)
    result = {}
    for key, size in sorted(values.items()):
        if not key.startswith("size_"):
            continue
        name = key.removeprefix("size_")
        record = {"size": size}
        if f"align_{name}" in values:
            record["alignment"] = values[f"align_{name}"]
        prefix = f"field_{name}__"
        fields = []
        for field_key, field_size in values.items():
            if field_key.startswith(prefix):
                field_name = field_key.removeprefix(prefix)
                fields.append({"name": field_name, "size": field_size,
                               "offset": values[f"offset_{name}__{field_name}"] - 1})
        if fields:
            fields.sort(key=lambda item: item["offset"])
            end = 0
            for field in fields:
                field["padding_before"] = field["offset"] - end
                if field["padding_before"] < 0:
                    raise ValueError(f"Overlapping fields in {name}")
                end = field["offset"] + field["size"]
            record["tail_padding"] = size - end
            record["padding"] = size - sum(field["size"] for field in fields)
            if record["tail_padding"] < 0:
                raise ValueError(f"Invalid layout for {name}")
            record["fields"] = fields
        result[name] = record
    for name in ("as608_t", "as608_transaction_t", "as608_config_t", "as608_port_t", "as608_result_t"):
        if not result[name]["fields"]:
            raise ValueError(f"Missing layout for {name}")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    build = args.build_dir
    audit = build / "audit"
    audit.mkdir(parents=True, exist_ok=True)
    archive = build / "libas608.a"
    probe = audit / "arm-memory-probe.o"

    # Use the same actual ABI flags as the production compilation, not
    # a guessed host layout. Reuse the first compile command's arguments
    # via explicit reference flags, and verify the ABI in the probe.
    subprocess.run(
        ["arm-none-eabi-gcc", "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft",
         "-std=c11", "-Os", "-ffreestanding", "-Wall", "-Wextra", "-Werror",
         "-pedantic", "-Isrc", "-c", "tools/arm_memory_probe.c", "-o", str(probe)],
        check=True,
    )
    raw_sections = run("arm-none-eabi-objdump", "-h", str(archive))
    raw_layout = run("arm-none-eabi-nm", "-P", "-S", "--radix=d", str(probe))
    symbols = run("arm-none-eabi-nm", "-S", "--size-sort", "--reverse-sort", "--radix=d", str(archive))
    (audit / "memory-sections.txt").write_text(raw_sections)
    (audit / "memory-layout-symbols.txt").write_text(raw_layout)
    (audit / "memory-function-symbols.txt").write_text(symbols)
    (audit / "memory-size.txt").write_text(run("arm-none-eabi-size", "-A", str(archive)))

    members = sections(raw_sections)
    layouts = layout(raw_layout)
    totals = {key: sum(item[key] for item in members.values()) for key in GROUPS}
    flash = sum(totals[key] for key in ("text", "rodata", "data", "other_flash"))
    ram = sum(totals[key] for key in ("data", "bss", "other_ram"))
    ctx = layouts["as608_t"]["size"]
    summary = {"members": members, "totals": totals, "layouts": layouts,
               "core_flash_section_bytes": flash, "core_static_ram_bytes": ram,
               "one_context_bytes": ctx, "two_context_bytes": 2 * ctx}
    (audit / "memory-summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    print("ARM MEMORY SUMMARY (bytes; archive section sums, not final firmware)")
    for member, amounts in members.items():
        print(member, json.dumps(amounts))
    print("TOTAL", json.dumps(totals))
    print(f"Core Flash section sum: {flash}; Core static RAM section sum: {ram}")
    for name, record in layouts.items():
        print(f"sizeof {name}: {record['size']}; alignment={record.get('alignment', '-')}; "
              f"direct padding={record.get('padding', '-')}")
    print(f"One context: {ctx}; Two contexts: {2 * ctx}")
    process = [line for line in symbols.splitlines() if line.split()[-1:] == ["as608_process"]]
    if len(process) != 1:
        raise ValueError("Expected one as608_process size symbol")
    print("as608_process symbol:", process[0])
    print("Excludes application/port buffers, stack, runtime library and link padding.")


if __name__ == "__main__":
    main()
