# Releasing TPC2 Next

The release workflow is in
[`.github/workflows/release.yml`](../.github/workflows/release.yml). It builds
every asset from the release tag, creates a draft GitHub release, uploads the
assets and checksums, and publishes the release only after all jobs succeed.

## Prepare the version

1. Update `project(TPC2Next VERSION ...)` in `CMakeLists.txt`.
2. Add a matching entry to [`CHANGELOG.md`](../CHANGELOG.md).
3. Update any version examples in the README or install documentation if the
   release changes user-facing instructions.
4. Run the normal CI matrix and the formatting check.

Use [Semantic Versioning](https://semver.org/). The tag must be `v` followed by
the exact project version, for example `v2.2.0`. A pre-release tag such as
`v2.2.0-rc1` is accepted when its base version matches `CMakeLists.txt`.

## Publish a tag

```sh
git tag -a v2.2.0 -m "TPC2 Next 2.2.0"
git push origin v2.2.0
```

The tag push starts the release workflow. It:

1. Validates the tag against `CMakeLists.txt`.
2. Creates a draft release if one does not exist.
3. Builds the Windows, Linux, macOS universal and Pico firmware assets.
4. Runs the unit tests and package smoke tests.
5. Generates an SPDX SBOM and build provenance attestations for the assets.
6. Regenerates `SHA256SUMS.txt`.
7. Publishes the draft release.

If a job fails, the release remains a draft. Fix the problem, then either re-run
the failed jobs from the Actions UI or push the same tag again after moving it
to the corrected commit. Do not move a tag after the release has been
published; create a new patch version instead.

## Manual dispatch

Use **Actions → Release packages → Run workflow** and enter an existing tag.
This is useful for rebuilding assets for a draft release or for a release whose
packaging changed without changing the application version.

## macOS signing and notarization

The macOS job uses ad-hoc signing when no certificate is configured. The app
then runs, but Gatekeeper may warn on first launch. To produce a signed and
notarized DMG, add these repository secrets:

| Secret | Meaning |
|--------|---------|
| `MACOS_CERTIFICATE` | Base64-encoded Developer ID Application `.p12` file |
| `MACOS_CERTIFICATE_PASSWORD` | Password for the `.p12` file |
| `MACOS_SIGN_IDENTITY` | Optional identity name; otherwise the first imported identity is used |
| `APPLE_ID` | Apple ID used for notarization |
| `APPLE_APP_SPECIFIC_PASSWORD` | App-specific password for that Apple ID |
| `APPLE_TEAM_ID` | Apple Developer team identifier |

The certificate is imported into a temporary keychain. The workflow signs the
app with the hardened runtime, signs the DMG, submits it with `notarytool`,
staples the ticket, and validates it. If the certificate is present but the
notarization secrets are missing, the DMG is signed but not notarized and the
job logs that fact.

## Verify a release locally

Download the assets and check them:

```sh
sha256sum -c SHA256SUMS.txt
gh attestation verify TPC2-Next-2.2.0-linux-x86_64.tar.gz \
  --repo dmang-dev/tpc2-next
```

The release assets are:

- `TPC2-Next-<version>-win64.zip`
- `TPC2-Next-<version>-linux-x86_64.tar.gz`
- `TPC2-Next-<version>-macos-universal.dmg`
- `tpc2_bridge.uf2`
- `TPC2-Next-<version>.spdx.json`
- `SHA256SUMS.txt`

## Notes

- The workflow never includes original Bung source, binaries or BIOS images.
- The Linux package is built on Ubuntu 22.04 (glibc 2.35) with Qt 6.8.3.
- The macOS package is built with the official Qt 6.8.3 macOS package and is
  universal (`arm64` and `x86_64`), targeting macOS 12 or newer.
- The Pico firmware is built from `firmware/pico` with Pico SDK 2.1.1.
- `main` is protected by a repository ruleset that requires a pull request and
  the CI, CodeQL, and dependency-review checks. The `v*` tag ruleset prevents
  tag deletion or movement after a release is published.
- GitHub Actions references are pinned to commit SHAs; Dependabot updates the
  pins and groups action updates into a single pull request.
