# PadFlow Client Installation

This archive contains original PadFlow builds. It does not contain or redistribute QuadBeatFX.
Development archives are marked `UNSIGNED`; production delivery requires signing and, on macOS,
Apple notarization.

## Windows 10/11 x64

1. Extract the complete ZIP. Do not run files from inside the ZIP.
2. Run `Install-PadFlow-Windows.cmd`. This copies the complete bundle to the current user's
   `%LOCALAPPDATA%\Programs\Common\VST3` folder without requiring administrator access. For an
   all-users install, run `Install-PadFlow-Windows.ps1 -AllUsers` from an elevated PowerShell.
3. The standalone application is `Standalone\PadFlow.exe` and can run directly.
4. In FL Studio, open **Options > Manage plugins**, add the chosen VST3 directory to **Plugin search
   paths** if it is not listed, and select **Find installed plugins**. Add **PadFlow** from the
   instrument list.

The Windows binaries use the static MSVC runtime, so the Microsoft Visual C++ Redistributable is
not an additional PadFlow dependency. Windows system graphics/audio libraries are still required.

## macOS 12 or newer

1. Extract the complete ZIP on macOS.
2. Run `Install-PadFlow-macOS.command`. It installs VST3 and AU for the current user and copies the
   standalone app to `~/Applications`.
3. Restart FL Studio, then rescan plugins. Use VST3 when a project must move between Windows and
   macOS; AU is macOS-only.

The macOS package is universal only when `lipo -archs` reports both `arm64` and `x86_64` for the
standalone, VST3, and AU executables. An unsigned archive is a development build and may be blocked
by Gatekeeper or rejected by a DAW scanner. Client release builds should be signed with the owner's
Developer ID Application identity and notarized by Apple.

## Project/sample note

PadFlow schema-v1 projects refer to imported external sample paths unless a later collect-and-save
workflow is used. Deliver or move the source samples with the project and relink missing files.
