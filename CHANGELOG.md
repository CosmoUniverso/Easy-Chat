# Changelog

## 0.2.0

- Renamed product surface to **EChat — Easy Chat**.
- Replaced static `crypto_box_easy` identity encryption with crypto v2:
  - Ed25519 identity signatures
  - independent X25519 key-exchange keys
  - one-use ephemeral X25519 sender key for every recipient envelope
  - BLAKE2b keyed derivation
  - XChaCha20-Poly1305 authenticated encryption
  - signed HELLO, message envelopes and ACKs
  - identity fingerprints and TOFU key-change rejection
- Kept migration support for the 0.1 SQLite identity/database layout.
- Added modern dark desktop GUI inspired by contemporary chat clients.
- Added conversation search, direct-chat chooser and route/security header.
- Kept groups first-class and fan-out encrypted.
- Made route availability require peer capability + local availability + actual peer reachability.
- Added experimental `echat-cli` sharing the exact same core/database/crypto/transports as the GUI.
- Added optional crypto self-test target.

## Packaging / CI

- Added GitHub Actions builds for Windows x64 and Linux x86_64.
- Added single-file Windows NSIS installer.
- Added Linux AppImage packaging.
- Added automatic GitHub prerelease publication for the CMake project version.
