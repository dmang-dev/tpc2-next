# dos-trace

Runs the original DOS tools (`TPC2.EXE` 2.0, `V64IO2.EXE` 2.0) on a small
8086/80186 interpreter. Their printer-port I/O at 0x378–0x37A is wired to the
same pin-level V64 model the unit tests use (`tests/v64`). The harness then
repeats each operation with `ParallelLink` and `Session` against a fresh copy of
the model, and diffs the two pin traces, the V64-side events, the DRAM contents
and the files each side produced.

Nothing here contains or depends on the original code. The interpreter and the
DOS shim are written from the public 8086 and DOS interface descriptions. You
supply the binaries yourself. Do not commit them, or any trace made from them.

## Build and run

The target is built along with the tests (`TPC2_BUILD_TESTS=ON`). It is not
registered with ctest, because the binaries are not in the repository.

```sh
cmake --build build --target tpc2_dos_trace
build/tpc2_dos_trace --tpc2 path/to/TPC2.EXE --v64io2 path/to/V64IO2.EXE --out dos-trace-out
```

Add `--only NAME` to run a single scenario. The output directory (keep it out of
the repository) receives:

- `NAME.dos.trace` and `NAME.next.trace`: one line per pin change, with
  virtual-µs timestamps. `H` marks a PC change and `V` a V64 change.
- `NAME.diff`: a Myers diff of the untimed traces, with context.
- `report.md`: a per-scenario summary covering exit status, line counts,
  virtual time, an event diff, DRAM and file comparisons, and the tail of the
  DOS console.

## Scenarios

| name | DOS side | ParallelLink side |
|---|---|---|
| upload-old / upload-new | `TPC2 1 GAME.BIN`, 40000 bytes | `Session::upload` |
| download-old / download-new | `TPC2 1 /OUT.BIN`, one file on the V64 | `Session::download` |
| download-names | the same, files `MY GAME.V64` and `A.B` | `Session::download` |
| download-empty | the same, no files | `Session::download` |
| v64io2-old / v64io2-new | stdin script: load, write, read, save, CPU write, call, print | the matching `Session` calls |

`-old` and `-new` mean the V64 accepts only that handshake. ParallelLink runs
call `Link::open()` and let each operation sync, as the DOS tools do. They use
the default timeouts on the model's virtual clock.

## How it works

- `Cpu8086.*` is a real-mode interpreter. It covers the integer 8086/186
  instruction set used by Borland C++ 3.x small-model programs and ignores ESC
  (FPU) opcodes. `IN`/`OUT` and `INT n` go to callbacks.
- `DosMachine.*` loads MZ executables and builds a PSP with a command tail and
  an environment. It provides an in-memory file system and a scripted stdin
  (INT 21h AH=3Fh reads one line, `getch` reads one character). It implements
  the INT 21h, 10h, 16h and 1Ah services the runtime uses; anything else throws
  with the faulting CS:IP. The BIOS data area lists one printer port at 0x378.
  `kbhit` always returns "no key", so the original tools retry a failed sync
  forever: every scenario needs a V64 that answers.
- Each port access advances the model clock by 1 µs, the same rule
  `FakeV64Port` uses for ParallelLink. A read of 0x378 returns the last byte
  written, as on an SPP port. Instructions themselves take no time, so virtual
  time counts polls, not CPU speed.

## What it found

See docs/MIGRATION.md §6. Uploads, CPU and DRAM memory commands, CALL and OSD
are pin-identical, except that ParallelLink writes data 0 before each sync
where the DOS tools leave the last byte on the bus. The differences are:

- the download request name field;
- how received file names are mapped to local names;
- the zero-file download result;
- the first sync against a V64 that answers only the new handshake. Both start
  in old mode, but the original gives up after 8192 polls; ParallelLink waits
  out its 4 s timeout. Later operations reuse the working mode on both sides.
  Before this harness, ParallelLink and the Pico firmware went back to old
  mode on every sync. That cost 4–5 s per operation, and it is now fixed.
