# PadFlow Delivery Plan

Only one milestone is implemented and reviewed at a time. Every completion report references named
tests from `TEST_PLAN.md`, updates `STATUS.md`, lists exact commands/results, and uses one scoped commit.

## Milestone 0 — foundation (complete)

Documentation, Git/JUCE bootstrap, central product configuration, empty resizable app, six CMake
targets, bounded-worker and immutable-asset interfaces, real-time queue and capture-writer interfaces,
schema-v1 bundle skeleton, shared headless smoke path, unit tests, scripts, and Linux/Windows/macOS CI.
The invalid video is recorded as `BLOCKED_REFERENCE_ASSET`; reference-parity approval is excluded.

## Milestone 1 — playable RAM-resident sampler (complete)

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
- Enforce unique-asset accounting and the configurable budget: 2 GiB default, 256 MiB minimum,
  capped at `min(16 GiB, 50% physical RAM)`.
- Implement cancellation, stale-result rejection, rapid replacement, off-callback retirement,
  generated fixtures, and `ASSET-M1-*`/asset-threading tests.

### Phase 3 — deterministic playback engine

- Add immutable published playback snapshots, bounded commands, a preallocated 128-voice pool,
  four-point Hermite interpolation, sample-rate conversion, ADSR, gain/pan/tuning, layer selection,
  one-shot/gate/toggle, mono/poly, choke groups, local limits, panic, metering, and safe silence.
- Keep every callback path allocation-, lock-, file-, log-, GUI-, and reference-destruction-free.
- Implement every `AUDIO-M1-*` allocation/rendering branch and populated-project smoke rendering.

### Phase 4 — devices, preview, keyboard, and MIDI

- Integrate `AudioDeviceManager`, persistent input/output routing, test tone, CPU/dropout snapshots,
  restart, and graceful unavailable-device behavior. Input is disabled by default, so playback
  does not request microphone access.
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

## Milestone 2 — waveform editing and recording (current)

Milestone 2 extends the playable sampler with non-destructive frame-bound editing, immutable
derived PCM, and audio-input recording. It does not include chopping, sequencing, effects,
resampling, skipback, song mode, or export. Milestone 1 behavior and schema-v1 compatibility remain
regression gates throughout.

### Phase 1 — acceptance contract and waveform cache

- Add the `WAVE-M2-*`, `EDIT-M2-*`, `AUDIO-M2-*`, `DERIVED-M2-*`, `RECORD-M2-*`,
  `THREAD-M2-*`, `SAVE-M2-*`, and `UIHEADLESS-M2-*` authorities before product changes.
- Generate immutable mono/stereo multi-resolution min/max summaries on bounded workers.
- Key caches by asset UUID, source fingerprint, algorithm version, channels, and source frames;
  enforce a separate bounded memory budget and stale/cancelled result rejection.

### Phase 2 — frame-bound model, persistence, and playback

- Add inclusive trim/loop starts, exclusive ends, loop enablement, reverse, zero-crossing preference,
  and selected-layer editor state with one-transaction undo/redo.
- Default imported assets to the full source range and validate every published boundary against
  immutable source metadata.
- Extend playback snapshots and fixed voices for forward/reverse trim and loop wrapping while
  retaining fractional overshoot, deterministic Hermite bounds, and new-trigger-only edit updates.
- Extend schema v1 additively, load Milestone 0/1 files unchanged, and reject invalid partial
  Milestone 2 state without a partial model commit.

### Phase 3 — functional waveform editor

- Add selected-layer waveform display, distinct trim/loop/playhead markers, loading/missing states,
  duration/position readouts, accessible focus, and minimum-window/high-DPI behavior.
- Add validated marker dragging/nudging, cursor-centred zoom, horizontal navigation, fit/selection
  views, optional zero-crossing/time snap, audition controls, loop toggle, and reverse toggle.
- Keep chopping markers and slice assignment out of this milestone.

### Phase 4 — immutable derived assets

- Render normalize, stereo-to-mono, linear fade-in/out, and crop on cancellable bounded workers.
- Write project-owned derived audio through sibling temporary files, validate before publication,
  retain deterministic provenance/recipes, reuse matching recipes, and never overwrite sources.
- Revalidate project/pad/layer/revision on the message thread, publish through the normal immutable
  registry, and commit one reference-based undo entry.

### Phase 5 — real-time-safe input capture

- Prepare one foreground session with at least four seconds of preallocated FIFO storage, a
  pre-roll ring, fixed session/target identity, and explicit state before arming.
- Limit the callback to input copies, fixed descriptors, bounded queues, threshold state, and atomic
  counters/meters. A dedicated writer owns `.part` creation, WAV encoding/finalization, validation,
  collision-safe publication, cleanup, and failure reporting.
- Support manual and threshold start, 0–2000 ms pre-roll, mono/stereo input, cancellation, overflow
  rejection, stale destinations, and safe project/device shutdown.

### Phase 6 — recording UI and assignment

- Add explicit Idle/Armed/Waiting/Recording/Stopping/Finalizing/Completed/Cancelled/Failed states,
  input routing, meter, mode, threshold, pre-roll, controls, destination, elapsed time, overflow,
  filename, and result messaging.
- Assign successful recordings through the immutable asset path and unified undo/redo; incomplete,
  failed, cancelled, or stale captures never create an assignment or undo entry.

### Phase 7 — integration, packaging, and hosted remediation

- Extend console/GUI smoke with trim/reverse/loop, derived normalize/crop, schema round trip, mocked
  capture/WAV validation, recording assignment, retrigger, undo/redo, and temporary cleanup.
- Run format/static validation, clean Debug/Release builds, CTest, smoke, unsigned packaging,
  architecture inspection, and artifact verification.
- Push coherent phases and inspect the actual hosted Linux, Windows, macOS universal, macOS Intel,
  and artifact jobs. Every discovered project defect receives a `REGRESSION-*` authority.
- Finish only after a full green workflow and an updated `STATUS.md`; exact reference parity remains
  blocked by `BLOCKED_REFERENCE_ASSET`.

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
