#pragma once

#include "core/Types.h"
#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextBrowser;

namespace ec {
class BluetoothTransport;
class ConversationManager;
class CryptoEngine;
class PeerManager;
class TransportManager;

class FullMainWindow : public QMainWindow {
    Q_OBJECT
public:
    FullMainWindow(const LocalIdentity &identity,
                   CryptoEngine &crypto,
                   PeerManager &peers,
                   ConversationManager &conversations,
                   TransportManager &transports,
                   BluetoothTransport &bluetooth,
                   QWidget *parent = nullptr);

    QString currentConversationId() const { return currentConversationId_; }
    void activateConversation(const QString &id);

signals:
    void requestLightMode(const QString &conversationId);

private:
    void applyTheme();
    void refreshConversations();
    void refreshMessages();
    void refreshHeader();
    void refreshDetails();
    void refreshRouteStatus();
    void selectConversation(const QString &id);
    void setConversationFilter(int mode);

    void createDirectChat();
    void createGroup();
    void manageCurrentGroup();
    void openAccountSettings();
    void openBluetoothScanner();
    void deleteMessageFromLink(const QString &messageId);
    TransportPolicy selectedPolicy() const;

    LocalIdentity identity_;
    CryptoEngine &crypto_;
    PeerManager &peers_;
    ConversationManager &conversations_;
    TransportManager &transports_;
    BluetoothTransport &bluetooth_;

    QString currentConversationId_;
    int conversationFilter_ = 0; // 0 all, 1 direct, 2 group

    QListWidget *conversationList_ = nullptr;
    QListWidget *membersList_ = nullptr;
    QTextBrowser *messages_ = nullptr;
    QLineEdit *messageEdit_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QComboBox *policyBox_ = nullptr;
    QLabel *conversationTitle_ = nullptr;
    QLabel *conversationSubtitle_ = nullptr;
    QLabel *userLabel_ = nullptr;
    QLabel *routeLabel_ = nullptr;
    QLabel *securityLabel_ = nullptr;
    QLabel *queueLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *detailsTitle_ = nullptr;
    QPushButton *manageGroupButton_ = nullptr;
    QPushButton *allFilterButton_ = nullptr;
    QPushButton *directFilterButton_ = nullptr;
    QPushButton *groupFilterButton_ = nullptr;
};

} // namespace ec
