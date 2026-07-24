# PadFlow

PadFlow is an original, offline, standalone desktop sampler project by Ali Ammar Audio. Version
0.1.0 contains the Milestone 1 playable RAM-resident sampler: four banks of sixteen pads, four
velocity layers per pad, WAV/AIFF/FLAC import and preview, mouse/keyboard/MIDI triggering,
deterministic 128-voice playback, device settings, and schema-v1 project save/load.

## Status and platforms

- Target: Windows 10/11 x64 and macOS 12+ arm64/x86_64/universal.
- Technology: C++20, CMake 3.28+, pinned JUCE 8.0.13.
- Current reference status: `BLOCKED_REFERENCE_ASSET` because the supplied MP4 is empty.
- No Resamplr branding, artwork, samples, or exact interface is included or claimed.

## Source checkout

```bash
git clone --recursive <repository-url>
cd PadFlow
git submodule update --init --recursive
```

Install CMake 3.28+, Ninja, Python 3.11+, and a supported compiler: a current Visual Studio with MSVC
on Windows, or Xcode on macOS. Run Windows presets from a Visual Studio developer shell. The JUCE
submodule is pinned; dependency upgrades happen only at milestone boundaries.

## Configure, build, and test

```bash
cmake --preset tests-debug --fresh
cmake --build --preset tests-debug
ctest --preset tests-debug --output-on-failure
```

Use `windows-debug`, `windows-release`, `macos-debug`, `macos-release`,
`macos-universal-release`, or `macos-intel-release` on the matching host. See
`docs/DEVELOPER_GUIDE.md` for exact commands.

Smoke paths:

```bash
padflow_smoke
PadFlow --headless-smoke-test --no-audio-device
```

They generate a temporary WAV, import it through the bounded worker path, exercise mouse/keyboard/
MIDI modes, render finite non-silence, save and reload a populated schema-v1 project, resolve its
external sample, retrigger, and clean temporary files without physical audio or MIDI devices.

## Installation, settings, and projects

Milestone 1 development archives are unsigned and contain `UNSIGNED.txt`. Production installers and
DMG publication arrive in Milestone 10. Gatekeeper or SmartScreen may warn about unsigned development
builds.

Planned settings locations:

- Windows: `%APPDATA%\Ali Ammar Audio\PadFlow`
- macOS: `~/Library/Application Support/Ali Ammar Audio/PadFlow`

Projects use the `.padflow` extension; the schema contract is documented in
`docs/PROJECT_FORMAT.md`.

## Optional integrations

FFmpeg is disabled by default and will use only a user-installed executable after licensing review.
ASIO is disabled and no SDK is bundled. Neither is required for the application foundation.

## CI, packaging, signing, and licences

See `docs/GITHUB_ACTIONS.md`, `docs/SIGNING.md`, and `THIRD_PARTY_LICENSES.md`. Ordinary CI does not
require signing credentials or audio hardware. Report defects with platform, build preset, exact
steps, logs, and a minimal redistributable project where possible.
