#include "ui/FullMainWindow.h"

#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "crypto/CryptoEngine.h"
#include "transport/TransportManager.h"
#include "transport/bluetooth/BluetoothTransport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace ec {
namespace {

QString displayNameForPeer(const Peer &peer) {
    return peer.username.trimmed().isEmpty() ? peer.userId : peer.username;
}

QString initials(const QString &name) {
    const QString simplified = name.trimmed().simplified();
    if (simplified.isEmpty()) return QStringLiteral("?");
    const QStringList parts = simplified.split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2)
        return (parts.first().left(1) + parts.last().left(1)).toUpper();
    return simplified.left(2).toUpper();
}

QString transportSummary(const RouteState &routes) {
    QStringList available;
    if (routes.bluetooth) available << QStringLiteral("Bluetooth");
    if (routes.lan) available << QStringLiteral("LAN");
    if (routes.internet) available << QStringLiteral("Internet");
    return available.isEmpty() ? QStringLiteral("offline") : available.join(QStringLiteral(" + "));
}

} // namespace

FullMainWindow::FullMainWindow(const LocalIdentity &identity, CryptoEngine &crypto, PeerManager &peers,
                               ConversationManager &conversations, TransportManager &transports,
                               BluetoothTransport &bluetooth, QWidget *parent)
    : QMainWindow(parent), identity_(identity), crypto_(crypto), peers_(peers), conversations_(conversations),
      transports_(transports), bluetooth_(bluetooth) {
    setWindowTitle(QStringLiteral("EChat Full — Easy Chat"));
    resize(1480, 900);
    setMinimumSize(1120, 680);

    auto *central = new QWidget(this);
    central->setObjectName("fullRoot");
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Left rail: quick actions and mode switch.
    auto *rail = new QWidget;
    rail->setObjectName("rail");
    rail->setFixedWidth(76);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(10, 14, 10, 14);
    railLayout->setSpacing(10);

    auto *logo = new QLabel(QStringLiteral("EC"));
    logo->setObjectName("railLogo");
    logo->setAlignment(Qt::AlignCenter);
    railLayout->addWidget(logo);

    auto makeRailButton = [rail](const QString &text, const QString &tip) {
        auto *button = new QPushButton(text, rail);
        button->setObjectName("railButton");
        button->setToolTip(tip);
        button->setFixedSize(52, 46);
        return button;
    };

    auto *newChatRail = makeRailButton(QStringLiteral("+C"), QStringLiteral("Nuova chat"));
    auto *newGroupRail = makeRailButton(QStringLiteral("+G"), QStringLiteral("Nuovo gruppo"));
    auto *bluetoothRail = makeRailButton(QStringLiteral("BT"), QStringLiteral("Bluetooth"));
    auto *lightRail = makeRailButton(QStringLiteral("L"), QStringLiteral("Passa a EChat Light"));
    railLayout->addWidget(newChatRail, 0, Qt::AlignHCenter);
    railLayout->addWidget(newGroupRail, 0, Qt::AlignHCenter);
    railLayout->addWidget(bluetoothRail, 0, Qt::AlignHCenter);
    railLayout->addStretch();
    railLayout->addWidget(lightRail, 0, Qt::AlignHCenter);

    // Conversation column.
    auto *sidebar = new QWidget;
    sidebar->setObjectName("fullSidebar");
    sidebar->setMinimumWidth(300);
    sidebar->setMaximumWidth(360);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(16, 16, 16, 14);
    side->setSpacing(10);

    auto *profileRow = new QHBoxLayout;
    auto *profileAvatar = new QLabel(initials(identity_.username));
    profileAvatar->setObjectName("profileAvatar");
    profileAvatar->setAlignment(Qt::AlignCenter);
    profileAvatar->setFixedSize(42, 42);
    auto *profileText = new QVBoxLayout;
    auto *profileName = new QLabel(QStringLiteral("EChat Full"));
    profileName->setObjectName("sidebarTitle");
    userLabel_ = new QLabel(QStringLiteral("@%1").arg(identity_.username));
    userLabel_->setObjectName("mutedLabel");
    profileText->addWidget(profileName);
    profileText->addWidget(userLabel_);
    auto *accountButton = new QPushButton(QStringLiteral("Account"));
    accountButton->setObjectName("ghostButton");
    profileRow->addWidget(profileAvatar);
    profileRow->addLayout(profileText, 1);
    profileRow->addWidget(accountButton);
    side->addLayout(profileRow);

    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("Cerca chat, gruppi o persone"));
    searchEdit_->setObjectName("fullSearch");
    side->addWidget(searchEdit_);

    auto *filters = new QHBoxLayout;
    filters->setSpacing(6);
    allFilterButton_ = new QPushButton(QStringLiteral("Tutte"));
    directFilterButton_ = new QPushButton(QStringLiteral("Chat"));
    groupFilterButton_ = new QPushButton(QStringLiteral("Gruppi"));
    for (auto *button : {allFilterButton_, directFilterButton_, groupFilterButton_}) {
        button->setCheckable(true);
        button->setObjectName("filterButton");
        filters->addWidget(button);
    }
    allFilterButton_->setChecked(true);
    side->addLayout(filters);

    conversationList_ = new QListWidget;
    conversationList_->setObjectName("fullConversationList");
    conversationList_->setSpacing(4);
    side->addWidget(conversationList_, 1);

    auto *newRow = new QHBoxLayout;
    auto *newChatButton = new QPushButton(QStringLiteral("+ Chat"));
    auto *newGroupButton = new QPushButton(QStringLiteral("+ Gruppo"));
    newChatButton->setObjectName("secondaryButton");
    newGroupButton->setObjectName("secondaryButton");
    newRow->addWidget(newChatButton);
    newRow->addWidget(newGroupButton);
    side->addLayout(newRow);

    // Main chat column.
    auto *chat = new QWidget;
    chat->setObjectName("fullChat");
    auto *chatLayout = new QVBoxLayout(chat);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    auto *header = new QWidget;
    header->setObjectName("fullHeader");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(22, 14, 20, 14);
    headerLayout->setSpacing(12);

    auto *avatar = new QLabel(QStringLiteral("—"));
    avatar->setObjectName("chatAvatar");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(46, 46);
    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(2);
    conversationTitle_ = new QLabel(QStringLiteral("Seleziona una conversazione"));
    conversationTitle_->setObjectName("fullConversationTitle");
    conversationSubtitle_ = new QLabel(QStringLiteral("EChat Full · stessa rete, crittografia e cronologia della modalità Light"));
    conversationSubtitle_->setObjectName("mutedLabel");
    headerText->addWidget(conversationTitle_);
    headerText->addWidget(conversationSubtitle_);
    manageGroupButton_ = new QPushButton(QStringLiteral("Gestisci gruppo"));
    manageGroupButton_->setObjectName("ghostButton");
    manageGroupButton_->setVisible(false);
    auto *lightButton = new QPushButton(QStringLiteral("Light"));
    lightButton->setObjectName("ghostButton");
    headerLayout->addWidget(avatar);
    headerLayout->addLayout(headerText, 1);
    headerLayout->addWidget(manageGroupButton_);
    headerLayout->addWidget(lightButton);
    chatLayout->addWidget(header);

    messages_ = new QTextBrowser;
    messages_->setObjectName("fullMessages");
    messages_->setOpenLinks(false);
    messages_->setOpenExternalLinks(false);
    chatLayout->addWidget(messages_, 1);

    auto *composerWrap = new QWidget;
    composerWrap->setObjectName("fullComposerWrap");
    auto *composer = new QHBoxLayout(composerWrap);
    composer->setContentsMargins(20, 14, 20, 16);
    composer->setSpacing(10);
    messageEdit_ = new QLineEdit;
    messageEdit_->setObjectName("fullMessageEdit");
    messageEdit_->setPlaceholderText(QStringLiteral("Scrivi un messaggio…"));
    auto *sendButton = new QPushButton(QStringLiteral("Invia"));
    sendButton->setObjectName("primaryButton");
    sendButton->setMinimumWidth(94);
    composer->addWidget(messageEdit_, 1);
    composer->addWidget(sendButton);
    chatLayout->addWidget(composerWrap);

    statusLabel_ = new QLabel(QStringLiteral("Pronto"));
    statusLabel_->setObjectName("fullStatus");
    statusLabel_->setContentsMargins(20, 5, 20, 6);
    chatLayout->addWidget(statusLabel_);

    // Right details column.
    auto *details = new QWidget;
    details->setObjectName("detailsPanel");
    details->setMinimumWidth(270);
    details->setMaximumWidth(330);
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(18, 18, 18, 18);
    detailsLayout->setSpacing(10);

    detailsTitle_ = new QLabel(QStringLiteral("Dettagli"));
    detailsTitle_->setObjectName("detailsTitle");
    detailsLayout->addWidget(detailsTitle_);

    auto *networkCard = new QWidget;
    networkCard->setObjectName("infoCard");
    auto *networkLayout = new QVBoxLayout(networkCard);
    networkLayout->setContentsMargins(12, 12, 12, 12);
    networkLayout->setSpacing(6);
    auto *networkTitle = new QLabel(QStringLiteral("Connessione"));
    networkTitle->setObjectName("cardTitle");
    routeLabel_ = new QLabel(QStringLiteral("Nessuna route"));
    routeLabel_->setObjectName("cardBody");
    routeLabel_->setWordWrap(true);
    queueLabel_ = new QLabel(conversations_.reliabilitySummary());
    queueLabel_->setObjectName("mutedLabel");
    policyBox_ = new QComboBox;
    policyBox_->addItem(QStringLiteral("Auto"), static_cast<int>(TransportPolicy::Auto));
    policyBox_->addItem(QStringLiteral("Solo Bluetooth"), static_cast<int>(TransportPolicy::BluetoothOnly));
    policyBox_->addItem(QStringLiteral("Solo LAN"), static_cast<int>(TransportPolicy::LanOnly));
    policyBox_->addItem(QStringLiteral("Solo Internet"), static_cast<int>(TransportPolicy::InternetOnly));
    policyBox_->addItem(QStringLiteral("Preferisci Bluetooth"), static_cast<int>(TransportPolicy::PreferBluetooth));
    policyBox_->addItem(QStringLiteral("Preferisci LAN"), static_cast<int>(TransportPolicy::PreferLan));
    policyBox_->addItem(QStringLiteral("Preferisci Internet"), static_cast<int>(TransportPolicy::PreferInternet));
    networkLayout->addWidget(networkTitle);
    networkLayout->addWidget(routeLabel_);
    networkLayout->addWidget(queueLabel_);
    networkLayout->addWidget(policyBox_);
    detailsLayout->addWidget(networkCard);

    auto *securityCard = new QWidget;
    securityCard->setObjectName("infoCard");
    auto *securityLayout = new QVBoxLayout(securityCard);
    securityLayout->setContentsMargins(12, 12, 12, 12);
    securityLayout->setSpacing(5);
    auto *securityTitle = new QLabel(QStringLiteral("Sicurezza"));
    securityTitle->setObjectName("cardTitle");
    securityLabel_ = new QLabel(QStringLiteral("E2EE v2"));
    securityLabel_->setObjectName("cardBody");
    securityLabel_->setWordWrap(true);
    securityLayout->addWidget(securityTitle);
    securityLayout->addWidget(securityLabel_);
    detailsLayout->addWidget(securityCard);

    auto *membersCard = new QWidget;
    membersCard->setObjectName("infoCard");
    auto *membersLayout = new QVBoxLayout(membersCard);
    membersLayout->setContentsMargins(12, 12, 12, 12);
    membersLayout->setSpacing(6);
    auto *membersTitle = new QLabel(QStringLiteral("Persone"));
    membersTitle->setObjectName("cardTitle");
    membersList_ = new QListWidget;
    membersList_->setObjectName("membersList");
    membersList_->setSelectionMode(QAbstractItemView::NoSelection);
    membersLayout->addWidget(membersTitle);
    membersLayout->addWidget(membersList_);
    detailsLayout->addWidget(membersCard, 1);

    auto *bluetoothButton = new QPushButton(QStringLiteral("Cerca dispositivi Bluetooth"));
    bluetoothButton->setObjectName("secondaryButton");
    detailsLayout->addWidget(bluetoothButton);

    root->addWidget(rail);
    root->addWidget(sidebar);
    root->addWidget(chat, 1);
    root->addWidget(details);
    setCentralWidget(central);
    applyTheme();

    auto switchToLight = [this] { emit requestLightMode(currentConversationId_); };
    connect(lightRail, &QPushButton::clicked, this, switchToLight);
    connect(lightButton, &QPushButton::clicked, this, switchToLight);
    connect(newChatRail, &QPushButton::clicked, this, &FullMainWindow::createDirectChat);
    connect(newGroupRail, &QPushButton::clicked, this, &FullMainWindow::createGroup);
    connect(bluetoothRail, &QPushButton::clicked, this, &FullMainWindow::openBluetoothScanner);
    connect(bluetoothButton, &QPushButton::clicked, this, &FullMainWindow::openBluetoothScanner);
    connect(newChatButton, &QPushButton::clicked, this, &FullMainWindow::createDirectChat);
    connect(newGroupButton, &QPushButton::clicked, this, &FullMainWindow::createGroup);
    connect(accountButton, &QPushButton::clicked, this, &FullMainWindow::openAccountSettings);
    connect(manageGroupButton_, &QPushButton::clicked, this, &FullMainWindow::manageCurrentGroup);

    connect(allFilterButton_, &QPushButton::clicked, this, [this] { setConversationFilter(0); });
    connect(directFilterButton_, &QPushButton::clicked, this, [this] { setConversationFilter(1); });
    connect(groupFilterButton_, &QPushButton::clicked, this, [this] { setConversationFilter(2); });
    connect(searchEdit_, &QLineEdit::textChanged, this, [this] { refreshConversations(); });

    connect(conversationList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current) {
        if (current) selectConversation(current->data(Qt::UserRole).toString());
    });

    connect(sendButton, &QPushButton::clicked, this, [this] {
        if (currentConversationId_.isEmpty()) return;
        const QString text = messageEdit_->text().trimmed();
        if (text.isEmpty()) return;
        conversations_.sendMessage(currentConversationId_, text, selectedPolicy());
        messageEdit_->clear();
        refreshMessages();
    });
    connect(messageEdit_, &QLineEdit::returnPressed, sendButton, &QPushButton::click);

    connect(policyBox_, &QComboBox::currentIndexChanged, this, [this] {
        refreshRouteStatus();
        if (!currentConversationId_.isEmpty())
            conversations_.retryQueuedMessages(currentConversationId_, selectedPolicy());
    });

    connect(messages_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const QString raw = url.toString();
        const QString prefix = QStringLiteral("echat-delete:");
        if (raw.startsWith(prefix)) deleteMessageFromLink(raw.mid(prefix.size()));
    });

    connect(&conversations_, &ConversationManager::conversationUpdated, this, [this](const Conversation &conversation) {
        refreshConversations();
        if (conversation.id == currentConversationId_) {
            refreshHeader();
            refreshDetails();
            refreshRouteStatus();
        }
    });
    connect(&conversations_, &ConversationManager::messageAdded, this, [this](const Message &message) {
        refreshConversations();
        if (message.conversationId == currentConversationId_) refreshMessages();
    });
    connect(&conversations_, &ConversationManager::messageRemoved, this, [this](const QString &conversationId, const QString &) {
        if (conversationId == currentConversationId_) refreshMessages();
        refreshRouteStatus();
    });
    connect(&conversations_, &ConversationManager::localUsernameChanged, this, [this](const QString &username) {
        identity_.username = username;
        userLabel_->setText(QStringLiteral("@%1").arg(username));
        statusLabel_->setText(QStringLiteral("Username aggiornato: @%1").arg(username));
        refreshDetails();
    });
    connect(&conversations_, &ConversationManager::deliveryInfo, this, [this](const QString &, const QString &info) {
        statusLabel_->setText(info);
        refreshMessages();
        refreshRouteStatus();
    });
    connect(&conversations_, &ConversationManager::reliabilityStateChanged, this, [this] {
        refreshMessages();
        refreshRouteStatus();
    });
    connect(&conversations_, &ConversationManager::protocolError, this, [this](const QString &error) {
        statusLabel_->setText(error);
    });
    connect(&peers_, &PeerManager::peerUpdated, this, [this](const Peer &) {
        refreshConversations();
        refreshHeader();
        refreshDetails();
        refreshRouteStatus();
    });
    connect(&peers_, &PeerManager::securityWarning, this, [this](const QString &warning) {
        statusLabel_->setText(warning);
        QMessageBox::warning(this, QStringLiteral("EChat — sicurezza"), warning);
    });
    connect(&transports_, &TransportManager::statusMessage, statusLabel_, &QLabel::setText);
    connect(&bluetooth_, &BluetoothTransport::connectionChanged, this, [this](const QString &, bool) {
        refreshRouteStatus();
    });

    refreshConversations();
    refreshDetails();
}

