#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" != "--development-archive" ]]; then
    echo "Milestone 1 supports only --development-archive" >&2
    exit 2
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${root}/build/macos-universal-release"
output="${root}/artifacts/macos"
stage="${output}/PadFlow-macOS-Universal-Development-Unsigned"
app="${build}/bin/PadFlow.app"
vst3="${build}/lib/VST3/PadFlow.vst3"
component="${build}/lib/AU/PadFlow.component"
[[ -d "${app}" ]] || { echo "Missing ${app}" >&2; exit 1; }
[[ -d "${vst3}" ]] || { echo "Missing ${vst3}" >&2; exit 1; }
[[ -d "${component}" ]] || { echo "Missing ${component}" >&2; exit 1; }

rm -rf "${stage}"
mkdir -p "${stage}"
mkdir -p "${stage}/Standalone" "${stage}/VST3" "${stage}/AU"
ditto "${app}" "${stage}/Standalone/PadFlow.app"
ditto "${vst3}" "${stage}/VST3/PadFlow.vst3"
ditto "${component}" "${stage}/AU/PadFlow.component"
cp "${root}/README.md" "${root}/LICENSE.md" "${root}/THIRD_PARTY_LICENSES.md" \
    "${root}/docs/CLIENT_INSTALLATION.md" "${root}/docs/FL_STUDIO_VALIDATION.md" "${stage}/"
cp "${root}/scripts/distribution/Install-PadFlow-macOS.command" "${stage}/"
chmod +x "${stage}/Install-PadFlow-macOS.command"
printf '%s\n' 'Unsigned Milestone 1 development build.' > "${stage}/UNSIGNED.txt"
printf '%s\n' '{"architectures":["arm64","x86_64"],"formats":["Standalone","VST3","AU"],"platform":"macos-universal","product":"PadFlow","signed":false,"version":"0.1.0"}' > "${stage}/build-manifest.json"
mkdir -p "${output}"
archive="${output}/PadFlow-macOS-Universal-Development-Unsigned.zip"
rm -f "${archive}"
(cd "${output}" && ditto -c -k --sequesterRsrc --keepParent "$(basename "${stage}")" "$(basename "${archive}")")
shasum -a 256 "${archive}" | sed "s#${output}/##" > "${output}/SHA256SUMS-macOS.txt"
