# Protocol reference

This document describes the wire protocol implemented by TPC2 Next. It is
derived from the documented behaviour of the original TPC 2.0 and V64IO2
tools:

- TPC 2.0 upload/download and the parallel-port register protocol.
- V64IO2 GD6E remote-control commands.

The same code is used for real hardware (parallel port or Pico bridge) and for
the built-in simulator, so the simulator is a useful reference implementation.

## 1. Physical layer

The V64 uses the PC parallel port as a byte-wide link with handshaked control
and status lines. TPC2 Next reproduces the SPP register layout, not raw pin
levels, so all timings below are expressed in terms of the data/status/control
registers.

### Registers

| Register | Bits used | Meaning |
|----------|-----------|---------|
| Data (base+0) | D0–D7 | byte to send, or D7 = read direction |
| Status (base+1) | D7 BUSY, D6 ACK, D5 PE, D4 SLCT, D3 ERROR | device → PC |
| Control (base+2) | D3 SELECTIN, D2 INIT, D1 AUTOFD, D0 STROBE | PC → device |

The PC's status and control ports invert some pins. The important consequences
for software are:

- Status bit 7 is high when the `BUSY` **pin** is low.
- Status bit 3 is high when the `ERROR` **pin** is low.
- Control bit 0 high drives `STROBE` low.
- Control bit 1 high drives `AUTOFD` low.
- Control bit 2 high drives `INIT` high (this one is **not** inverted).
- Control bit 3 high drives `SELECTIN` low.

`AUTOFD` and `SELECTIN` are open-drain: either side may pull them low, and the
PC reads back the actual line level. The Pico firmware models this with a
pull-up and a switched output driver.

### Old and new handshakes

The V64 BIOS family has two incompatible synchronization sequences.

**Old** (pre-1.2x, `SLCT_IN`/`AUTO.FX`):

1. Wait until control bit 3 (`SELECTIN`) reads 0 (line high).
2. Write `0xAA` to data, then write control `0x00`.
3. Wait until control bit 3 reads 1 (V64 pulled the line low).
4. Write control `0x04`, wait until bit 3 returns to 0.
5. Repeat steps 2–4 with data `0x55`.

**New** (BIOS 1.2x+, `ERROR`/`SLCT`):

1. Wait until status bit 3 (`ERROR`) reads 1.
2. Write `0xA5` to data, then control `0x00`.
3. Wait until status bit 3 reads 0.
4. Write control `0x04`, wait until status bit 3 returns to 1.
5. Repeat steps 2–4 with data `0x5A`.

Auto mode tries old first and then new, resetting the interface between
attempts. The host reports the mode that actually succeeded.

### Writing bytes

TPC continues the even/odd phase across calls. The phase is stored in the
`STROBE` bit of the control register, so it survives a read or a separate
command.

- **Even byte:** wait until `AUTO.FX` (old) reads 0, or `SLCT` (new) reads 1;
  write the data byte; write control `0x05` (STROBE low).
- **Odd byte:** wait until `AUTO.FX` (old) reads 1, or `SLCT` (new) reads 0;
  write the data byte; write control `0x04` (STROBE high).

The next call derives the phase from control bit 0, exactly like the original
assembly.

### Reading bytes

1. Write data `0x80` (D7 = 1, direction = read).
2. Wait for status bit 7 high; the low nibble is status bits 3–6.
3. Write data `0x00`.
4. Wait for status bit 7 low; the high nibble is status bits 3–6.
5. Write data `0x80` again.
6. The byte is `low | (high << 4)`.

### Command acknowledgement

After a 4- or 5-byte command header the V64 may hold the transfer. `checkSync`
waits for:

- old mode: `(control & 3) == 3` or `(control & 3) == 0`, or
- new mode: `(control & 1) XOR ((status & 0x10) >> 4) != 0`,

then writes data `0x00` and waits for status bit 7 low. A failure here means the
V64 did not accept the command.

## 2. TPC 2.0 upload (`GD6R`)

Every file transfer starts with a fresh handshake.

```
host:  sync
host:  "GD6R" 0x00                 ; 5 bytes; the original always sends 0
host:  size_le32                    ; 4 bytes
host:  name[11]                     ; 8.3 name padded with spaces
host:  data[size]                   ; streamed in chunks
```

- `size` is a 32-bit little-endian byte count.
- `name` is 8 characters of base name followed by 3 characters of extension,
  each space-padded and uppercased. It is not NUL-terminated.
- The original TPC always sends a file counter byte of `0`; the V64 ignores it.
- There is no per-file acknowledgement in the upload direction. A timeout while
  writing means the V64 stopped accepting bytes.

## 3. TPC 2.0 download (`GD6W`)

```
host:  sync
host:  "GD6W"                       ; 4 bytes
host:  requested_name[11]           ; blank in TPC2 Next
host:  (V64 handshake: checkSync)
host:  read 1 byte                  ; number of files the V64 will send
repeat for each file:
  host: read 15 bytes              ; size_le32 + name[11]
  host: read size bytes            ; raw file data
```

