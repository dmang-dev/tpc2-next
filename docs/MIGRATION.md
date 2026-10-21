# Migration guide: TPC2.EXE and V64IO2.EXE → TPC2 Next

TPC2 Next replaces two DOS programs:

- **TPC2.EXE 2.0** — PC → V64 upload and V64 → PC download.
- **V64IO2.EXE 2.0** — V64 remote control (memory read/write, routine call, OSD
  print, local buffer editor).

The original Borland C and assembly sources are not redistributed with TPC2
Next. This guide maps each old command and calls out deliberate behaviour
changes based on the documented TPC2/GD6 and V64IO2 protocols.

## 1. What was in the original archives

| Archive | Contents | Status in TPC2 Next |
|---------|----------|---------------------|
| `Tpc2.zip` | `TPC2.EXE` | Reimplemented |
| `Tpc2src.zip` | `TPC2.C`, `IN32K.ASM`, `OUT32KX.ASM` | Ported and documented |
| `V64io2.zip` | `V64IO2.EXE` | Reimplemented on the Remote control page |
| `V64io2sr.zip` | `V64IO2.C`, `IN32K.ASM`, `OUT32KX.ASM` | Ported and documented |
| `Tsf3.zip` | `TSF3.EXE` | Reviewed; no source was supplied, so it is not reimplemented |
| `V64_v175.zip` | `V64_VER1.75` BIOS image | Reviewed; the image is left untouched |

The original reference page is
<https://web.archive.org/web/19981212023426/http://www.bung.com.hk/>.

## 2. TPC2.EXE

### Original workflow

`TPC2.EXE` scanned the BIOS data area for LPT1–LPT4, printed the addresses, and
asked for a port number. It then showed an interactive prompt:

```text
[out(file)/in(/file)/quit]
```

- `out(file)` uploads one or more files to the V64.
- `in(/file)` receives the image the V64 pushes after its front-panel `>>`
  button.
- `quit` exits.

A command-line form was also supported:

```text
TPC2.EXE <port-number> <file> [file...]
```

### Modern equivalents

| Original | TPC2 Next GUI | TPC2 Next CLI |
|----------|---------------|---------------|
| Port prompt (1–4) | **Backend: Parallel port**, then choose the LPT base | `--backend lpt --lpt 0x378` |
| `out(file)` | **Transfer → Add files → Send all** | `tpc2cli upload file…` |
| `in(/file)` | **Transfer → Receive DRAM image** | `tpc2cli download <dir>` |
| `quit` | Close the window | Process exits |
| `TPC2.EXE 1 GAME.Z64` | Add the file and press **Send all** | `tpc2cli --backend lpt upload GAME.Z64` |

### Transfer details preserved

- The `GD6R` upload header, little-endian 32-bit size and 11-byte 8.3 name field.
- The `GD6W` download header and its 11-byte request field.
- The per-file 15-byte download header (`size_le32 + name[11]`).
- Old and new handshakes, including the 2 ms post-ACK pause in new mode.
- Nibble-oriented reads and the persistent even/odd write phase.
- No per-file acknowledgement during uploads.

### Deliberate changes

| Topic | TPC2.EXE | TPC2 Next |
|-------|----------|-----------|
| Transfer buffer | fixed 32 KB DOS buffer | streaming in 64 KB chunks |
| Multiple uploads | repeated `out(...)` commands | queue in the table, send once |
| Names | typed DOS paths, uppercased | automatic 8.3 conversion with preview |
| Download destination | current directory | chosen folder; duplicate names get ` (1)` |
| Progress | spinner | byte count, percentage, speed and ETA |
| Cancel | Esc between 32 KB blocks | Cancel button / `--cancel` behaviour |
| Ports | ISA LPT1–LPT4 from `0040:0008` | explicit LPT base or Pico bridge |
| Platform | DOS | Windows, Linux, macOS |

The 32 KB limit is gone. A single upload may be any size that fits in a 32-bit
length field and the available memory/disk.

## 3. V64IO2.EXE

### Original command syntax

All addresses and lengths were hexadecimal, up to eight digits. The old tool
kept one 32 KB local buffer and started every command with a fresh handshake.

```text
read  xxxx yyyy      read V64 memory into the local buffer
write xxxx yyyy      write the local buffer to V64 memory
call  xxxx           call a 6502 routine (0x0000–0xFFFF)
print xxxx yyyy zzzz print text on the V64 OSD
load  xxxx           load a file into the local buffer (max 32 KB)
save  xxxx yyyy      save code1 bytes of the buffer to a file
dump  xxxx yyyy      hex-dump the local buffer
edit  xxxx           edit local buffer bytes
help                 show the command summary
quit                 exit
```

`print` used row `0–0x0A` and column `0–0x17`. D7 of each character meant
"flashing" and D6 selected the NEC6450 OSD font.

### Modern equivalents

