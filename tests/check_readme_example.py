"""Syntax-check the actual C application fragment in README.md."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    readme = (root / "README.md").read_text(encoding="utf-8")
    blocks = re.findall(r"^\x60\x60\x60c\s*\n(.*?)^\x60\x60\x60\s*$",
                        readme, re.MULTILINE | re.DOTALL)
    if not blocks:
        raise SystemExit("No C code block found in README.md")
    with tempfile.TemporaryDirectory() as tmp:
        for index, block in enumerate(blocks, start=1):
            source = Path(tmp) / f"readme_{index}.c"
            source.write_text(block, encoding="utf-8")
            subprocess.run(
                [args.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-pedantic", "-I", str(root / "src"), "-fsyntax-only",
                 str(source)],
                check=True,
            )
    print(f"README C fragments syntax-checked: {len(blocks)}")


if __name__ == "__main__":
    main()
