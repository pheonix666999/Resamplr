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

### Milestone 0 CI regressions

| ID | Test | Acceptance |
|---|---|---|
| REGRESSION-CI-001 | Windows toolchain selection | Windows presets require `cl`; CI initializes the installed MSVC x64 toolchain, never unsupported MinGW. |
| REGRESSION-CI-002 | Third-party warning boundary | JUCE includes are system headers; PadFlow sources retain warnings as errors. |
| REGRESSION-CI-003 | Headless UTF-8 diagnostics | GUI headless smoke compiles on Apple Clang and prints the JUCE UTF-8 pointer directly. |
| REGRESSION-CI-004 | Pre-GUI argument parsing | Linux/macOS parse raw process arguments before JUCE startup, so headless smoke never initializes a display. |
| REGRESSION-CI-005 | Intentional queue alignment | MSVC `/WX` accepts documented cache-line padding while all other project warnings remain errors. |
| REGRESSION-CI-006 | Windows manifest encoding | Windows PowerShell packaging writes canonical BOM-free UTF-8 accepted by artifact verification. |
| REGRESSION-CI-007 | Core-only stable UUIDs | Deterministic model UUID generation compiles with the linked `juce_core` module and does not rely on `juce_cryptography`. |
| REGRESSION-CI-008 | JUCE character namespace | Bank-name construction uses the qualified `juce::juce_wchar` alias on Apple Clang, GCC, and MSVC. |
| REGRESSION-CI-009 | Extensible asset fixtures | Foundation fixtures explicitly initialize required immutable-asset metadata and remain warning-clean when metadata gains optional fields. |
| REGRESSION-CI-010 | Device/input JUCE boundaries | Device-query methods match JUCE's mutable API, MIDI callback declarations include their owning module, and keyboard routing remains independent of GUI-only key classes. |
| REGRESSION-CI-011 | Exact-width coarse pitch UI | The coarse-pitch slider converts directly to the model's validated `int16_t` storage without an intermediate narrowing diagnostic on GCC, Apple Clang, or MSVC. |
| REGRESSION-CI-012 | Component/key-listener overload visibility | The sampler view preserves JUCE `Component` keyboard overload visibility while implementing `KeyListener`, remaining clean under Apple Clang's overloaded-virtual diagnostic. |
| REGRESSION-CI-013 | Published snapshot wrapper generation | Snapshot reclamation reads the generation from the owned playback payload after immutable asset owners are added to the publication wrapper. |
| REGRESSION-CI-014 | Quiescent snapshot-reclamation assertion | The baseline reclamation test explicitly clears prior input voices before asserting that only the current immutable snapshot remains. |

## Milestone 1 playable sampler

These granular IDs are the Milestone 1 acceptance authority. Programmatically generated fixtures
must be used; physical audio and MIDI hardware are never required.

### Model and editing

| ID | Acceptance |
|---|---|
| MODEL-M1-001 | A new project contains exactly four banks named A–D. |
| MODEL-M1-002 | Every bank contains exactly sixteen addressable pads. |
| MODEL-M1-003 | All sixty-four pad UUIDs are non-empty, unique, stable, and deterministic in fixtures. |
| MODEL-M1-004 | Copy/paste duplicates pad state and asset references without copying decoded PCM. |
| MODEL-M1-005 | Duplicate preserves values while generating required new pad/layer UUIDs. |
| MODEL-M1-006 | A parameter command and its undo/redo restore exact prior/next state and revision. |
| MODEL-M1-007 | Clear pad removes assignments and resets parameters without affecting other pads/assets. |
| MODEL-M1-008 | Rename/recolor accepts valid values and rejects invalid names/colours without mutation. |

### Assets and imports

| ID | Acceptance |
|---|---|
| ASSET-M1-001 | A generated WAV decodes asynchronously to immutable PCM with correct metadata. |
| ASSET-M1-002 | A generated AIFF decodes asynchronously to immutable PCM with correct metadata. |
| ASSET-M1-003 | A generated FLAC decodes asynchronously to immutable PCM with correct metadata. |
| ASSET-M1-004 | Invalid, corrupt, and unsupported-channel audio is rejected without model mutation. |
| ASSET-M1-005 | A generated mono asset preserves one channel and exact frame accounting. |
| ASSET-M1-006 | A generated stereo asset preserves two channels and interleaving. |
| ASSET-M1-007 | 44.1/48/88.2/96 kHz fixtures preserve source-rate metadata. |
| ASSET-M1-008 | Decoded-memory usage equals checked frame × channel × float-byte accounting. |
| ASSET-M1-009 | Multiple layer/pad references count a unique decoded asset exactly once. |
| ASSET-M1-010 | An over-budget import fails before project or asset-registry mutation. |
| ASSET-M1-011 | Rapid replacement retains live readers and publishes only the latest valid revision. |
| ASSET-M1-012 | Project unload cancels imports and releases assets outside the callback. |
| ASSET-M1-013 | Audio-visible replacement destruction occurs only after epoch acknowledgement off callback. |
| ASSET-M1-014 | A completed import for a stale target UUID/revision is discarded. |
| ASSET-M1-015 | A cancelled import leaves the model, undo stack, budget, and playback state unchanged. |