void FullMainWindow::applyTheme() {
    setStyleSheet(R"(
        #fullRoot, #fullChat { background:#0f1115; color:#edf0f5; }
        #rail { background:#0b0d11; border-right:1px solid #242833; }
        #fullSidebar { background:#171a21; border-right:1px solid #292e38; }
        #detailsPanel { background:#151820; border-left:1px solid #292e38; }
        #fullHeader { background:#13161c; border-bottom:1px solid #292e38; }
        #fullComposerWrap { background:#13161c; border-top:1px solid #292e38; }
        #fullStatus { background:#0d0f13; color:#8993a3; font-size:11px; }

        #railLogo { background:#5865f2; color:#ffffff; border-radius:16px; font-weight:900; font-size:16px; padding:8px; }
        #railButton { background:#20242c; color:#dfe4ec; border:none; border-radius:14px; font-weight:800; }
        #railButton:hover { background:#5865f2; color:#ffffff; }
        #profileAvatar, #chatAvatar { background:#2a3040; color:#ffffff; border-radius:16px; font-weight:800; }
        #sidebarTitle { font-size:17px; font-weight:800; color:#ffffff; }
        #fullConversationTitle { font-size:19px; font-weight:800; color:#ffffff; }
        #detailsTitle { font-size:18px; font-weight:800; color:#f5f7fa; }
        #cardTitle { color:#f0f3f7; font-size:12px; font-weight:800; }
        #cardBody { color:#c4cbd6; font-size:12px; }
        #mutedLabel { color:#8f99a8; font-size:11px; }

        #fullSearch, #fullMessageEdit, QComboBox {
            background:#222630; color:#eef2f7; border:1px solid #323844;
            border-radius:10px; padding:10px 12px; selection-background-color:#5865f2;
        }
        #fullMessageEdit { font-size:14px; padding:12px 14px; }

        #fullConversationList, #membersList { background:transparent; color:#e4e8ee; border:none; outline:none; }
        #fullConversationList::item { padding:12px 10px; border-radius:10px; }
        #fullConversationList::item:hover { background:#222731; }
        #fullConversationList::item:selected { background:#30384a; color:#ffffff; }
        #membersList::item { padding:7px 4px; border-radius:7px; }
        #membersList::item:hover { background:#20242d; }

        #fullMessages { background:#0f1115; color:#edf0f5; border:none; padding:18px; }
        #infoCard { background:#1c2028; border:1px solid #2c323e; border-radius:12px; }

        QPushButton { border:none; border-radius:9px; padding:9px 12px; font-weight:700; }
        #primaryButton { background:#5865f2; color:#ffffff; }
        #primaryButton:hover { background:#6874f5; }
        #secondaryButton { background:#252a34; color:#e7ebf1; }
        #secondaryButton:hover { background:#303641; }
        #ghostButton { background:transparent; color:#b7c0cd; border:1px solid #303641; padding:7px 10px; }
        #ghostButton:hover { background:#242933; color:#ffffff; }
        #filterButton { background:#20242c; color:#aeb7c5; padding:7px 10px; }
        #filterButton:hover { background:#282e39; color:#ffffff; }
        #filterButton:checked { background:#5865f2; color:#ffffff; }
    )");
}

