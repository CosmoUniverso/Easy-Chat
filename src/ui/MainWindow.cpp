#include "ui/MainWindow.h"
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
#include <QTextBrowser>
#include <QUrl>
#include <QScrollBar>
#include <QShortcut>
#include <QKeySequence>
#include <algorithm>
#include <QVBoxLayout>

namespace ec {

MainWindow::MainWindow(const LocalIdentity &identity, CryptoEngine &crypto, PeerManager &peers,
                       ConversationManager &conversations, TransportManager &transports,
                       BluetoothTransport &bluetooth, QWidget *parent)
    : QMainWindow(parent), identity_(identity), crypto_(crypto), peers_(peers), conversations_(conversations),
      transports_(transports), bluetooth_(bluetooth) {
    setWindowTitle(QStringLiteral("EChat Light — Easy Chat"));
    resize(1220, 760);
    setMinimumSize(900, 580);

    auto *central = new QWidget(this);
    auto *outer = new QHBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setMinimumWidth(310);
    sidebar->setMaximumWidth(390);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(18, 18, 18, 18);
    side->setSpacing(10);

    auto *brandRow = new QHBoxLayout;
    auto *brand = new QLabel("EChat"); brand->setObjectName("brand");
    userLabel_ = new QLabel(QStringLiteral("@%1").arg(identity_.username)); userLabel_->setObjectName("mutedLabel");
    auto *accountButton = new QPushButton(QStringLiteral("Account"));
    accountButton->setObjectName("headerActionButton");
    brandRow->addWidget(brand); brandRow->addStretch(); brandRow->addWidget(userLabel_); brandRow->addWidget(accountButton);
    side->addLayout(brandRow);

    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("Cerca conversazioni"));
    searchEdit_->setObjectName("searchBox");
    side->addWidget(searchEdit_);

    auto *actions = new QHBoxLayout;
    auto *newChatButton = new QPushButton(QStringLiteral("+ Chat"));
    auto *groupButton = new QPushButton(QStringLiteral("+ Gruppo"));
    newChatButton->setObjectName("secondaryButton"); groupButton->setObjectName("secondaryButton");
    actions->addWidget(newChatButton); actions->addWidget(groupButton);
    side->addLayout(actions);

    conversationList_ = new QListWidget;
    conversationList_->setObjectName("conversationList");
    conversationList_->setSpacing(3);
    side->addWidget(conversationList_, 1);

    auto *scanButton = new QPushButton(QStringLiteral("Bluetooth  ·  cerca dispositivi"));
    scanButton->setObjectName("primaryButton");
    side->addWidget(scanButton);

