# PadFlow User Guide

PadFlow is a playable RAM-resident sampler. It provides four banks (A–D) of twelve pads in the
supplied original 3x4 layout, with four velocity layers per pad. Waveform editing, input recording,
transient and lazy chopping, transactional slice assignment, internal transport, pattern editing,
and step/live sequencing are available. Effects, song arrangement, and mobile delivery follow their
dedicated milestones.

## Make a pattern

Choose Sequence in the top bar. Set BPM, Loop, Metronome, and Count-in in the transport row. Use the
pattern row to create, rename, duplicate, select, clear, or delete a pattern. Scroll through the 48
pad lanes and click a cell to toggle a step. Click an event and use the bottom controls for velocity,
probability, ratchets, or timing nudge. Playhead motion is read from the audio transport snapshot.

Record starts a live overdub; input during count-in is heard but is not written. Pad mouse gestures,
the 3x4 computer-key map, and MIDI notes share the same bounded recording path and preserve MIDI
velocity. Stop/Record completes held gates and commits one undoable take; Panic cancels it.

## Load and play samples

Select a bank and pad, choose a layer, then use **Import / Replace** to load WAV, AIFF, or FLAC.
You can also drop one or more supported files on a pad; multiple files fill pads sequentially and
PadFlow asks before replacing occupied destinations. **Audition** previews the selected layer
through the configured output.

Press a pad with the mouse, use its displayed computer-key assignment, or send its mapped MIDI note.
Mouse and keyboard use the project's fixed trigger velocity; MIDI uses incoming velocity. One-shot
continues after release, Gate follows press/release, and Toggle alternates start and stop.

The selected-pad panel edits name, active layer, gain, pan, coarse/fine pitch, ADSR, playback mode,
mono/poly mode, choke group, local voice limit, MIDI note, and keyboard assignment. Right-click a
pad for load, clear, rename, recolour, copy, paste, and duplicate operations.

## Devices

**Audio Settings** selects output routing, sample rate, buffer size, channel masks, test tone,
restart, and dropout reset. Audio input is disabled by default and is opened only after you
explicitly select an input device; ordinary sample playback therefore does not request microphone
access.

**MIDI Settings** enables one input, chooses Omni or a channel 1–16 filter, and provides panic.
If a saved device is unavailable, PadFlow remains usable with mouse and keyboard input.

## Projects and external samples

Use **New**, **Open**, and **Save** for `.padflow` projects. Schema v1 stores all Milestone 1 pad,
layer, mapping, device, and window state. Samples remain external references; collect-and-save and
relinking are deferred. On load, available files decode asynchronously. Missing files keep their
pad/layer assignments and are visibly marked rather than silently cleared.

The decoded sample registry requests a 2 GiB default budget, capped at half of physical RAM and
16 GiB, with a 256 MiB supported minimum. The status bar shows current decoded use and the active
budget.

Development archives are unsigned, so Windows SmartScreen or macOS Gatekeeper may warn. No exact
Resamplr parity is claimed: the supplied reference remains `BLOCKED_REFERENCE_ASSET`.
