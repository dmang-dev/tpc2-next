# Changelog

All notable changes to TPC2 Next are documented here. The project follows
[Semantic Versioning](https://semver.org/).

## 2.2.0 — 2026-10-21

### Release packaging

- The release workflow now derives the version and every asset name from the
  release tag instead of hardcoding `2.1.0`.
- Pushing a `v*` tag creates a draft release, builds all assets, refreshes
  `SHA256SUMS.txt`, and publishes the release only after every job succeeds.
- Windows x64 packages are now built in CI with MSYS2/UCRT64, including the
  Qt, MinGW and third-party runtime DLLs.
- Linux packages are built on Ubuntu 22.04 with Qt 6.8.3, lowering the minimum
  glibc requirement to 2.35.
- The Pico bridge firmware is built and attached to the release from the same
  tag as the desktop packages.

### Desktop integration

- Added the TPC2 Next application icon in SVG, PNG, ICO and macOS iconset
  formats.
- Windows executables now embed the icon and version resources.
- macOS bundles now include a generated `.icns` icon.
- Linux installs now include a `.desktop` entry and hicolor icons.

### macOS

- macOS packages are universal (`arm64` and `x86_64`) and target macOS 12 or
  newer.
- Added optional Developer ID signing and notarization. When the signing
  secrets are not configured, the bundle is ad-hoc signed and the workflow
  clearly reports that it is not notarized.

### Security and documentation

- Added GitHub build provenance attestations for every release asset.
- Added CodeQL analysis and Dependabot updates for GitHub Actions.
- Added `docs/INSTALL.md`, `docs/RELEASING.md`, and this changelog.
- Documented checksum and attestation verification for release users.

## 2.1.0 — 2026-10-20

- First public release of the modern Qt 6 GUI and CLI.
- Preserved the TPC2/GD6 upload/download and V64IO2 remote-control protocols.
- Added Pico USB bridge, legacy parallel-port and demo backends.
- Added simulator-backed tests for framing, handshakes, 8.3 names, uploads,
  downloads and extended commands.
- Published Windows x64, Linux x86_64 and macOS arm64 packages.
