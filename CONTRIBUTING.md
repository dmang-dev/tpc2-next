# Contributing

Thanks for helping improve TPC2 Next.

## Scope

TPC2 Next preserves the original TPC2/GD6 and V64IO2 wire behaviour. Changes to
the protocol, 8.3 filename handling, OSD flags, or bridge framing should include
a test or a clear protocol reference.

Do not submit original Bung binaries, Doctor V64 BIOS images, or copied
copyrighted source. The project is an independent implementation; see
[`NOTICE`](NOTICE).

## Build and test

Windows (MSYS2 UCRT64):

```sh
cmake -S . -B build/windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build/windows
ctest --test-dir build/windows --output-on-failure
```

Linux and macOS use the `linux` and `macos` CMake presets. See the
[README](README.md) for package names and prerequisites.

Run the formatting checks before submitting:

```sh
python scripts/check-format.py
```

The GitHub Actions matrix builds and tests Windows/MSYS2, Ubuntu, and macOS.
CodeQL runs on pushes, pull requests and a weekly schedule.

Application icons are generated from `packaging/icons/tpc2-next.svg` with
`python scripts/generate-icons.py` (requires Pillow). Commit the generated PNG,
ICO and iconset files with the source artwork.

## Pull requests

- Keep changes focused and explain the user-visible effect.
- Update documentation when commands, configuration, or behaviour changes.
- Add simulator-backed tests for protocol or file-handling changes.
- Remove usernames, local paths, serial numbers, and other personal information
  from logs and screenshots.
- Confirm that no original Bung binaries, BIOS images, or source files are
  included.

## Reporting bugs

Use the bug report template and include:

- TPC2 Next version or commit
- Operating system
- Backend: demo, Pico bridge, or parallel port
- Reproduction steps
- Relevant logs with personal information removed

For security issues, follow [`SECURITY.md`](SECURITY.md) instead of opening a
public issue.

## Releasing

See [`docs/RELEASING.md`](docs/RELEASING.md) for the tag, version and optional
macOS signing/notarization process.
