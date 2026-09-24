#include "crypto/CryptoEngine.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <stdexcept>
#include <sodium.h>

namespace ec {
namespace {

void appendField(QByteArray &out, const QByteArray &field) {
    const quint32 n = static_cast<quint32>(field.size());
    out.append(char((n >> 24) & 0xff));
    out.append(char((n >> 16) & 0xff));
    out.append(char((n >> 8) & 0xff));
    out.append(char(n & 0xff));
    out.append(field);
}

QByteArray signatureMaterial(const EncryptedEnvelope &e, const QByteArray &aad) {
    QByteArray out("ECSignedEnvelope-v2");
    appendField(out, aad);
    appendField(out, e.nonce);
    appendField(out, e.cipherText);
    return out;
}

} // namespace

CryptoEngine::CryptoEngine() {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed");
}

LocalIdentity CryptoEngine::createIdentity(const QString &username, const QString &existingUserId) const {
    LocalIdentity identity;
    identity.userId = existingUserId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingUserId;
    identity.username = username;
    identity.kxPublicKey.resize(crypto_kx_PUBLICKEYBYTES);
    identity.kxSecretKey.resize(crypto_kx_SECRETKEYBYTES);
    identity.signingPublicKey.resize(crypto_sign_PUBLICKEYBYTES);
    identity.signingSecretKey.resize(crypto_sign_SECRETKEYBYTES);
    crypto_kx_keypair(reinterpret_cast<unsigned char *>(identity.kxPublicKey.data()),
                      reinterpret_cast<unsigned char *>(identity.kxSecretKey.data()));
    crypto_sign_keypair(reinterpret_cast<unsigned char *>(identity.signingPublicKey.data()),
                        reinterpret_cast<unsigned char *>(identity.signingSecretKey.data()));
    return identity;
}

LocalIdentity CryptoEngine::upgradeIdentity(const LocalIdentity &old) const {
    LocalIdentity out = old;
    if (out.userId.isEmpty()) out.userId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (out.kxPublicKey.size() != crypto_kx_PUBLICKEYBYTES ||
        out.kxSecretKey.size() != crypto_kx_SECRETKEYBYTES) {
        out.kxPublicKey.resize(crypto_kx_PUBLICKEYBYTES);
        out.kxSecretKey.resize(crypto_kx_SECRETKEYBYTES);
        crypto_kx_keypair(reinterpret_cast<unsigned char *>(out.kxPublicKey.data()),
                          reinterpret_cast<unsigned char *>(out.kxSecretKey.data()));
    }
    if (out.signingPublicKey.size() != crypto_sign_PUBLICKEYBYTES ||
        out.signingSecretKey.size() != crypto_sign_SECRETKEYBYTES) {
        out.signingPublicKey.resize(crypto_sign_PUBLICKEYBYTES);
        out.signingSecretKey.resize(crypto_sign_SECRETKEYBYTES);
        crypto_sign_keypair(reinterpret_cast<unsigned char *>(out.signingPublicKey.data()),
                            reinterpret_cast<unsigned char *>(out.signingSecretKey.data()));
    }
    return out;
}

bool CryptoEngine::identityValid(const LocalIdentity &i) const {
    return !i.userId.isEmpty() && !i.username.isEmpty() &&
           i.kxPublicKey.size() == crypto_kx_PUBLICKEYBYTES &&
           i.kxSecretKey.size() == crypto_kx_SECRETKEYBYTES &&
           i.signingPublicKey.size() == crypto_sign_PUBLICKEYBYTES &&
           i.signingSecretKey.size() == crypto_sign_SECRETKEYBYTES;
}

QString CryptoEngine::fingerprint(const QByteArray &signPk, const QByteArray &kxPk) const {
    if (signPk.size() != crypto_sign_PUBLICKEYBYTES || kxPk.size() != crypto_kx_PUBLICKEYBYTES) return {};
    const QByteArray hex = QCryptographicHash::hash(QByteArray("ECIdentity-v2") + signPk + kxPk,
                                                    QCryptographicHash::Sha256).toHex().toUpper().left(32);
    QStringList groups;
    for (int i = 0; i < hex.size(); i += 4) groups << QString::fromLatin1(hex.mid(i, 4));
    return groups.join('-');
}

QByteArray CryptoEngine::envelopeAad(const EncryptedEnvelope &e) {
    QByteArray out("ECMessage-v2");
    appendField(out, QByteArray::number(e.cryptoVersion));
    appendField(out, e.messageId.toUtf8());
    appendField(out, e.conversationId.toUtf8());
    appendField(out, e.senderId.toUtf8());
    appendField(out, e.recipientId.toUtf8());
    appendField(out, e.ephemeralPublicKey);
    return out;
}

QByteArray CryptoEngine::deriveAeadKey(const QByteArray &sharedSecret, const QByteArray &aad) {
    if (sharedSecret.size() != crypto_scalarmult_curve25519_BYTES) return {};
    QByteArray input("EC-X25519-XCHACHA20POLY1305-v2");
    appendField(input, aad);
    QByteArray key(crypto_aead_xchacha20poly1305_ietf_KEYBYTES, Qt::Uninitialized);
    if (crypto_generichash(reinterpret_cast<unsigned char *>(key.data()), key.size(),
                           reinterpret_cast<const unsigned char *>(input.constData()),
                           static_cast<unsigned long long>(input.size()),
                           reinterpret_cast<const unsigned char *>(sharedSecret.constData()),
                           sharedSecret.size()) != 0) return {};
    return key;
}

EncryptedEnvelope CryptoEngine::encryptFor(const Message &message,
                                            const LocalIdentity &sender,
                                            const Peer &recipient) const {
    if (!identityValid(sender) || recipient.kxPublicKey.size() != crypto_kx_PUBLICKEYBYTES ||
        recipient.signingPublicKey.size() != crypto_sign_PUBLICKEYBYTES)
        throw std::runtime_error("EC v2: invalid identity/key material");

    QJsonArray members;
    for (const auto &id : message.conversationMembers) members.append(id);
    const QJsonObject body{
        {"messageId", message.id}, {"conversationId", message.conversationId},
        {"senderId", message.senderId}, {"text", message.text},
        {"timestampMs", static_cast<double>(message.timestampMs)},
        {"conversationType", static_cast<int>(message.conversationType)},
        {"conversationName", message.conversationName}, {"conversationMembers", members}
    };
    const QByteArray plain = QJsonDocument(body).toJson(QJsonDocument::Compact);

    EncryptedEnvelope e;
    e.messageId = message.id;
    e.conversationId = message.conversationId;
    e.senderId = message.senderId;
    e.recipientId = recipient.userId;
    e.ephemeralPublicKey.resize(crypto_kx_PUBLICKEYBYTES);
    QByteArray ephemeralSecret(crypto_kx_SECRETKEYBYTES, Qt::Uninitialized);
    crypto_kx_keypair(reinterpret_cast<unsigned char *>(e.ephemeralPublicKey.data()),
                      reinterpret_cast<unsigned char *>(ephemeralSecret.data()));

    QByteArray shared(crypto_scalarmult_curve25519_BYTES, Qt::Uninitialized);
    if (crypto_scalarmult_curve25519(
            reinterpret_cast<unsigned char *>(shared.data()),
            reinterpret_cast<const unsigned char *>(ephemeralSecret.constData()),
            reinterpret_cast<const unsigned char *>(recipient.kxPublicKey.constData())) != 0) {
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        throw std::runtime_error("EC v2: X25519 key agreement failed");
    }
    sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());

