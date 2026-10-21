#!/usr/bin/env python3
"""Exercise the CLI against the deterministic demo backend."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def run(executable: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(executable), *arguments],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def require(result: subprocess.CompletedProcess[str], code: int, text: str) -> None:
    combined = (result.stdout + result.stderr).lower()
    if result.returncode != code:
        raise AssertionError(
            f"command returned {result.returncode}, expected {code}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if text.lower() not in combined:
        raise AssertionError(
            f"output did not contain {text!r}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()

    executable = args.exe.resolve()
    if not executable.is_file():
        print(f"CLI executable not found: {executable}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="tpc2-cli-") as temporary:
        root = Path(temporary)
        sample = root / "sample.bin"
        sample.write_bytes(bytes(range(256)) * 4)
        output = root / "read.bin"

        require(run(executable, "--backend", "demo", "info"), 0, "demo")
        require(run(executable, "--backend", "demo", "ports"), 0, "serial ports")
        require(
            run(executable, "--backend", "demo", "upload", str(sample)),
            0,
            "sent SAMPLE.BIN",
        )
        require(
            run(
                executable,
                "--backend",
                "demo",
                "read",
                "0x00100000",
                "64",
                str(output),
            ),
            0,
            "read 64 bytes",
        )
        if output.stat().st_size != 64:
            raise AssertionError(
                f"read command wrote {output.stat().st_size} bytes, expected 64"
            )

        require(
            run(
                executable,
                "--backend",
                "demo",
                "write",
                "0x00100000",
                str(sample),
            ),
            0,
            "wrote 1024 bytes",
        )
        require(
            run(executable, "--backend", "demo", "call", "0xFFE3"),
            0,
            "called 0xFFE3",
        )
        require(
            run(
                executable,
                "--backend",
                "demo",
                "print",
                "2",
                "4",
                "HELLO",
                "V64",
            ),
            0,
            "OSD text sent",
        )
        require(
            run(executable, "--backend", "not-a-backend", "info"),
            1,
            "backend must be",
        )

    print("CLI integration checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