    auto *main = new QWidget;
    main->setObjectName("chatPanel");
    auto *right = new QVBoxLayout(main);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);

    auto *chatHeader = new QWidget; chatHeader->setObjectName("chatHeader");
    auto *headerLayout = new QVBoxLayout(chatHeader);
    headerLayout->setContentsMargins(22, 14, 22, 12);
    headerLayout->setSpacing(4);
    auto *titleRow = new QHBoxLayout;
    conversationTitle_ = new QLabel(QStringLiteral("Seleziona una conversazione"));
    conversationTitle_->setObjectName("conversationTitle");
    policyBox_ = new QComboBox;
    policyBox_->addItem("Auto", static_cast<int>(TransportPolicy::Auto));
    policyBox_->addItem("Solo Bluetooth", static_cast<int>(TransportPolicy::BluetoothOnly));
    policyBox_->addItem("Solo LAN", static_cast<int>(TransportPolicy::LanOnly));
    policyBox_->addItem("Solo Internet", static_cast<int>(TransportPolicy::InternetOnly));
    policyBox_->addItem("Preferisci Bluetooth", static_cast<int>(TransportPolicy::PreferBluetooth));
    policyBox_->addItem("Preferisci LAN", static_cast<int>(TransportPolicy::PreferLan));
    policyBox_->addItem("Preferisci Internet", static_cast<int>(TransportPolicy::PreferInternet));
    policyBox_->setObjectName("policyBox");
    groupManageButton_ = new QPushButton(QStringLiteral("Gestisci gruppo"));
    groupManageButton_->setObjectName("headerActionButton");
    groupManageButton_->setVisible(false);
    auto *fullButton = new QPushButton(QStringLiteral("Full UI"));
    fullButton->setObjectName("headerActionButton");
    fullButton->setToolTip(QStringLiteral("Passa all'interfaccia completa"));
    auto *stealthButton = new QPushButton(QStringLiteral("Stealth"));
    stealthButton->setObjectName("headerActionButton");
    stealthButton->setToolTip(QStringLiteral("Modalita' compatta · Ctrl+Shift+S"));
    titleRow->addWidget(conversationTitle_); titleRow->addStretch(); titleRow->addWidget(groupManageButton_); titleRow->addWidget(fullButton); titleRow->addWidget(stealthButton); titleRow->addWidget(policyBox_);
    routeLabel_ = new QLabel(QStringLiteral("Nessuna route")); routeLabel_->setObjectName("routeLabel");
    securityLabel_ = new QLabel(QStringLiteral("E2EE v2 · Ed25519 + X25519 + XChaCha20-Poly1305"));
    securityLabel_->setObjectName("securityLabel");
    headerLayout->addLayout(titleRow); headerLayout->addWidget(routeLabel_); headerLayout->addWidget(securityLabel_);
    right->addWidget(chatHeader);

    messages_ = new QTextBrowser;
    messages_->setObjectName("messages");
    messages_->setOpenLinks(false);
    messages_->setOpenExternalLinks(false);
    right->addWidget(messages_, 1);

    auto *composerBox = new QWidget; composerBox->setObjectName("composerBox");
    auto *composer = new QHBoxLayout(composerBox);
    composer->setContentsMargins(18, 14, 18, 14);
    messageEdit_ = new QLineEdit;
    messageEdit_->setPlaceholderText(QStringLiteral("Scrivi un messaggio..."));
    messageEdit_->setObjectName("messageEdit");
    auto *sendButton = new QPushButton(QStringLiteral("Invia")); sendButton->setObjectName("sendButton");
    composer->addWidget(messageEdit_, 1); composer->addWidget(sendButton);
    right->addWidget(composerBox);

    statusLabel_ = new QLabel(QStringLiteral("Avvio..."));
    statusLabel_->setObjectName("statusBarLabel");
    statusLabel_->setContentsMargins(18, 5, 18, 5);
    right->addWidget(statusLabel_);

    outer->addWidget(sidebar);
    outer->addWidget(main, 1);
    setCentralWidget(central);
    applyTheme();

    connect(scanButton, &QPushButton::clicked, this, &MainWindow::openBluetoothScanner);
    connect(accountButton, &QPushButton::clicked, this, &MainWindow::openAccountSettings);
    connect(fullButton, &QPushButton::clicked, this, [this] { emit requestFullMode(currentConversationId_); });
    connect(stealthButton, &QPushButton::clicked, this, &MainWindow::openStealthMode);
    auto *stealthShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")), this);
    connect(stealthShortcut, &QShortcut::activated, this, &MainWindow::openStealthMode);
    connect(groupManageButton_, &QPushButton::clicked, this, &MainWindow::manageCurrentGroup);
    connect(groupButton, &QPushButton::clicked, this, &MainWindow::createGroup);
    connect(newChatButton, &QPushButton::clicked, this, &MainWindow::createDirectChat);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this] { refreshConversations(); });
    connect(policyBox_, &QComboBox::currentIndexChanged, this, [this] {
        refreshRouteStatus();
        if (!currentConversationId_.isEmpty())
            conversations_.retryQueuedMessages(currentConversationId_, selectedPolicy());
    });
    connect(sendButton, &QPushButton::clicked, this, [this] {
        if (currentConversationId_.isEmpty()) return;
        const QString text = messageEdit_->text().trimmed();
        if (text.isEmpty()) return;
        conversations_.sendMessage(currentConversationId_, text, selectedPolicy());
        messageEdit_->clear(); refreshMessages();
    });
    connect(messageEdit_, &QLineEdit::returnPressed, sendButton, &QPushButton::click);
    connect(conversationList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current) {
        if (current) selectConversation(current->data(Qt::UserRole).toString());
    });
    connect(messages_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const QString raw = url.toString();
        const QString prefix = QStringLiteral("echat-delete:");
        if (raw.startsWith(prefix)) deleteMessageFromLink(raw.mid(prefix.size()));
    });
    connect(&conversations_, &ConversationManager::conversationUpdated, this, [this](const Conversation &c) {
        refreshConversations();
        if (c.id == currentConversationId_) { refreshHeader(); refreshRouteStatus(); }
    });
    connect(&conversations_, &ConversationManager::messageAdded, this, [this](const Message &m) {
        refreshConversations();
        if (m.conversationId == currentConversationId_) { refreshMessages(); refreshStealthMode(); }
    });
    connect(&conversations_, &ConversationManager::messageRemoved, this, [this](const QString &conversationId, const QString &) {
        if (conversationId == currentConversationId_) { refreshMessages(); refreshStealthMode(); }
        refreshRouteStatus();
    });
    connect(&conversations_, &ConversationManager::localUsernameChanged, this, [this](const QString &username) {
        identity_.username = username;
        userLabel_->setText(QStringLiteral("@%1").arg(username));
        statusLabel_->setText(QStringLiteral("Username aggiornato: @%1").arg(username));
    });
    connect(&peers_, &PeerManager::peerUpdated, this, [this](const Peer &) { refreshConversations(); refreshRouteStatus(); refreshHeader(); });
    connect(&peers_, &PeerManager::securityWarning, this, [this](const QString &warning) {
        statusLabel_->setText(warning); QMessageBox::warning(this, "EChat — sicurezza", warning);
    });
    connect(&transports_, &TransportManager::statusMessage, statusLabel_, &QLabel::setText);
    connect(&conversations_, &ConversationManager::deliveryInfo, this, [this](const QString &, const QString &info) {
        statusLabel_->setText(info); refreshMessages(); refreshStealthMode(); refreshRouteStatus();
    });
    connect(&conversations_, &ConversationManager::reliabilityStateChanged, this, [this] {
        refreshMessages(); refreshStealthMode(); refreshRouteStatus();
    });
    connect(&conversations_, &ConversationManager::protocolError, this, [this](const QString &error) { statusLabel_->setText(error); });
    connect(&bluetooth_, &BluetoothTransport::connectionChanged, this, [this](const QString &, bool) { refreshRouteStatus(); });

    refreshConversations();
}

