# PadFlow Status

## Current milestone

Milestone 2 — waveform editing and recording: **started on
`feature/milestone-2-waveform-recording`**.

Milestone 1 was merged into `main` at `e11eb4e45b6b04ab6504f070f1ffd646a18f1389`.
[Post-merge run 30076870715](https://github.com/pheonix666999/Resamplr/actions/runs/30076870715)
passed validation, Windows x64, macOS universal, macOS Intel smoke, and cross-platform artifact
verification before the Milestone 2 branch was created. Milestone 2 acceptance authorities are now
defined in `PLANS.md` and `TEST_PLAN.md`; product implementation has not yet been claimed complete.

The Milestone 0 repository, pinned dependency, targets, interfaces, schema-v1 skeleton, tests, smoke
paths, packaging, and workflows remain the validated baseline. Milestone 1 adds the fixed
four-bank/64-pad model, four immutable velocity layers per pad, stable identities, validated pad
parameters, unified undo/redo, asynchronous WAV/AIFF/FLAC import and preview, mouse/keyboard/MIDI
input, persistent device routing, deterministic 128-voice playback, the original sampler UI,
external-reference project load/resolution, populated-project smoke coverage, and Milestone 1
development artifacts.

The schema-v1 persistence slice now serializes and validates the complete Milestone 1 model,
preserves Milestone 0 manifest compatibility, rejects partial/invalid payloads without changing the
live project, and performs semantic archive validation before publication. Hosted validation for
this slice passed in
[run 30063978227](https://github.com/pheonix666999/Resamplr/actions/runs/30063978227) on commit
`ab3bd120ccd01c2ed28b6feaa8fa7609b5e39ad7`; all five jobs passed.

The immutable import slice now has bounded asynchronous WAV/AIFF/FLAC decoding, checked mono/stereo
frame and memory accounting, source metadata/fingerprints, configurable unique-asset registry
budgeting, cancellation/stale-result rejection, atomic layer assignment, rollback, and off-callback
epoch retirement coverage.

[Import run 30064539191](https://github.com/pheonix666999/Resamplr/actions/runs/30064539191)
compiled the importer but warnings-as-errors rejected a Milestone 0 aggregate fixture after optional
asset metadata was added. The explicit fixture initialization fix is tracked by
`REGRESSION-CI-009`. Hosted revalidation passed in
[run 30064703121](https://github.com/pheonix666999/Resamplr/actions/runs/30064703121) on commit
`21d124b39b0641f43ab0a1102868e23408c6c74a`; all five jobs passed.

The deterministic playback slice now has a 128-voice fixed pool, bounded trigger/release/panic
commands, immutable snapshots, deterministic local/global allocation, Hermite interpolation,
source-rate conversion, ADSR, gain/pan/tuning/velocity, playback/polyphony/choke behavior, finite
guards, safe silence, and atomic meters. Hosted validation passed in
[run 30065256470](https://github.com/pheonix666999/Resamplr/actions/runs/30065256470) on commit
`32c0bc8512f2ef2bc1894e87f06b9c3af91b71b0`; all five jobs passed.

The final device/input, preview, UI, integration, and documentation phases add explicit input/output
routing, a platform-capped 2 GiB default decoded-memory budget, safe device transitions, active
voice-aware immutable asset retirement, persisted external sample resolution, and populated
save/load/retrigger smoke coverage. The actual Windows artifact was launched at 125% display scale;
the final 1180×760 UI is recorded in
[`docs/images/padflow-milestone1-windows-125pct.png`](docs/images/padflow-milestone1-windows-125pct.png).

## Milestone 1 final validation — 2026-07-24

[Final run 30074453172](https://github.com/pheonix666999/Resamplr/actions/runs/30074453172)
completed successfully on validated commit `a966bcb0b9c561a6d874af6637850802cadf1a39`.
All five jobs passed with zero hosted warnings, errors, or JUCE assertions:

- Linux GCC Debug and Release each passed 3/3 CTest tests; Debug also passed console and GUI
  populated-project smoke.
- Windows MSVC Debug and Release each passed 3/3 CTest tests; Release passed both smoke paths,
  Milestone 1 packaging, and archive verification.
- macOS universal Release passed 3/3 CTest tests, both smoke paths, arm64/x86_64 `lipo` inspection,
  packaging, and archive verification.
- macOS Intel Release passed 3/3 CTest tests, both smoke paths, and x86_64 inspection.
- Cross-platform artifact verification passed for `windows-development` and
  `macos-development`.

That is 18/18 CTest registrations across six native configurations plus eight explicit successful
console/GUI smoke invocations. The final artifact IDs are `8589587424` (Windows) and `8589408404`
(macOS).

Milestone 1 Actions runs, including remediation history:

| Run | Result | Scope |
|---|---|---|
| `30062883832` | success | Feature-branch CI enablement |
| `30063293341` | failure | Initial model compile diagnostics |
| `30063416386` | success | Model remediation |
| `30063978227` | success | Schema-v1 persistence |
| `30064539191` | failure | Extended asset fixture diagnostic |
| `30064703121` | success | Immutable import remediation |
| `30065256470` | success | Deterministic playback |
| `30065664071` | success | Release-stealing coverage |
| `30067246828` | failure | Initial device/input JUCE API diagnostics |
| `30067376891` | failure | Const device-query diagnostics |
| `30067485658` | success | Device/input remediation |
| `30068041246` | success | Sample preview |
| `30071339563` | cancelled | UI exact-width diagnostic; superseded by fix |
| `30071468019` | cancelled | UI overload diagnostic; superseded by fix |
| `30071662177` | success | Playable sampler UI remediation |
| `30072772685` | failure | Wrapped snapshot generation diagnostic |
| `30073048635` | cancelled | Safer snapshot retirement exposed stale test setup |
| `30073315645` | success | Populated integration smoke remediation |
| `30074453172` | success | Final full Milestone 1 gate |

The scoped implementation and remediation commits remain unsquashed for review. The final hosted
run validated all source, test, workflow, packaging, and documentation changes through `a966bcb`.

Feature-branch CI was enabled and validated independently in
[run 30062883832](https://github.com/pheonix666999/Resamplr/actions/runs/30062883832) on commit
`ee8df6b9fe0299871a3fee72a76e4834a5d7d825`; all five jobs passed.

[Model run 30063293341](https://github.com/pheonix666999/Resamplr/actions/runs/30063293341)
found two hosted compile regressions: stable UUID generation referenced unlinked
`juce_cryptography`, and bank-name casts used an unqualified JUCE character alias. The focused
core-only and namespace fixes are tracked by `REGRESSION-CI-007` and `REGRESSION-CI-008`; hosted
revalidation passed in
[run 30063416386](https://github.com/pheonix666999/Resamplr/actions/runs/30063416386) on commit
`38b24d3ee0849a2ebb52c7db97b9355c04167f89`. All five jobs passed, including Linux
Debug/Release, both macOS variants, Windows x64, model unit tests, smoke tests, packaging, and
artifact verification.

This Windows host still has no supported native compiler, so local native builds remain blocked.
GitHub Actions is the authoritative native compiler/test environment. Local static validation,
script parsing, artifact inspection, and launching/screenshotting the hosted Windows executable are
available and pass.

## Reference

`BLOCKED_REFERENCE_ASSET`: the local MP4 is 0 bytes and cannot be decoded. It did not block the
original PadFlow Milestone 1 implementation. Exact Resamplr UI, interaction, or chopping parity is
not claimed.

## CI remediation — 2026-07-23

[CI run 30030706795](https://github.com/pheonix666999/Resamplr/actions/runs/30030706795)
failed on commit `166de0f130d7eeaf84d0f6e75158db835e3f8abc`:

- Windows selected unsupported MinGW because the Ninja preset did not select MSVC.
- Linux promoted a conversion warning originating in a JUCE header to an error.
- macOS attempted to call `.getAddress()` on the `const char*` returned by `String::toRawUTF8()`.

The Windows presets now require `cl`, and CI initializes the installed MSVC x64 toolchain through
`vswhere`; JUCE targets are added with CMake's `SYSTEM` third-party boundary; and the GUI smoke
diagnostic uses the returned UTF-8 pointer directly. Regression coverage is tracked by
`REGRESSION-CI-001` through `REGRESSION-CI-006`. Linux/macOS process arguments are also parsed
before JUCE startup so GUI headless smoke does not initialize a display. Hosted validation is
complete.

MSVC warning C4324 is narrowly disabled around the SPSC queue template because its cache-line
separation intentionally pads the class. This exception is tracked by `REGRESSION-CI-005`; `/WX`
remains enabled for all other project diagnostics.

[Run 30031942176](https://github.com/pheonix666999/Resamplr/actions/runs/30031942176)
passed Linux Debug/Release validation and both macOS jobs. Windows reached the MSVC build and failed
only on C4324 before the narrow suppression above; artifact verification was consequently skipped.

[Run 30032715720](https://github.com/pheonix666999/Resamplr/actions/runs/30032715720)
passed Linux Debug/Release, both macOS jobs, the Windows MSVC build, all Windows tests/smokes, and
Windows packaging. Windows artifact verification alone rejected the PowerShell 5.1 UTF-8 BOM in the
manifest. Packaging now writes explicit BOM-free UTF-8, tracked by `REGRESSION-CI-006`.

[Run 30033326030](https://github.com/pheonix666999/Resamplr/actions/runs/30033326030)
completed successfully on commit `5dc209317c2087a04ce8063b28e954a96cbce1ec`. All five jobs passed:
Linux Debug/Release validation, Windows x64 build/tests/smokes/package, macOS universal
build/tests/smokes/package, macOS Intel build/tests/smokes, and cross-platform artifact
verification. The `windows-development` and `macos-development` artifacts were uploaded.

[Follow-up run 30031473884](https://github.com/pheonix666999/Resamplr/actions/runs/30031473884)
confirmed clean Linux and macOS universal compilation. It exposed two masked infrastructure issues:
the Windows 2025 runner carries Visual Studio 2026 rather than 2022, and pre-startup JUCE argument
storage is empty on Linux/macOS. The portable `cl`/`vswhere` setup and raw process-argument parsing
address both without changing Milestone 0 product scope.

## Local environment and validation — 2026-07-23

- Available initially: Git 2.50.0 and Python 3.13.
- No MSVC, Clang, GCC, Visual Studio installation, system CMake, Ninja, system clang-format, GitHub
  CLI, FFmpeg, or ffprobe is available.
- Temporary checksum/version-pinned validation tools were installed outside the repository:
  clang-format 18.1.8, CMake 3.31.6, Ninja 1.11.1.4, PyYAML 6.0.2, and actionlint 1.7.12.

Passing checks:

- JUCE submodule HEAD equals `7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2`.
- `git diff --check`.
- clang-format dry-run with warnings as errors after applying the repository style.
- Real-time scan: 13 callback/real-time interface bodies inspected.
- Project-schema/document-authority verification.
- CMake preset JSON parsing and `cmake --list-presets=all`.
- Python byte-compilation, PowerShell script parsing, Git Bash `bash -n`, YAML parsing, and actionlint.
- Explicit CMake source inventory and Milestone 1+ source-boundary scan.
- Windows preset parsing confirms explicit `cl` compiler selection and Release configuration.

Blocked/failed checks:

- Windows Debug and Release configure stop at compiler discovery because Visual Studio is not
  installed locally.
- Builds, unit tests, smoke executables, and development packaging could not be produced after the
  configure failure.
- A first empty-build-tree CTest invocation returned success while finding no tests. Presets were
  corrected with `noTestsAction: error`; the repeated invocation correctly exits 8.
- Windows artifact verification correctly fails because no archive was built.
- macOS arm64/Intel/universal and Linux builds cannot run on this Windows host.
- The GitHub CLI is unavailable. CI run/job/log inspection uses the connected GitHub integration.

Principal commands executed:

```text
git init -b main
git submodule add https://github.com/juce-framework/JUCE.git external/JUCE
git -C external/JUCE checkout 7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2
git diff --check
python scripts/check-format.py
python scripts/verify-realtime-code.py
python scripts/verify-project-schema.py
python -m compileall -q scripts
python -m json.tool CMakePresets.json
cmake --list-presets=all
cmake --preset windows-debug --fresh
cmake --preset windows-release --fresh
cmake --build --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-debug --output-on-failure
ctest --preset windows-release --output-on-failure
powershell -NoProfile -File scripts/package-windows.ps1 -DevelopmentArchive
python scripts/verify-artifacts.py --mode milestone0 --platform windows
C:\Program Files\Git\bin\bash.exe -n scripts/configure.sh scripts/build.sh scripts/test.sh scripts/package-macos.sh
actionlint.exe -no-color .github/workflows/ci.yml .github/workflows/release.yml
```
