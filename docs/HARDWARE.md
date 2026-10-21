# Hardware guide

TPC2 Next supports three backends. The Pico USB bridge is the recommended one
because it works on modern Windows, Linux and macOS and needs no kernel driver.

| Backend | Windows | Linux | macOS | Notes |
|---------|---------|-------|-------|-------|
| Pico USB bridge | ✅ | ✅ | ✅ | Recommended; no driver, no admin rights |
| Legacy parallel port | ✅ InpOut | ✅ ioperm/`/dev/port` | ❌ | Needs a real SPP/FIFO port |
| Demo device | ✅ | ✅ | ✅ | No hardware |

## 1. Pico USB bridge

### Parts

- Raspberry Pi Pico (RP2040) or a compatible RP2040 board.
- 2 × 74LVC245 (or 74AHCT245) octal bus transceivers. One for the Pico→V64
  outputs, one for the V64→Pico inputs.
- 1 × DB25 male connector and a full-pin DB25 male-to-male cable.
- 2 × 4.7 kΩ resistors (pull-ups on AUTOFD and SELECTIN).
- Protoboard, headers, USB cable.
- Optional: 100 nF decoupling capacitors near the level shifters.

The RP2040 is **3.3 V only**. Do not connect its GPIOs directly to the V64's
5 V parallel signals.

### DB25 pinout

| DB25 | Signal | Direction | Pico default |
|------|--------|-----------|--------------|
| 1 | nSTROBE | Pico → V64 | GP8 |
| 2 | D0 | Pico → V64 | GP0 |
| 3 | D1 | Pico → V64 | GP1 |
| 4 | D2 | Pico → V64 | GP2 |
| 5 | D3 | Pico → V64 | GP3 |
| 6 | D4 | Pico → V64 | GP4 |
| 7 | D5 | Pico → V64 | GP5 |
| 8 | D6 | Pico → V64 | GP6 |
| 9 | D7 | Pico → V64 | GP7 |
| 10 | nACK | V64 → Pico | GP13 |
| 11 | BUSY | V64 → Pico | GP12 |
| 12 | PE | V64 → Pico | GP14 |
| 13 | SLCT | V64 → Pico | GP15 |
| 14 | nAUTOFD | open drain | GP9 |
| 15 | nERROR | V64 → Pico | GP16 |
| 16 | INIT | Pico → V64 | GP10 |
| 17 | nSELECTIN | open drain | GP11 |
| 18–25 | GND | common | GND |

Pin numbers and GPIO assignments can be changed in `firmware/pico/board.h`.
Keep D0–D7 contiguous if you change the base pin, because the firmware writes
them as one 8-bit mask.

### Level shifters

Use a 74LVC245 with `VCCA = 3.3 V`, `VCCB = 5 V`, `DIR` set for the required
direction and `OE` tied low. Do not use a simple resistor divider on outputs;
the V64 needs clean edges.

For AUTOFD and SELECTIN, use an open-drain arrangement with a 4.7 kΩ pull-up to
5 V. The firmware switches the pin between output-low and input (released). This
lets the V64 pull the line low for the old handshake without contention.

### Power and ground

- Power the Pico and level shifters from USB.
- Connect all grounds (Pico, shifters, DB25 pins 18–25) together.
- Do not feed 5 V from the V64 into the Pico's VBUS or 3V3 rail.
- If the V64 and PC are on different outlets, a common ground through the
  parallel cable is still required; use a proper full-pin cable.

### Firmware

See [`../firmware/pico/README.md`](../firmware/pico/README.md) for the build and
flashing steps. After flashing, the Pico appears as a USB serial device. Select
that port in the GUI or pass it to `tpc2cli --port`.

### Verifying the bridge

1. Connect the bridge to USB only (no V64).
2. Start the GUI, choose **USB bridge**, select the port and press **Connect**.
   You should see "USB bridge (…, fw tpc2-pico 1.0.0)" and then a handshake
   timeout, which is expected without a V64.
3. Connect the V64, power it, put it in transfer mode and connect again.
4. The activity log shows the detected handshake mode.

## 2. Legacy parallel port

A real SPP or EPP-capable LPT port is required. Most USB-to-parallel printer
adapters only implement a printer class and cannot toggle individual lines; they
will not work.

### Windows

TPC2 Next loads `inpoutx64.dll` (64-bit) or `inpout32.dll` (32-bit) at runtime
and uses its `Inp32`/`Out32` functions.

1. Download the InpOut binaries (for example from the InpOut project).
2. Run the installer as administrator to install the kernel driver.
3. Copy `inpoutx64.dll` next to `tpc2.exe` (or `tpc2cli.exe`), or into
   `C:\Windows\System32`.
4. Reboot if the driver requests it.

The backend refuses to open if the driver is not running, with a message that
points at the DLL/driver. `tpc2cli ports` shows whether the driver was found.

### Linux

The backend tries `ioperm(base, 3, 1)` first. If that fails (common on modern
kernels without `CAP_SYS_RAWIO`), it falls back to `/dev/port`.

- Run as root **or** grant access to `/dev/port` (for example a udev rule or an
  ACL) and keep the process privileged enough for port I/O.
- Check the port address with `tpc2cli ports`; the defaults are `0x378` (LPT1),
  `0x278` (LPT2) and `0x3BC` (LPT3).
- Some distributions block `ioperm` via `CONFIG_IO_STRICT_DEVMEM`; `/dev/port`
  may still work.

### macOS

macOS has no supported legacy parallel-port path. Use the Pico bridge.

## 3. Cabling and signal integrity

- Use a short (under 2 m) full-pin DB25 cable. Cheap printer cables sometimes
  omit ground returns or connect only a subset of pins.
- Keep the level shifters close to the DB25 connector.
- If transfers are unreliable, lower the cable length before changing software
  timing.
- The V64's parallel interface is 5 V TTL. Never exceed 5 V on any signal.

## 4. Troubleshooting matrix

| Symptom | Likely cause | Action |
|---------|--------------|--------|
| No serial port | Missing USB serial driver / cable | Try another cable, install CH340/CP210x driver if needed, press Refresh |
| Handshake timeout on bridge | V64 not in transfer mode, wrong cable, no common ground | Check V64 screen, cable, grounds; try Old/New handshake |
| Handshake works, transfer stalls | Open-drain pull-ups missing, long cable, weak 5 V | Add 4.7 kΩ pull-ups, shorten cable, check V64 PSU |
| Works on demo but not LPT | InpOut driver not running / no port permission | See the Windows/Linux sections above |
| Random CRC errors on bridge | USB power noise, bad ground, firmware mismatch | Use a powered hub, check firmware version, re-flash |
| OSD text rejected | Non-ASCII text or `0xFF` | Use printable ASCII only |

## 5. Safety checklist

- [ ] V64 and Pico grounds connected.
- [ ] All Pico→V64 signals go through a level shifter.
- [ ] AUTOFD and SELECTIN have 4.7 kΩ pull-ups to 5 V.
- [ ] No 5 V fed back into the Pico.
- [ ] DB25 pinout checked against the table above.
- [ ] V64 in transfer mode before connecting.
- [ ] Demo backend tested first.