### Layers

| ID | Acceptance |
|---|---|
| LAYER-M1-001 | Every pad supports at least four stable layer slots. |
| LAYER-M1-002 | Velocity minimum and maximum boundaries are inclusive. |
| LAYER-M1-003 | Inverted or out-of-domain velocity ranges are rejected without mutation. |
| LAYER-M1-004 | Every enabled overlapping layer plays in stable layer order. |
| LAYER-M1-005 | Empty disabled or unassigned layers are valid and silent. |
| LAYER-M1-006 | UUIDs, asset refs, ranges, enabled state, gain/pan/tuning persist and round-trip. |

### Audio rendering and voice allocation

| ID | Acceptance |
|---|---|
| AUDIO-M1-001 | The engine owns exactly 128 stable-index preallocated voices. |
| AUDIO-M1-002 | Allocation selects the lowest-index fully inactive eligible voice. |
| AUDIO-M1-003 | A pad-local voice limit releases/steals deterministically before global allocation. |
| AUDIO-M1-004 | Mono retrigger clears prior ownership and uses the documented short release. |
| AUDIO-M1-005 | Choke-group triggering releases all other group members before allocation. |
| AUDIO-M1-006 | Completed release, lowest released envelope, then other release envelope order is exact. |
| AUDIO-M1-007 | Global exhaustion selects the oldest active trigger age. |
| AUDIO-M1-008 | Equal candidates use stable pool index as final tie-breaker. |
| AUDIO-M1-009 | One-shot continues after source release until sample/envelope completion. |
| AUDIO-M1-010 | Gate release enters release and cannot leave a stuck voice. |
| AUDIO-M1-011 | Toggle alternates deterministic start and stop for the same source. |
| AUDIO-M1-012 | Attack, decay, sustain, and release stages meet documented sample tolerances. |
| AUDIO-M1-013 | Pad/layer gain produces expected finite amplitude. |
| AUDIO-M1-014 | Constant-power pan produces expected left/right values. |
| AUDIO-M1-015 | Coarse pitch uses the documented semitone ratio and changes duration. |
| AUDIO-M1-016 | Fine pitch uses the documented cent ratio and changes duration. |
| AUDIO-M1-017 | Source-rate/output-rate conversion uses the documented playback ratio. |
| AUDIO-M1-018 | Four-point Hermite interpolation produces bounded finite output at edge positions. |
| AUDIO-M1-019 | Every supported rendering path produces zero NaN/infinity samples. |
| AUDIO-M1-020 | Stop/panic silences and clears all voices and ownership. |
| AUDIO-M1-021 | A stolen voice retains no pad/layer/MIDI/source/gate/choke ownership. |
| AUDIO-M1-022 | Repeated identical allocation scenarios produce identical voice-index traces. |
| AUDIO-M1-023 | Missing/unpublished assets and unavailable output produce exact safe silence. |
| AUDIO-M1-024 | Sample-rate change resets runtime state and recomputes ratios off callback. |
| AUDIO-M1-025 | Buffer-size variants preserve resolved output and completion within tolerance. |

### Trigger input

| ID | Acceptance |
|---|---|
| INPUT-M1-001 | Mouse press triggers one-shot at configured fixed velocity. |
| INPUT-M1-002 | Mouse release/capture loss releases gate mode without a stuck note. |
| INPUT-M1-003 | The default 4×4 keyboard map triggers the active-bank pad. |
| INPUT-M1-004 | Repeated key-down is ignored until a matching release. |
| INPUT-M1-005 | MIDI note-on triggers the mapped pad and records MIDI ownership. |
| INPUT-M1-006 | MIDI note-off releases only matching gate ownership. |
| INPUT-M1-007 | MIDI velocity selects inclusive layers and scales the trigger. |
| INPUT-M1-008 | MIDI note-on velocity zero behaves exactly as note-off. |
| INPUT-M1-009 | MIDI device disconnect invokes panic and clears ownership. |
| INPUT-M1-010 | Omni and channels 1–16 filters accept/reject deterministically. |
| INPUT-M1-011 | Keyboard and default chromatic MIDI mapping follow the selected bank. |

