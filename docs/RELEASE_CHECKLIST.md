# Release Checklist

This checklist becomes enforceable in Milestone 10.

- All required Linux, Windows, macOS arm64, Intel, and universal checks pass.
- Universal executable contains both architectures and targets macOS 12+.
- Unit, integration, smoke, packaging, checksum, and artifact-verification tests pass.
- Product/version/bundle metadata and changelog match the tag.
- Third-party licences and owner-approved proprietary notice are complete.
- Signing/notarization is verified when credentials exist; otherwise artifacts are clearly unsigned.
- Windows portable/installer/symbols and macOS app/DMG/dSYM are attached with combined checksums.
- No credentials, copyrighted reference assets, proprietary samples, or temporary signing files exist.

