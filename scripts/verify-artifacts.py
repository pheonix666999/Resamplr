#!/usr/bin/env python3
"""Verify unsigned Milestone 0 development archives."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import zipfile


def verify_archive(path: pathlib.Path) -> list[str]:
    failures: list[str] = []
    if not path.is_file() or path.stat().st_size == 0:
        return [f"missing or empty archive: {path}"]
    try:
        with zipfile.ZipFile(path) as archive:
            names = set(archive.namelist())
            unsigned = next((name for name in names if name.endswith("UNSIGNED.txt")), None)
            manifest = next((name for name in names if name.endswith("build-manifest.json")), None)
            if unsigned is None:
                failures.append(f"{path}: missing UNSIGNED.txt")
            if manifest is None:
                failures.append(f"{path}: missing build-manifest.json")
            else:
                data = json.loads(archive.read(manifest).decode("utf-8"))
                if data.get("signed") is not False:
                    failures.append(f"{path}: manifest must contain signed=false")
    except (OSError, zipfile.BadZipFile, json.JSONDecodeError) as error:
        failures.append(f"{path}: {error}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["milestone0"], required=True)
    parser.add_argument("--platform", choices=["windows", "macos", "all"], required=True)
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("artifacts"))
    arguments = parser.parse_args()

    patterns = {
        "windows": "**/PadFlow-Windows-x64-Development-Unsigned.zip",
        "macos": "**/PadFlow-macOS-Universal-Development-Unsigned.zip",
    }
    platforms = patterns if arguments.platform == "all" else {arguments.platform: patterns[arguments.platform]}
    failures: list[str] = []
    for platform, pattern in platforms.items():
        matches = list(arguments.root.glob(pattern))
        if len(matches) != 1:
            failures.append(f"expected one {platform} archive, found {len(matches)}")
        for match in matches:
            failures.extend(verify_archive(match))
    if failures:
        print("Artifact verification failed:", *failures, sep="\n- ", file=sys.stderr)
        return 1
    print("Milestone 0 artifact verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