### Persistence

| ID | Acceptance |
|---|---|
| SAVE-M1-001 | A loaded pad and external asset record survive semantic round-trip. |
| SAVE-M1-002 | Every layer field and stable UUID survive semantic round-trip. |
| SAVE-M1-003 | Every implemented pad parameter survives semantic round-trip. |
| SAVE-M1-004 | Keyboard assignments and fixed trigger velocity survive round-trip. |
| SAVE-M1-005 | MIDI notes, channel filter, and stable device preference survive round-trip. |
| SAVE-M1-006 | Missing external assets retain pad/layer state and explicit missing status. |
| SAVE-M1-007 | Invalid persisted ranges are rejected with diagnostics and no partial commit. |
| SAVE-M1-008 | Project/bank/pad/layer/asset UUIDs are unchanged by save/load. |
| SAVE-M1-009 | A Milestone 0 schema-v1 manifest loads with deterministic default banks, pads, layers, and mappings. |

### Threading

| ID | Acceptance |
|---|---|
| THREAD-M1-001 | Import worker cancellation completes without publication or leaked outstanding work. |
| THREAD-M1-002 | Owner/target UUID or revision mismatch discards the immutable result. |
| THREAD-M1-003 | Full command/job queues return observable failure and accept no silent drop. |
| THREAD-M1-004 | Rapid immutable playback publication is epoch-safe and deterministic. |
| THREAD-M1-005 | Final asset destruction is observed on a non-audio thread. |
| THREAD-M1-006 | Project close during import cancels/drains safely and produces no late mutation. |

### Devices and preview

| ID | Acceptance |
|---|---|
| DEVICE-M1-001 | Mock device setup persists output channels, rate, and buffer size. |
| DEVICE-M1-002 | Unavailable/open-failed device reports an error and renders safe silence. |
| DEVICE-M1-003 | Test tone is bounded, finite, explicitly started/stopped, and uses no input permission. |
| DEVICE-M1-004 | CPU/dropout snapshots are atomic; reset clears the dropout count off callback. |
| PREVIEW-M1-001 | Generated WAV/AIFF/FLAC can be previewed before assignment. |
| PREVIEW-M1-002 | File change and stop release the fixed preview voice without stale ownership. |
| PREVIEW-M1-003 | Failed preview leaves silence, no active preview voice, and a user-facing error. |
| PREVIEW-M1-004 | Preview volume is bounded, persistent, and applied without callback allocation. |

### Milestone 1 regressions

| ID | Test | Acceptance |
|---|---|---|
| REGRESSION-M1-001 | MIDI callback producer isolation | Hardware MIDI writes only to its bounded SPSC ingress; overflow is observable and message-thread flushing preserves the engine queue's single-producer contract. |
| REGRESSION-M1-002 | Device-error panic ownership | An asynchronous device error requests panic atomically; voice mutation occurs on the next audio callback rather than the error-reporting thread. |
| REGRESSION-M1-003 | Live playback snapshot retirement | Message-thread publication retains immutable snapshots until the audio callback acknowledges a newer generation; reclamation never occurs on the callback. |
| REGRESSION-M1-004 | Unified project edit undo | Pad imports, asset-reference changes, mappings, and persistent device settings undo/redo the complete validated project state without orphaning model records. |
| REGRESSION-M1-005 | Active playback asset lifetime | A voice started from an older snapshot keeps its immutable sample owner alive after registry replacement until the callback reports that the voice no longer references that generation. |
| REGRESSION-M1-006 | Loaded external asset availability | Project load refreshes persisted missing flags from the filesystem without clearing layer references and asynchronously resolves every available external asset. |
| REGRESSION-M1-007 | Device transition callback quiescence | Apply/restart removes the audio callback before directly resetting playback/preview state and reinstalls it after the device transition. |
| REGRESSION-M1-008 | Production decoded-memory default | The default registry requests 2 GiB and clamps configured production values to 256 MiB through `min(16 GiB, 50% physical RAM)` while injected test registries retain explicit limits. |
| REGRESSION-M1-009 | Explicit audio-input activation | Playback starts with zero input channels; input device/channel persistence is applied only after explicit user selection, while output routing remains independently configurable. |

