# PadFlow FL Studio Validation Checklist

Record the FL Studio version, operating system, CPU architecture, PadFlow archive SHA-256, audio
device, sample rate, and buffer size with every run. A scan alone is not a complete host test.

## Windows x64 and macOS native

- Install the VST3 bundle, run **Find installed plugins**, and verify PadFlow appears once as an
  instrument without scan errors.
- Insert PadFlow in a new Channel Rack slot. Open, resize, close, and reopen its editor; audio must
  continue after the editor closes.
- Import a redistributable WAV, AIFF, and FLAC fixture. Trigger pad 1 from the UI and MIDI note 36;
  verify stereo output, note-off behavior in Gate mode, and no stuck notes after Panic.
- Save the FL Studio project, close FL Studio, reopen it, and verify PadFlow state and mappings.
- Test 44.1, 48, and 96 kHz with 64, 256, 512, and 1024-sample buffers. Check playback, transport,
  chopping preview, the internal sequencer, and input-bus recording where the FL Studio routing
  exposes an input.
- Run a 10-minute loop while opening/closing the editor and changing buffer size. Record underruns,
  crashes, scan warnings, non-finite output, or state loss as failures.
- Render a short FL Studio WAV and confirm finite, non-silent output of the expected duration.

## macOS-specific matrix

- On Apple Silicon, repeat the VST3 checklist in native FL Studio.
- Repeat under Rosetta only if the installed FL Studio build supports Rosetta; confirm the universal
  PadFlow VST3 loads as `x86_64`.
- On Intel hardware or an Intel macOS runner, repeat the VST3 checklist natively.
- Validate the AU with `auval -v aumu PdFw Amma`, scan it in FL Studio, and repeat open/play/save/
  reload/render. AU projects are not cross-platform with Windows, so use VST3 for exchange.

Do not mark a row passed without executing it on the named host/architecture. Attach the FL Studio
scan log, screenshots or video, saved test project, rendered WAV metadata, and crash report if any.