The 11-byte request field is present in the original DOS tool. The V64 takes
the actual output names from the per-file headers, so TPC2 Next sends spaces,
but the field must not be omitted: the V64 has already parsed the command when
it starts the handshake.

The PC cannot ask the V64 to list files. The transfer must be started on the V64
front panel (`>>`).

## 4. GD6E extended commands (V64IO2)

These require V64 BIOS 1.30 or newer. All multi-byte arguments are **big-endian**
(unlike the TPC file headers, which are little-endian).

```
host: sync
host: "GD6E" <code>                 ; 5 bytes: R, W, C or P
host: arguments
host: (V64 handshake: checkSync)
host: read 1 byte                   ; acknowledge
host: payload (direction depends on command)
```

| Code | Command | Arguments | ACK | Payload |
|------|---------|-----------|-----|---------|
| `R` | Read memory | address_be32, length_be32 | `0xA0` | V64 sends `length` bytes |
| `W` | Write memory | address_be32, length_be32 | `0xA1` | host sends `length` bytes |
| `C` | Call routine | address_be16 | `0xA2` | none |
| `P` | Print OSD | row, column | `0xA3` | ASCII string + `0xFF` |

The original V64IO2 comment says the length field is "length-1", but the code
uses the value as the actual byte count. TPC2 Next follows the code.

After the ACK, V64IO2 pauses briefly before streaming in the new handshake
mode. TPC2 Next inserts a 2 ms delay for the same reason.

### Address spaces

The high byte of the 32-bit address selects the V64 memory region:

- **DRAM write:** `0x00000000`–`0x01FFFFFF` (up to 32 MB); the top address bit
  is 0.
- **CPU write:** top bit set; only `0x0200`–`0x07FF` is writable.
- **CPU read:** top bit set; `0x0000`–`0xFFFF` except `0xC000`–`0xCFFF`.
- **Call:** 16-bit address in `0x0000`–`0xFFFF`.
- **OSD:** row `0`–`10`, column `0`–`23`.

TPC2 Next validates the generic 32-bit range and the call/OSD limits, but leaves
the V64's region rules to the device, which reports a non-ACK byte if it rejects
a request.

### OSD text

Characters are sent as bytes followed by `0xFF`. V64IO2 defines two flags per
character:

- **D7 = 1** — flashing character.
- **D6 = 1** — NEC6450 OSD character code.

The modern app exposes both as checkboxes and applies them to every character.
The string must be printable ASCII (`0x20`–`0x7E`); `0xFF` is the terminator and
cannot appear in the text.

## 5. USB bridge protocol

The Pico bridge performs the parallel handshake locally. The host and firmware
exchange SLIP frames over USB CDC.

```
SLIP: 0xC0 = end, 0xDB 0xDC = escaped 0xC0, 0xDB 0xDD = escaped 0xDB
CRC:  CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), low byte first
```

Frame body:

```
request:  [cmd]      [seq] [status=0] [len_lo] [len_hi] [payload] [crc_lo] [crc_hi]
response: [cmd|0x80] [seq] [status]   [len_lo] [len_hi] [payload] [crc_lo] [crc_hi]
```

Commands:

| Value | Command | Payload | Response |
|-------|---------|---------|----------|
| 1 | Ping | none | firmware version string |
| 2 | Sync | preferred mode: 0 auto, 1 old, 2 new | 1 byte: 0 old, 1 new |
| 3 | SetMode | 0 auto, 1 old, 2 new | none |
| 4 | Write | up to 8 KiB of data | none |
| 5 | Read | count_le32 (max 8 KiB) | count bytes |
| 6 | CheckSync | none | none |
| 7 | Reset | none | none |
| 8 | Info | none | human-readable status text |

Statuses: `0` OK, `1` timeout, `2` protocol error, `3` I/O error,
`4` unsupported, `5` cancelled, `6` bad request.

The desktop application splits larger TPC transfers into 8 KiB bridge frames.
The firmware keeps the parallel even/odd phase in the emulated STROBE bit, just
like a real LPT port, so chunk boundaries are invisible to the V64.

## 6. Error handling

- Any handshake or byte timeout aborts the operation; the link is reset before
  the next command.
- A non-ACK byte in an extended command is reported as `DeviceRejected` with the
  returned value.
- Bridge frames with a bad CRC or length are discarded; the host times out.
- The GUI reports errors in the activity log and status bar, and leaves the
  connection state unchanged so the user can retry.

## 7. Known deviations from the DOS tools

| Topic | DOS original | TPC2 Next |
|-------|--------------|-----------|
| Buffer | fixed 32 KB | streaming, no transfer-size limit below 4 GiB |
| Ports | ISA LPT1–LPT4 from the BIOS data area | explicit LPT base or Pico bridge |
| Handshake | manual old/new selection | auto with old-then-new fallback |
| Filenames | DOS 8.3 from the command line | 8.3 conversion with a preview |
| Download name field | user-supplied `/name` | blank; device headers name the files |
| OSD text | DOS codepage bytes | printable ASCII plus V64IO2 flags |
| Output | text console | GUI + scriptable CLI |