TransportPolicy FullMainWindow::selectedPolicy() const {
    return static_cast<TransportPolicy>(policyBox_->currentData().toInt());
}

void FullMainWindow::setConversationFilter(int mode) {
    conversationFilter_ = mode;
    allFilterButton_->setChecked(mode == 0);
    directFilterButton_->setChecked(mode == 1);
    groupFilterButton_->setChecked(mode == 2);
    refreshConversations();
}

void FullMainWindow::refreshConversations() {
    if (!conversationList_) return;
    const QString selected = currentConversationId_;
    const QString filter = searchEdit_ ? searchEdit_->text().trimmed() : QString();
    conversationList_->clear();

    auto list = conversations_.conversations();
    std::sort(list.begin(), list.end(), [](const Conversation &a, const Conversation &b) {
        return a.name.toLower() < b.name.toLower();
    });

    for (const auto &conversation : list) {
        if (conversationFilter_ == 1 && conversation.type != ConversationType::Direct) continue;
        if (conversationFilter_ == 2 && conversation.type != ConversationType::Group) continue;
        QString title = conversation.name.isEmpty() ? conversation.id.left(8) : conversation.name;
        if (!filter.isEmpty() && !title.contains(filter, Qt::CaseInsensitive)) continue;

        QString subtitle;
        if (conversation.type == ConversationType::Group) {
            subtitle = QStringLiteral("Gruppo · %1 membri").arg(conversation.memberIds.size());
        } else {
            for (const auto &id : conversation.memberIds) {
                if (id == identity_.userId || !peers_.hasPeer(id)) continue;
                subtitle = transportSummary(transports_.routesFor(peers_.peer(id)));
                break;
            }
            if (subtitle.isEmpty()) subtitle = QStringLiteral("Chat diretta");
        }

        const QString prefix = conversation.type == ConversationType::Group ? QStringLiteral("#  ") : QStringLiteral("●  ");
        auto *item = new QListWidgetItem(QStringLiteral("%1%2\n   %3").arg(prefix).arg(title).arg(subtitle), conversationList_);
        item->setData(Qt::UserRole, conversation.id);
        item->setToolTip(conversation.id);
        if (conversation.id == selected) conversationList_->setCurrentItem(item);
    }
}