### UI-independent and GUI-headless integration

| ID | Acceptance |
|---|---|
| UIHEADLESS-M1-001 | The main sampler view constructs with accessible named controls. |
| UIHEADLESS-M1-002 | Bank selection visits A–D and exposes sixteen pads each. |
| UIHEADLESS-M1-003 | Selection visits every global pad index without invalid access. |
| UIHEADLESS-M1-004 | A generated sample imports through worker/controller commit into A1. |
| UIHEADLESS-M1-005 | Triggering loaded A1 renders finite non-silence. |
| UIHEADLESS-M1-006 | A populated project saves, reloads, resolves refs, and retriggers. |
| UIHEADLESS-M1-007 | Audio-disabled mode retains model/import/save/UI behavior and safe silence. |
| UIHEADLESS-M1-008 | Keyboard mapping edit, duplicate policy, active bank, and persistence work. |
| UIHEADLESS-M1-009 | MIDI mapping/channel/device model edits and persistence work without hardware. |
| UIHEADLESS-M1-010 | File drop assigns multiple generated files sequentially with overwrite policy. |
| UIHEADLESS-M1-011 | Main layout meets minimum bounds and exposes focus indicators at tested scales. |

## Milestone 2 waveform editing and recording

All Milestone 2 fixtures are synthetic and all capture input is mocked. No physical audio interface
or microphone is required. Frame boundaries use an inclusive start and exclusive end. Invalid edits
are rejected without mutation unless an acceptance row explicitly specifies deterministic clamping.

### Waveform cache

| ID | Acceptance |
|---|---|
| WAVE-M2-001 | A mono asset produces immutable per-channel minimum/maximum peak summaries. |
| WAVE-M2-002 | A stereo asset preserves independent left/right peak summaries. |
| WAVE-M2-003 | One-frame and very short assets produce valid non-empty cache levels. |
| WAVE-M2-004 | Successive resolution levels cover the complete source with deterministic block aggregation. |
| WAVE-M2-005 | Asset UUID, fingerprint, algorithm, channel, or frame mismatch invalidates a cached result. |
| WAVE-M2-006 | A completion for a stale project/asset UUID or revision is discarded without publication. |
| WAVE-M2-007 | Cancellation leaves the cache registry and project unchanged and releases work safely. |
| WAVE-M2-008 | Separate cache accounting is checked, bounded, counts shared entries once, and rejects over-budget publication. |

### Frame-bound editing

| ID | Acceptance |
|---|---|
| EDIT-M2-001 | A newly assigned asset defaults to `[0, sourceFrameCount)` with loop bounds matching trim, loop off, and reverse off. |
| EDIT-M2-002 | Trim and loop starts are inclusive, ends exclusive, and every accepted range contains at least one frame. |
| EDIT-M2-003 | A one-frame trim and one-frame loop are valid and stable. |
| EDIT-M2-004 | A trim start outside `[0, endFrame)` is rejected without mutation. |
| EDIT-M2-005 | A trim end outside `(startFrame, sourceFrameCount]` is rejected without mutation. |
| EDIT-M2-006 | A loop wholly inside the active trim is accepted. |
| EDIT-M2-007 | A loop outside the active trim is rejected without mutation. |
| EDIT-M2-008 | Shrinking trim clamps both loop bounds into trim while preserving one frame where possible; otherwise loop resets to trim and is disabled. |
| EDIT-M2-009 | One completed marker drag creates exactly one undo entry and undo/redo restores exact bounds. |
| EDIT-M2-010 | Reverse enablement survives semantic save/load and undo/redo. |
| EDIT-M2-011 | Loop enablement and bounds survive semantic save/load and undo/redo. |

### Trim, loop, and reverse playback

