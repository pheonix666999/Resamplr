# PadFlow Test Plan

This tracked file is the authoritative test specification. A prompt, chat, issue, or untracked note
cannot substitute for an entry here. Every bug fix adds or updates a `REGRESSION-*` entry. A deferred
test states its milestone and reason. Unless stated otherwise, tests are automated through CTest and
must not require physical audio or MIDI hardware.

## Numerical policy

- PPQ-to-sample onset and real-time/offline equivalence: at most one target-rate sample.
- Buffer-size variants: identical resolved absolute frame indices.
- Render length: exact; resampling may use a documented ±1-frame rounding allowance.
- Simple-path floats: absolute error ≤ `1e-5`, relative error ≤ `1e-4`.
- Peak/RMS: ±0.1 dB simple paths, ±0.25 dB effect paths.
- Spectral-band energy where used: ±0.5 dB.
- Digital silence: absolute peak ≤ `1e-7`; expected non-silence: RMS ≥ `1e-5`.
- Tail transition: within one processing block; total output frame count remains exact.
- NaN/infinity: zero occurrences.

Exact hashes are required for canonical manifests, integer fixtures, deterministic archive metadata,
probability decisions, and event schedules. Floating-point audio hashes are limited to a documented
bit-stable platform/compiler or proven integer-only path.

## Milestone 0 foundation

| ID | Test | Acceptance |
|---|---|---|
| MODEL-001 | Empty project/product metadata | Central values and schema version 1 are correct. |
| SAVE-001 | Canonical manifest and schema-v1 round trip | Repeated manifest text matches; save/load is semantically equal. |
| THREAD-001 | Bounded SPSC command queue | FIFO order, full and empty behavior pass without runtime allocation. |
| THREAD-002 | Background completion | Worker returns an immutable target/revision result and shuts down safely. |
| THREAD-003 | Capture FIFO lifecycle | Prepared blocks move audio→ready→free without allocation or locks. |
| ASSET-001 | Immutable PCM/accounting | Metadata, const samples, decoded bytes, and 2 GiB default are correct. |
| UIHEADLESS-001 | Console smoke | Metadata, schema round trip, finite non-silent synthetic render, cleanup, exit 0. |
| UIHEADLESS-002 | GUI headless smoke | Same path with required flag, no window/devices/permission, exit 0. |

## Project model — Milestone 1 unless noted

| ID | Test |
|---|---|
| MODEL-002 | Create/destroy and deep-copy project-owned editable state. |
| MODEL-003 | Bank A–D indexing and 64 stable pad identifiers. |
| MODEL-004 | Pad/layer validation, four layers minimum, velocity ranges. |
| MODEL-005 | Stable UUID references survive container reorder and deletion checks. |
| MODEL-006 | Unified undo/redo for pad and sample-reference changes. |

## Samples and assets — Milestones 1–2

| ID | Test |
|---|---|
| ASSET-002 | Load valid WAV, AIFF, and FLAC synthetic fixtures. |
| ASSET-003 | Reject invalid/corrupt and unsupported-channel fixtures without mutation. |
| ASSET-004 | Mono, stereo, one-frame, and very short assets. |
| ASSET-005 | Source sample-rate preparation at 44.1/48/88.2/96 kHz. |
| ASSET-006 | Start/end and loop inclusive/exclusive validation. |
| ASSET-007 | Decoded memory accounting counts shared assets once. |
| ASSET-008 | Oversized import fails cleanly before commit. |
| ASSET-009 | Replacement/unload retain live readers and reclaim off callback. |
| ASSET-010 | Rapid replacement and deferred destruction stress. |
| ASSET-011 | Derived recipe/provenance and deterministic cache reuse. |
| ASSET-012 | Failed/cancelled derived render leaves source reference unchanged. |

## Chopping — Milestone 3

| ID | Test |
|---|---|
| CHOP-001 | Equal boundaries use `start + floor(i*length/count)` and exact endpoints. |
| CHOP-002 | Uneven division has no gaps, overlap, or empty slices. |
| CHOP-003 | One-frame trim permits exactly one slice. |
| CHOP-004 | Slice count equal to trim length creates one-frame slices. |
| CHOP-005 | Count above trim length is rejected; explicit clamp is deterministic. |
| CHOP-006 | Very large frame counts do not overflow boundary arithmetic. |
| CHOP-007 | Fixed-length mode includes a non-empty remainder by default. |
| CHOP-008 | Explicit discard-remainder omits only the remainder. |
| CHOP-009 | Manual markers clamp to trim; duplicates are rejected. |
| CHOP-010 | Transient markers obey sensitivity/minimum duration/look-back bounds. |
| CHOP-011 | Lazy markers are ordered and remain inside trim. |
| CHOP-012 | Sequential bank/pad and layer assignment is correct. |
| CHOP-013 | Overwrite cancellation and assignment cancellation make no mutation. |
| CHOP-014 | Transaction failure rolls back every pad/layer change. |

## Sequencer and timing — Milestones 4–5

