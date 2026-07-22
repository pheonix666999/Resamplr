# PadFlow Requirements

## Product

PadFlow 0.1.0 is a proprietary, offline, standalone C++20/JUCE sampler and drum machine by
Ali Ammar Audio (`com.aliammaraudio.padflow`). It targets Windows 10/11 x64 and macOS 12+
arm64/x86_64, including a universal macOS package. It has no mobile target, plug-in formats,
telemetry, cloud service, bundled FFmpeg, or bundled ASIO SDK.

Commercial distribution is conditional on the owner maintaining an appropriate JUCE licence or
fully satisfying the applicable open-source obligations.

## Milestone 0 boundary

Milestone 0 provides documentation, repository/build/CI foundations, an original empty resizable
application, architectural interfaces, schema-v1 serialization, unit tests, and console/GUI
headless smoke paths. It does not provide sampler playback, pads, waveform editing, chopping,
sequencing, effects, recording, or product export. The synthetic offline smoke render is the only
audio-generation path permitted in this milestone.

## Approved product roadmap

- Four banks of sixteen pads, at least four velocity layers per pad, and 128 deterministic voices.
- RAM-resident immutable decoded PCM with a configurable 2 GiB default budget; no claim of
  general-purpose disk streaming in v0.1.0.
- WAV, AIFF, and FLAC import; optional user-installed FFmpeg behind `PADFLOW_ENABLE_FFMPEG`.
- Mouse, computer-keyboard, velocity-sensitive MIDI, sequencer, 16 Levels, and Roll triggering.
- Non-destructive trim, loop, reverse, slices, gain, pan, pitch, envelope, filter, and routing.
- Derived immutable PCM assets for normalize, mono, fades, crop, and other PCM transformations.
- Manual/threshold input recording, master/pad/bus resampling, and skipback through preallocated
  capture FIFOs and non-real-time writers.
- Equal, fixed-length, transient, lazy, and manual chopping with transactional pad/layer assignment.
- 960 PPQ patterns, song arrangement, swing, velocity, probability, ratchets, nudge, per-step slice,
  reverse, and nine parameter locks.
- Four effect buses, performance effects, mixer/output processing, resampling, and skipback.
- Versioned single-file `.padflow` ZIP bundles, autosave, recovery, migration, missing-file
  relinking, and collect-and-save.
- Offline WAV master, dry-pad, isolated-processed-pad, bus-return, pattern, and song products with
  deterministic event decisions and tolerance-tested floating-point rendering.
- Accessible, resizable, original desktop UI for 1280x800 through 4K at 100–200% scaling.

Detailed invariants live in `ARCHITECTURE.md`, project rules in `docs/PROJECT_FORMAT.md`, audio
rules in `docs/AUDIO_THREAD_RULES.md`, test cases in `TEST_PLAN.md`, and delivery order in
`PLANS.md`.

