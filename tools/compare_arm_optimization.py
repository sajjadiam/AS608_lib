"""Compare the stage-64 baseline and current core under one ARM toolchain."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "dccbbf64c97d88fd16b21a2592abbe76e790c43b"
LEVELS = ("Os", "O2", "O3")


def run(*args: str, cwd: Path = ROOT) -> None:
    subprocess.run(args, cwd=cwd, check=True)


def measure(source: Path, build: Path, evidence: Path, level: str) -> dict:
    run("cmake", "-S", str(source), "-B", str(build),
        "-DCMAKE_TOOLCHAIN_FILE=" + str(source / "cmake/toolchains/arm-cortex-m3-gcc.cmake"),
        "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_C_FLAGS_MINSIZEREL=-" + level + " -DNDEBUG",
        "-DAS608_BUILD_TESTS=OFF", cwd=source)
    run("cmake", "--build", str(build), "--parallel", "2", cwd=source)
    for report in ("report_arm_memory.py", "report_arm_stack.py"):
        run("python3", str(source / "tools" / report), str(build), cwd=source)
    audit = build / "audit"
    shutil.copytree(audit, evidence, dirs_exist_ok=True)
    shutil.copyfile(build / "compile_commands.json", evidence / "compile_commands.json")
    with (evidence / "disassembly.txt").open("w", encoding="utf-8") as output:
        subprocess.run(
            ["arm-none-eabi-objdump", "-dr", str(build / "libas608.a")],
            stdout=output, check=True,
        )
    memory = json.loads((audit / "memory-summary.json").read_text())
    stack = json.loads((audit / "stack-summary.json").read_text())
    frames = stack["frames"]
    source_frames = [v["bytes"] for k, v in frames.items()
                     if "poll_download_source" in k]
    return {
        "flash_bytes": memory["core_flash_section_bytes"],
        "static_ram_bytes": memory["core_static_ram_bytes"],
        "context_bytes": memory["one_context_bytes"],
        "largest_frame_bytes": max(v["bytes"] for v in frames.values()),
        "source_frame_bytes": max(source_frames) if source_frames else None,
        "process_frame_bytes": frames["as608_process"]["bytes"],
        "process_known_path_bytes": stack["public_paths"]["as608_process"]["known_path_bytes"],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    toolchain = subprocess.check_output(["arm-none-eabi-gcc", "--version"], text=True)
    candidate = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                        text=True).strip()
    results = {"baseline": BASELINE, "candidate": candidate,
               "toolchain": toolchain.strip(), "builds": {}}
    with tempfile.TemporaryDirectory(prefix="as608-opt-") as temporary:
        work = Path(temporary)
        baseline = work / "baseline"
        run("git", "worktree", "add", "--detach", str(baseline), BASELINE)
        try:
            for level in LEVELS:
                for name, source in (("baseline", baseline), ("candidate", ROOT)):
                    key = name + "-" + level
                    results["builds"][key] = measure(
                        source, work / key, output / key, level)
                    print("OPT", key, json.dumps(results["builds"][key]), flush=True)
        finally:
            run("git", "worktree", "remove", "--force", str(baseline))
    (output / "comparison.json").write_text(json.dumps(results, indent=2) + "\n")
    for level in LEVELS:
        before = results["builds"]["baseline-" + level]
        after = results["builds"]["candidate-" + level]
        if before["context_bytes"] != after["context_bytes"]:
            raise SystemExit("Unexpected context ABI size change")
        if before["static_ram_bytes"] != after["static_ram_bytes"]:
            raise SystemExit("Unexpected static RAM change")


if __name__ == "__main__":
    main()
