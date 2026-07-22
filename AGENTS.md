# PadFlow Repository Instructions

Read `REQUIREMENTS.md`, `ARCHITECTURE.md`, `PLANS.md`, `STATUS.md`, and `TEST_PLAN.md`
before changing this repository. `TEST_PLAN.md` is the authoritative test specification;
`prompt.txt` and chat history are not test authorities.

## Non-negotiable engineering rules

- Never allocate memory, access files, acquire blocking locks, log, or touch GUI objects from a
  real-time audio callback.
- The callback may only process prepublished state, copy into preallocated buffers, use bounded
  lock-free queues, and update atomics.
- Keep UI, model, sequencing, background work, persistence, and audio rendering separated.
- Expensive work runs in bounded background jobs. Workers return immutable results; only the
  message thread may validate and commit them to the live model.
- Recording uses preallocated FIFOs and dedicated writer threads. The callback never calls an
  audio-file writer or filesystem API.
- Use RAII, smart pointers, explicit ownership, and no raw owning pointers. Destruction of audio
  assets must never occur on the callback.
- Preserve deterministic PPQ serialization, probability decisions, event scheduling, project
  output, and offline rendering decisions.
- Add or update a stable `TEST_PLAN.md` regression identifier for every bug fixed.
- Run format, clean configure, Debug and Release builds, tests, smoke tests, and applicable
  artifact checks before marking a milestone complete.
- Treat project-owned warnings as errors. Do not apply that policy to JUCE or other third-party
  source.
- Update `STATUS.md` when work is completed, deferred, blocked, or validated.
- Keep commits scoped to one milestone. Never commit credentials, certificates, signing files,
  copyrighted samples, or copied product artwork.
- Do not leave placeholder implementations for an acceptance criterion being claimed complete.
- Document platform-specific behavior and preserve project-schema compatibility after schema v1.
- Upgrade pinned dependencies only at milestone boundaries.

The invalid reference video is tracked as `BLOCKED_REFERENCE_ASSET`. It does not block the
Milestone 0 foundation, but exact reference UI or chopping parity must not be claimed.

