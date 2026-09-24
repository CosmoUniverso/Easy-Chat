# EChat 0.2 architecture

```text
                         EChat Core
                            |
       +--------------------+--------------------+
       |                    |                    |
    Identity          Conversations           Storage
 Ed25519/X25519       Direct + Group          SQLite
       |                    |
       +-------------- CryptoEngine
                            |
                 signed encrypted envelope
                            |
                    TransportManager
                      /      |      \
               Bluetooth    LAN    Internet
                RFCOMM      next     next
```

## Routing invariant

EChat separates:

1. **Capability**: peer software supports a transport.
2. **Reachability**: that peer is currently reachable over that transport.
3. **Policy**: user choice, such as BluetoothOnly or PreferLan.

A route is eligible only when the local transport is available and the target peer is reachable through it. Merely having Wi-Fi/Ethernet installed does not make LAN a valid route to another peer.

## Identity and trust

Each local identity has two independent key pairs:

```text
Ed25519  -> identity signatures / proof of possession
X25519   -> recipient key agreement
```

HELLO frames are self-signed with Ed25519. On first contact EChat stores the identity public key (TOFU). If the same `userId` later presents a different Ed25519 public key, the connection is rejected and a security warning is surfaced.

Fingerprints are displayed so users can later verify identities out-of-band.

## Message crypto v2

For each recipient independently:

```text
sender
  generate ephemeral X25519 keypair
              |
              | X25519(ephemeral_sk, recipient_static_pk)
              v
         shared secret
              |
        BLAKE2b KDF + authenticated message context
              v
        256-bit AEAD key
              |
       XChaCha20-Poly1305
              v
          ciphertext
              |
Ed25519-sign(metadata || eph_pk || nonce || ciphertext)
```

The one-use ephemeral secret is wiped with `sodium_memzero()` immediately after X25519. The derived shared secret and AEAD key are also wiped after use.

This design is not called a Double Ratchet. Recipient long-term-key compromise remains a historical-capture risk; a future ratcheting session layer is required for full forward secrecy/post-compromise security.

## Groups

Groups are already first-class conversations. Current group delivery is fan-out:

```text
Group message M
  -> encrypted envelope for Alice
  -> encrypted envelope for Bob
  -> encrypted envelope for Carol
```

This works without a server and lets each member have a different usable transport. Later versions can replace fan-out with a mature group key protocol without changing the UI/conversation abstraction.

## GUI and CLI

`EChat` is the supported desktop GUI.

`echat-cli` is experimental but uses the exact same:

- identity
- database
- crypto engine
- conversation manager
- Bluetooth transport
- route selection

This prevents the console interface from becoming a second incompatible implementation.