| ID | Acceptance |
|---|---|
| AUDIO-M2-001 | Forward playback begins exactly at the inclusive trim start. |
| AUDIO-M2-002 | Forward non-loop playback stops before the exclusive trim end. |
| AUDIO-M2-003 | Reverse playback begins immediately before the exclusive trim end. |
| AUDIO-M2-004 | Reverse non-loop playback stops before crossing the inclusive trim start. |
| AUDIO-M2-005 | Forward playback wraps inside `[loopStartFrame, loopEndFrame)`. |
| AUDIO-M2-006 | Reverse playback wraps inside `[loopStartFrame, loopEndFrame)`. |
| AUDIO-M2-007 | Forward and reverse wrapping retain fractional overshoot deterministically. |
| AUDIO-M2-008 | A one-frame loop stays finite, in range, and stoppable. |
| AUDIO-M2-009 | Gate release continues within the loop until envelope completion. |
| AUDIO-M2-010 | Hermite interpolation at every trim/loop/source boundary is finite and deterministically clamped. |
| AUDIO-M2-011 | Guarded fixtures detect no source read outside `[0, sourceFrameCount)`. |
| AUDIO-M2-012 | Reverse and boundary edits affect new triggers only; active voices retain their published state. |
| AUDIO-M2-013 | Every Milestone 1 playback, voice-allocation, trigger, and panic test remains valid. |

### Derived immutable assets

| ID | Acceptance |
|---|---|
| DERIVED-M2-001 | Normalize scales non-silent PCM without changing source PCM. |
| DERIVED-M2-002 | Normalize leaves silent PCM silent with no NaN or infinity. |
| DERIVED-M2-003 | Default normalize output peaks at -1.0 dBFS within numerical policy. |
| DERIVED-M2-004 | Stereo-to-mono output is `(left + right) * 0.5` per frame. |
| DERIVED-M2-005 | Mono-to-mono returns an explicit deterministic reusable no-op result. |
| DERIVED-M2-006 | Linear fade-in starts at zero, reaches unity at its boundary, and never reads outside the operation region. |
| DERIVED-M2-007 | Linear fade-out starts at unity, ends at zero, and never reads outside the operation region. |
| DERIVED-M2-008 | Crop output contains exactly `[startFrame, endFrame)` and reports its exact frame count. |
| DERIVED-M2-009 | Crop resets assigned trim to the complete derived asset. |
| DERIVED-M2-010 | Crop rebases a retained valid loop; otherwise it resets loop to trim and disables it. |
| DERIVED-M2-011 | Every operation leaves the source file bytes and source immutable PCM unchanged. |
| DERIVED-M2-012 | Identical canonical source fingerprint/operation/version/parameters reuse one derived recipe result. |
| DERIVED-M2-013 | Cancellation removes temporary output and leaves model, registry, and undo history unchanged. |
| DERIVED-M2-014 | A stale project/pad/layer/revision completion is discarded without assignment. |
| DERIVED-M2-015 | Undo/redo switches the layer asset reference while retaining both immutable assets off callback. |
| DERIVED-M2-016 | Unreferenced derived data remains available for undo and is removed only by explicit cleanup/compaction. |

### Recording

| ID | Acceptance |
|---|---|
| RECORD-M2-001 | Preparing a session allocates at least four seconds of bounded FIFO storage and all pre-roll storage before arming. |
| RECORD-M2-002 | Manual start/stop records the exact accepted synthetic frames and reaches Completed. |
| RECORD-M2-003 | Threshold mode remains Waiting while the input peak is below the configured threshold. |
| RECORD-M2-004 | The first threshold crossing starts one capture exactly once. |
| RECORD-M2-005 | Included pre-roll frames are written in chronological order. |
| RECORD-M2-006 | Pre-roll remains chronological across circular-buffer wrap. |
| RECORD-M2-007 | The pre-roll/live boundary contains no duplicated or omitted trigger frames. |
| RECORD-M2-008 | Mono input produces a valid mono recording with exact frame accounting. |
| RECORD-M2-009 | Stereo input preserves interleaving and exact frame accounting. |
| RECORD-M2-010 | Writer stop drains, finalizes, flushes, closes, validates, and publishes only off callback. |
| RECORD-M2-011 | A successful default WAV has a readable header, expected channels/rate/frames, and finite samples. |
| RECORD-M2-012 | FIFO saturation atomically increments overflow and marks the session incomplete. |
| RECORD-M2-013 | Incomplete capture deletes `.part`/output and creates no successful asset or undo entry. |
| RECORD-M2-014 | Cancellation shuts down safely, deletes temporary output, and creates no assignment. |
| RECORD-M2-015 | Repeated recording names publish through deterministic collision-safe suffixes. |
| RECORD-M2-016 | A stale project/pad/layer/revision never mutates another destination; a valid file may remain unassigned. |
| RECORD-M2-017 | A completed valid recording publishes through the immutable registry and assigns the selected layer. |
| RECORD-M2-018 | Undo/redo removes and restores the recording assignment without rewriting recorded PCM. |
| RECORD-M2-019 | Device/sample-rate change is rejected while active or cancels safely before recreating storage off callback. |
| RECORD-M2-020 | Project/application close cancels or drains the writer and leaves no abandoned temporary capture. |

