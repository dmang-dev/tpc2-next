#!/usr/bin/env python3
"""Convert a raw RP2040 flash image to a UF2 file.

The Pico SDK normally uses picotool for this. Some toolchains cannot build
picotool, so this small dependency-free converter keeps the firmware build
self-contained.

Usage:
    python bin2uf2.py tpc2_bridge.bin tpc2_bridge.uf2
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

MAGIC_START0 = 0x0A324655
MAGIC_START1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
FLAG_FAMILY_PRESENT = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
RP2040_FLASH_BASE = 0x10000000
PAYLOAD_SIZE = 256
BLOCK_SIZE = 512


def convert(source: Path, destination: Path) -> int:
    image = source.read_bytes()
    if not image:
        raise SystemExit(f"{source} is empty")

    padded = image + b"\x00" * (-len(image) % PAYLOAD_SIZE)
    blocks = len(padded) // PAYLOAD_SIZE

    with destination.open("wb") as out:
        for index in range(blocks):
            payload = padded[index * PAYLOAD_SIZE : (index + 1) * PAYLOAD_SIZE]
            header = struct.pack(
                "<IIIIIIII",
                MAGIC_START0,
                MAGIC_START1,
                FLAG_FAMILY_PRESENT,
                RP2040_FLASH_BASE + index * PAYLOAD_SIZE,
                PAYLOAD_SIZE,
                index,
                blocks,
                RP2040_FAMILY_ID,
            )
            block = header + payload
            block += b"\x00" * (BLOCK_SIZE - 4 - len(block))
            block += struct.pack("<I", MAGIC_END)
            out.write(block)

    return blocks


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="raw .bin file")
    parser.add_argument("destination", type=Path, help="output .uf2 file")
    args = parser.parse_args()

    blocks = convert(args.source, args.destination)
    print(f"Wrote {args.destination} ({blocks} blocks)")


if __name__ == "__main__":
    main()
