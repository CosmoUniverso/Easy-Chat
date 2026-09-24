#include "core/Protocol.h"

#include <QDataStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>
#include <QIODevice>
#include <QtGlobal>
#include <sodium.h>

#ifndef ECHAT_HAVE_MSQUIC
#define ECHAT_HAVE_MSQUIC 0
#endif

namespace ec::protocol {
namespace {

void appendField(QByteArray &out, const QByteArray &field) {
    const quint32 n = static_cast<quint32>(field.size());
    out.append(char((n >> 24) & 0xff));
    out.append(char((n >> 16) & 0xff));
    out.append(char((n >> 8) & 0xff));
    out.append(char(n & 0xff));
    out.append(field);
}

QByteArray helloMaterial(const QString &userId,
                         const QString &username,
                         const QByteArray &signingPublicKey,
                         const QByteArray &kxPublicKey,
                         const QJsonObject &caps) {
    QByteArray out("ECHello-v3");
    appendField(out, QByteArray::number(ProtocolVersion));
    appendField(out, userId.toUtf8());
    appendField(out, username.toUtf8());
    appendField(out, signingPublicKey);
    appendField(out, kxPublicKey);
    QByteArray capabilityBits;
    capabilityBits.append(caps.value("bluetooth").toBool() ? '1' : '0');
    capabilityBits.append(caps.value("lan").toBool() ? '1' : '0');
    capabilityBits.append(caps.value("internet").toBool() ? '1' : '0');
    capabilityBits.append(caps.value("groups").toBool() ? '1' : '0');
    capabilityBits.append(caps.value("files").toBool() ? '1' : '0');
    capabilityBits.append(caps.value("mesh").toBool() ? '1' : '0');
    capabilityBits.append(caps.value("quic").toBool() ? '1' : '0');
    capabilityBits.append(caps.value("tcpFallback").toBool() ? '1' : '0');
    appendField(out, capabilityBits);
    return out;
}

QByteArray ackMaterial(const QString &messageId, const QString &senderId) {
    QByteArray out("ECAck-v3");
    appendField(out, messageId.toUtf8());
    appendField(out, senderId.toUtf8());
    return out;
}

QByteArray relayMaterial(const QString &packetId, const QString &originId,
                         const QString &targetId, qint64 createdAtMs, int maxHops,
                         int policy, const QByteArray &innerPayload) {
    QByteArray out("ECRelay-v3");
    appendField(out, packetId.toUtf8());
    appendField(out, originId.toUtf8());
    appendField(out, targetId.toUtf8());
    appendField(out, QByteArray::number(createdAtMs));
    appendField(out, QByteArray::number(maxHops));
    appendField(out, QByteArray::number(policy));
    appendField(out, innerPayload);
    return out;
}

} // namespace

QByteArray frame(const QJsonObject &object) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray output;
    QDataStream stream(&output, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(payload.size());
    output.append(payload);
    return output;
}

QList<QJsonObject> consume(QByteArray &buffer) {
    QList<QJsonObject> objects;
    while (buffer.size() >= static_cast<int>(sizeof(quint32))) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(buffer.constData());
        const quint32 size = (quint32(bytes[0]) << 24) | (quint32(bytes[1]) << 16) |
                             (quint32(bytes[2]) << 8) | quint32(bytes[3]);
        if (size == 0 || size > MaxFrameBytes) {
            buffer.clear();
            break;
        }
        const qsizetype total = qsizetype(sizeof(quint32)) + size;
        if (buffer.size() < total) break;
        const QByteArray payload = buffer.mid(sizeof(quint32), size);
        buffer.remove(0, total);
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) objects.append(doc.object());
    }
    return objects;
}

