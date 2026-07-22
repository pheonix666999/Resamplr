# `.padflow` Project Format

## Container and schema

A project is a single ZIP-compatible `.padflow` file. Schema v1 requires `manifest.json`. Optional
ordered paths are `Assets/Source/`, `Assets/Derived/`, `Cache/Waveforms/`, and `Recovery/`.
Waveform caches are never authoritative and may be deleted/regenerated.

The manifest uses explicit UTF-8, `/` separators, ordered keys, stable escaping, finite shortest
round-trip floating-point formatting, and lossless integer encodings. Signed 64-bit PPQ ticks and
other potentially unsafe JSON integers use canonical decimal strings.

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

