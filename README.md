# EChat — Easy Chat

EChat 0.3 is an offline-first desktop messenger prototype for **Windows 10/11 and Linux/Fedora**. Bluetooth Classic/RFCOMM remains the primary no-network data channel; 0.3 adds LAN communication and multi-hop peer relay.

## 0.3 implemented

- Desktop GUI inspired by modern WhatsApp/Discord layouts.
- Experimental console client: `echat-cli`.
- Direct chats and groups use the same conversation/message model.
- **Bluetooth Classic/RFCOMM** direct data channel.
- **LAN discovery over UDP broadcast**.
- **QUIC over UDP via MsQuic 2.6.1** as the preferred LAN data channel when available.
- **TCP fallback** on LAN when QUIC is unavailable or cannot establish a connection.
- Per-peer transport capability, reachability and user policy remain separate.
- Multi-hop **P2P mesh relay** for both direct chats and groups.
- A node with multiple transports can bridge peers that do not share a transport, for example Bluetooth -> relay PC -> LAN.
- Relay packets have packet IDs, hop limit/TTL and duplicate suppression.
- Relay headers and destination are signed by the origin identity; intermediaries cannot silently retarget or modify the encrypted payload.
- Group fan-out still creates an independently encrypted envelope for each recipient; each recipient may use a different direct or relayed route.
- SQLite history and delivery state.
- Signed HELLO and ACK frames, fingerprints and TOFU identity-key change protection.

## Example: Bluetooth-to-LAN relay

```text
PC2 (Bluetooth only)
        |
     RFCOMM
        |
        v
PC1 (Bluetooth + LAN)
        |
   QUIC preferred
   TCP fallback
        |
        v
PC3 (LAN only)
```

PC2 encrypts the message **for PC3** before giving it to PC1. PC1 forwards the signed relay envelope but does not possess PC3's private key and therefore cannot decrypt the message body.

## LAN transport

EChat 0.3 uses three LAN ports by default:

| Purpose | Protocol | Port |
|---|---|---:|
| preferred data channel | QUIC / UDP | 45454 |
| local discovery | UDP broadcast | 45455 |
| compatibility fallback | TCP | 45456 |

QUIC provides reliable streams, TLS 1.3 transport security, loss recovery and congestion control. EChat currently sends chat protocol frames on reliable QUIC streams. QUIC datagrams are reserved for later ephemeral features such as presence/typing and are not yet part of the user-facing protocol.

The TCP fallback is currently a raw TCP transport carrying the same **application-level E2EE frames**. It is intended as a compatibility path when QUIC/UDP is blocked. A TLS/WSS Internet fallback belongs to the future relay-server layer and is not implemented in 0.3.

If local UDP discovery itself is blocked or clients are isolated by a managed network/VLAN, EChat does not try to bypass that policy. A later Internet relay can provide a permitted remote route instead.

## Routing

For every peer EChat separates:

1. **capability** — what the peer software supports;
2. **reachability** — what is connected/reachable now;
3. **policy** — Auto, Only Bluetooth/LAN/Internet, Prefer Bluetooth/LAN/Internet.

A direct route is attempted first. If the destination has no direct route, EChat can wrap the already encrypted object in a signed relay envelope and forward it through reachable mesh-capable peers, up to the hop limit.

0.3 uses bounded flooding plus deduplication rather than a full link-state routing protocol. RTT from MsQuic is exposed in route descriptions, but automatic global path-cost optimization is a later step.

## Cryptography v2 + mesh protocol v3

Message encryption remains crypto v2:

- **Ed25519** long-term identity signatures.
- **X25519** key agreement.
- Fresh ephemeral X25519 sender key for every recipient envelope.
- **BLAKE2b** key derivation.
- **XChaCha20-Poly1305-IETF** authenticated message encryption.

Protocol v3 adds signed relay metadata around the existing encrypted envelope. Relay nodes see routing metadata needed to forward a packet, but message text remains encrypted for the final recipient.

### Security boundary

This is **not a Signal Double Ratchet implementation**. A compromise of a recipient's long-term X25519 private key can still expose previously captured crypto-v2 envelopes. Full forward secrecy/post-compromise security requires a ratcheting session protocol.

Private identity keys are still stored in the local SQLite database in this development build. They should move to OS-protected credential/key storage or an encrypted local vault before a production release.

The self-signed certificate used by local QUIC authenticates the QUIC transport cryptographically but EChat's peer identity is still authenticated at the application layer by signed Ed25519 HELLO frames. The client currently accepts the local self-signed QUIC certificate because EChat performs its own identity pinning above the transport.

## Compatibility

Protocol v3 changes the signed capability/relay protocol. For mesh/LAN testing, use EChat 0.3 on all participating machines; 0.2 should not be treated as protocol-compatible with 0.3.

## Build on Fedora

The project can compile without MsQuic; in that case LAN uses the TCP fallback only.

```bash
sudo dnf install -y gcc-c++ cmake qt6-qtbase-devel qt6-qtconnectivity-devel libsodium-devel pkgconf-pkg-config openssl
cmake -S . -B build
cmake --build build -j
./build/EChat
```

For a QUIC-enabled manual build, install a current MsQuic 2.6.x package/header or point CMake at it:

```bash
cmake -S . -B build \
  -DMSQUIC_INCLUDE_DIR=/path/to/msquic/include \
  -DMSQUIC_LIBRARY=/path/to/libmsquic.so
cmake --build build -j
```

The GitHub release workflow installs **MsQuic 2.6.1** automatically and packages it with the release artifacts.

Optional self-test:

```bash
cmake -S . -B build-test -DECHAT_BUILD_TESTS=ON
cmake --build build-test -j
./build-test/EChatCryptoSelfTest
```

Experimental CLI:

```bash
./build/echat-cli
```

## Build on Windows

Use **Qt 6 for MSVC**, CMake and Visual Studio Build Tools. The release workflow downloads the official MsQuic 2.6.1 Schannel native package and uses vcpkg for libsodium.

Do not use MinGW for the current Windows Bluetooth backend.

## Transport status in 0.3

| Transport | Status | Use |
|---|---|---|
| Bluetooth Classic/RFCOMM | implemented | primary offline messaging |
| LAN QUIC | implemented in 0.3, pending CI/device validation | preferred LAN data |
| LAN TCP fallback | implemented in 0.3, pending CI/device validation | UDP/QUIC compatibility fallback |
| P2P multi-hop relay | implemented in 0.3, pending multi-device validation | bridge Bluetooth/LAN peers, direct + groups |
| BLE/GATT | planned | discovery/presence/capability advertisement |
| Internet relay QUIC/WSS | planned | remote communication / restrictive networks |
| file transfer engine | planned | chunked/resumable large transfers |

## Download without compiling

Every push to `main` runs GitHub Actions. When both platform builds succeed, the release for the version in `CMakeLists.txt` contains:

- `EChat-Windows-x64-Setup.exe`
- `EChat-Linux-x86_64.AppImage`

The experimental `echat-cli` is bundled alongside the GUI.
