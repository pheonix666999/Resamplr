# PadFlow Delivery Plan

Only one milestone is implemented and reviewed at a time. Every completion report references named
tests from `TEST_PLAN.md`, updates `STATUS.md`, lists exact commands/results, and uses one scoped commit.

## Milestone 0 — foundation (complete)

Documentation, Git/JUCE bootstrap, central product configuration, empty resizable app, six CMake
targets, bounded-worker and immutable-asset interfaces, real-time queue and capture-writer interfaces,
schema-v1 bundle skeleton, shared headless smoke path, unit tests, scripts, and Linux/Windows/macOS CI.
The invalid video is recorded as `BLOCKED_REFERENCE_ASSET`; reference-parity approval is excluded.

## Milestone 1 — playable RAM-resident sampler (current)

Milestone 1 replaces the foundation view with a genuinely playable sampler. It remains limited to
RAM-resident sample playback: no trimming, loops, recording, chopping, sequencing, effects,
resampling, skipback, export, MIDI clock, parameter locks, 16 Levels, or Roll.

### Phase 1 — model and persistence contract

- Add four fixed banks, sixteen pads per bank, four-or-more layers per pad, stable UUIDs, validated
  pad/layer parameters, keyboard/MIDI mappings, UI state, and external asset records.
- Add controller commands plus unified undo/redo for rename, recolor, parameter edits, layer
  assignment/clear, copy, paste, duplicate, and clear.
- Extend schema v1 canonically and retain compatibility with Milestone 0 project files.
- Implement the `MODEL-M1-*`, `LAYER-M1-*`, and model-only `SAVE-M1-*` tests.

### Phase 2 — immutable asset import

- Extend immutable sample metadata with source path/format/rate/channels/frames/duration/fingerprint
  and decoded bytes.
- Decode WAV, AIFF, and FLAC on bounded workers; validate owner/target UUID and revision on message
  thread before committing.
- Enforce unique-asset accounting and the configurable 256 MiB–platform-cap budget.
- Implement cancellation, stale-result rejection, rapid replacement, off-callback retirement,
  generated fixtures, and `ASSET-M1-*`/asset-threading tests.

### Phase 3 — deterministic playback engine

- Add immutable published playback snapshots, bounded commands, a preallocated 128-voice pool,
  four-point Hermite interpolation, sample-rate conversion, ADSR, gain/pan/tuning, layer selection,
  one-shot/gate/toggle, mono/poly, choke groups, local limits, panic, metering, and safe silence.
- Keep every callback path allocation-, lock-, file-, log-, GUI-, and reference-destruction-free.
- Implement every `AUDIO-M1-*` allocation/rendering branch and populated-project smoke rendering.

### Phase 4 — devices, preview, keyboard, and MIDI

- Integrate `AudioDeviceManager`, persistent output settings, test tone, CPU/dropout snapshots,
  restart, and graceful unavailable-device behavior without requesting microphone permission.
- Add a fixed-capacity preview path, configurable preview volume, file-change stop, and failure
  cleanup.
- Add active-bank keyboard mappings, repeat suppression/focus guards, MIDI device selection,
  channel filtering, velocity, note-off, disconnect panic, and all-notes-off.
- Implement `INPUT-M1-*`, `PREVIEW-M1-*`, and `DEVICE-M1-*` tests using synthetic/mock paths.

### Phase 5 — functional sampler UI

- Replace `FoundationView` with the original dark-charcoal/teal sampler layout: top bar, selected-pad
  editor, bank tabs, accessible 4×4 pad grid, and status area.
- Wire mouse press/release, selection, context operations, file chooser, file drag/drop,
  sequential multi-file assignment with overwrite confirmation, audition, project open/save, and
  audio/MIDI settings.
- Persist window bounds and useful UI state; support approximately 1180×760 minimum size and high
  DPI without continuous full-window repainting.
- Implement `UIHEADLESS-M1-*` controller/component coverage.

### Phase 6 — integration, packaging, and documentation

- Upgrade console/GUI smoke to create a generated sample, assign A1, render non-silence, save/load,
  verify references, retrigger, and clean temporary files.
- Run format/static validation, Debug/Release builds, CTest, smoke paths, unsigned packaging,
  architecture inspection, and artifact verification.
- Update user/developer/project-format/audio-thread documentation without claiming reference parity
  or Milestone 2 functionality.

### Phase 7 — hosted remediation and review

- Push each coherent phase and inspect the actual GitHub Actions jobs/logs.
- Add a stable `REGRESSION-*` test for each discovered project bug; do not weaken warnings, tests,
  runner coverage, action pinning, or JUCE.
- Finish only after Linux Debug/Release, Windows Debug/Release, macOS universal, macOS Intel, smoke,
  packaging, and cross-platform artifact verification are green and `STATUS.md` records the facts.

## Milestone 2 — waveform editing and recording

Waveform cache; metadata trim/loop/reverse; derived normalize/mono/fade/crop; input recording,
pre-roll, capture FIFO/writer, and reference-based undo/redo.

## Milestone 3 — chopping

Equal, fixed, transient, lazy, and manual chop modes; exact integer bounds; transactional preview,
overwrite, sequential pad/layer assignment, and cancel-without-mutation.

## Milestone 4 — transport and sequencing

960 PPQ/Q16 scheduling, runtime sample conversion, tempo/metre, stateless probability, swing,
ratchets, nudge, recording, and panic.

## Milestone 5 — performance sequencing

Nine locks, per-step slice/reverse, 16 Levels, Roll, performance recording, and export inclusion.

## Milestone 6 — effects and mixer

Four buses, performance effects/automation, mixer, output protection, and state persistence.

## Milestone 7 — capture, song, and rendering

Master/pad/bus resampling, skipback, song arrangement, explicit stem products, deterministic event
decisions, and tolerance-tested offline WAV rendering.

## Milestone 8 — project robustness and MIDI

Compaction, autosave/recovery/migration, relinking, collect-and-save, MIDI learn/CC, disconnect panic,
and optional MIDI clock after the internal transport is stable.

## Milestone 9 — UI and performance refinement

Original responsive UI, accessibility, high DPI, shortcuts, persistence, repaint profiling, and
real-time audit. Reference-specific approval requires a valid video.

## Milestone 10 — distribution

Windows portable/installer, universal macOS app/DMG, symbols, checksums, conditional signing and
notarization, release workflow, manuals, licences, and regression gates.
