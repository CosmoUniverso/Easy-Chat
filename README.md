# EChat — Easy Chat

EChat 0.2 is an offline-first desktop messenger prototype for **Windows 10/11 and Linux/Fedora**, with Bluetooth Classic/RFCOMM as the primary direct messaging transport.

## 0.2 implemented

- GUI desktop inspired by modern WhatsApp/Discord layouts.
- Experimental console client: `echat-cli`.
- Bluetooth Classic/RFCOMM transport.
- Direct chats and groups using the same conversation model.
- Group fan-out: every recipient receives an independently encrypted envelope.
- Per-peer route selection: a transport is selectable only if it is locally available **and actually reachable for that peer**.
- Policies: Auto, Only Bluetooth/LAN/Internet, Prefer Bluetooth/LAN/Internet.
- SQLite history and delivery state.
- Signed protocol HELLO and signed ACK frames.
- Identity-change protection (TOFU): an unexpected Ed25519 identity-key change is rejected.

## Cryptography v2

EChat deliberately separates cryptographic roles:

- **Ed25519**: long-term identity signatures.
- **X25519**: key agreement.
- **Ephemeral X25519 key per message**: a fresh sender-side DH secret for every encrypted recipient envelope.
- **BLAKE2b** (`crypto_generichash`): derives a message-specific AEAD key from the X25519 shared secret and authenticated context.
- **XChaCha20-Poly1305-IETF**: authenticated encryption of message content.
- The envelope metadata, ephemeral key, nonce and ciphertext are authenticated by an **Ed25519 signature**.

The username is never treated as a cryptographic identity. A peer identity is pinned by its Ed25519 public key; the GUI exposes an identity fingerprint.

### Security boundary

This is materially stronger than 0.1, but it is **not yet a Signal Double Ratchet implementation**. Per-message ephemeral sender keys improve key separation and erase ephemeral sender secrets after use, but compromise of a recipient's long-term X25519 private key can still expose previously captured v2 envelopes. Full bidirectional forward secrecy and post-compromise security require a ratcheting session protocol, planned for a later security milestone.

Private keys are still stored in the local SQLite database in this development build. Before a public release they must move to protected OS credential/key storage or an encrypted local vault.

## Build on Fedora

```bash
sudo dnf install -y gcc-c++ cmake qt6-qtbase-devel qt6-qtconnectivity-devel libsodium-devel pkgconf-pkg-config
cmake -S . -B build
cmake --build build -j
./build/EChat
```

Optional crypto self-test:

```bash
cmake -S . -B build-test -DECHAT_BUILD_TESTS=ON
cmake --build build-test -j
./build-test/EChatCryptoSelfTest
```

Experimental CLI:

```bash
./build/echat-cli
```

Both GUI and CLI use the same EChat application data directory/database.

## Build on Windows

Use **Qt 6 for MSVC 2022**, CMake, Visual Studio Build Tools, and libsodium. With vcpkg:

```powershell
vcpkg install libsodium:x64-windows
```

Configure CMake with the Qt MSVC path and vcpkg toolchain. Do not use MinGW for the current Windows Bluetooth backend.

## Transport status in 0.2

| Transport | Status | Intended use |
|---|---|---|
| Bluetooth RFCOMM | implemented | primary messaging / no network required |
| LAN | adapter present, transfer not implemented yet | high-speed messages/files |
| Internet relay | adapter present, not implemented yet | remote communication |

The application protocol does not belong to a transport. A future message/file can switch Bluetooth -> LAN -> Internet without changing conversation identity.

## CLI status

The CLI is intentionally marked **experimental**. It shares the same core, crypto, database and Bluetooth transport as the GUI, but its command UX and shutdown/input handling are not considered stable APIs.

Run `help` inside `echat-cli` for commands.

## Download senza compilare

Ogni push su `main` avvia GitHub Actions e pubblica/aggiorna la release della versione indicata in `CMakeLists.txt`.

- **Windows 10/11 x64:** scarica `EChat-Windows-x64-Setup.exe` e fai doppio click.
- **Linux/Fedora x86_64:** scarica `EChat-Linux-x86_64.AppImage`, rendilo eseguibile (`chmod +x`) se il file manager non lo fa automaticamente, quindi avvialo con doppio click.

La CLI sperimentale `echat-cli` viene inclusa nei pacchetti ma la GUI `EChat` resta l'interfaccia principale.