void FullMainWindow::activateConversation(const QString &id) {
    if (id.isEmpty()) return;
    selectConversation(id);
}

void FullMainWindow::selectConversation(const QString &id) {
    currentConversationId_ = id;
    refreshHeader();
    refreshMessages();
    refreshDetails();
    refreshRouteStatus();
    if (messageEdit_) messageEdit_->setFocus();
}

void FullMainWindow::refreshHeader() {
    if (currentConversationId_.isEmpty()) {
        conversationTitle_->setText(QStringLiteral("Seleziona una conversazione"));
        conversationSubtitle_->setText(QStringLiteral("Le modalità Light e Full condividono account, messaggi e rete"));
        manageGroupButton_->setVisible(false);
        return;
    }

    const Conversation conversation = conversations_.conversation(currentConversationId_);
    conversationTitle_->setText(conversation.type == ConversationType::Group
                                    ? QStringLiteral("# %1").arg(conversation.name)
                                    : conversation.name);
    manageGroupButton_->setVisible(conversation.type == ConversationType::Group);

    if (conversation.type == ConversationType::Group) {
        conversationSubtitle_->setText(QStringLiteral("%1 membri · E2EE fan-out · mesh P2P")
                                           .arg(conversation.memberIds.size()));
        return;
    }

    for (const auto &id : conversation.memberIds) {
        if (id == identity_.userId || !peers_.hasPeer(id)) continue;
        const Peer peer = peers_.peer(id);
        const RouteState routes = transports_.routesFor(peer);
        conversationSubtitle_->setText(QStringLiteral("%1 · %2")
                                           .arg(transportSummary(routes))
                                           .arg(peer.identityFingerprint.isEmpty()
                                                    ? QStringLiteral("fingerprint non disponibile")
                                                    : peer.identityFingerprint));
        return;
    }
    conversationSubtitle_->setText(QStringLiteral("Peer non ancora disponibile"));
}

