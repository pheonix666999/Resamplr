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

Milestone 3 adds original non-destructive sample chopping while retaining every Milestone 1/2
regression gate. It does not add transport, sequencing, patterns, probability, ratchets, parameter
locks, effects, song mode, resampling, skipback, or export.

### Phase 1 — acceptance contract, slice model, and provisional session

- Add the complete `CHOP-M3-*`, `ASSIGN-M3-*`, `AUDIO-M3-*`, `SAVE-M3-*`, `THREAD-M3-*`, and
  `UIHEADLESS-M3-*` authorities before product changes.
- Add stable UUID slice regions and sets with signed 64-bit inclusive starts/exclusive ends,
  immutable source references, deterministic algorithm/version parameters, and ordered validation.
- Keep provisional markers, slices, analysis, audition, destination choices, overwrite decisions,
  errors, and session-local undo outside the permanent project model until confirmed commit.
- Revalidate session/project/source/pad/layer/revision identity on the message thread; cancellation
  or stale work makes no project mutation.

### Phase 2 — deterministic equal, fixed, and manual slicing

- Generate equal boundaries with checked `start + floor(i * length / count)` arithmetic, exact
  outer endpoints, no gaps/overlaps, and rejection when count exceeds source-frame length.
- Generate fixed-frame slices with explicit include/discard remainder policy and no empty output.
  Millisecond UI values convert once to canonical source frames; musical labels require manual BPM.
- Support marker add/delete/move/nudge/reset with fixed trim endpoints, duplicate rejection,
  neighbour clamping, optional zero/time snapping, and one session undo step per completed drag.

### Phase 3 — transient analysis and lazy chop

- Run deterministic mono/stereo transient envelope/onset analysis on bounded workers using only the
  active trim region, finite validated sensitivity/minimum-duration/look-back parameters, and
  immutable provisional results.
- Handle silence, constant input, very short sources, cancellation, and stale targets without
  publication or project mutation.
- Capture lazy marker frames from callback-published audition position through a bounded fixed-size
  queue. Mouse, keyboard, MIDI, and explicit marker commands are consumed by the session while lazy
  capture is active; the message thread validates, orders, quantizes when requested, and commits
  provisional markers.

### Phase 4 — audition and chopping workspace

- Extend fixed-capacity preview for selected once/gated, sequential slices, and lazy source audition
  without callback allocation, file access, stale ownership, or project assignment.
- Add an accessible chopping workspace integrated with the waveform editor for all five modes,
  parameters, marker editing, selection/readouts, audition, destination planning, overwrite review,
  commit, and cancel.
- Render distinct trim, loop, slice, selected-slice, and playhead markers with high-DPI hit areas and
  bounded labels while preserving the existing editing and recording workflows.

### Phase 5 — immutable destination plan and transactional assignment

- Build an immutable ordered `AssignmentPlan` before mutation with source/session/revision identity,
  pad/layer destinations, occupancy summaries, explicit overwrite/skip decisions, capacity status,
  and stable slice mapping.
- Consecutive-pad assignment proceeds A through D without default wrap. Consecutive-layer assignment
  fills from the requested layer and crosses pads only through an explicit option. Insufficient
  capacity is rejected; slices are never silently discarded or shifted.
- Revalidate the entire plan and candidate project state, then commit all assignments as one unified
  undo transaction and publish playback once. Failure aborts all mutation; undo restores every
  replaced destination and redo reproduces the mapping.

### Phase 6 — playback and schema-v1-compatible persistence

- Assigned layers share the immutable source PCM and use slice trim bounds, loop disabled, and
  reverse disabled by default. Forward/reverse, one-frame, pitched, velocity-layer, and existing
  playback modes remain finite and in range.
- Persist committed slice sets, stable slice UUIDs, algorithm/version/parameters, source trim,
  boundaries, layer references, and assignment provenance additively in schema v1.
- Never persist provisional sessions. Validate all loaded boundaries atomically, retain missing
  source references, and keep Milestone 0/1/2 projects behaviorally compatible.

### Phase 7 — integration, packaging, and hosted remediation

- Extend console/GUI smoke with synthetic transients, equal/fixed/transient/manual/lazy workflows,
  assignment preview, transactional commit, trigger, undo/redo, save/load, cancellation, and cleanup.
- Generate actual CI screenshots for equal, transient, lazy, and assignment-preview states.
- Run static checks, clean Debug/Release builds, CTest, both smoke paths, unsigned packaging,
  architecture inspection, and artifact verification on Linux, Windows, macOS universal, and macOS
  Intel. Every discovered project defect receives a stable `REGRESSION-M3-*` authority.
- Finish only after the complete hosted matrix is green and `STATUS.md` records the evidence.

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
