# `.padflow` Project Format

## Container and schema

A project is a single ZIP-compatible `.padflow` file. Schema v1 requires `manifest.json`. Optional
ordered paths are `Assets/Source/`, `Assets/Derived/`, `Cache/Waveforms/`, and `Recovery/`.
Waveform caches are never authoritative and may be deleted/regenerated.

The manifest uses explicit UTF-8, `/` separators, ordered keys, stable escaping, finite shortest
round-trip floating-point formatting, and lossless integer encodings. Signed 64-bit PPQ ticks and
other potentially unsafe JSON integers use canonical decimal strings.

## Milestone 1 model payload

Schema v1 remains the active schema. Milestone 1 manifests add required `audio`, `assets`, `banks`,
`midi`, and `ui` members while retaining the Milestone 0 root metadata. `banks` contains exactly
four ordered bank objects named A through D; every bank contains exactly sixteen pads; and every pad
contains exactly four layer records. Project, bank, pad, layer, and asset UUIDs are persisted
verbatim.

Pad records persist name, ARGB colour as an unsigned decimal string, keyboard key, MIDI note,
playback/polyphony/choke/voice parameters, gain/pan/tuning, and ADSR values. Layer records persist
their enabled state, external asset UUID, inclusive velocity range, gain, pan, tuning, and stable
UUID. External asset records persist path/name/format/fingerprint, byte and frame counts, source
metadata, and explicit missing status. Potentially unsafe 64-bit values and the project revision are
decimal strings.

Milestone 0 schema-v1 manifests without the model payload remain loadable. They receive deterministic
default banks, pads, layers, mappings, audio/MIDI settings, and UI state derived from the persisted
project UUID. A partially present Milestone 1 payload is invalid rather than silently defaulted.
Loading parses and validates a complete candidate state before committing it to the live project.

## Milestone 2 additive layer editing payload

Schema v1 remains active. An assigned layer edited or imported by Milestone 2 carries an `editing`
object with decimal-string `startFrame`, `endFrame`, `loopStartFrame`, and `loopEndFrame` values plus
loop, reverse, and zero-crossing-snap booleans. Starts are inclusive and ends are exclusive. Trim
must be non-empty inside the referenced source frame count, and loop must be non-empty inside trim.

Milestone 1 layer records without `editing` remain valid and resolve at runtime to the complete
source range with loop and reverse disabled. Once a Milestone 2 edit is committed, all editing
members are serialized together; partially present or invalid editing state is rejected without a
partial project commit. Waveform caches remain optional, non-authoritative, and regenerable.

Milestone 2 derived renders add a root `derivedAssets` array while retaining schema v1. Each record
stores the output and parent UUIDs, captured source fingerprint, operation identifier and algorithm
version, canonical parameters, output fingerprint, deterministic renderer metadata, and
project-owned relative path. Older Milestone 0/1 manifests may omit `derivedAssets` and load it as
an empty list. A present record must resolve both its exact parent fingerprint and its exact output
fingerprint; incomplete or dangling provenance is rejected before project state changes.

## Musical time

All musical positions use 960 PPQ. Absolute positions are `{ wholePpqTicks: int64,
fractionalTickQ16: uint16 }`; offsets use signed Q16.16 ticks. Patterns, songs, automation, and locks
never persist sample-frame or buffer offsets. Tempo uses integer micro-BPM and metre uses integer
numerator/denominator.

Probability records include an algorithm identifier. Schema v1 uses `siphash24-v1` and stores a
128-bit project seed plus stable event/pattern/song-instance UUIDs and iteration inputs.

## Assets

Collected source files reside under `Assets/Source/`. Non-collected sources remain external and store
UUID, normalized path, original name, size, modification-time hint, SHA-256 when available, channels,
sample rate, frame count, and optional project-relative path.

Derived PCM resides under `Assets/Derived/` and stores UUID, original/source UUID, operation/version,
canonical parameters, recipe hash, source/output fingerprints, and relevant creation metadata. Undo
changes references rather than embedding PCM. Compaction removes only data proven unreferenced by the
saved model, undo retention policy, recovery data, or an active job.

The v1 derived renderer normalizes the complete immutable source, converts stereo with
`(left + right) * 0.5`, applies linear fades only inside the active trim region, and crops the
half-open active trim range. Crop resets trim to the complete derived asset and rebases the valid
loop relative to the crop start. Rendering writes a sibling `.part-*` WAV and publishes it by a
same-directory rename; failed, cancelled, or stale newly-created outputs are removed.

## Save and recovery

1. Snapshot an immutable model revision on the message thread.
2. Build a sibling temporary archive on a worker.
3. Finalize/close, reopen, and validate schema, required entries, sizes, and fingerprints.
4. Rotate one prior file to `.padflow.bak`.
5. Use `ReplaceFileW` or same-filesystem rename where supported.
6. Preserve the backup and report a non-atomic fallback when atomic replacement is unavailable.
7. On startup, inspect and safely clean stale temporaries after recovery checks.

Ordinary saves require semantic round-trip equality, not byte-identical ZIPs. Deterministic archive
mode additionally normalizes entry order, timestamps, compression settings, separators, and metadata.

Loading rejects absolute/traversal paths, duplicate entries, corrupt data, unsupported schema, and
unreasonable expanded sizes. Unsupported data is never silently discarded. Migrations are explicit,
tested, one-way transformations that preserve the original backup.