void FullMainWindow::refreshMessages() {
    if (currentConversationId_.isEmpty()) {
        messages_->setHtml(QStringLiteral(
            "<html><body style='font-family:sans-serif;background:#0f1115;color:#8f99a8;'>"
            "<div style='text-align:center;margin-top:150px;font-size:15px;'>Scegli una chat dalla colonna a sinistra.</div>"
            "</body></html>"));
        return;
    }

    QString html = QStringLiteral("<html><body style='font-family:sans-serif;background:#0f1115;color:#edf0f5;'>");
    const auto items = conversations_.messages(currentConversationId_);
    QString lastDay;
    for (const auto &message : items) {
        const bool mine = message.senderId == identity_.userId;
        QString sender = mine ? QStringLiteral("Tu") : message.senderId;
        if (peers_.hasPeer(message.senderId)) sender = displayNameForPeer(peers_.peer(message.senderId));

        const QDateTime when = QDateTime::fromMSecsSinceEpoch(message.timestampMs).toLocalTime();
        const QString day = when.date().toString(QStringLiteral("dd/MM/yyyy"));
        if (day != lastDay) {
            html += QStringLiteral("<div style='text-align:center;margin:15px 0 10px;color:#7f8998;font-size:10px;'>%1</div>")
                        .arg(day.toHtmlEscaped());
            lastDay = day;
        }

        const QString time = when.toString(QStringLiteral("HH:mm"));
        const QString delivery = mine ? conversations_.deliveryIndicator(message.id) : QString();
        const QString meta = delivery.isEmpty() ? time : QStringLiteral("%1 · %2").arg(time).arg(delivery);
        const QString align = mine ? QStringLiteral("right") : QStringLiteral("left");
        const QString background = mine ? QStringLiteral("#384997") : QStringLiteral("#20242c");
        const QString border = mine ? QStringLiteral("#4d5fb6") : QStringLiteral("#2f3540");
        const QString deleteAction = mine
            ? QStringLiteral(" · <a href=\"echat-delete:%1\" style=\"color:#dce1ff;text-decoration:none;\">Elimina</a>")
                  .arg(message.id.toHtmlEscaped())
            : QString();

        QString body = message.text.toHtmlEscaped();
        body.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        html += QStringLiteral(
                    "<div style='text-align:%1;margin:8px 7px;'>"
                    "<div style='display:inline-block;max-width:70%;background:%2;border:1px solid %3;"
                    "border-radius:14px;padding:9px 12px;text-align:left;'>"
                    "<div style='font-size:10px;color:#b2bbc9;margin-bottom:4px;'><b>%4</b> · %5%6</div>"
                    "<div style='font-size:14px;line-height:1.35;'>%7</div>"
                    "</div></div>")
                    .arg(align).arg(background).arg(border).arg(sender.toHtmlEscaped()).arg(meta.toHtmlEscaped()).arg(deleteAction).arg(body);
    }
    if (items.isEmpty()) {
        html += QStringLiteral("<div style='text-align:center;margin-top:120px;color:#7f8998;'>Nessun messaggio. Scrivi il primo messaggio.</div>");
    }
    html += QStringLiteral("</body></html>");
    messages_->setHtml(html);
    messages_->verticalScrollBar()->setValue(messages_->verticalScrollBar()->maximum());
}