void MainWindow::applyTheme() {
    setStyleSheet(R"(
        QMainWindow, #chatPanel { background:#111318; color:#e9edf3; }
        #sidebar { background:#181b22; border-right:1px solid #2a2f39; }
        #brand { font-size:24px; font-weight:800; color:#ffffff; }
        #mutedLabel, #routeLabel, #securityLabel, #statusBarLabel { color:#9ca6b5; }
        #conversationTitle { font-size:18px; font-weight:700; color:#f7f9fc; }
        #chatHeader { background:#151820; border-bottom:1px solid #292e38; }
        #composerBox { background:#151820; border-top:1px solid #292e38; }
        #statusBarLabel { background:#101217; font-size:11px; }
        QLineEdit, QComboBox { background:#222630; color:#eef2f7; border:1px solid #323844; border-radius:9px; padding:9px 11px; selection-background-color:#5865f2; }
        #searchBox { background:#20242c; }
        #messageEdit { font-size:14px; padding:11px 13px; }
        QListWidget { background:transparent; color:#dfe4eb; border:none; outline:none; }
        QListWidget::item { padding:12px 10px; border-radius:9px; }
        QListWidget::item:hover { background:#222731; }
        QListWidget::item:selected { background:#2b3240; color:#ffffff; }
        QTextBrowser#messages { background:#111318; color:#e9edf3; border:none; padding:14px; }
        QPushButton { border:none; border-radius:9px; padding:9px 13px; font-weight:600; }
        #primaryButton, #sendButton { background:#5865f2; color:white; }
        #primaryButton:hover, #sendButton:hover { background:#6874f5; }
        #secondaryButton, #headerActionButton { background:#252a34; color:#e7ebf1; }
        #secondaryButton:hover, #headerActionButton:hover { background:#303641; }
        #headerActionButton { padding:7px 10px; font-size:12px; }
        #stealthRoot { background:#111318; border:1px solid #303641; }
        #stealthTitle { color:#cdd4df; font-size:12px; font-weight:700; }
        #stealthMessages { background:#151820; color:#e9edf3; border:1px solid #292e38; border-radius:8px; padding:6px; }
        #stealthExpand { background:#252a34; color:#e7ebf1; min-width:34px; max-width:34px; padding:8px 0; }
        #stealthExpand:hover { background:#303641; }
        #policyBox { min-width:170px; }

        QDialog { background:#151820; color:#e9edf3; }
        QDialog QLabel { background:transparent; color:#e9edf3; }
        QDialog QLineEdit, QDialog QComboBox, QDialog QListWidget {
            background:#222630;
            color:#eef2f7;
            border:1px solid #323844;
            border-radius:9px;
            padding:9px 11px;
        }
        QDialog QListWidget::item { color:#e9edf3; padding:10px; border-radius:8px; }
        QDialog QListWidget::item:hover { background:#252b36; }
        QDialog QListWidget::item:selected { background:#2f3850; color:#ffffff; }
        QDialog QPushButton {
            background:#252a34;
            color:#f1f4f8;
            border:1px solid #343a46;
            border-radius:9px;
            padding:9px 14px;
        }
        QDialog QPushButton:hover { background:#303641; }
        QDialog QPushButton:default {
            background:#5865f2;
            color:#ffffff;
            border-color:#5865f2;
        }
        QDialogButtonBox { background:transparent; }
    )");
}

TransportPolicy MainWindow::selectedPolicy() const {
    return static_cast<TransportPolicy>(policyBox_->currentData().toInt());
}

void MainWindow::refreshConversations() {
    const QString selected=currentConversationId_; const QString filter=searchEdit_?searchEdit_->text().trimmed():QString();
    conversationList_->clear();
    auto list=conversations_.conversations();
    std::sort(list.begin(), list.end(), [](const Conversation &a,const Conversation &b){ return a.name.toLower()<b.name.toLower(); });
    for (const auto &c:list) {
        QString title=c.name; if (title.isEmpty()) title=c.id.left(8);
        if (!filter.isEmpty() && !title.contains(filter,Qt::CaseInsensitive)) continue;
        const QString prefix=c.type==ConversationType::Group?QStringLiteral("#  "):QStringLiteral("●  ");
        auto *item=new QListWidgetItem(prefix+title,conversationList_); item->setData(Qt::UserRole,c.id);
        if (c.id==selected) conversationList_->setCurrentItem(item);
    }
}

void MainWindow::activateConversation(const QString &id) {
    if (id.isEmpty()) return;
    selectConversation(id);
}

void MainWindow::selectConversation(const QString &id) {
    currentConversationId_=id; refreshHeader(); refreshMessages(); refreshStealthMode(); refreshRouteStatus(); messageEdit_->setFocus();
}

void MainWindow::refreshHeader() {
    if (currentConversationId_.isEmpty()) {
        conversationTitle_->setText("Seleziona una conversazione");
        groupManageButton_->setVisible(false);
        securityLabel_->setText(QStringLiteral("E2EE v2 · Ed25519 + X25519 + XChaCha20-Poly1305"));
        return;
    }
    const auto c=conversations_.conversation(currentConversationId_);
    conversationTitle_->setText(c.type==ConversationType::Group?QStringLiteral("# %1").arg(c.name):c.name);
    groupManageButton_->setVisible(c.type==ConversationType::Group);
    securityLabel_->setText(QStringLiteral("E2EE v2 · Ed25519 + X25519 + XChaCha20-Poly1305"));
    if (c.type==ConversationType::Group) {
        securityLabel_->setText(QStringLiteral("E2EE v2 · fan-out cifrato · mesh P2P · %1 membri").arg(c.memberIds.size()));
        return;
    }
    for (const auto &id:c.memberIds) {
        if (id==identity_.userId || !peers_.hasPeer(id)) continue;
        const auto p=peers_.peer(id);
        securityLabel_->setText(QStringLiteral("E2EE v2 · fingerprint %1").arg(p.identityFingerprint.isEmpty()?QStringLiteral("non disponibile"):p.identityFingerprint));
        return;
    }
}

void MainWindow::refreshMessages() {
    if (currentConversationId_.isEmpty()) { messages_->setHtml(""); return; }
    QString html=QStringLiteral("<html><body style='font-family:sans-serif;background:#111318;color:#e9edf3;'>");
    for (const auto &m:conversations_.messages(currentConversationId_)) {
        const bool mine=m.senderId==identity_.userId;
        QString sender=mine?QStringLiteral("Tu"):m.senderId;
        if (peers_.hasPeer(m.senderId)) sender=peers_.peer(m.senderId).username;
        const QString time=QDateTime::fromMSecsSinceEpoch(m.timestampMs).toLocalTime().toString("HH:mm");
        const QString delivery=mine?conversations_.deliveryIndicator(m.id):QString();
        const QString meta=delivery.isEmpty()?time:QStringLiteral("%1 · %2").arg(time).arg(delivery);
        const QString align=mine?QStringLiteral("right"):QStringLiteral("left");
        const QString bg=mine?QStringLiteral("#3d4f9f"):QStringLiteral("#222731");
        const QString deleteAction = mine
            ? QStringLiteral(" · <a href=\"echat-delete:%1\" style=\"color:#d9ddff;text-decoration:none;\">Elimina</a>").arg(m.id.toHtmlEscaped())
            : QString();
        html += QStringLiteral("<div style='text-align:%1;margin:9px 4px;'>"
                               "<div style='display:inline-block;max-width:72%;background:%2;border-radius:12px;padding:9px 12px;text-align:left;'>"
                               "<div style='font-size:11px;color:#aeb7c5;margin-bottom:4px;'><b>%3</b> · %4%6</div>"
                               "<div style='font-size:14px;'>%5</div></div></div>")
                    .arg(align).arg(bg).arg(sender.toHtmlEscaped()).arg(meta.toHtmlEscaped())
                    .arg(m.text.toHtmlEscaped().replace("\n","<br>")).arg(deleteAction);
    }
    html += "</body></html>"; messages_->setHtml(html); messages_->verticalScrollBar()->setValue(messages_->verticalScrollBar()->maximum());
}

void MainWindow::refreshRouteStatus() {
    if (currentConversationId_.isEmpty()) { routeLabel_->setText("Nessuna route"); return; }
    const auto c=conversations_.conversation(currentConversationId_);
    int recipients=0,bt=0,lan=0,net=0,mesh=0;
    const auto policy=selectedPolicy();
    for (const auto &id:c.memberIds) {
        if (id==identity_.userId || !peers_.hasPeer(id)) continue; ++recipients;
        const auto r=transports_.routesFor(peers_.peer(id)); bt+=r.bluetooth; lan+=r.lan; net+=r.internet;
        if (!r.bluetooth && !r.lan && !r.internet && conversations_.relayPossible(id,policy)) ++mesh;
    }
    routeLabel_->setText(QStringLiteral("Bluetooth %1/%5   ·   LAN %2/%5   ·   Internet %3/%5   ·   Relay P2P %4/%5   ·   policy: %6   ·   %7")
                         .arg(bt).arg(lan).arg(net).arg(mesh).arg(recipients).arg(policyBox_->currentText()).arg(conversations_.reliabilitySummary()));
}

void MainWindow::openStealthMode() {
    if (currentConversationId_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("EChat"),
                                 QStringLiteral("Seleziona prima una conversazione da usare in modalita' Stealth."));
        return;
    }

    if (!stealthDialog_) {
        stealthDialog_ = new QDialog(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
        stealthDialog_->setWindowTitle(QStringLiteral("EChat Stealth"));
        stealthDialog_->setObjectName(QStringLiteral("stealthRoot"));
        stealthDialog_->setModal(false);
        stealthDialog_->resize(520, 190);
        stealthDialog_->setMinimumSize(380, 145);
        stealthDialog_->setStyleSheet(styleSheet());

        auto *layout = new QVBoxLayout(stealthDialog_);
        layout->setContentsMargins(10, 9, 10, 10);
        layout->setSpacing(7);

        auto *top = new QHBoxLayout;
        stealthTitle_ = new QLabel;
        stealthTitle_->setObjectName(QStringLiteral("stealthTitle"));
        auto *expandButton = new QPushButton(QStringLiteral("↗"));
        expandButton->setObjectName(QStringLiteral("stealthExpand"));
        expandButton->setToolTip(QStringLiteral("Torna alla finestra completa"));
        top->addWidget(stealthTitle_, 1);
        top->addWidget(expandButton);
        layout->addLayout(top);

        stealthMessages_ = new QTextBrowser;
        stealthMessages_->setObjectName(QStringLiteral("stealthMessages"));
        stealthMessages_->setOpenLinks(false);
        stealthMessages_->setOpenExternalLinks(false);
        stealthMessages_->setMinimumHeight(68);
        stealthMessages_->setMaximumHeight(88);
        layout->addWidget(stealthMessages_, 1);

        auto *composer = new QHBoxLayout;
        stealthMessageEdit_ = new QLineEdit;
        stealthMessageEdit_->setPlaceholderText(QStringLiteral("Scrivi..."));
        stealthMessageEdit_->setObjectName(QStringLiteral("messageEdit"));
        auto *sendButton = new QPushButton(QStringLiteral("Invia"));
        sendButton->setObjectName(QStringLiteral("sendButton"));
        composer->addWidget(stealthMessageEdit_, 1);
        composer->addWidget(sendButton);
        layout->addLayout(composer);

        auto sendCompact = [this] {
            if (currentConversationId_.isEmpty() || !stealthMessageEdit_) return;
            const QString text = stealthMessageEdit_->text().trimmed();
            if (text.isEmpty()) return;
            conversations_.sendMessage(currentConversationId_, text, selectedPolicy());
            stealthMessageEdit_->clear();
            refreshMessages();
            refreshStealthMode();
        };
        connect(sendButton, &QPushButton::clicked, stealthDialog_, sendCompact);
        connect(stealthMessageEdit_, &QLineEdit::returnPressed, stealthDialog_, sendCompact);
        connect(expandButton, &QPushButton::clicked, stealthDialog_, [this] { stealthDialog_->reject(); });
        connect(stealthDialog_, &QDialog::rejected, this, [this] {
            showNormal();
            raise();
            activateWindow();
            if (messageEdit_) messageEdit_->setFocus();
        });

        auto *exitShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), stealthDialog_);
        connect(exitShortcut, &QShortcut::activated, stealthDialog_, &QDialog::reject);
        auto *toggleShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")), stealthDialog_);
        connect(toggleShortcut, &QShortcut::activated, stealthDialog_, &QDialog::reject);
        connect(this, &QObject::destroyed, stealthDialog_, &QObject::deleteLater);
    }

    refreshStealthMode();
    hide();
    stealthDialog_->show();
    stealthDialog_->raise();
    stealthDialog_->activateWindow();
    if (stealthMessageEdit_) stealthMessageEdit_->setFocus();
}

void MainWindow::refreshStealthMode() {
    if (!stealthDialog_ || !stealthMessages_ || !stealthTitle_ || currentConversationId_.isEmpty()) return;
    const Conversation c = conversations_.conversation(currentConversationId_);
    const QString name = c.type == ConversationType::Group ? QStringLiteral("# %1").arg(c.name) : c.name;
    stealthTitle_->setText(QStringLiteral("EChat · %1").arg(name.isEmpty() ? QStringLiteral("chat") : name));

    const auto all = conversations_.messages(currentConversationId_);
    QString html = QStringLiteral("<html><body style='font-family:sans-serif;background:#151820;color:#e9edf3;margin:2px;'>");
    const int first = qMax(0, all.size() - 3);
    for (int i = first; i < all.size(); ++i) {
        const auto &m = all.at(i);
        const bool mine = m.senderId == identity_.userId;
        QString sender = mine ? QStringLiteral("Tu") : m.senderId;
        if (peers_.hasPeer(m.senderId)) sender = peers_.peer(m.senderId).username;
        QString body = m.text.simplified();
        if (body.size() > 150) body = body.left(147) + QStringLiteral("...");
        const QString time = QDateTime::fromMSecsSinceEpoch(m.timestampMs).toLocalTime().toString(QStringLiteral("HH:mm"));
        html += QStringLiteral("<div style='margin:2px 1px;white-space:nowrap;overflow:hidden;'>"
                               "<span style='color:#9ca6b5;font-size:10px;'>%1 · %2</span> "
                               "<span style='font-size:12px;'><b>%3</b> %4</span></div>")
                    .arg(time.toHtmlEscaped(), sender.toHtmlEscaped(), mine ? QStringLiteral("→") : QStringLiteral("←"), body.toHtmlEscaped());
    }
    if (all.isEmpty()) html += QStringLiteral("<div style='color:#9ca6b5;font-size:12px;'>Nessun messaggio ancora.</div>");
    html += QStringLiteral("</body></html>");
    stealthMessages_->setHtml(html);
    stealthMessages_->verticalScrollBar()->setValue(stealthMessages_->verticalScrollBar()->maximum());
}

void MainWindow::openBluetoothScanner() {
    auto *dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("EChat — Bluetooth");
    auto *layout=new QVBoxLayout(dialog); auto *info=new QLabel("Bluetooth Classic/RFCOMM. Per questa build è consigliato associare prima i PC dal sistema operativo.");
    info->setWordWrap(true); auto *list=new QListWidget; auto *connectButton=new QPushButton("Connetti"); connectButton->setObjectName("primaryButton");
    layout->addWidget(info); layout->addWidget(list,1); layout->addWidget(connectButton); dialog->resize(560,440);
    connect(&bluetooth_,&BluetoothTransport::deviceFound,dialog,[list](const QString &name,const QString &address){
        for(int i=0;i<list->count();++i) if(list->item(i)->data(Qt::UserRole).toString()==address) return;
        auto *item=new QListWidgetItem(QStringLiteral("%1  [%2]").arg(name.isEmpty()?QStringLiteral("Senza nome"):name).arg(address),list); item->setData(Qt::UserRole,address);
    });
    connect(connectButton,&QPushButton::clicked,dialog,[this,list]{ if(auto *item=list->currentItem()) bluetooth_.connectToDevice(item->data(Qt::UserRole).toString()); });
    connect(list,&QListWidget::itemDoubleClicked,dialog,[this](QListWidgetItem *item){ bluetooth_.connectToDevice(item->data(Qt::UserRole).toString()); });
    dialog->show(); bluetooth_.scan();
}

void MainWindow::createDirectChat() {
    QDialog dialog(this); dialog.setWindowTitle("Nuova chat EChat"); QVBoxLayout layout(&dialog); QListWidget peers;
    for (const auto &p:peers_.peers()) { auto *item=new QListWidgetItem(QStringLiteral("%1\n%2").arg(p.username).arg(p.identityFingerprint),&peers); item->setData(Qt::UserRole,p.userId); }
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout.addWidget(&peers); layout.addWidget(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted || !peers.currentItem()) return;
    const QString id=conversations_.ensureDirectConversation(peers.currentItem()->data(Qt::UserRole).toString()); refreshConversations(); selectConversation(id);
}

void MainWindow::createGroup() {
    QDialog dialog(this); dialog.setWindowTitle("Nuovo gruppo EChat"); QVBoxLayout layout(&dialog); QLineEdit name; name.setPlaceholderText("Nome gruppo");
    QListWidget members; members.setSelectionMode(QAbstractItemView::MultiSelection);
    for(const auto &p:peers_.peers()){ auto *item=new QListWidgetItem(p.username.isEmpty()?p.userId:p.username,&members); item->setData(Qt::UserRole,p.userId); }
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout.addWidget(&name); layout.addWidget(new QLabel("Membri")); layout.addWidget(&members); layout.addWidget(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted) return; QStringList ids; for(auto *item:members.selectedItems()) ids<<item->data(Qt::UserRole).toString();
    if(ids.isEmpty()){ QMessageBox::information(this,"EChat","Seleziona almeno un altro membro."); return; }
    const QString id=conversations_.createGroup(name.text(),ids); refreshConversations(); selectConversation(id);
}

void MainWindow::manageCurrentGroup() {
    if (currentConversationId_.isEmpty()) return;
    const Conversation c = conversations_.conversation(currentConversationId_);
    if (c.type != ConversationType::Group) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Gestisci gruppo — %1").arg(c.name));
    dialog.resize(560, 620);
    auto *layout = new QVBoxLayout(&dialog);

    auto *nameLabel = new QLabel(QStringLiteral("Nome gruppo"));
    auto *nameEdit = new QLineEdit(c.name);
    nameEdit->setMaxLength(80);
    layout->addWidget(nameLabel);
    layout->addWidget(nameEdit);

    layout->addWidget(new QLabel(QStringLiteral("Membri attuali")));
    auto *currentMembers = new QListWidget;
    currentMembers->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto &id : c.memberIds) {
        QString label;
        if (id == identity_.userId) label = QStringLiteral("Tu (@%1)").arg(identity_.username);
        else if (peers_.hasPeer(id)) {
            const auto peer = peers_.peer(id);
            label = peer.username.isEmpty() ? peer.userId : QStringLiteral("@%1").arg(peer.username);
        } else label = id;
        auto *item = new QListWidgetItem(label, currentMembers);
        item->setToolTip(id);
    }
    layout->addWidget(currentMembers, 1);

    layout->addWidget(new QLabel(QStringLiteral("Aggiungi persone")));
    auto *available = new QListWidget;
    available->setSelectionMode(QAbstractItemView::MultiSelection);
    int availableCount = 0;
    for (const auto &peer : peers_.peers()) {
        if (c.memberIds.contains(peer.userId)) continue;
        auto *item = new QListWidgetItem(peer.username.isEmpty() ? peer.userId : QStringLiteral("@%1").arg(peer.username), available);
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
    if (conversations_.updateGroup(c.id, nameEdit->text(), added, selectedPolicy())) {
        statusLabel_->setText(QStringLiteral("Gruppo aggiornato"));
        refreshHeader(); refreshConversations(); refreshRouteStatus();
    } else {
        statusLabel_->setText(QStringLiteral("Nessuna modifica al gruppo"));
    }
}

void MainWindow::openAccountSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Account EChat"));
    dialog.resize(560, 300);
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

    auto *info = new QLabel(QStringLiteral("Cambiare username non cambia User ID, chiavi crittografiche o fingerprint."));
    info->setWordWrap(true);
    info->setObjectName("mutedLabel");
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
    if (next == identity_.username) return;
    if (!conversations_.changeUsername(next)) {
        QMessageBox::warning(this, QStringLiteral("EChat"), QStringLiteral("Username non valido o non modificato."));
    }
}

void MainWindow::deleteMessageFromLink(const QString &messageId) {
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