QJsonObject hello(const LocalIdentity &identity) {
    const QJsonObject caps{
        {"bluetooth", true},
        {"lan", true},
        {"internet", false},
        {"groups", true},
        {"files", false},
        {"mesh", true},
        {"quic", ECHAT_HAVE_MSQUIC != 0},
        {"tcpFallback", true}
    };
    const QByteArray material = helloMaterial(identity.userId, identity.username,
                                              identity.signingPublicKey, identity.kxPublicKey, caps);
    QByteArray signature(crypto_sign_BYTES, Qt::Uninitialized);
    crypto_sign_detached(reinterpret_cast<unsigned char *>(signature.data()), nullptr,
                         reinterpret_cast<const unsigned char *>(material.constData()),
                         static_cast<unsigned long long>(material.size()),
                         reinterpret_cast<const unsigned char *>(identity.signingSecretKey.constData()));
    return {
        {"type", "hello"},
        {"protocol", ProtocolVersion},
        {"userId", identity.userId},
        {"username", identity.username},
        {"signingPublicKey", QString::fromLatin1(identity.signingPublicKey.toBase64())},
        {"kxPublicKey", QString::fromLatin1(identity.kxPublicKey.toBase64())},
        {"capabilities", caps},
        {"signature", QString::fromLatin1(signature.toBase64())}
    };
}

bool verifyHello(const QJsonObject &object) {
    if (object.value("protocol").toInt() != ProtocolVersion) return false;
    const QString userId = object.value("userId").toString();
    const QString username = object.value("username").toString();
    const QByteArray signPk = QByteArray::fromBase64(object.value("signingPublicKey").toString().toLatin1());
    const QByteArray kxPk = QByteArray::fromBase64(object.value("kxPublicKey").toString().toLatin1());
    const QByteArray sig = QByteArray::fromBase64(object.value("signature").toString().toLatin1());
    if (userId.isEmpty() || signPk.size() != crypto_sign_PUBLICKEYBYTES ||
        kxPk.size() != crypto_kx_PUBLICKEYBYTES || sig.size() != crypto_sign_BYTES) return false;
    const QByteArray material = helloMaterial(userId, username, signPk, kxPk,
                                              object.value("capabilities").toObject());
    return crypto_sign_verify_detached(
        reinterpret_cast<const unsigned char *>(sig.constData()),
        reinterpret_cast<const unsigned char *>(material.constData()),
        static_cast<unsigned long long>(material.size()),
        reinterpret_cast<const unsigned char *>(signPk.constData())) == 0;
}

QJsonObject encryptedMessage(const EncryptedEnvelope &e) {
    return {
        {"type", "message"},
        {"cryptoVersion", e.cryptoVersion},
        {"messageId", e.messageId},
        {"conversationId", e.conversationId},
        {"senderId", e.senderId},
        {"recipientId", e.recipientId},
        {"ephemeralPublicKey", QString::fromLatin1(e.ephemeralPublicKey.toBase64())},
        {"nonce", QString::fromLatin1(e.nonce.toBase64())},
        {"cipherText", QString::fromLatin1(e.cipherText.toBase64())},
        {"signature", QString::fromLatin1(e.signature.toBase64())}
    };
}

QJsonObject ack(const QString &messageId, const LocalIdentity &identity) {
    const QByteArray material = ackMaterial(messageId, identity.userId);
    QByteArray signature(crypto_sign_BYTES, Qt::Uninitialized);
    crypto_sign_detached(reinterpret_cast<unsigned char *>(signature.data()), nullptr,
                         reinterpret_cast<const unsigned char *>(material.constData()),
                         static_cast<unsigned long long>(material.size()),
                         reinterpret_cast<const unsigned char *>(identity.signingSecretKey.constData()));
    return {
        {"type", "ack"},
        {"messageId", messageId},
        {"senderId", identity.userId},
        {"signature", QString::fromLatin1(signature.toBase64())}
    };
}

