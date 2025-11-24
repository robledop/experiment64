#!/usr/bin/env python3
import json
import pathlib
import shutil
import subprocess
import sys


def run_all_with_loop(root: pathlib.Path, data: list[dict]) -> int:
    errors = 0
    for entry in data:
        if entry["file"].endswith(".S"):
            continue
        rel = pathlib.Path(entry["file"]).resolve().relative_to(root)
        cmd = ["clang-tidy", "-p", str(root), str(rel)]
        print("==>", " ".join(cmd))
        result = subprocess.run(cmd, cwd=root)
        errors |= result.returncode
    return errors


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    cdb = root / "compile_commands.json"
    data = json.load(cdb.open())

    if shutil.which("run-clang-tidy"):
        print("Running run-clang-tidy -p .")
        return subprocess.run(["run-clang-tidy", "-p", str(root)], cwd=root).returncode

    print("run-clang-tidy not found, invoking clang-tidy per file")
    return run_all_with_loop(root, data)


if __name__ == "__main__":
    sys.exit(main())