void FullMainWindow::refreshDetails() {
    membersList_->clear();
    if (currentConversationId_.isEmpty()) {
        detailsTitle_->setText(QStringLiteral("Dettagli"));
        securityLabel_->setText(QStringLiteral("E2EE v2 · Ed25519 + X25519 + XChaCha20-Poly1305"));
        auto *item = new QListWidgetItem(QStringLiteral("Seleziona una conversazione"), membersList_);
        item->setFlags(Qt::NoItemFlags);
        return;
    }

    const Conversation conversation = conversations_.conversation(currentConversationId_);
    detailsTitle_->setText(conversation.type == ConversationType::Group ? QStringLiteral("Dettagli gruppo")
                                                                        : QStringLiteral("Dettagli chat"));

    if (conversation.type == ConversationType::Group) {
        securityLabel_->setText(QStringLiteral("E2EE v2 · fan-out cifrato per destinatario · relay firmato"));
    } else {
        securityLabel_->setText(QStringLiteral("E2EE v2 · Ed25519 + X25519 + XChaCha20-Poly1305"));
    }

    for (const auto &id : conversation.memberIds) {
        QString label;
        QString toolTip = id;
        if (id == identity_.userId) {
            label = QStringLiteral("Tu · @%1").arg(identity_.username);
        } else if (peers_.hasPeer(id)) {
            const Peer peer = peers_.peer(id);
            const QString route = transportSummary(transports_.routesFor(peer));
            label = QStringLiteral("%1 · %2").arg(displayNameForPeer(peer)).arg(route);
            toolTip = peer.identityFingerprint.isEmpty() ? id : peer.identityFingerprint;
        } else {
            label = QStringLiteral("%1 · sconosciuto").arg(id.left(12));
        }
        auto *item = new QListWidgetItem(label, membersList_);
        item->setToolTip(toolTip);
    }
}

