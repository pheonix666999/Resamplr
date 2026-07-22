# PadFlow Delivery Plan

Only one milestone is implemented and reviewed at a time. Every completion report references named
tests from `TEST_PLAN.md`, updates `STATUS.md`, lists exact commands/results, and uses one scoped commit.

## Milestone 0 — foundation (current)

Documentation, Git/JUCE bootstrap, central product configuration, empty resizable app, six CMake
targets, bounded-worker and immutable-asset interfaces, real-time queue and capture-writer interfaces,
schema-v1 bundle skeleton, shared headless smoke path, unit tests, scripts, and Linux/Windows/macOS CI.
The invalid video is recorded as `BLOCKED_REFERENCE_ASSET`; reference-parity approval is excluded.

## Milestone 1 — playable RAM-resident sampler

Memory-budgeted immutable PCM, four banks/64 pads, layers, deterministic 128 voices and stealing,
mouse/keyboard/MIDI triggers, device settings, sample import, and preview.

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

