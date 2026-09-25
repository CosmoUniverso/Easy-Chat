# EChat — Easy Chat

EChat 0.7.0 is an offline-first desktop messenger prototype for **Windows 10/11 and Linux/Fedora**. It combines Bluetooth Classic/RFCOMM, LAN QUIC/TCP, end-to-end encrypted multi-hop relay and persistent retry/store-and-forward for intermittent networks.

## 0.7.0: Light + Full nello stesso EChat

EChat ora mantiene **due interfacce sopra lo stesso core**, senza creare due codebase o due protocolli:

- **Light**: l'interfaccia minimale gia' esistente, con chat, gruppi, policy di rete e modalita' Stealth. E' pensata per occupare poco spazio e restare veloce.
- **Full**: una nuova interfaccia piu' ricca, ispirata ai moderni client desktop di messaggistica: rail di azioni, lista chat filtrabile, area messaggi centrale e pannello laterale con membri, sicurezza, route e coda.

Le due UI condividono esattamente **account, SQLite, cronologia, E2EE, peer, outbox, relay spool e trasporti**. Passare da Light a Full non duplica dati e non crea una seconda identita'. La modalita' scelta viene ricordata al riavvio.

Avvio forzato da riga di comando:

```bash
./EChat --light
./EChat --full
# equivalenti: --ui=light / --ui=full
```

La strategia di sviluppo da 0.7 in poi e': **Light stabile e minimale**, con bugfix e parita' delle funzioni essenziali; **Full** diventa il ramo UX principale per funzioni piu' ricche. Entrambe usano sempre lo stesso backend.


### 0.6.0 Stealth / compact desktop mode

- `Stealth` button in the active chat header.
- `Ctrl+Shift+S` toggles the compact view; `Esc` returns to the full window.
- Small always-on-top popup with the current conversation name, the last three message previews and a compact composer.
- The full EChat window is hidden while compact mode is active, reducing desktop footprint without changing network visibility or protocol behavior.
- Uses the same selected transport policy, E2EE path, outbox, ACK and retry logic as the normal interface.

### 0.5.1 reliability fix

Pending messages no longer remain pinned forever to an obsolete `Solo Bluetooth` / `Solo LAN` / `Solo Internet` selection. Changing the route policy for the current conversation, or sending another message with a different policy, updates older queued message/control envelopes and retries them immediately on the newly allowed route.

## 0.5 implemented

- Desktop GUI plus experimental `echat-cli`.
- Direct chats and groups share the same conversation/message model.
- **Bluetooth Classic/RFCOMM** offline data channel.
- **LAN discovery over UDP broadcast**.
- **QUIC/UDP via MsQuic 2.6.1** as preferred LAN data channel.
- **TCP LAN fallback** when QUIC is unavailable.
- Signed multi-hop P2P relay capable of bridging Bluetooth and LAN peers.
- Per-recipient group fan-out with an independently encrypted E2EE envelope.
- **Persistent sender outbox** in SQLite. A message remains queued until the final recipient sends a valid signed ACK.
- **Persistent relay spool** in SQLite. An intermediate bridge can hold an opaque relay envelope while the next path is unavailable and forward it later.
- Exponential retry/backoff with bounded retry batches and expiry.
- Unknown-recipient delivery is materialized automatically after that peer's signed identity becomes known.
- **Adaptive direct transport scoring**: Auto mode uses current link cost instead of a fixed Bluetooth-first order. QUIC RTT participates in the LAN score.
- Delivery indicators (`…`, `✓`, `✓✓`) and queue/spool diagnostics in the GUI.
- **Delete for everyone** for messages sent by the local account. Deletions are E2EE control messages with persistent retry and tombstones to suppress late duplicates.
- **Live group management**: add already-known peers to an existing group and rename the group. Updates are sent as recipient-specific encrypted control messages.
  - In 0.5 there is not yet an admin/role model: every current group member may add known peers or rename the group. Member removal/expulsion is not implemented yet.
- **Account settings**: change the local username without rotating the User ID, identity keys or fingerprint; the signed identity announcement propagates the new display name.
- Signed HELLO and ACK frames, fingerprints and TOFU identity-key change protection.

## Intermittent bridge example

```text
PC2 (Bluetooth only)       PC1 (Bluetooth + LAN)         PC3 (LAN only)
        |                           |                           |
        +--------- RFCOMM -------->+                           |
                                    |   PC3 temporarily off    |
                                    |   [relay ciphertext]      |
                                    |   stored in SQLite        |
                                    |                           |
                                    +------ QUIC/TCP ---------->+
                                            when PC3 returns
```

PC2 encrypts the message **for PC3** before PC1 receives it. PC1 can store and forward the signed relay envelope, but it does not have PC3's private key and cannot decrypt the chat text.

The sender also keeps its encrypted recipient envelope in its own persistent outbox until a valid end-to-end ACK arrives. Therefore a successful local socket write is not treated as final delivery.

## LAN transport

| Purpose | Protocol | Port |
|---|---|---:|
| preferred data channel | QUIC / UDP | 45454 |
| local discovery | UDP broadcast | 45455 |
| compatibility fallback | TCP | 45456 |

