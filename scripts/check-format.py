#!/usr/bin/env python3
"""Run clang-format in check-only mode over project-owned C/C++ files."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    executable = shutil.which("clang-format")
    if executable is None:
        print("ERROR: clang-format is not available on PATH", file=sys.stderr)
        return 2

    files = sorted(
        path
        for parent in (ROOT / "Source", ROOT / "Tests")
        for path in parent.rglob("*")
        if path.suffix in {".cpp", ".h", ".hpp"} or path.name.endswith(".h.in")
    )
    if not files:
        print("ERROR: no project-owned C/C++ files found", file=sys.stderr)
        return 1

    command = [executable, "--dry-run", "--Werror", *map(str, files)]
    print("Running:", " ".join(command))
    return subprocess.run(command, cwd=ROOT, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
