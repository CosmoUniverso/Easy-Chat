# Changelog

## 0.3.1

- Fixed unreadable light popup/dialog backgrounds with light text on Linux/Fedora.
- Added a global Fusion dark palette before the first input dialog is shown.
- Added explicit dark styling for QDialog, QInputDialog, QMessageBox, dialog lists, inputs and standard buttons.
- Kept the existing EChat dark palette and accent colors consistent across the main window and modal dialogs.

## 0.3.0

- Bumped wire protocol to **v3**.
- Added LAN discovery over UDP broadcast.
- Added preferred LAN transport using **Microsoft MsQuic 2.6.1**.
- Added automatic **TCP LAN fallback** when QUIC is unavailable/not connected.
- Added per-link route descriptions, including QUIC RTT when available.
- Added signed P2P relay envelopes with packet ID, final destination, transport policy and bounded hop count.
- Added multi-hop relay for both direct chats and group fan-out.
- Added duplicate suppression to prevent relay loops.
- Added signed identity propagation through the mesh so peers can learn the public identity/key of a non-direct destination.
- Relay nodes forward the final recipient's encrypted message envelope without decrypting message text.
- Separated QUIC and TCP receive-buffer keys so simultaneous LAN channels cannot corrupt application framing.
- Added mesh relay signature/tamper checks to the self-test.
- Windows CI now packages the official MsQuic Schannel native DLL.
- Linux CI installs MsQuic 2.6.1 and includes it in AppImage dependency deployment.
- BLE/GATT discovery, Internet relay/WSS, persistent delay-tolerant store-and-forward, file transfer and full path-cost routing remain future work.

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
- Added GitHub Actions builds, NSIS Windows installer, Linux AppImage and automatic prerelease publication.