QUIC supplies reliable streams, TLS 1.3 transport protection, congestion control and RTT statistics. Chat protocol frames currently use reliable QUIC streams. QUIC datagrams are reserved for later presence/typing features.

The TCP fallback is raw TCP carrying the same **application-level E2EE frames**. An Internet relay with QUIC plus WSS/TCP 443 fallback is still future work.

## Adaptive routing

EChat separates peer capability, live reachability and user policy. `Auto` now scores available direct transports rather than using a fixed order.

Current direct-link cost model:

- LAN/QUIC: low base cost plus measured MsQuic RTT.
- LAN/TCP fallback: medium fixed cost.
- Bluetooth/RFCOMM: medium fixed cost.
- Internet relay transport: reserved higher cost once implemented.

`Prefer Bluetooth/LAN/Internet` gives the preferred reachable transport priority while still allowing fallback. `Only ...` policies remain strict.

Multi-hop selection is still **bounded mesh flooding**, not a global link-state/Dijkstra protocol. Each hop does use the adaptive local transport choice. Full path-cost advertisements and loss/stability/bandwidth metrics remain later work.

## Persistent delivery semantics

For locally-originated messages:

1. Encrypt once for the final recipient.
2. Save the encrypted protocol object in SQLite outbox.
3. Attempt direct or mesh delivery immediately.
4. Keep retrying with exponential backoff until a signed end-to-end ACK is received or the entry expires.
5. Delete the outbox entry only after that ACK.

For intermediate relays:

1. Verify the origin-signed relay envelope.
2. Try the final peer directly, then reachable mesh neighbors.
3. If no next path exists, persist the opaque relay envelope in the relay spool.
4. Retry when connectivity changes / on the periodic retry loop.
5. Remove local custody after successful forwarding. The origin's end-to-end outbox remains the ultimate reliability mechanism.

Current defaults are seven days for sender outbox entries, 24 hours for relay-spool custody, a maximum of 256 stored relay packets, and retry delays capped at 60 seconds.

## Cryptography v2 + mesh protocol v4

Message encryption remains crypto v2:

- Ed25519 long-term identity signatures.
- X25519 key agreement.
- Fresh ephemeral X25519 sender key per recipient/message.
- BLAKE2b keyed derivation.
- XChaCha20-Poly1305-IETF authenticated encryption.

The same encrypted envelope may be retransmitted while waiting for its ACK; retries do not re-encrypt the message or expose plaintext to relay nodes.

Protocol v4 keeps origin-signed relay metadata and adds encrypted control-message semantics for message deletion and group metadata updates. Because 0.5 bumps the wire protocol, all peers participating in a 0.5 mesh should run EChat 0.5.

### Security boundary

This is **not Signal Double Ratchet**. Compromise of a recipient's long-term X25519 private key can expose previously captured crypto-v2 envelopes. A future ratcheting session layer is required for forward secrecy and post-compromise security.

Private identity keys are still stored in the local SQLite database in this development build. Production should move them to OS-protected credential storage or an encrypted local vault.

## Build on Fedora

Without MsQuic the project still builds and uses LAN TCP fallback only.

```bash
sudo dnf install -y gcc-c++ cmake qt6-qtbase-devel qt6-qtconnectivity-devel libsodium-devel pkgconf-pkg-config openssl
cmake -S . -B build
cmake --build build -j
./build/EChat
```

For QUIC:

```bash
cmake -S . -B build \
  -DMSQUIC_INCLUDE_DIR=/path/to/msquic/include \
  -DMSQUIC_LIBRARY=/path/to/libmsquic.so
cmake --build build -j
```

Self-tests:

```bash
cmake -S . -B build-test -DECHAT_BUILD_TESTS=ON
cmake --build build-test -j
./build-test/EChatCryptoSelfTest
./build-test/EChatReliabilitySelfTest
```

## Build on Windows

Use Qt 6 for MSVC, CMake and Visual Studio Build Tools. The release workflow downloads the official MsQuic 2.6.1 Schannel package and uses vcpkg for libsodium. MinGW is not supported by the current Windows Bluetooth backend.

## Status

| Transport / subsystem | Status |
|---|---|
| Bluetooth Classic/RFCOMM | implemented |
| LAN QUIC | implemented |
| LAN TCP fallback | implemented |
| signed multi-hop relay | implemented |
| persistent sender outbox | implemented |
| relay store-and-forward | implemented |
| adaptive direct-link scoring | implemented |
| delete own messages for everyone | implemented in 0.5 |
| add members / rename existing groups | implemented in 0.5 |
| change local username | implemented in 0.5 |
| Light + Full shared-core desktop UIs | implemented in 0.7 |
| full global path-cost routing | planned |
| BLE/GATT discovery | planned |
| Internet relay QUIC/WSS | planned |
| resumable file transfer | planned |
| Double Ratchet/session crypto | planned |

Every push to `main` builds the Windows installer and Linux AppImage and publishes the prerelease when both jobs succeed.
