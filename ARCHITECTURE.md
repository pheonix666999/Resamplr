# PadFlow Architecture

## Targets and ownership

- `padflow_core`: product configuration, project model, immutable assets, jobs, and serialization.
- `padflow_audio`: real-time interfaces and deterministic render/capture infrastructure.
- `padflow_ui`: JUCE desktop components only.
- `PadFlow`: standalone GUI application.
- `padflow_tests`: headless JUCE unit/integration tests registered with CTest.
- `padflow_smoke`: console executable using the same core smoke scenario as GUI headless mode.

The message thread owns live model commits and undo history. Workers own expensive temporary work
and return immutable results. The audio callback consumes prepublished snapshots and fixed-size
commands. Writer threads own files and audio-file encoders. UI observes controller snapshots and
never reaches into audio state directly.

## Musical time

Persist positions at 960 PPQ as `int64 wholePpqTicks + uint16 fractionalTickQ16`. Fractions have
1/65536-tick precision. Signed swing/nudge offsets use Q16.16 ticks. Tempo is stored as integer
micro-BPM. Sample rate and buffer size never enter serialized musical positions. Scheduling uses
checked 128-bit intermediate arithmetic and round-to-nearest/ties-to-even to produce absolute
sample frames; block offsets exist only in runtime events.

## Jobs and immutable assets

`BackgroundJobSystem` has a bounded priority queue (default 256) and 2–8 workers. Every job carries
owner UUID, target UUID, target revision, cancellation, and progress. A completed result is committed
only after the message thread revalidates those values. Queue exhaustion, cancellation, failure, or
stale targets leave the project unchanged.

Decoded mono/stereo float PCM is immutable and shared. The Milestone 1 registry defaults to 2 GiB
and accepts a configured limit from 256 MiB through `min(16 GiB, 50% physical RAM)`. Unique
source/derived PCM counts once. WAV, AIFF, and FLAC readers
run only on bounded workers. Before allocating decoded PCM they validate channel count, frame
arithmetic, and the request budget. Message-thread commit rechecks owner UUID, target UUID, and
revision, publishes the immutable asset, then atomically assigns the layer and external reference;
failure rolls registry publication back.
Audio-visible asset retirement is epoch-acknowledged and reclaimed off the callback. The provider
boundary permits future streaming without changing pad and voice identities.

## Real-time and writers

UI/model commands cross bounded SPSC queues. The callback uses no allocation, file I/O, logging,
locks, GUI calls, shared-owner destruction, or writer APIs. Capture sessions allocate at least four
seconds of block storage before arming. The callback copies samples and enqueues indices; a dedicated
writer owns a `.part` file. Overflow increments an atomic counter, marks the result incomplete,
deletes the temporary output, warns the user, and never creates an asset.

Milestone 1 playback uses an immutable raw-view snapshot published off callback, a bounded SPSC
command queue, and exactly 128 stable-index preallocated voices. Each published wrapper retains the
immutable asset owners referenced by its raw views. The callback reports the oldest generation
still used by active voices; the message thread reclaims older wrappers only after that
acknowledgement. Voice selection is deterministic, with pad-local limits before global oldest-age
stealing and pool index as the final tie-breaker. Rendering performs source/output-rate conversion,
four-point Hermite interpolation, ADSR, layer and pad gain/pan/tuning, velocity selection,
mono/poly behavior, gate/one-shot/toggle modes, choke release, finite-output guards, panic, and
atomic metering without callback ownership changes.

## Determinism

Probability algorithm `siphash24-v1` hashes a canonical fixed-width tuple of project seed, pattern,
event, optional song instance, loop iterations, and algorithm version. It is stateless and independent
of traversal, platform, buffer size, and unrelated events.

The global pool has 128 stable-index voices. Allocation applies choke, local pad limit, inactive and
completed-release reuse, lowest released envelope, lowest release-stage envelope, oldest monotonic
trigger age, then stable index. Stop/panic clears all ownership.

## Persistence and editing

`.padflow` is a ZIP-compatible archive with canonical `manifest.json`, optional collected sources,
derived assets, optional regenerable caches, and recovery metadata. Non-collected sources stay
external and use metadata plus SHA-256 when available. Save builds and validates a sibling temporary,
rotates one backup, and replaces the destination using the safest platform mechanism.

Metadata edits reference existing PCM. PCM transforms render new immutable assets with provenance.
Undo switches UUID references. Unreferenced derived data is reclaimed only by explicit cleanup or
save compaction.

See `docs/AUDIO_THREAD_RULES.md` and `docs/PROJECT_FORMAT.md` for the normative contracts.
