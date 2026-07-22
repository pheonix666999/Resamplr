#!/usr/bin/env python3
"""Verify that schema-v1 declarations and required documentation agree."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
REQUIRED = {
    "Source/App/ProductInfoGenerated.h.in": "PADFLOW_SCHEMA_VERSION_VALUE 1",
    "docs/PROJECT_FORMAT.md": "Schema v1",
    "TEST_PLAN.md": "SAVE-001",
    "REFERENCE_GAPS.md": "BLOCKED_REFERENCE_ASSET",
}


def main() -> int:
    failures: list[str] = []
    for relative, marker in REQUIRED.items():
        path = ROOT / relative
        if not path.is_file():
            failures.append(f"missing {relative}")
            continue
        if marker not in path.read_text(encoding="utf-8"):
            failures.append(f"{relative} lacks {marker!r}")
    if failures:
        print("Schema verification failed:", *failures, sep="\n- ", file=sys.stderr)
        return 1
    print("Schema/document authority verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

