# Signing

Milestone 1 archives are unsigned and must include `UNSIGNED.txt` plus a build manifest containing
`"signed": false`. CI must not fail merely because credentials are absent.

Milestone 10 will import Windows certificates and Apple credentials only in protected release jobs,
never fork pull requests. Temporary key material/keychains will be removed after use. Signed code and
installers will be verified; macOS artifacts will use hardened runtime, notarization, stapling, and
Gatekeeper verification. Secrets must never be printed or committed.

The macOS standalone, VST3, and AU bundles all require a `Developer ID Application` identity with
hardened runtime and timestamping before notarization. The owner must supply protected CI secrets
for the signing certificate/private key and either an App Store Connect team API key or an Apple ID
app-specific password workflow accepted by `notarytool`. Individual API keys are not assumed.
Unsigned packages are for development testing only and must never be described as Gatekeeper-ready.