    const QByteArray aad = envelopeAad(e);
    QByteArray key = deriveAeadKey(shared, aad);
    sodium_memzero(shared.data(), shared.size());
    if (key.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES)
        throw std::runtime_error("EC v2: KDF failed");

    e.nonce.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    randombytes_buf(e.nonce.data(), e.nonce.size());
    e.cipherText.resize(plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long cipherLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            reinterpret_cast<unsigned char *>(e.cipherText.data()), &cipherLen,
            reinterpret_cast<const unsigned char *>(plain.constData()), plain.size(),
            reinterpret_cast<const unsigned char *>(aad.constData()), aad.size(),
            nullptr, reinterpret_cast<const unsigned char *>(e.nonce.constData()),
            reinterpret_cast<const unsigned char *>(key.constData())) != 0) {
        sodium_memzero(key.data(), key.size());
        throw std::runtime_error("EC v2: XChaCha20-Poly1305 encryption failed");
    }
    sodium_memzero(key.data(), key.size());
    e.cipherText.resize(static_cast<qsizetype>(cipherLen));

    const QByteArray toSign = signatureMaterial(e, aad);
    e.signature.resize(crypto_sign_BYTES);
    crypto_sign_detached(reinterpret_cast<unsigned char *>(e.signature.data()), nullptr,
                         reinterpret_cast<const unsigned char *>(toSign.constData()),
                         static_cast<unsigned long long>(toSign.size()),
                         reinterpret_cast<const unsigned char *>(sender.signingSecretKey.constData()));
    return e;
}

