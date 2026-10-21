# TPC2 Next — Pico USB bridge firmware

This firmware turns a Raspberry Pi Pico (RP2040) into the recommended parallel
interface for a Doctor V64. The Pico performs the time-critical TPC/GD6
handshake locally and the desktop application talks to it over USB CDC using a
small SLIP + CRC-16 protocol.

The host does **not** need a parallel port, a kernel driver, or administrator
rights when this bridge is used.

## Why a microcontroller?

USB-to-parallel adapters cannot bit-bang the V64 handshake. The V64 protocol
polls individual status and control lines and relies on tight timing, so the
bridge has to drive the DB25 pins directly. A Pico is fast, cheap, 3.3 V, and
has enough GPIOs for the full SPP interface.

## Wiring

The Pico is **not** 5 V tolerant. Use level shifters between the Pico and the
V64's 5 V parallel signals:

- 74LVC245 (or 74AHCT245) for Pico → V64: data D0–D7, STROBE, AUTOFD, INIT,
  SELECTIN.
- 74LVC245 (direction V64 → Pico) or a 74LVC244 for BUSY, ACK, PE, SLCT, ERROR.
- 4.7 kΩ pull-ups on AUTOFD (pin 14) and SELECTIN (pin 17), because those two
  control lines are open drain in both directions.
- Common ground between the Pico, the level shifters and the V64.
- Power the Pico and the level shifters from USB. Do not back-feed the V64.

Default pin map (`board.h`, override with `-D` if needed):

| Pico GPIO | DB25 pin | Signal | Direction |
|-----------|----------|--------|-----------|
| GP0–GP7 | 2–9 | D0–D7 | Pico → V64 |
| GP8 | 1 | nSTROBE | Pico → V64 |
| GP9 | 14 | nAUTOFD | open drain, bidirectional |
| GP10 | 16 | INIT | Pico → V64 |
| GP11 | 17 | nSELECTIN | open drain, bidirectional |
| GP12 | 11 | BUSY | V64 → Pico |
| GP13 | 10 | nACK | V64 → Pico |
| GP14 | 12 | PE | V64 → Pico |
| GP15 | 13 | SLCT | V64 → Pico |
| GP16 | 15 | nERROR | V64 → Pico |
| GND | 18–25 | GND | common |

The firmware reproduces the PC's SPP register layout, including the inverting
buffers on STROBE/AUTOFD/ERROR/ACK and the non-inverted INIT bit, so the host
code behaves exactly as it does on a real LPT port.

## Build

Requirements:

- Pico SDK 2.1 or newer (with submodules, for TinyUSB).
- Arm GNU toolchain (`arm-none-eabi-gcc`).
- CMake 3.24+, Ninja and Python 3.

```sh
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk
export PICO_SDK_PATH="$PWD/pico-sdk"

cmake -S firmware/pico -B build/pico -G Ninja -DPICO_BOARD=pico
cmake --build build/pico
```

The result is `build/pico/tpc2_bridge.uf2`. Hold BOOTSEL while plugging in the
Pico, copy the UF2 to the `RPI-RP2` drive, and the bridge reboots into the
firmware. The USB serial device that appears is the one to select in the
desktop application or pass to `tpc2cli --port`.

The build generates the UF2 with a small Python converter
(`bin2uf2.py`) as well as through picotool, so a broken or unavailable picotool
installation does not block firmware builds. If picotool cannot be built on
your host, add `-DPICO_NO_PICOTOOL=1`:

```sh
cmake -S firmware/pico -B build/pico -G Ninja \
  -DPICO_BOARD=pico -DPICO_NO_PICOTOOL=1
cmake --build build/pico
```

On Windows/MSYS2, install the Arm toolchain and point the build at it:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-arm-none-eabi-gcc
export PATH=/ucrt64/bin:/mingw64/bin:/usr/bin:$PATH
export PICO_SDK_PATH=C:/Users/<you>/pico-sdk
```

The firmware is known to compile with Pico SDK 2.1.1, Arm GCC 16.2.0 and CMake
4.4.4. It has not been run on physical hardware in this environment; see the
safety and validation notes below.

The firmware is built separately from the desktop application because it needs
the Arm cross-toolchain. Re-run the commands above whenever the firmware
changes; the GUI/CLI build does not depend on the Pico SDK.

## Protocol summary

Frames use SLIP framing (`0xC0` delimiter, `0xDB 0xDC` / `0xDB 0xDD` escapes)
and a CRC-16/CCITT-FALSE over the body:

```
request:  [cmd]      [seq] [status=0] [len_lo] [len_hi] [payload] [crc_lo] [crc_hi]
response: [cmd|0x80] [seq] [status]   [len_lo] [len_hi] [payload] [crc_lo] [crc_hi]
```

Commands: `Ping=1`, `Sync=2`, `SetMode=3`, `Write=4`, `Read=5`,
`CheckSync=6`, `Reset=7`, `Info=8`. The full details are in
`docs/PROTOCOL.md`.

`Write` and `Read` payloads are limited to 8 KiB per frame. The desktop
application splits larger transfers automatically. The even/odd byte phase is
kept in the STB bit of the emulated control register, exactly like the original
TPC code.

## Status LED

If `PICO_DEFAULT_LED_PIN` exists, the LED toggles once per handled frame. It is
useful for confirming that the host and firmware are talking before attaching
the V64.

## Validation

The firmware compiles cleanly and the UF2 container is checked by the build (the
script verifies block count, addresses, family ID and payload length). The
parallel state machine mirrors the host-side simulator, including the inverted
SPP status/control bits and the open-drain AUTOFD/SELECTIN behavior.

It has **not** been flashed onto a Pico or connected to a real V64 in this
environment. Before relying on it:

1. Verify the 3.3 V/5 V level shifting and common ground.
2. Connect the Pico over USB only and confirm the serial port appears.
3. Connect the V64 and check the status LED toggles when the host pings the
   bridge.
4. Try a small upload and an OSD message before a full DRAM dump.

## Safety

- Check the V64's DB25 pinout before connecting. A wrong +5 V/GND pairing can
  damage the Pico or the V64.
- Use a full-pin DB25 cable; some "parallel printer" cables omit ground pins.
- Never connect a 3.3 V GPIO directly to a 5 V V64 signal.
- Start with **Backend: Demo device** in the GUI to verify the software, then
  switch to the bridge backend.