void FullMainWindow::refreshRouteStatus() {
    queueLabel_->setText(conversations_.reliabilitySummary());
    if (currentConversationId_.isEmpty()) {
        routeLabel_->setText(QStringLiteral("Nessuna conversazione selezionata"));
        return;
    }

    const Conversation conversation = conversations_.conversation(currentConversationId_);
    int recipients = 0;
    int bluetooth = 0;
    int lan = 0;
    int internet = 0;
    int mesh = 0;
    const TransportPolicy policy = selectedPolicy();

    for (const auto &id : conversation.memberIds) {
        if (id == identity_.userId || !peers_.hasPeer(id)) continue;
        ++recipients;
        const Peer peer = peers_.peer(id);
        const RouteState routes = transports_.routesFor(peer);
        bluetooth += routes.bluetooth ? 1 : 0;
        lan += routes.lan ? 1 : 0;
        internet += routes.internet ? 1 : 0;
        if (!routes.bluetooth && !routes.lan && !routes.internet && conversations_.relayPossible(id, policy)) ++mesh;
    }

    if (recipients == 0) {
        routeLabel_->setText(QStringLiteral("Nessun destinatario raggiungibile"));
        return;
    }

    routeLabel_->setText(QStringLiteral("BT %1/%5 · LAN %2/%5 · Internet %3/%5 · Relay %4/%5")
                             .arg(bluetooth).arg(lan).arg(internet).arg(mesh).arg(recipients));
    refreshDetails();
}

void FullMainWindow::createDirectChat() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Nuova chat EChat"));
    dialog.resize(540, 500);
    QVBoxLayout layout(&dialog);
    auto *info = new QLabel(QStringLiteral("Peer conosciuti tramite HELLO firmata"));
    info->setObjectName("mutedLabel");
    QListWidget list;
    for (const auto &peer : peers_.peers()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2")
                                             .arg(displayNameForPeer(peer)).arg(peer.identityFingerprint), &list);
        item->setData(Qt::UserRole, peer.userId);
    }
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(info);
    layout.addWidget(&list, 1);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !list.currentItem()) return;
    const QString id = conversations_.ensureDirectConversation(list.currentItem()->data(Qt::UserRole).toString());
    refreshConversations();
    selectConversation(id);
}

void FullMainWindow::createGroup() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Nuovo gruppo EChat"));
    dialog.resize(560, 570);
    QVBoxLayout layout(&dialog);
    QLineEdit name;
    name.setPlaceholderText(QStringLiteral("Nome gruppo"));
    name.setMaxLength(80);
    QListWidget members;
    members.setSelectionMode(QAbstractItemView::MultiSelection);
    for (const auto &peer : peers_.peers()) {
        auto *item = new QListWidgetItem(displayNameForPeer(peer), &members);
        item->setData(Qt::UserRole, peer.userId);
        item->setToolTip(peer.identityFingerprint);
    }
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&name);
    layout.addWidget(new QLabel(QStringLiteral("Membri")));
    layout.addWidget(&members, 1);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    QStringList ids;
    for (auto *item : members.selectedItems()) ids << item->data(Qt::UserRole).toString();
    if (ids.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("EChat"), QStringLiteral("Seleziona almeno un altro membro."));
        return;
    }
    const QString id = conversations_.createGroup(name.text(), ids);
    refreshConversations();
    selectConversation(id);
}

