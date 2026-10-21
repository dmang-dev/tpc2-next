#!/usr/bin/env python3
"""Check tracked text files for basic formatting problems."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

BINARY_SUFFIXES = {
    ".bin",
    ".dll",
    ".elf",
    ".exe",
    ".dmg",
    ".gif",
    ".icns",
    ".ico",
    ".jpg",
    ".jpeg",
    ".png",
    ".uf2",
    ".zip",
}


def tracked_files() -> list[Path]:
    output = subprocess.check_output(["git", "ls-files", "-z"])
    return [Path(item) for item in output.decode("utf-8").split("\0") if item]


def main() -> int:
    failures = 0
    for path in tracked_files():
        if path.suffix.lower() in BINARY_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            print(f"{path}: not valid UTF-8", file=sys.stderr)
            failures += 1
            continue

        for number, line in enumerate(text.splitlines(), start=1):
            if line.rstrip() != line:
                print(f"{path}:{number}: trailing whitespace", file=sys.stderr)
                failures += 1
            if "\t" in line:
                print(f"{path}:{number}: tab character", file=sys.stderr)
                failures += 1

        if not text.endswith("\n"):
            print(f"{path}: missing final newline", file=sys.stderr)
            failures += 1

    if failures:
        print(f"{failures} formatting problem(s) found", file=sys.stderr)
        return 1
    print("Formatting checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
