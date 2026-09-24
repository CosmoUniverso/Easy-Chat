#pragma once

#include "core/Types.h"

namespace ec {

class CryptoEngine {
public:
    CryptoEngine();

    LocalIdentity createIdentity(const QString &username, const QString &existingUserId = {}) const;
    LocalIdentity upgradeIdentity(const LocalIdentity &identity) const;
    bool identityValid(const LocalIdentity &identity) const;
    QString fingerprint(const QByteArray &signingPublicKey, const QByteArray &kxPublicKey) const;

    EncryptedEnvelope encryptFor(const Message &message,
                                 const LocalIdentity &sender,
                                 const Peer &recipient) const;
    bool decryptFrom(const EncryptedEnvelope &envelope,
                     const LocalIdentity &recipient,
                     const Peer &sender,
                     Message &messageOut) const;

private:
    static QByteArray envelopeAad(const EncryptedEnvelope &envelope);
    static QByteArray deriveAeadKey(const QByteArray &sharedSecret, const QByteArray &aad);
};

} // namespace ec
