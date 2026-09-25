# Changelog

## 0.6.0

- Added **Stealth/compact desktop mode**: a small always-on-top popup that shows only the current chat, a short recent-message preview, and the composer.
- Added `Ctrl+Shift+S` to enter/leave Stealth mode; `Esc` also expands back to the full window.
- Stealth mode uses the currently selected transport policy and keeps delivery/retry behavior identical to the full UI.
- Recent-message preview updates live for incoming, outgoing, deleted and delivery-state changes.
- Kept the v0.5.1 queued-message policy rebinding fix and wire protocol v4.

## 0.5.1

- Fixed queued messages remaining permanently tied to the transport policy that was active when they were first created.
- Changing a conversation policy now rebinds its pending encrypted message/control envelopes to the new policy and wakes the retry queue immediately.
- Sending a new message also reapplies the selected policy to older queued messages in the same conversation, preventing a stale `BluetoothOnly`/`LanOnly` route from stranding them.
- Policy changes reset retry backoff for affected queued items so a newly available route is attempted immediately.
- ACK packets remain transport-agnostic and continue to use `Auto`.
- Added reliability self-test coverage for outbox policy rebinding/enumeration.

## 0.5.0

- Bumped wire protocol to **v4** and QUIC ALPN to `echat-v4`.
- Added encrypted **delete-for-everyone** control messages for messages authored by the local identity.
- Added persistent deletion tombstones so a delayed/retried old ciphertext cannot resurrect a deleted message.
- Deleting a local message also cancels its pending sender outbox/delivery rows before propagating the deletion.
- Added **group management** for existing groups: add known peers and rename the group.
- Group metadata updates are recipient-specific E2EE control messages and use the same retry/ACK/mesh path as chat messages.
- Group membership updates are additive/mergeable; group names use the latest update timestamp.
- New groups now send their metadata immediately instead of waiting for the first chat message.
- Added **Account** settings to change username while retaining User ID, Ed25519/X25519 keys and fingerprint.
- Username changes are persisted and propagated via signed identity announcements; direct-chat display names refresh when a peer renames itself.
- Added UI delete links on locally authored messages and a `Gestisci gruppo` dialog.
- Extended self-tests for encrypted control payloads, deletion tombstones and persisted group metadata.

## 0.4.0

- Added a persistent encrypted sender outbox in SQLite.
- Messages are retained and retried until a valid signed end-to-end ACK arrives.
- Added exponential retry/backoff with seven-day sender-outbox expiry.
- Added persistent intermediate relay store-and-forward with a 24-hour custody window and 256-packet cap.
- Relay nodes retry opaque signed relay envelopes after temporary path loss without decrypting message text.
- Added automatic materialization/retry when a previously unknown peer identity becomes available through signed HELLO propagation.
- Replaced fixed Auto transport order with adaptive direct-link scoring.
- LAN/QUIC score incorporates live MsQuic RTT; TCP fallback and RFCOMM use fixed baseline costs.
- Prefer-* policies now bias scoring while Only-* policies remain strict.
- Added delivery indicators and queue/relay-spool diagnostics to the GUI.
- Added SQLite reliability self-test and CI execution on Linux.
- Wire protocol remains v3; 0.4 is intended to remain compatible with 0.3 at the protocol framing level.

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
