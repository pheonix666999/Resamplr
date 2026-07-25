# Developer Guide

Start by reading the five documents named in `AGENTS.md`. Milestone boundaries are strict: foundation
work must not smuggle in partially implemented product features.

## Requirements

- Git with submodule support
- CMake 3.28 or later
- Ninja
- Python 3.11 or later
- A current Visual Studio with MSVC on Windows
- Xcode/Apple Clang on macOS, or GCC/Clang on Linux
- Optional `clang-format`; no FFmpeg or ASIO SDK is required

Use `git submodule update --init --recursive`, then configure/build/test with the matching preset.
Run Windows presets from a Visual Studio developer shell. They explicitly select `cl`, because
MinGW is unsupported by JUCE; hosted CI locates and initializes the installed Visual Studio version
before configuring. All executables are placed under the preset build directory's `bin` folder. Run
`python scripts/check-format.py`, `python scripts/verify-realtime-code.py`, and
`python scripts/verify-project-schema.py` before builds.

Product identity is owned by `cmake/ProductConfig.cmake` and configured into C++ at build time.
Dependencies are immutable and upgraded only in a reviewed milestone-boundary commit.

Expensive work uses `BackgroundJobSystem`; results carry target UUID/revision and are committed only
by the message thread. Audio and capture code must follow `docs/AUDIO_THREAD_RULES.md`. Project work
must follow `docs/PROJECT_FORMAT.md`. Add stable test IDs before claiming new acceptance behavior.

Milestone 1 playback publication couples each raw-view snapshot to message-thread-owned immutable
sample owners. Reclamation is gated by the oldest generation still referenced by a callback voice;
never replace this with current-snapshot acknowledgement alone.

## Milestone 2 data flow

Waveform summaries, derived renders, and completed-recording decode all use explicit `JobKind`
routing. Jobs capture the current project UUID, target UUID, and revision; only the message thread
may accept a matching immutable completion. Waveform caches have their own bounded registry and are
regenerable. Derived files use deterministic recipes and project-owned `Assets/Derived` paths.
Recorded files use `Assets/Recorded`, retain their capture provenance, and enter the same immutable
asset registry as imported audio.

Capture is prepared completely before arming. The device callback copies into fixed blocks, updates
atomics, and returns immediately when the FIFO is full. The session writer owns `.part` creation,
24-bit WAV encoding, close/validation, collision-safe publication, and failure cleanup. Project
close, application shutdown, device restart, or format change cancels an active capture before
storage is recreated. An incomplete or stale take is never assigned automatically.

The integration smoke is intentionally hardware-independent. It exercises cache generation,
trim/reverse/loop playback, normalize/crop source preservation, schema-v1 round trip, mocked capture
through the real FIFO/writer path, recording assignment, retrigger, and undo/redo. Set
`PADFLOW_SCREENSHOT_DIR` while running `padflow_tests` to write actual offscreen JUCE snapshots of
the populated waveform editor and armed recording panel.