| ID | Test |
|---|---|
| SEQ-001 | Stored positions remain identical at 44.1/48/88.2/96 kHz. |
| SEQ-002 | Buffer sizes do not alter resolved absolute event frames. |
| SEQ-003 | Real-time simulation and offline scheduling agree within one sample. |
| SEQ-004 | Tempo changes preserve stored PPQ and resolve correct frames. |
| SEQ-005 | Supported time signatures and pattern lengths resolve correctly. |
| SEQ-006 | Q16 microtiming/nudge range, normalization, and rounding. |
| SEQ-007 | Swing produces bounded monotonic event positions. |
| SEQ-008 | `siphash24-v1` repeated runs are identical. |
| SEQ-009 | Probability is invariant to buffer size and sample rate. |
| SEQ-010 | Probability is invariant to container reorder/unrelated insertion. |
| SEQ-011 | Real-time/offline probability decisions match in pattern/song iterations. |
| SEQ-012 | Event duplication creates a new UUID and intentionally new sequence. |
| SEQ-013 | Ratchets stay in event/loop bounds and use deterministic spacing. |
| SEQ-014 | Events and held gates crossing loop boundaries behave correctly. |
| SEQ-015 | Nine parameter locks apply only to their event and serialize. |
| SEQ-016 | Per-step reverse and slice selection serialize/render. |
| SEQ-017 | Stop and panic clear events, voices, and gates. |
| SEQ-018 | 16 Levels polyphony and generated-event recording. |
| SEQ-019 | Roll division changes create no duplicates or stuck notes. |

## Audio — Milestones 1, 2, 6, and 7

| ID | Test |
|---|---|
| AUDIO-001 | Reuse fully inactive voices before stealing. |
| AUDIO-002 | Global exhaustion follows released-level/release-level/age/index order. |
| AUDIO-003 | Per-pad local limit is deterministic. |
| AUDIO-004 | Choke groups ramp without allocation or stale ownership. |
| AUDIO-005 | Identical levels use oldest age then stable index. |
| AUDIO-006 | Rapid retrigger and mono behavior are deterministic. |
| AUDIO-007 | Stop/panic releases every ownership and MIDI gate. |
| AUDIO-008 | ADSR, pitch interpolation, forward/reverse, loop, and choke behavior. |
| AUDIO-009 | All render paths contain no NaN, infinity, or unsafe output peak. |
| AUDIO-010 | Writer overflow marks incomplete, increments counter, deletes temp, creates no asset. |
| AUDIO-011 | Input/master/pad/bus capture writers finalize only off callback. |
| AUDIO-012 | Skipback recreation on sample-rate change occurs off callback. |
| AUDIO-013 | Resampling prevents feedback and handles measured latency. |
| AUDIO-014 | Device start/stop/format changes clear safely without hardware in tests. |

## Persistence — Milestones 8 and 10

| ID | Test |
|---|---|
| SAVE-002 | Full schema save/load semantic equality. |
| SAVE-003 | External metadata versus collected-source archive entries. |
| SAVE-004 | Derived assets, provenance, and cache regeneration. |
| SAVE-005 | Canonical JSON key order, UTF-8, number and path formatting. |
| SAVE-006 | Deterministic archive mode entry order/timestamps/metadata. |
| SAVE-007 | Ordinary save is semantically equal without requiring identical ZIP bytes. |
| SAVE-008 | Missing source, locate-file, and locate-folder relinking. |
| SAVE-009 | Corrupt/truncated/traversal/duplicate/oversized archive rejection. |
| SAVE-010 | Sibling temp validation, backup rotation, and recovery. |
| SAVE-011 | Autosave recovery and stale temporary cleanup. |
| SAVE-012 | Migration from every retained older fixture without silent data loss. |
| SAVE-013 | Save compaction removes only safely unreferenced derived/cache data. |

## Export — Milestone 7

| ID | Test |
|---|---|
| EXPORT-001 | Master mix includes full routing/master processing. |
| EXPORT-002 | Dry pad stem is post gain/pan and excludes sends/returns/master. |
| EXPORT-003 | Isolated processed stem includes only target influence on buses. |
| EXPORT-004 | Bus-return stem uses full-project sends and excludes unrelated output. |
| EXPORT-005 | Optional no-limiter master is explicitly labelled and differs as expected. |
| EXPORT-006 | Channel count, sample rate, duration, and non-empty output. |
| EXPORT-007 | Mute/solo routing inclusion and exclusion. |
| EXPORT-008 | Common configured tail length and effect-tail tolerance. |
| EXPORT-009 | Parameter locks, slices, reverse, probability, and automation included. |
| EXPORT-010 | Deterministic base names and collision-safe suffixes. |
| EXPORT-011 | Cancellation removes every `.part` file. |
| EXPORT-012 | Float renders meet finite/peak/RMS/onset/value/spectral tolerances. |

## Threading and UI-independent integration — milestone shown

| ID | Milestone | Test |
|---|---:|---|
| THREAD-004 | 1 | Stress command queue without lost accepted commands. |
| THREAD-005 | 1 | Memory-budget commit racing with replacement is safe. |
| THREAD-006 | 2 | Worker cancellation and stale UUID/revision discard. |
| THREAD-007 | 2 | Close project/app cancels or drains jobs and writers safely. |
| THREAD-008 | 2 | Writer never blocks callback and reports saturation. |
| THREAD-009 | 8 | Compression/extraction/collection failure leaves live model unchanged. |
| UIHEADLESS-003 | 1 | Construct controllers; switch all four banks and trigger synthetic control path. |
| UIHEADLESS-004 | 3 | Switch editor modes and perform transactional chop controller flow. |
| UIHEADLESS-005 | 4 | Load project and schedule a short pattern without devices. |
| UIHEADLESS-006 | 7 | Render and cancel a short pattern/song/stem batch. |

No test above is considered implemented merely because its entry exists. `STATUS.md` records which
IDs actually pass. Deferred entries are assigned to the stated milestone because their production
subsystem does not exist in Milestone 0.

