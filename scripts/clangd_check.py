#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys
import os


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    cdb = root / "compile_commands.json"
    data = json.load(cdb.open())

    errors = 0
    for entry in data:
        if entry["file"].endswith(".S"):
            continue
        rel = pathlib.Path(entry["file"]).resolve().relative_to(root)
        cmd = ["clangd", "--log=error", f"--check={rel}"]
        result = subprocess.run(cmd, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

        if result.returncode == 0 and not result.stdout.strip():
            if os.environ.get("VERBOSE"):
                print(f"[OK] {rel}")
        else:
            print(f"[FAIL] {rel}")
            if result.stdout:
                print(result.stdout.rstrip())
        errors |= result.returncode
    return errors


if __name__ == "__main__":
    sys.exit(main())
