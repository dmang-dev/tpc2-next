# Installing TPC2 Next

Prebuilt packages are attached to the
[latest release](https://github.com/dmang-dev/tpc2-next/releases/latest).
Every desktop package contains the GUI, the `tpc2cli` command-line tool and the
Qt runtime it needs. The Windows and Linux archives also include the project
documentation. The Pico bridge firmware is published as a separate `.uf2` file
in the same release.

The packages are self-contained. You do not need to install Qt or a compiler.

## Verify the download

Download `SHA256SUMS.txt` from the same release and verify the archive before
running it.

Linux:

```sh
sha256sum -c SHA256SUMS.txt
```

macOS:

```sh
shasum -a 256 -c SHA256SUMS.txt
```

Windows (PowerShell):

```powershell
Get-FileHash .\TPC2-Next-2.2.1-win64.zip -Algorithm SHA256
# Compare the output with the matching line in SHA256SUMS.txt.
```

If you use the GitHub CLI, you can also verify the build provenance attestation:

```sh
gh attestation verify TPC2-Next-2.2.1-win64.zip \
  --repo dmang-dev/tpc2-next
```

Each release also includes `TPC2-Next-2.2.1.spdx.json`, an SPDX software bill
of materials. It can be inspected with any SPDX-compatible tool, or with
`jq`:

```sh
jq '.packages[] | {name, versionInfo, licenseConcluded}' \
  TPC2-Next-2.2.1.spdx.json
```

## Windows

1. Extract `TPC2-Next-2.2.1-win64.zip`.
2. Run `TPC2-Next-2.2.1-win64\bin\tpc2.exe`.
3. For scripted use, run `bin\tpc2cli.exe`.

The ZIP is portable and does not need MSYS2 or a separate Qt installation.
Windows SmartScreen may warn about an unsigned executable. Choose **More
info → Run anyway** only if the checksum and attestation match. A future
release can add an Authenticode certificate; the current packages are not
code-signed on Windows.

The bridge backend needs a USB serial port. The legacy parallel-port backend
also needs InpOut32/InpOutx64 and its kernel driver; see the
[README](../README.md#hardware).

## Linux

Requirements:

- x86_64 Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, or newer).
- An X11 or Wayland desktop for the GUI. The CLI works headlessly.
- `dialout` group membership for the Pico bridge, or root/`/dev/port` access
  for a legacy parallel port.

Extract and run:

```sh
tar -xzf TPC2-Next-2.2.1-linux-x86_64.tar.gz
./TPC2-Next-2.2.1-linux-x86_64/bin/tpc2
./TPC2-Next-2.2.1-linux-x86_64/bin/tpc2cli --backend demo info
```

The GUI and CLI use the bundled Qt libraries in `lib/` and `plugins/`.

### Desktop entry and icon

The archive includes `share/applications/tpc2-next.desktop` and hicolor icons.
For a system-wide install:

```sh
sudo mkdir -p /opt/tpc2-next
sudo cp -a TPC2-Next-2.2.1-linux-x86_64/. /opt/tpc2-next/
sudo tee /usr/local/bin/tpc2 >/dev/null <<'EOF'
#!/bin/sh
exec /opt/tpc2-next/bin/tpc2 "$@"
EOF
sudo chmod 755 /usr/local/bin/tpc2
sudo tee /usr/local/bin/tpc2cli >/dev/null <<'EOF'
#!/bin/sh
exec /opt/tpc2-next/bin/tpc2cli "$@"
EOF
sudo chmod 755 /usr/local/bin/tpc2cli
sudo install -Dm644 /opt/tpc2-next/share/applications/tpc2-next.desktop \
  /usr/local/share/applications/tpc2-next.desktop
sudo install -Dm644 \
  /opt/tpc2-next/share/icons/hicolor/256x256/apps/tpc2-next.png \
  /usr/local/share/icons/hicolor/256x256/apps/tpc2-next.png
```

For a per-user install, replace `/opt/tpc2-next` with `~/.local/opt/tpc2-next`,
`/usr/local/bin` with `~/.local/bin`, and `/usr/local/share` with
`~/.local/share`. The desktop entry assumes `tpc2` is on `PATH`.

If your desktop does not refresh the menu immediately, run
`update-desktop-database ~/.local/share/applications` (or the system-wide
equivalent).

### Serial permissions

```sh
sudo usermod -aG dialout "$USER"
```

Log out and back in after changing group membership. Some distributions use
`uucp` instead of `dialout`; check the group that owns `/dev/ttyACM*`.

## macOS

1. Open `TPC2-Next-2.2.1-macos-universal.dmg`.
2. Drag **TPC2 Next** to **Applications**.
3. Launch it from Applications.

The DMG is universal and runs natively on Apple Silicon and Intel Macs. It
requires macOS 12 or newer.

The CLI is inside the application bundle:

```sh
/Applications/tpc2.app/Contents/MacOS/tpc2cli --backend demo info
```

To put it on `PATH`:

```sh
sudo ln -sf /Applications/tpc2.app/Contents/MacOS/tpc2cli /usr/local/bin/tpc2cli
```

If the release is not notarized, Gatekeeper may show a warning. You can open
the app with **Control-click → Open** once. If you have verified the checksum
and attestation and still need to remove the quarantine attribute, run:

```sh
xattr -dr com.apple.quarantine /Applications/tpc2.app
```

Only do this for a download you trust. The release notes state whether the
current DMG was notarized.

## Pico bridge firmware

1. Download `tpc2_bridge.uf2`.
2. Hold **BOOTSEL** while connecting the Pico.
3. Copy `tpc2_bridge.uf2` to the `RPI-RP2` drive.
4. The Pico reboots and exposes a USB serial port.

See [`firmware/pico/README.md`](../firmware/pico/README.md) for wiring,
level-shifting and safety notes. The firmware has not been validated on
physical V64 hardware in this repository's automated environment.

## Troubleshooting

- **No serial ports:** install the USB serial driver for non-genuine RP2040
  boards and check the `dialout`/`uucp` group on Linux.
- **GUI does not start on Linux:** confirm that an X11 or Wayland session is
  available and that the bundled `plugins/platforms` directory is intact.
- **macOS says the app is damaged:** verify the checksum first; then use
  Control-click → Open. Do not bypass Gatekeeper for an unverified download.
- **Parallel port unavailable:** the Pico bridge is the recommended backend;
  legacy LPT requires a real port and the platform-specific driver/permission.

For protocol and hardware details, see [`PROTOCOL.md`](PROTOCOL.md),
[`HARDWARE.md`](HARDWARE.md), and [`MIGRATION.md`](MIGRATION.md).
