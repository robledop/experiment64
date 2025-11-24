#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys


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
        cmd = ["clang-tidy", "-p", str(root), str(rel_path)]
        print("==>", " ".join(cmd))
        result = subprocess.run(cmd, cwd=root)
        errors |= result.returncode
    return errors


if __name__ == "__main__":
    sys.exit(main())