| Original | Modern GUI | CLI |
|----------|------------|-----|
| `read xxxx yyyy` | **Remote control → Read memory** | `tpc2cli read <address> <length> <file>` |
| `write xxxx yyyy` | **Remote control → Write memory** | `tpc2cli write <address> <file>` |
| `call xxxx` | **Remote control → Call** | `tpc2cli call <address>` |
| `print row col text` | **Remote control → Send text** | `tpc2cli print <row> <col> <text>` |
| `load xxxx` | **Write memory → Choose file** | `tpc2cli write <address> <file>` |
| `save xxxx yyyy` | **Read memory → Save…** | `tpc2cli read <address> <length> <file>` |
| `dump xxxx yyyy` | Hex view after **Read** | Pipe `tpc2cli read` output to `xxd`/`hexdump` |
| `edit xxxx` | Read → Save → edit in any hex editor → Write | Same with files |
| `help` | This guide and the in-app hints | `tpc2cli --help` |
| `quit` | Close the window | Process exits |

### The 32 KB local buffer is gone

The old tool was limited by a single 32 KB buffer: `read` could only fetch 32 KB
at a time, `load` only read 32 KB, and `dump`/`edit` operated on that buffer.

TPC2 Next has no local transfer buffer:

- `read` streams directly to the hex view or output file.
- `write` streams the selected file to the V64.
- `load`/`save` are ordinary file reads/writes around `write`/`read`.
- `dump`/`edit` are replaced by the hex view and the user's preferred editor.

The protocol still transfers in chunks, but chunk boundaries are invisible.

### Flags and parsing

| Old behaviour | Modern behaviour |
|---------------|------------------|
| Addresses/lengths parsed as hex | `0x` prefix or decimal; bare values are decimal |
| `read` length stored in `code2` | explicit length argument, 32-bit |
| `print` D7/D6 embedded in text bytes | explicit **Flashing text** / **NEC6450 font** checkboxes or `--flash` / `--nec` |
| OSD row validated with `& 0x0F` | row must be 0–10 |
| OSD column validated with `& 0x1F` | column must be 0–23 |
| Text could contain arbitrary DOS bytes | printable ASCII `0x20`–`0x7E`; `0xFF` is the terminator |
| Buffer reused between commands | every operation is independent |

The old `print` row check `(code1 & 0x0F) > 0x0A` accepted values such as
`0x1F` after masking. The modern app rejects anything outside 0–10 instead of
silently truncating.

### Address spaces

The V64 interprets the top address bit:

- **DRAM:** `0x00000000`–`0x01FFFFFF` (up to 32 MB).
- **CPU write:** top bit set; only `0x0200`–`0x07FF`.
- **CPU read:** top bit set; `0x0000`–`0xFFFF` except `0xC000`–`0xCFFF`.
- **Call:** 16-bit `0x0000`–`0xFFFF`.

TPC2 Next validates the generic 32-bit range and the call/OSD limits, and lets
the V64 reject region violations with a non-ACK byte. This mirrors the original
tool, which relied on the V64 for the region rules.

## 4. Handshake and port selection

| Topic | Original | TPC2 Next |
|-------|----------|-----------|
| Default mode | old | auto: old, then new |
| Mode switch | automatic after a failed old sync | automatic, or force Old/New |
| Port discovery | BIOS LPT table | serial port list or explicit LPT base |
| Driver | direct DOS port I/O | InpOut (Windows), `ioperm`/`/dev/port` (Linux), Pico bridge (all) |
| No parallel port | program exits | demo backend, bridge backend, or clear error |

The Pico bridge is the functional replacement for the DOS-era parallel port and
is the only backend available on macOS.

## 5. Behaviour changes checklist

- [x] Same TPC 2.0/GD6 byte stream and handshake.
- [x] Same 8.3 filename field layout and padding.
- [x] Same GD6E big-endian arguments and ACK values.
- [x] Same V64IO2 OSD flags (D7 flash, D6 NEC6450).
- [x] Streaming transfers instead of a 32 KB buffer.
- [x] GUI queue, progress, cancellation and logs.
- [x] CLI for scripts and CI.
- [x] Demo backend for testing without a V64.
- [x] Independent implementation; original sources and binaries are not redistributed.
- [ ] Real-hardware regression run on every V64 BIOS revision. The protocol is
  covered by the simulator and fixtures; see the note below.

## 6. Validation and known limitations

The automated suite (`tpc2_tests`, 56 checks) covers handshake parsing, CRC and
SLIP framing, name conversion, upload/download, extended commands, cancellation
and BIOS filenames. The demo device implements the same state machine as the
real protocol.

Not covered by automated tests:

- Electrical timing on a specific V64/BIOS/cable combination.
- Windows InpOut driver installation.
- Linux port permissions.
- Pico firmware on physical hardware. The firmware compiles cleanly with Pico
  SDK 2.1.1 and Arm GCC 16.2.0 and produces a valid UF2, but it has not been
  flashed onto a board or connected to a V64 in this environment.

If a real transfer fails, start with the demo backend, then the bridge with a
short full-pin cable, then force the handshake mode. See the troubleshooting
sections in `README.md` and `docs/HARDWARE.md`.

## 7. Migration checklist for users

1. Build or install TPC2 Next.
2. Confirm the GUI works with **Demo device**.
3. Connect the Pico bridge or configure the legacy LPT backend.
4. Upload one small file and verify it appears on the V64.
5. Receive a DRAM image and compare its size with the V64's announcement.
6. Try a short OSD message, with and without flashing/NEC6450 flags.
7. Replace old batch scripts with `tpc2cli` invocations.
8. Keep any original archives you own outside this repository if you need them
   for reference.
