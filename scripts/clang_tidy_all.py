#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys


# Files/directories to skip from clang-tidy analysis
SKIP_PATTERNS = [
    "user/edit.c",      # Third-party editor code with many false positives
    "user/doom/",       # Doom port with many legacy code warnings
]


def should_skip(rel_path: pathlib.Path) -> bool:
    """Check if a file should be skipped based on skip patterns."""
    path_str = str(rel_path)
    for pattern in SKIP_PATTERNS:
        if path_str.startswith(pattern) or path_str == pattern:
            return True
    return False


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    cdb = root / "compile_commands.json"
    data = json.load(cdb.open())

    errors = 0
    allowed_suffixes = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}

    for entry in data:
        rel_path = pathlib.Path(entry["file"]).resolve().relative_to(root)
        if rel_path.suffix not in allowed_suffixes:
            continue
        if should_skip(rel_path):
            print(f"==> Skipping {rel_path}")
            continue
        cmd = ["clang-tidy", "-p", str(root), str(rel_path)]
        print("==>", " ".join(cmd))
        result = subprocess.run(cmd, cwd=root)
        errors |= result.returncode
    return errors


if __name__ == "__main__":
    sys.exit(main())
