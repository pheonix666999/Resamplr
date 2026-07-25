# Changelog

## 0.1.0 — unreleased

- Milestone 0 repository, architecture, build, test, smoke, documentation, and CI foundation.
- Milestone 1 four-bank/64-pad sampler model and original resizable desktop interface.
- Asynchronous WAV/AIFF/FLAC import and preview with immutable RAM-resident assets, external
  schema-v1 references, missing-file reporting, and a platform-capped 2 GiB default decoded budget.
- Mouse, configurable computer-keyboard, and velocity-sensitive MIDI triggering.
- Deterministic preallocated 128-voice Hermite playback with layers, gain, pan, pitch, ADSR,
  one-shot/gate/toggle, mono/poly, choke groups, local limits, and safe snapshot retirement.
- Persistent audio/MIDI settings, project operations, unified undo/redo, populated-project smoke
  coverage, unsigned development packaging, and cross-platform CI.
- Immutable bounded multi-resolution waveform caches and an original selected-layer editor with
  trim, loop, reverse, zoom, pan, marker nudging, source/selection fitting, audition, and a live
  callback-published playhead.
- Deterministic project-owned Normalize, Stereo to mono, linear Fade in/out, and Crop derivatives
  with immutable publication, recipe reuse, provenance persistence, stale-result rejection, and
  reference-based undo/redo.
- Manual and threshold-triggered mono/stereo recording with up to two seconds of pre-roll, a
  preallocated four-second-or-larger FIFO, dedicated WAV writer, overflow rejection, explicit UI
  states, fixed destination validation, assignment, and schema-v1 recording provenance.
- Hardware-independent integration smoke coverage, CI-generated waveform/recording screenshots,
  and green Linux, Windows, macOS universal, macOS Intel, packaging, and artifact validation.
