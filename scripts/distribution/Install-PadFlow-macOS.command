#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "${HOME}/Library/Audio/Plug-Ins/VST3" "${HOME}/Library/Audio/Plug-Ins/Components" \
    "${HOME}/Applications"
ditto "${root}/VST3/PadFlow.vst3" "${HOME}/Library/Audio/Plug-Ins/VST3/PadFlow.vst3"
ditto "${root}/AU/PadFlow.component" \
    "${HOME}/Library/Audio/Plug-Ins/Components/PadFlow.component"
ditto "${root}/Standalone/PadFlow.app" "${HOME}/Applications/PadFlow.app"
killall -9 AudioComponentRegistrar 2>/dev/null || true
printf '%s\n' "Installed PadFlow VST3, AU, and standalone app for the current user."
printf '%s\n' "Unsigned builds may be rejected by macOS; use a Developer ID signed/notarized build for clients."