bool verifyAck(const QJsonObject &object, const QByteArray &signingPublicKey) {
    if (signingPublicKey.size() != crypto_sign_PUBLICKEYBYTES) return false;
    const QString messageId = object.value("messageId").toString();
    const QString senderId = object.value("senderId").toString();
    const QByteArray signature = QByteArray::fromBase64(object.value("signature").toString().toLatin1());
    if (messageId.isEmpty() || senderId.isEmpty() || signature.size() != crypto_sign_BYTES) return false;
    const QByteArray material = ackMaterial(messageId, senderId);
    return crypto_sign_verify_detached(
        reinterpret_cast<const unsigned char *>(signature.constData()),
        reinterpret_cast<const unsigned char *>(material.constData()),
        static_cast<unsigned long long>(material.size()),
        reinterpret_cast<const unsigned char *>(signingPublicKey.constData())) == 0;
}


QJsonObject relay(const LocalIdentity &identity, const QString &targetId,
                  const QJsonObject &inner, TransportPolicy policy, int ttl) {
    const int maxHops = qBound(1, ttl, 8);
    const QString packetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const qint64 createdAtMs = QDateTime::currentMSecsSinceEpoch();
    const QByteArray innerPayload = QJsonDocument(inner).toJson(QJsonDocument::Compact);
    const QByteArray material = relayMaterial(packetId, identity.userId, targetId, createdAtMs,
                                              maxHops, static_cast<int>(policy), innerPayload);
    QByteArray signature(crypto_sign_BYTES, Qt::Uninitialized);
    crypto_sign_detached(reinterpret_cast<unsigned char *>(signature.data()), nullptr,
                         reinterpret_cast<const unsigned char *>(material.constData()),
                         static_cast<unsigned long long>(material.size()),
                         reinterpret_cast<const unsigned char *>(identity.signingSecretKey.constData()));
    return {
        {"type", "relay"},
        {"packetId", packetId},
        {"originId", identity.userId},
        {"targetId", targetId},
        {"createdAtMs", static_cast<double>(createdAtMs)},
        {"maxHops", maxHops},
        {"ttl", maxHops},
        {"policy", static_cast<int>(policy)},
        {"originHello", hello(identity)},
        {"innerPayload", QString::fromLatin1(innerPayload.toBase64())},
        {"signature", QString::fromLatin1(signature.toBase64())}
    };
}

bool verifyRelay(const QJsonObject &object, QJsonObject *inner) {
    if (object.value("type").toString() != QStringLiteral("relay")) return false;
    const QString packetId = object.value("packetId").toString();
    const QString originId = object.value("originId").toString();
    const QString targetId = object.value("targetId").toString();
    const qint64 createdAtMs = static_cast<qint64>(object.value("createdAtMs").toDouble());
    const int maxHops = object.value("maxHops").toInt(-1);
    const int ttl = object.value("ttl").toInt(-1);
    const int policy = object.value("policy").toInt(-1);
    const QJsonObject originHello = object.value("originHello").toObject();
    const QByteArray payload = QByteArray::fromBase64(object.value("innerPayload").toString().toLatin1());
    const QByteArray signature = QByteArray::fromBase64(object.value("signature").toString().toLatin1());
    if (packetId.isEmpty() || originId.isEmpty() || targetId.isEmpty() || createdAtMs <= 0 ||
        maxHops < 1 || maxHops > 8 || ttl < 0 || ttl > maxHops ||
        policy < static_cast<int>(TransportPolicy::Auto) ||
        policy > static_cast<int>(TransportPolicy::PreferInternet) ||
        payload.isEmpty() || signature.size() != crypto_sign_BYTES) return false;
    if (originHello.value("userId").toString() != originId || !verifyHello(originHello)) return false;
    const QByteArray signPk = QByteArray::fromBase64(originHello.value("signingPublicKey").toString().toLatin1());
    const QByteArray material = relayMaterial(packetId, originId, targetId, createdAtMs, maxHops, policy, payload);
    if (crypto_sign_verify_detached(
            reinterpret_cast<const unsigned char *>(signature.constData()),
            reinterpret_cast<const unsigned char *>(material.constData()),
            static_cast<unsigned long long>(material.size()),
            reinterpret_cast<const unsigned char *>(signPk.constData())) != 0) return false;

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    if (inner) *inner = doc.object();
    return true;
}

} // namespace ec::protocol
