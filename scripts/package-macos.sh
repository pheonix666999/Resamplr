#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" != "--development-archive" ]]; then
    echo "Milestone 0 supports only --development-archive" >&2
    exit 2
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${root}/build/macos-universal-release"
output="${root}/artifacts/macos"
stage="${output}/PadFlow-macOS-Universal-Development-Unsigned"
app="${build}/bin/PadFlow.app"
[[ -d "${app}" ]] || { echo "Missing ${app}" >&2; exit 1; }

rm -rf "${stage}"
mkdir -p "${stage}"
cp -R "${app}" "${stage}/"
cp "${root}/README.md" "${root}/LICENSE.md" "${root}/THIRD_PARTY_LICENSES.md" "${stage}/"
printf '%s\n' 'Unsigned Milestone 0 development build.' > "${stage}/UNSIGNED.txt"
printf '%s\n' '{"platform":"macos-universal","product":"PadFlow","signed":false,"version":"0.1.0"}' > "${stage}/build-manifest.json"
mkdir -p "${output}"
archive="${output}/PadFlow-macOS-Universal-Development-Unsigned.zip"
rm -f "${archive}"
(cd "${output}" && ditto -c -k --sequesterRsrc --keepParent "$(basename "${stage}")" "$(basename "${archive}")")
shasum -a 256 "${archive}" | sed "s#${output}/##" > "${output}/SHA256SUMS-macOS.txt"
