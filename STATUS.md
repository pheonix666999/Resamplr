# PadFlow Status

## Current milestone

Milestone 1 — playable RAM-resident sampler: **planning and interface audit complete; implementation
in progress on `feature/milestone-1-playable-sampler`**.

The Milestone 0 repository, pinned dependency, targets, interfaces, schema-v1 skeleton, tests, smoke
paths, packaging, and workflows remain the validated baseline. The Milestone 1 authority now has
phased delivery tasks in `PLANS.md` and granular acceptance IDs in `TEST_PLAN.md`. The first
implementation slice adds the fixed four-bank/64-pad model, four layers per pad, stable identities,
parameter validation, copy/paste/duplicate/clear commands, and undo/redo coverage. It is awaiting
hosted compilation and test validation before the next phase begins.

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
GitHub Actions remains the authoritative native compiler/test environment for each implementation
phase.

## Reference

`BLOCKED_REFERENCE_ASSET`: the local MP4 is 0 bytes and cannot be decoded. This does not block the
Milestone 0 foundation. Exact Resamplr UI, interaction, or chopping parity is not claimed.

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
