#pragma once

#include "core/Types.h"
#include <QObject>

namespace ec {
class BluetoothTransport;
class ConversationManager;
class CryptoEngine;
class PeerManager;
class TransportManager;

class CliController : public QObject {
    Q_OBJECT
public:
    CliController(const LocalIdentity &identity,
                  CryptoEngine &crypto,
                  PeerManager &peers,
                  ConversationManager &conversations,
                  TransportManager &transports,
                  BluetoothTransport &bluetooth,
                  QObject *parent = nullptr);

public slots:
    void execute(const QString &line);

private:
    void print(const QString &text) const;
    void printHelp() const;
    void listPeers() const;
    void listChats() const;
    void showMessages() const;
    void showRoute() const;
    QString resolvePeer(const QString &token) const;
    QString resolveConversation(const QString &token) const;
    static TransportPolicy parsePolicy(const QString &token, bool *ok);

    LocalIdentity identity_;
    CryptoEngine &crypto_;
    PeerManager &peers_;
    ConversationManager &conversations_;
    TransportManager &transports_;
    BluetoothTransport &bluetooth_;
    QString currentConversationId_;
    TransportPolicy policy_ = TransportPolicy::Auto;
};

} // namespace ec
