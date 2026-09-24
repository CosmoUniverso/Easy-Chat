# EChat 0.4 architecture

```text
                              EChat Core
                                  |
       +--------------------------+--------------------------+
       |                          |                          |
    Identity                Conversations                Storage
 Ed25519/X25519             Direct + Group               SQLite
       |                          |                    /      |      \
       +-------------------- CryptoEngine          history  outbox  relay spool
                                  |
                         E2EE recipient envelope
                                  |
                         ConversationManager
                    direct / retry / store-forward
                                  |
                         TransportManager
                     adaptive local scoring
                       /          |           \
               Bluetooth          LAN         Internet
                RFCOMM       QUIC -> TCP        future
                              fallback        QUIC/WSS
```

## End-to-end delivery

A local socket write is not considered final delivery.

```text
message plaintext
      |
      v
recipient-specific E2EE envelope
      |
      +----> SQLite sender outbox ------------------+
      |                                             |
      +----> direct / relay attempt                 |
                                                    |
final recipient decrypts + sends signed ACK         |
      |                                             |
      +---------------------- ACK ------------------+
                                                    v
                                            delete outbox entry
```

The outbox persists across application restart. Retries use exponential backoff and can create a fresh relay wrapper around the same final-recipient ciphertext.

## Relay store-and-forward

```text
A ----> B ----X----> C
        |
        +-- verify origin relay signature
        +-- store opaque relay object in SQLite
        |
        +-- retry later ----> C
```

B never needs to decrypt the message body. Relay spool custody is bounded: entries expire after 24 hours and the local spool is capped at 256 packets. The origin's persistent outbox remains the end-to-end reliability source, so an intermediate relay can remove its local spool entry after it successfully forwards the packet to another hop.

## Routing

EChat still separates capability, reachability and user policy. In 0.4 the direct transport order is computed dynamically.

```text
score(Bluetooth) = fixed RFCOMM cost
score(LAN/QUIC)  = base + measured RTT
score(LAN/TCP)   = fixed fallback cost
score(Internet)  = higher reserved cost
```

`Prefer*` policies apply a strong preference bias so the requested reachable transport wins; `Only*` policies restrict the candidate set. The lowest-cost reachable direct transport is tried first.

Mesh path discovery is still bounded flooding with packet IDs, TTL and duplicate suppression. This release does not yet exchange a global topology or run Dijkstra. Therefore the route score is **per direct hop**, not an end-to-end path metric.

## Direct and relayed paths

```text
PC2                              PC1                              PC3
Bluetooth only             Bluetooth + LAN                    LAN only
     |                            |                                |
     +------ RFCOMM ------------>+--------- QUIC/TCP ------------>+

     [E2EE envelope encrypted for PC3 remains opaque to PC1]
```

If PC3 disappears, PC1 may persist the signed relay object and continue later. PC2 also retains its own encrypted message object until PC3's signed ACK returns.

## LAN stack

```text
UDP/45455       local discovery
QUIC/UDP 45454  preferred reliable data channel (MsQuic)
TCP/45456       compatibility fallback
```

QUIC RTT feeds the direct-route cost. QUIC and TCP keep separate receive-buffer keys so simultaneous channels from one IP cannot corrupt application framing.

## Identity and trust

Each identity has independent Ed25519 signing and X25519 key-exchange keys. Signed HELLO objects can propagate through the mesh, but indirect discovery does not mark a peer directly reachable. Unexpected Ed25519 identity-key changes are rejected under TOFU.

## Relay privacy

Intermediate nodes can inspect protocol/routing metadata needed for forwarding, including origin, target, timing, size, TTL and policy. The message body remains final-recipient ciphertext.

## Cryptography

Crypto v2 remains:

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

This is not a Double Ratchet and does not yet provide Signal-style forward secrecy/post-compromise security.

## Next layers

- Full topology/path-cost routing with hop cost, RTT, loss/stability and bandwidth.
- BLE/GATT low-power discovery/presence.
- Internet relay with QUIC primary and WSS/TCP 443 fallback.
- Chunked/resumable file transfer.
- Ratcheting E2EE sessions and OS-protected private-key storage.
