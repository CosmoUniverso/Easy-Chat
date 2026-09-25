#pragma once

#include "core/Types.h"
#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextBrowser;
class QPushButton;
class QDialog;

namespace ec {
class BluetoothTransport;
class ConversationManager;
class CryptoEngine;
class PeerManager;
class TransportManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const LocalIdentity &identity,
               CryptoEngine &crypto,
               PeerManager &peers,
               ConversationManager &conversations,
               TransportManager &transports,
               BluetoothTransport &bluetooth,
               QWidget *parent = nullptr);

private:
    void applyTheme();
    void refreshConversations();
    void selectConversation(const QString &id);
    void refreshMessages();
    void refreshRouteStatus();
    void refreshHeader();
    void openBluetoothScanner();
    void createGroup();
    void createDirectChat();
    void manageCurrentGroup();
    void openAccountSettings();
    void deleteMessageFromLink(const QString &messageId);
    void openStealthMode();
    void refreshStealthMode();
    TransportPolicy selectedPolicy() const;

    LocalIdentity identity_;
    CryptoEngine &crypto_;
    PeerManager &peers_;
    ConversationManager &conversations_;
    TransportManager &transports_;
    BluetoothTransport &bluetooth_;

    QString currentConversationId_;
    QListWidget *conversationList_ = nullptr;
    QTextBrowser *messages_ = nullptr;
    QLineEdit *messageEdit_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QComboBox *policyBox_ = nullptr;
    QLabel *routeLabel_ = nullptr;
    QLabel *securityLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *conversationTitle_ = nullptr;
    QLabel *userLabel_ = nullptr;
    QPushButton *groupManageButton_ = nullptr;
    QDialog *stealthDialog_ = nullptr;
    QLabel *stealthTitle_ = nullptr;
    QTextBrowser *stealthMessages_ = nullptr;
    QLineEdit *stealthMessageEdit_ = nullptr;
};

} // namespace ec
