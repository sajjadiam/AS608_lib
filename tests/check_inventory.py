"""Check CTest registration against the independent on-disk test inventory."""

import argparse
import collections
import json
from pathlib import Path
import subprocess

COMPILE_ONLY = {"types", "packet_header", "port_header", "transaction_header"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    expected: dict[str, str] = {}
    for group in ("unit", "integration"):
        for source in sorted((root / group).glob("test_as608_*.c")):
            name = source.stem.removeprefix("test_as608_")
            if name in COMPILE_ONLY:
                continue
            names = ("public_c", "public_cpp") if name == "header" else (name,)
            for item in names:
                if item in expected:
                    raise SystemExit(f"Duplicate test source: {item}")
                expected[item] = group

    report = subprocess.run(
        ["ctest", "--test-dir", str(args.build_dir), "--show-only=json-v1"],
        check=True,
        capture_output=True,
        text=True,
    )
    tests = json.loads(report.stdout)["tests"]
    counts = collections.Counter(test["name"] for test in tests)
    missing = sorted(expected.keys() - counts.keys())
    extra = sorted(counts.keys() - expected.keys())
    duplicates = sorted(name for name, count in counts.items() if count != 1)
    wrong_labels = []
    for test in tests:
        name = test["name"]
        properties = {p["name"]: p["value"] for p in test.get("properties", [])}
        if name in expected and expected[name] not in properties.get("LABELS", []):
            wrong_labels.append(name)
    if not expected or missing or extra or duplicates or wrong_labels:
        raise SystemExit(
            f"CTest inventory mismatch: missing={missing}, extra={extra}, "
            f"duplicates={duplicates}, wrong_labels={wrong_labels}"
        )
    print(f"CTest inventory verified: {len(tests)} registered executable tests")


if __name__ == "__main__":
    main()
