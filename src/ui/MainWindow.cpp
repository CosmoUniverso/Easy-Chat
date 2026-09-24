#include "ui/MainWindow.h"
#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "crypto/CryptoEngine.h"
#include "transport/TransportManager.h"
#include "transport/bluetooth/BluetoothTransport.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QScrollBar>
#include <algorithm>
#include <QVBoxLayout>

namespace ec {

MainWindow::MainWindow(const LocalIdentity &identity, CryptoEngine &crypto, PeerManager &peers,
                       ConversationManager &conversations, TransportManager &transports,
                       BluetoothTransport &bluetooth, QWidget *parent)
    : QMainWindow(parent), identity_(identity), crypto_(crypto), peers_(peers), conversations_(conversations),
      transports_(transports), bluetooth_(bluetooth) {
    setWindowTitle(QStringLiteral("EChat — Easy Chat"));
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
    auto *user = new QLabel(QStringLiteral("@%1").arg(identity_.username)); user->setObjectName("mutedLabel");
    brandRow->addWidget(brand); brandRow->addStretch(); brandRow->addWidget(user);
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
    titleRow->addWidget(conversationTitle_); titleRow->addStretch(); titleRow->addWidget(policyBox_);
    routeLabel_ = new QLabel(QStringLiteral("Nessuna route")); routeLabel_->setObjectName("routeLabel");
    securityLabel_ = new QLabel(QStringLiteral("E2EE v2 · Ed25519 + X25519 + XChaCha20-Poly1305"));
    securityLabel_->setObjectName("securityLabel");
    headerLayout->addLayout(titleRow); headerLayout->addWidget(routeLabel_); headerLayout->addWidget(securityLabel_);
    right->addWidget(chatHeader);

    messages_ = new QTextBrowser;
    messages_->setObjectName("messages");
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
    connect(groupButton, &QPushButton::clicked, this, &MainWindow::createGroup);
    connect(newChatButton, &QPushButton::clicked, this, &MainWindow::createDirectChat);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this] { refreshConversations(); });
    connect(policyBox_, &QComboBox::currentIndexChanged, this, [this] { refreshRouteStatus(); });
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
    connect(&conversations_, &ConversationManager::conversationUpdated, this, [this](const Conversation &) { refreshConversations(); });
    connect(&conversations_, &ConversationManager::messageAdded, this, [this](const Message &m) {
        refreshConversations(); if (m.conversationId == currentConversationId_) refreshMessages();
    });
    connect(&peers_, &PeerManager::peerUpdated, this, [this](const Peer &) { refreshConversations(); refreshRouteStatus(); refreshHeader(); });
    connect(&peers_, &PeerManager::securityWarning, this, [this](const QString &warning) {
        statusLabel_->setText(warning); QMessageBox::warning(this, "EChat — sicurezza", warning);
    });
    connect(&transports_, &TransportManager::statusMessage, statusLabel_, &QLabel::setText);
    connect(&conversations_, &ConversationManager::deliveryInfo, this, [this](const QString &, const QString &info) { statusLabel_->setText(info); });
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
        #secondaryButton { background:#252a34; color:#e7ebf1; }
        #secondaryButton:hover { background:#303641; }
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

void MainWindow::selectConversation(const QString &id) {
    currentConversationId_=id; refreshHeader(); refreshMessages(); refreshRouteStatus(); messageEdit_->setFocus();
}

void MainWindow::refreshHeader() {
    if (currentConversationId_.isEmpty()) { conversationTitle_->setText("Seleziona una conversazione"); return; }
    const auto c=conversations_.conversation(currentConversationId_);
    conversationTitle_->setText(c.type==ConversationType::Group?QStringLiteral("# %1").arg(c.name):c.name);
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
        const QString align=mine?QStringLiteral("right"):QStringLiteral("left");
        const QString bg=mine?QStringLiteral("#3d4f9f"):QStringLiteral("#222731");
        html += QStringLiteral("<div style='text-align:%1;margin:9px 4px;'>"
                               "<div style='display:inline-block;max-width:72%;background:%2;border-radius:12px;padding:9px 12px;text-align:left;'>"
                               "<div style='font-size:11px;color:#aeb7c5;margin-bottom:4px;'><b>%3</b> · %4</div>"
                               "<div style='font-size:14px;'>%5</div></div></div>")
                    .arg(align).arg(bg).arg(sender.toHtmlEscaped()).arg(time).arg(m.text.toHtmlEscaped().replace("\n","<br>"));
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
    routeLabel_->setText(QStringLiteral("Bluetooth %1/%5   ·   LAN %2/%5   ·   Internet %3/%5   ·   Relay P2P %4/%5   ·   policy: %6")
                         .arg(bt).arg(lan).arg(net).arg(mesh).arg(recipients).arg(policyBox_->currentText()));
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

} // namespace ec
