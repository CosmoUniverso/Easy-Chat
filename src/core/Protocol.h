#pragma once

#include <QList>
#include "core/Types.h"
#include <QByteArray>
#include <QJsonObject>

namespace ec::protocol {

constexpr quint32 MaxFrameBytes = 8 * 1024 * 1024;
constexpr int ProtocolVersion = 2;

QByteArray frame(const QJsonObject &object);
QList<QJsonObject> consume(QByteArray &buffer);

QJsonObject hello(const LocalIdentity &identity);
bool verifyHello(const QJsonObject &object);
QJsonObject encryptedMessage(const EncryptedEnvelope &envelope);
QJsonObject ack(const QString &messageId, const LocalIdentity &identity);
bool verifyAck(const QJsonObject &object, const QByteArray &signingPublicKey);

} // namespace ec::protocol
