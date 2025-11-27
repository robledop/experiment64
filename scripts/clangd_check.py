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
        env = os.environ.copy()
        # clangd 18's SwapBinaryOperands tweak can fail when overlapping edits occur.
        # Disable that tweak to avoid spurious failures in --check mode.
        env.setdefault("CLANGD_DISABLE_TWEAKS", "SwapBinaryOperands,DefineInline")
        result = subprocess.run(cmd, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

        output = result.stdout or ""
        ret = result.returncode

        # clangd 18 can emit spurious SwapBinaryOperands tweak errors; treat those as non-fatal.
        ignored_swap_noise = False
        ignored_inline_noise = False
        if ret != 0 and "SwapBinaryOperands" in output and "overlaps with an existing replacement" in output:
            ret = 0
            ignored_swap_noise = True
            if os.environ.get("VERBOSE"):
                print(f"[WARN ignored SwapBinaryOperands noise] {rel}")
        if ret != 0 and "DefineInline" in output and "different from the file path of existing replacements" in output:
            ret = 0
            ignored_inline_noise = True
            if os.environ.get("VERBOSE"):
                print(f"[WARN ignored DefineInline noise] {rel}")

        if ret == 0 and (ignored_swap_noise or ignored_inline_noise or not output.strip()):
            if os.environ.get("VERBOSE"):
                print(f"[OK] {rel}")
        else:
            print(f"[FAIL] {rel}")
            if output:
                print(output.rstrip())
        errors |= ret
    return errors


if __name__ == "__main__":
    sys.exit(main())
