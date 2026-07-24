# Signing

Milestone 1 archives are unsigned and must include `UNSIGNED.txt` plus a build manifest containing
`"signed": false`. CI must not fail merely because credentials are absent.

Milestone 10 will import Windows certificates and Apple credentials only in protected release jobs,
never fork pull requests. Temporary key material/keychains will be removed after use. Signed code and
installers will be verified; macOS artifacts will use hardened runtime, notarization, stapling, and
Gatekeeper verification. Secrets must never be printed or committed.