void FullMainWindow::manageCurrentGroup() {
    if (currentConversationId_.isEmpty()) return;
    const Conversation conversation = conversations_.conversation(currentConversationId_);
    if (conversation.type != ConversationType::Group) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Gestisci gruppo — %1").arg(conversation.name));
    dialog.resize(580, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *nameEdit = new QLineEdit(conversation.name);
    nameEdit->setMaxLength(80);
    layout->addWidget(new QLabel(QStringLiteral("Nome gruppo")));
    layout->addWidget(nameEdit);

    layout->addWidget(new QLabel(QStringLiteral("Membri attuali")));
    auto *currentMembers = new QListWidget;
    currentMembers->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto &id : conversation.memberIds) {
        QString label;
        if (id == identity_.userId) label = QStringLiteral("Tu (@%1)").arg(identity_.username);
        else if (peers_.hasPeer(id)) label = QStringLiteral("@%1").arg(displayNameForPeer(peers_.peer(id)));
        else label = id;
        auto *item = new QListWidgetItem(label, currentMembers);
        item->setToolTip(id);
    }
    layout->addWidget(currentMembers, 1);

    layout->addWidget(new QLabel(QStringLiteral("Aggiungi persone")));
    auto *available = new QListWidget;
    available->setSelectionMode(QAbstractItemView::MultiSelection);
    int availableCount = 0;
    for (const auto &peer : peers_.peers()) {
        if (conversation.memberIds.contains(peer.userId)) continue;
        auto *item = new QListWidgetItem(QStringLiteral("@%1").arg(displayNameForPeer(peer)), available);
        item->setData(Qt::UserRole, peer.userId);
        item->setToolTip(peer.identityFingerprint);
        ++availableCount;
    }
    if (availableCount == 0) {
        auto *empty = new QListWidgetItem(QStringLiteral("Nessun altro peer conosciuto"), available);
        empty->setFlags(Qt::NoItemFlags);
    }
    layout->addWidget(available, 1);

    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    if (auto *save = buttons.button(QDialogButtonBox::Save)) save->setText(QStringLiteral("Salva"));
    layout->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    QStringList added;
    for (auto *item : available->selectedItems()) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty()) added << id;
    }
    if (conversations_.updateGroup(conversation.id, nameEdit->text(), added, selectedPolicy())) {
        statusLabel_->setText(QStringLiteral("Gruppo aggiornato"));
        refreshHeader();
        refreshConversations();
        refreshDetails();
        refreshRouteStatus();
    } else {
        statusLabel_->setText(QStringLiteral("Nessuna modifica al gruppo"));
    }
}

void FullMainWindow::openAccountSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Account EChat"));
    dialog.resize(570, 320);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *username = new QLineEdit(identity_.username);
    username->setMaxLength(48);
    auto *userId = new QLineEdit(identity_.userId);
    userId->setReadOnly(true);
    auto *fingerprint = new QLineEdit(crypto_.fingerprint(identity_.signingPublicKey, identity_.kxPublicKey));
    fingerprint->setReadOnly(true);
    form->addRow(QStringLiteral("Username"), username);
    form->addRow(QStringLiteral("User ID"), userId);
    form->addRow(QStringLiteral("Fingerprint"), fingerprint);
    layout->addLayout(form);
    auto *info = new QLabel(QStringLiteral("Light e Full usano lo stesso account locale. Cambiare username non ruota le chiavi."));
    info->setWordWrap(true);
    layout->addWidget(info);
    layout->addStretch();

    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    if (auto *save = buttons.button(QDialogButtonBox::Save)) save->setText(QStringLiteral("Salva"));
    layout->addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString next = username->text().trimmed();
    if (next.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("EChat"), QStringLiteral("Lo username non puo' essere vuoto."));
        return;
    }
    if (next != identity_.username && !conversations_.changeUsername(next)) {
        QMessageBox::warning(this, QStringLiteral("EChat"), QStringLiteral("Username non valido o non modificato."));
    }
}

void FullMainWindow::openBluetoothScanner() {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("EChat — Bluetooth"));
    auto *layout = new QVBoxLayout(dialog);
    auto *info = new QLabel(QStringLiteral("Bluetooth Classic/RFCOMM. È consigliato associare prima i PC dal sistema operativo."));
    info->setWordWrap(true);
    auto *list = new QListWidget;
    auto *connectButton = new QPushButton(QStringLiteral("Connetti"));
    connectButton->setObjectName("primaryButton");
    layout->addWidget(info);
    layout->addWidget(list, 1);
    layout->addWidget(connectButton);
    dialog->resize(570, 460);

    connect(&bluetooth_, &BluetoothTransport::deviceFound, dialog, [list](const QString &name, const QString &address) {
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == address) return;
        auto *item = new QListWidgetItem(QStringLiteral("%1  [%2]")
                                             .arg(name.isEmpty() ? QStringLiteral("Senza nome") : name).arg(address), list);
        item->setData(Qt::UserRole, address);
    });
    connect(connectButton, &QPushButton::clicked, dialog, [this, list] {
        if (auto *item = list->currentItem()) bluetooth_.connectToDevice(item->data(Qt::UserRole).toString());
    });
    connect(list, &QListWidget::itemDoubleClicked, dialog, [this](QListWidgetItem *item) {
        bluetooth_.connectToDevice(item->data(Qt::UserRole).toString());
    });
    dialog->show();
    bluetooth_.scan();
}

void FullMainWindow::deleteMessageFromLink(const QString &messageId) {
    if (messageId.isEmpty()) return;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Elimina messaggio"),
        QStringLiteral("Eliminare questo messaggio per tutti i destinatari EChat?\n\n"
                       "La richiesta verra' cifrata e ritentata anche se un destinatario e' temporaneamente offline."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    if (!conversations_.deleteOwnMessage(messageId, selectedPolicy())) {
        QMessageBox::warning(this, QStringLiteral("EChat"),
                             QStringLiteral("Puoi eliminare solo i messaggi inviati da questo account."));
        return;
    }
    statusLabel_->setText(QStringLiteral("Messaggio eliminato; propagazione E2EE accodata"));
    refreshMessages();
}

} // namespace ec
