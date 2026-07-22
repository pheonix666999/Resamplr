#!/usr/bin/env python3
"""Conservative scan of real-time callback bodies for obvious forbidden operations."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
CALLBACK = re.compile(
    r"\b(processBlock|audioDeviceIOCallback|renderNextBlock|beginAudioWrite|"
    r"commitAudioWrite|acknowledgeAudioEpoch|tryPush|tryPop)\s*\("
)
FORBIDDEN = re.compile(
    r"\b(new|delete|malloc|calloc|realloc|fopen|ofstream|FileOutputStream|"
    r"AudioFormatWriter|lock_guard|unique_lock|sleep|Logger|MessageManager)\b"
)


def function_body(text: str, start: int) -> str:
    brace = text.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace : index + 1]
    return text[brace:]


def main() -> int:
    failures: list[str] = []
    callbacks = 0
    paths = list((ROOT / "Source").rglob("*.cpp")) + list((ROOT / "Source").rglob("*.h"))
    for path in sorted(paths):
        text = path.read_text(encoding="utf-8")
        for match in CALLBACK.finditer(text):
            callbacks += 1
            body = function_body(text, match.start())
            forbidden = FORBIDDEN.search(body)
            if forbidden:
                failures.append(f"{path.relative_to(ROOT)}: {forbidden.group(0)}")

    if failures:
        print("Potential real-time violations:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"Real-time source scan passed ({callbacks} callback bodies inspected).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
