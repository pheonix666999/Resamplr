# Developer Guide

Start by reading the five documents named in `AGENTS.md`. Milestone boundaries are strict: foundation
work must not smuggle in partially implemented product features.

## Requirements

- Git with submodule support
- CMake 3.28 or later
- Python 3.11 or later
- Visual Studio 2022 with MSVC on Windows
- Ninja and Xcode/Apple Clang on macOS, or Ninja and GCC/Clang on Linux
- Optional `clang-format`; no FFmpeg or ASIO SDK is required

Use `git submodule update --init --recursive`, then configure/build/test with the matching preset.
Windows presets select the Visual Studio 2022 x64 generator explicitly; MinGW is unsupported by
JUCE. All executables are placed under the preset build directory's `bin` folder. Run
`python scripts/check-format.py`, `python scripts/verify-realtime-code.py`, and
`python scripts/verify-project-schema.py` before builds.

Product identity is owned by `cmake/ProductConfig.cmake` and configured into C++ at build time.
Dependencies are immutable and upgraded only in a reviewed milestone-boundary commit.

Expensive work uses `BackgroundJobSystem`; results carry target UUID/revision and are committed only
by the message thread. Audio and capture code must follow `docs/AUDIO_THREAD_RULES.md`. Project work
must follow `docs/PROJECT_FORMAT.md`. Add stable test IDs before claiming new acceptance behavior.