### Milestone 2 threading

| ID | Acceptance |
|---|---|
| THREAD-M2-001 | Waveform worker cancellation cannot publish a late result. |
| THREAD-M2-002 | Derived-render cancellation cannot publish a file, asset, or model edit. |
| THREAD-M2-003 | A full capture FIFO returns failure immediately and never silently accepts a descriptor. |
| THREAD-M2-004 | A deliberately slow writer cannot block the producer and yields observable incomplete status. |
| THREAD-M2-005 | Callback capture paths contain no filesystem or audio-writer operation. |
| THREAD-M2-006 | Steady-state capture and pre-roll callback paths perform zero allocation. |
| THREAD-M2-007 | Writer shutdown drains or cancels deterministically and joins off callback. |
| THREAD-M2-008 | Recording asset publication occurs only after writer finalization and validation. |
| THREAD-M2-009 | Waveform, derived, and recorded immutable owners are destroyed off the audio thread. |

### Milestone 2 persistence

| ID | Acceptance |
|---|---|
| SAVE-M2-001 | Milestone 1 schema-v1 projects load with deterministic full-range editing defaults. |
| SAVE-M2-002 | Trim bounds survive semantic round trip exactly. |
| SAVE-M2-003 | Loop bounds and enablement survive semantic round trip exactly. |
| SAVE-M2-004 | Reverse and zero-crossing preference survive semantic round trip exactly. |
| SAVE-M2-005 | Derived records and canonical provenance survive semantic round trip. |
| SAVE-M2-006 | Recorded project-owned asset records survive semantic round trip. |
| SAVE-M2-007 | Missing derived/recorded files retain references and explicit missing state. |
| SAVE-M2-008 | Invalid persisted boundaries produce diagnostics and no partial project commit. |
| SAVE-M2-009 | Missing/invalid waveform caches are ignored and regenerable because caches are non-authoritative. |
| SAVE-M2-010 | A populated Milestone 2 project is semantically equal after save/load. |

### Milestone 2 UI-independent and GUI-headless integration

| ID | Acceptance |
|---|---|
| UIHEADLESS-M2-001 | The waveform editor constructs with accessible named controls and explicit loading/missing states. |
| UIHEADLESS-M2-002 | Every layer can be selected without invalid access and displays its own edit state. |
| UIHEADLESS-M2-003 | Controller/UI commits a valid trim as one transaction and republishes playback. |
| UIHEADLESS-M2-004 | Controller/UI rejects an invalid trim without model or playback mutation. |
| UIHEADLESS-M2-005 | Loop control commits only valid bounds and enablement. |
| UIHEADLESS-M2-006 | Reverse control updates new triggers and visible direction state. |
| UIHEADLESS-M2-007 | A derived operation reports progress and commits one valid immutable result. |
| UIHEADLESS-M2-008 | Recording controls traverse only valid explicit state transitions. |
| UIHEADLESS-M2-009 | A completed recording can be assigned to the selected destination and retriggered. |
| UIHEADLESS-M2-010 | Populated trim/reverse/loop/derived/recording state saves, reloads, resolves, and renders finite non-silence. |

### Milestone 2 regressions

| ID | Test | Acceptance |
|---|---|---|
| REGRESSION-M2-001 | Legacy raw playback snapshot bounds | A Milestone 1/raw test snapshot whose new trim fields are all zero resolves to the complete immutable asset in the callback, while initialized model snapshots remain prevalidated. |
| REGRESSION-M2-002 | Portable waveform-editor compilation | Waveform drawing uses JUCE's supported path API, numeric domain changes are explicit, and heterogeneous controls use a typed component array under warnings-as-errors. |
| REGRESSION-M2-003 | Typed asynchronous completion routing | Late cancelled import completions are routed by explicit job kind and never access an empty UI import queue or masquerade as waveform/derived/recording work. |
| REGRESSION-M2-004 | Portable recording-control suffixes | Recording sliders use JUCE's supported `setTextValueSuffix` API and every aggregate job target carries an explicit operation kind under warnings-as-errors. |
| REGRESSION-M2-005 | Nested recording controls in headless UI | UI acceptance checks traverse the component hierarchy, and showing a recording panel without a desktop peer never attempts to take keyboard focus. |

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
