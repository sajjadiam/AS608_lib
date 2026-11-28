"""Report compiled stack frames and known direct-call paths; not a WCET or stack proof."""

import argparse
from functools import lru_cache
import json
from pathlib import Path
import re
import shutil

NODE = re.compile(r'^node:\s*\{\s*title:\s*"([^"]+)"\s+label:\s*"((?:\\.|[^"\\])*)"', re.M)
EDGE = re.compile(r'^edge:\s*\{\s*sourcename:\s*"([^"]+)"\s+targetname:\s*"([^"]+)"', re.M)
FRAME = re.compile(r'\\n(\d+) bytes \(([^)]+)\)')
PUBLIC = re.compile(r'^(?:as608_status_t|as608_progress_t|bool)\s+(as608_\w+)\s*\([^;]*\);', re.M)


def analyze(graphs: list[Path], usage: list[Path], public: list[str]) -> dict:
    frames: dict[str, dict] = {}
    edges: dict[str, set[str]] = {}
    for path in graphs:
        raw = path.read_text(encoding="utf-8")
        nodes = NODE.findall(raw)
        links = EDGE.findall(raw)
        if len(nodes) != len(re.findall(r'^node:', raw, re.M)):
            raise ValueError(f"Unparsed nodes: {path}")
        if len(links) != len(re.findall(r'^edge:', raw, re.M)):
            raise ValueError(f"Unparsed edges: {path}")
        if not nodes:
            raise ValueError(f"Empty call graph: {path}")
        for name, label in nodes:
            match = FRAME.search(label)
            if match:
                size, qualifier = match.groups()
                record = {"bytes": int(size), "qualifier": qualifier}
                if name in frames and frames[name] != record:
                    raise ValueError(f"Conflicting frame: {name}")
                frames[name] = record
        for source, target in links:
            edges.setdefault(source, set()).add(target)

    # Independently retain the compiler's .su inventory and reject
    # unbounded dynamic frames, even if absent from the graph.
    usage_rows = []
    for path in usage:
        for line in path.read_text(encoding="utf-8").splitlines():
            location, size, qualifier = line.rsplit("\t", 2)
            if qualifier not in ("static", "dynamic,bounded"):
                raise ValueError(f"Unbounded or unknown stack usage: {line}")
            usage_rows.append({"location": location, "bytes": int(size),
                               "qualifier": qualifier})
    if not usage_rows or not frames:
        raise ValueError("No compiler stack records")
    if any(frame["qualifier"] not in ("static", "dynamic,bounded")
           for frame in frames.values()):
        raise ValueError("Unbounded or unknown graph stack frame")
    missing = sorted(set(public) - frames.keys())
    if missing:
        raise ValueError(f"Missing public graph roots: {missing}")

    # For a DAG, sum active frames along the largest known direct path.
    # Counting tail calls as ordinary calls is conservative for that
    # path. Unknown callees are boundaries, never assumed zero in the
    # complete application budget. Reject recursion instead of claiming
    # a finite bound.
    visiting: set[str] = set()

    @lru_cache(maxsize=None)
    def longest(name: str) -> tuple[int, tuple[str, ...], frozenset[str]]:
        if name not in frames:
            return 0, (), frozenset({name})
        if name in visiting:
            raise ValueError(f"Recursive known call path: {name}")
        visiting.add(name)
        best = (0, ())
        unknown: set[str] = set()
        for target in sorted(edges.get(name, ())):
            size, chain, boundary = longest(target)
            unknown.update(boundary)
            if size > best[0]:
                best = (size, chain)
        visiting.remove(name)
        return frames[name]["bytes"] + best[0], (name,) + best[1], frozenset(unknown)

    roots = {}
    for name in public:
        size, chain, unknown = longest(name)
        roots[name] = {"known_path_bytes": size, "path": list(chain),
                       "unresolved_callees": sorted(unknown)}
    # Also inspect all compiled frames, not just reachable public ones.
    for name in frames:
        longest(name)
    return {"frames": frames, "direct_edges": {k: sorted(v) for k, v in edges.items()},
            "stack_usage_records": usage_rows, "public_paths": roots,
            "limitations": ["Compiler graph only; not a binary-level complete call graph.",
                            "External/runtime and indirect callback frames excluded.",
                            "Interrupts, caller frames and exception stacking excluded."]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    build = args.build_dir
    audit = build / "audit"
    audit.mkdir(parents=True, exist_ok=True)
    graphs = sorted((build / "CMakeFiles").rglob("*.ci"))
    usage = sorted((build / "CMakeFiles").rglob("*.su"))
    # Compiler-identification/try-compile artifacts are not core objects.
    names = {"as608.c", "as608_packet.c", "as608_transaction.c"}
    def is_core(path: Path) -> bool:
        return any(path.name.startswith(name + ".") or
                   path.name == name.removesuffix(".c") + path.suffix
                   for name in names)
    graphs = [path for path in graphs if is_core(path)]
    usage = [path for path in usage if is_core(path)]
    if len(graphs) != 3 or len(usage) != 3:
        raise ValueError(f"Expected three core graphs and usage files: {graphs}, {usage}")
    raw_dir = audit / "stack-raw"
    raw_dir.mkdir(exist_ok=True)
    for path in graphs + usage:
        shutil.copyfile(path, raw_dir / path.name)
    root = Path(__file__).resolve().parents[1]
    public = PUBLIC.findall((root / "src/as608.h").read_text(encoding="utf-8"))
    if len(public) != 46 or len(set(public)) != 46:
        raise ValueError("Unexpected public API inventory")
    report = analyze(graphs, usage, public)
    (audit / "stack-summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print("ARM STACK SUMMARY (compiler-reported bytes; known direct paths only)")
    frames = report["frames"]
    roots = report["public_paths"]
    for name, frame in sorted(frames.items(), key=lambda item: -item[1]["bytes"])[:10]:
        print(f"FRAME {name}: {frame['bytes']} ({frame['qualifier']})")
    for name, info in sorted(roots.items(), key=lambda item: -item[1]["known_path_bytes"])[:10]:
        print(f"PATH {name}: {info['known_path_bytes']} bytes; "
              f"{' -> '.join(info['path'])}; unresolved={info['unresolved_callees']}")
    print("PROCESS", json.dumps(roots["as608_process"]))
    print("Excluded: external/runtime, indirect callbacks, interrupts, caller frames.")


if __name__ == "__main__":
    main()
