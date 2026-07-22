# Audio Thread Rules

This file is normative for every callback reached from a physical device or real-time render.

## Permitted

- Read immutable prepublished state whose lifetime is guaranteed through the current audio epoch.
- Process preallocated voices, events, DSP buffers, and scratch space.
- Push/pop fixed-size values through bounded lock-free queues.
- Copy samples into already acquired capture blocks.
- Update lock-free atomics for meters, counters, state, and epoch acknowledgement.
- Produce silence safely when state or a device is unavailable.

## Forbidden

- Heap allocation/deallocation, reference-count destruction, or container growth.
- File open/close/read/write, encoding, flush, archive, path, or other filesystem operations.
- Mutexes, condition variables, waits, sleeps, blocking atomics, or unbounded loops.
- Logging, exceptions escaping the callback, GUI calls, message-thread calls, or user callbacks.
- JUCE audio-file writers, sample decoding, waveform analysis, or project mutation.

## Commands and state

Each producer uses an appropriate bounded queue; SPSC queues are not silently shared by multiple
producers. The callback validates command type, index, and generation. Queue overflow is observable
and returns failure to the producer; commands are never accepted and silently dropped.

Assets are immutable. Audio code uses non-owning views into an epoch-published asset set. Replaced
sets enter a retirement queue and are destroyed only after the audio thread acknowledges a later
epoch. No final `shared_ptr` release may occur in the callback.

## Capture

Before arming, a capture session allocates block storage for at least four seconds, initializes free
and ready queues, opens a collision-safe sibling `.part` destination on its writer thread, and verifies
format limits. One foreground capture is allowed at a time in the initial design.

The callback acquires a free block, copies interleaved audio, and enqueues its descriptor. If no block
is available it increments overflow/dropout atomics and marks the session incomplete. It never waits.
The writer drains ready blocks, encodes, returns blocks to the free queue, finalizes/flushes/closes,
validates, and publishes the file off callback.

An incomplete, failed, or cancelled session deletes its temporary output and never commits an asset.
The user receives an explicit warning. Skipback has a separate preallocated ring; extraction occurs on
a writer/background thread. Device changes recreate storage only after the callback is stopped.

## Verification

`scripts/verify-realtime-code.py` performs a conservative source scan. It supplements code review and
stress tests; a clean scan is not proof of real-time safety.

