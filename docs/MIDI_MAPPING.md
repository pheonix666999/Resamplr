# MIDI Mapping

Milestone 1 supports one selected MIDI input, Omni or channels 1–16 filtering, default chromatic pad
notes, velocity-sensitive note-on, note-off (including note-on velocity zero), persistent pad-note
mapping, and panic on disconnect or user request. Mappings apply across the fixed 64-pad project;
the active bank controls computer-keyboard routing, not MIDI note identity.

CC mapping, MIDI learn, clock, parameter locks, and later performance mappings remain deferred. The
automated smoke paths inject synthetic MIDI messages and do not require physical hardware.
