# GitHub Actions

`.github/workflows/ci.yml` runs on pull requests, pushes to `main` and `feature/**`, and manual
dispatch. Default token permissions are read-only and superseded branch runs are cancelled.

Milestone 1 jobs:

- `validation` on Ubuntu 24.04: formatting/source/schema checks, Debug/Release builds, CTest, and
  populated-project console/GUI headless smoke without hardware or display.
- `windows-x64` on Windows 2025: Debug build/CTest plus Release app/tests/smoke and unsigned
  development archive.
- `macos-universal` on macOS 15 arm64: universal Release app, tests, `lipo`, unsigned archive.
- `macos-intel-smoke` on macOS 15 Intel: x86_64 build, CTest, console and GUI headless smoke.
- `artifact-verification`: downloads development archives and verifies expected non-empty contents,
  `UNSIGNED.txt`, manifest `signed:false`, and checksums.

Actions are pinned to immutable SHAs with version comments. Runner labels are current infrastructure
assumptions and are revalidated at milestone boundaries. Pull requests receive no signing secrets.
Final installers, DMG publication, signing/notarization, releases, and strict production gates become
mandatory in Milestone 10. A workflow file existing is not a successful run; `STATUS.md` records only
actual hosted results.
