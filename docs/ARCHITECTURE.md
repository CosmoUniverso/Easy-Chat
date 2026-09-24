# EChat 0.3 architecture

```text
                              EChat Core
                                  |
       +--------------------------+--------------------------+
       |                          |                          |
    Identity                Conversations                Storage
 Ed25519/X25519             Direct + Group               SQLite
       |                          |
       +-------------------- CryptoEngine
                                  |
                         E2EE recipient envelope
                                  |
                         ConversationManager
                         direct or mesh relay
                                  |
                         TransportManager
                       /          |           \
               Bluetooth          LAN         Internet
                RFCOMM       QUIC -> TCP        future
                              fallback        QUIC/WSS
```

## Direct and relayed paths

A recipient does not need to share a physical transport with the sender.

```text
PC2                              PC1                              PC3
Bluetooth only             Bluetooth + LAN                    LAN only
     |                            |                                |
     +------ RFCOMM ------------>+--------- QUIC/TCP ------------>+

     [E2EE envelope encrypted for PC3 remains opaque to PC1]
```

The same mechanism is used for a direct conversation and for each recipient envelope in a group fan-out.

## Routing invariant

EChat separates capability, current reachability and user policy. A direct route requires all three to allow the transport.

When the final peer has no direct route, protocol v3 may send a signed `relay` object to a reachable mesh-capable neighbor. Current mesh selection is bounded flooding, not a full route table:

```text
origin packetId
   -> TTL/maxHops <= 4 by default
   -> seen-packet cache suppresses loops/duplicates
   -> previous hop excluded from immediate forwarding
   -> destination consumes the inner object
```

The relay signature covers the immutable routing fields and exact inner payload. `ttl` is deliberately mutable so each hop can decrement it, but it cannot exceed the origin-signed `maxHops`.

## LAN stack

```text
UDP/45455       local discovery
QUIC/UDP 45454  preferred reliable data channel (MsQuic)
TCP/45456       compatibility fallback
```

On discovery, peers may establish QUIC and TCP in parallel. Application sends prefer an established QUIC connection; if QUIC is unavailable or a stream cannot be sent, TCP is attempted.

QUIC exposes RTT statistics used in the GUI/delivery route description. MsQuic performs QUIC congestion control and loss recovery, so EChat does not implement a home-grown UDP reliability layer.

TCP is only a transport fallback in 0.3. Message bodies are still protected by EChat E2EE, but a TLS-wrapped local TCP fallback is not yet implemented.

## Receive framing

Every transport carries the same application framing:

```text
uint32 big-endian payload length
JSON protocol object
```

QUIC and TCP use distinct receive-buffer keys even for the same IP address, preventing partial TCP reads from being interleaved with complete QUIC stream frames.

## Identity and trust

Each identity has independent keys:

```text
Ed25519 -> identity signatures
X25519  -> recipient key agreement
```

Direct HELLO frames and mesh-propagated HELLO frames are signed. Indirect identity discovery updates public identity material but does **not** mark that peer as directly reachable.

Unexpected Ed25519 identity-key changes for a known `userId` are rejected (TOFU).

## Relay privacy model

Intermediate peers need enough metadata to forward:

```text
packetId
originId
targetId
TTL / maxHops
policy
signed origin HELLO
opaque inner payload
```

The inner message envelope contains ciphertext encrypted for the final recipient. A relay can see routing metadata and traffic timing/size, but cannot decrypt the chat text without the final recipient's private key.

## Message crypto v2

For each recipient independently:

```text
sender ephemeral X25519 + recipient static X25519
                    |
                shared secret
                    |
              BLAKE2b KDF
                    |
          XChaCha20-Poly1305
                    |
                ciphertext
                    |
          Ed25519 envelope signature
```

This is not a Double Ratchet. A later session layer is required for full forward secrecy/post-compromise security.

## Groups

Group delivery remains per-recipient fan-out:

```text
Group M
  -> E2EE for Alice -> direct or relay route
  -> E2EE for Bob   -> direct or relay route
  -> E2EE for Carol -> direct or relay route
```

This means a mixed group can contain Bluetooth-only and LAN-only members as long as the live mesh provides a path between them.

## Planned layers

- BLE/GATT advertising for low-power discovery/presence.
- QUIC datagrams for ephemeral typing/presence data.
- Internet relay: QUIC primary, WSS/TCP 443 compatibility fallback.
- Persistent store-and-forward retry queue across temporary disconnections.
- Link-state/path-cost routing using RTT, loss/stability, bandwidth and hop cost.
- Chunked/resumable file transfer engine.
- Ratcheting E2EE session protocol.