bool CryptoEngine::decryptFrom(const EncryptedEnvelope &e,
                               const LocalIdentity &recipient,
                               const Peer &sender,
                               Message &out) const {
    if (e.cryptoVersion != 2 || !identityValid(recipient) ||
        sender.signingPublicKey.size() != crypto_sign_PUBLICKEYBYTES ||
        e.ephemeralPublicKey.size() != crypto_kx_PUBLICKEYBYTES ||
        e.nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES ||
        e.signature.size() != crypto_sign_BYTES ||
        e.cipherText.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) return false;

    const QByteArray aad = envelopeAad(e);
    const QByteArray signedData = signatureMaterial(e, aad);
    if (crypto_sign_verify_detached(
            reinterpret_cast<const unsigned char *>(e.signature.constData()),
            reinterpret_cast<const unsigned char *>(signedData.constData()),
            static_cast<unsigned long long>(signedData.size()),
            reinterpret_cast<const unsigned char *>(sender.signingPublicKey.constData())) != 0) return false;

    QByteArray shared(crypto_scalarmult_curve25519_BYTES, Qt::Uninitialized);
    if (crypto_scalarmult_curve25519(
            reinterpret_cast<unsigned char *>(shared.data()),
            reinterpret_cast<const unsigned char *>(recipient.kxSecretKey.constData()),
            reinterpret_cast<const unsigned char *>(e.ephemeralPublicKey.constData())) != 0) return false;
    QByteArray key = deriveAeadKey(shared, aad);
    sodium_memzero(shared.data(), shared.size());
    if (key.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) return false;

    QByteArray plain(e.cipherText.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES, Qt::Uninitialized);
    unsigned long long plainLen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char *>(plain.data()), &plainLen, nullptr,
        reinterpret_cast<const unsigned char *>(e.cipherText.constData()), e.cipherText.size(),
        reinterpret_cast<const unsigned char *>(aad.constData()), aad.size(),
        reinterpret_cast<const unsigned char *>(e.nonce.constData()),
        reinterpret_cast<const unsigned char *>(key.constData()));
    sodium_memzero(key.data(), key.size());
    if (rc != 0) return false;
    plain.resize(static_cast<qsizetype>(plainLen));

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(plain, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const auto obj = doc.object();
    out.id = obj.value("messageId").toString();
    out.conversationId = obj.value("conversationId").toString();
    out.senderId = obj.value("senderId").toString();
    out.text = obj.value("text").toString();
    out.timestampMs = static_cast<qint64>(obj.value("timestampMs").toDouble());
    out.conversationType = static_cast<ConversationType>(obj.value("conversationType").toInt());
    out.conversationName = obj.value("conversationName").toString();
    for (const auto &v : obj.value("conversationMembers").toArray()) out.conversationMembers << v.toString();
    return !out.id.isEmpty() && !out.senderId.isEmpty();
}

} // namespace ec
