#include "cli/CliController.h"
#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "crypto/CryptoEngine.h"
#include "transport/TransportManager.h"
#include "transport/bluetooth/BluetoothTransport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QRegularExpression>
#include <iostream>

namespace ec {

CliController::CliController(const LocalIdentity &identity, CryptoEngine &crypto, PeerManager &peers,
                             ConversationManager &conversations, TransportManager &transports,
                             BluetoothTransport &bluetooth, QObject *parent)
    : QObject(parent), identity_(identity), crypto_(crypto), peers_(peers), conversations_(conversations),
      transports_(transports), bluetooth_(bluetooth) {
    connect(&bluetooth_, &BluetoothTransport::deviceFound, this, [this](const QString &name, const QString &addr) {
        print(QStringLiteral("[bt] %1  %2").arg(name.isEmpty()?QStringLiteral("Senza nome"):name).arg(addr));
    });
    connect(&bluetooth_, &BluetoothTransport::scanFinished, this, [this] { print("[bt] scansione terminata"); });
    connect(&transports_, &TransportManager::statusMessage, this, [this](const QString &m) { print(QStringLiteral("[net] %1").arg(m)); });
    connect(&peers_, &PeerManager::securityWarning, this, [this](const QString &m) { print(QStringLiteral("[SECURITY] %1").arg(m)); });
    connect(&conversations_, &ConversationManager::protocolError, this, [this](const QString &m) { print(QStringLiteral("[crypto] %1").arg(m)); });
    connect(&conversations_, &ConversationManager::messageAdded, this, [this](const Message &m) {
        if (m.senderId == identity_.userId) return;
        QString sender = peers_.hasPeer(m.senderId) ? peers_.peer(m.senderId).username : m.senderId;
        print(QStringLiteral("\n[%1] %2: %3").arg(m.conversationId.left(8), sender, m.text));
    });
}

void CliController::print(const QString &text) const {
    std::cout << text.toStdString() << std::endl;
}

void CliController::printHelp() const {
    print("Comandi EChat CLI (SPERIMENTALE):\n"
          "  help                         mostra questo aiuto\n"
          "  whoami                       identita' locale e fingerprint\n"
          "  peers                        peer conosciuti\n"
          "  scan                         scansione Bluetooth\n"
          "  connect <BT-address>         connessione RFCOMM\n"
          "  chats                        conversazioni\n"
          "  chat <peer-id|username>      crea/seleziona chat diretta\n"
          "  use <id|indice>              seleziona conversazione\n"
          "  messages                     cronologia conversazione corrente\n"
          "  send <testo>                 invia messaggio\n"
          "  group <nome>|<peer1,peer2>   crea gruppo\n"
          "  route                        route disponibili nella chat corrente\n"
          "  policy <auto|bt|lan|net|prefer-bt|prefer-lan|prefer-net>\n"
          "  quit                         esci");
}

void CliController::listPeers() const {
    const auto ps = peers_.peers();
    if (ps.isEmpty()) { print("Nessun peer conosciuto."); return; }
    int i=1;
    for (const auto &p:ps) {
        const auto r = transports_.routesFor(p);
        print(QStringLiteral("%1) %2  id=%3  fp=%4  [BT:%5 LAN:%6 NET:%7]")
              .arg(i++).arg(p.username,p.userId,p.identityFingerprint)
              .arg(r.bluetooth?QStringLiteral("up"):QStringLiteral("-")).arg(r.lan?QStringLiteral("up"):QStringLiteral("-")).arg(r.internet?QStringLiteral("up"):QStringLiteral("-")));
    }
}

void CliController::listChats() const {
    const auto cs=conversations_.conversations();
    if(cs.isEmpty()){ print("Nessuna conversazione."); return; }
    int i=1;
    for(const auto &c:cs){
        print(QStringLiteral("%1) %2%3  id=%4%5")
              .arg(i++).arg(c.type==ConversationType::Group?QStringLiteral("# "):QString()).arg(c.name).arg(c.id)
              .arg(c.id==currentConversationId_?QStringLiteral("  *"):QString()));
    }
}

QString CliController::resolvePeer(const QString &token) const {
    const QString t=token.trimmed();
    for(const auto &p:peers_.peers()) if(p.userId==t || p.userId.startsWith(t) || p.username.compare(t,Qt::CaseInsensitive)==0) return p.userId;
    return {};
}

QString CliController::resolveConversation(const QString &token) const {
    bool numberOk=false; int n=token.toInt(&numberOk); const auto cs=conversations_.conversations();
    if(numberOk && n>=1 && n<=cs.size()) return cs.at(n-1).id;
    for(const auto &c:cs) if(c.id==token || c.id.startsWith(token)) return c.id;
    return {};
}

TransportPolicy CliController::parsePolicy(const QString &token, bool *ok) {
    *ok=true; const QString t=token.toLower();
    if(t=="auto") return TransportPolicy::Auto;
    if(t=="bt") return TransportPolicy::BluetoothOnly;
    if(t=="lan") return TransportPolicy::LanOnly;
    if(t=="net") return TransportPolicy::InternetOnly;
    if(t=="prefer-bt") return TransportPolicy::PreferBluetooth;
    if(t=="prefer-lan") return TransportPolicy::PreferLan;
    if(t=="prefer-net") return TransportPolicy::PreferInternet;
    *ok=false; return TransportPolicy::Auto;
}

void CliController::showMessages() const {
    if(currentConversationId_.isEmpty()){ print("Nessuna conversazione selezionata."); return; }
    for(const auto &m:conversations_.messages(currentConversationId_)){
        QString sender=m.senderId==identity_.userId?QStringLiteral("tu"):(peers_.hasPeer(m.senderId)?peers_.peer(m.senderId).username:m.senderId);
        const QString time=QDateTime::fromMSecsSinceEpoch(m.timestampMs).toLocalTime().toString("HH:mm:ss");
        print(QStringLiteral("[%1] %2: %3").arg(time,sender,m.text));
    }
}

void CliController::showRoute() const {
    if(currentConversationId_.isEmpty()){ print("Nessuna conversazione selezionata."); return; }
    const auto c=conversations_.conversation(currentConversationId_);
    for(const auto &id:c.memberIds){
        if(id==identity_.userId) continue;
        if(!peers_.hasPeer(id)){ print(QStringLiteral("%1: peer non conosciuto").arg(id)); continue; }
        const auto p=peers_.peer(id); const auto r=transports_.routesFor(p);
        print(QStringLiteral("%1 -> Bluetooth=%2 LAN=%3 Internet=%4")
              .arg(p.username).arg(r.bluetooth?QStringLiteral("YES"):QStringLiteral("no")).arg(r.lan?QStringLiteral("YES"):QStringLiteral("no")).arg(r.internet?QStringLiteral("YES"):QStringLiteral("no")));
    }
}

void CliController::execute(const QString &raw) {
    const QString line=raw.trimmed(); if(line.isEmpty()) return;
    const int sp=line.indexOf(' '); const QString cmd=(sp<0?line:line.left(sp)).toLower(); const QString arg=sp<0?QString():line.mid(sp+1).trimmed();
    if(cmd=="help" || cmd=="?"){ printHelp(); return; }
    if(cmd=="quit" || cmd=="exit"){ QCoreApplication::quit(); return; }
    if(cmd=="whoami"){
        print(QStringLiteral("%1  id=%2\nfingerprint=%3").arg(identity_.username,identity_.userId,crypto_.fingerprint(identity_.signingPublicKey,identity_.kxPublicKey))); return;
    }
    if(cmd=="peers"){ listPeers(); return; }
    if(cmd=="scan"){ bluetooth_.scan(); return; }
    if(cmd=="connect"){ if(arg.isEmpty()) print("Uso: connect <BT-address>"); else bluetooth_.connectToDevice(arg); return; }
    if(cmd=="chats"){ listChats(); return; }
    if(cmd=="chat"){
        const QString peer=resolvePeer(arg); if(peer.isEmpty()){ print("Peer non trovato."); return; }
        currentConversationId_=conversations_.ensureDirectConversation(peer); print(QStringLiteral("Chat selezionata: %1").arg(currentConversationId_)); return;
    }
    if(cmd=="use"){
        const QString id=resolveConversation(arg); if(id.isEmpty()){ print("Conversazione non trovata."); return; }
        currentConversationId_=id; print(QStringLiteral("Conversazione: %1").arg(conversations_.conversation(id).name)); return;
    }
    if(cmd=="messages"){ showMessages(); return; }
    if(cmd=="send"){
        if(currentConversationId_.isEmpty()){ print("Prima usa 'chat' o 'use'."); return; }
        if(arg.isEmpty()){ print("Uso: send <testo>"); return; }
        const bool sent=conversations_.sendMessage(currentConversationId_,arg,policy_); print(sent?QStringLiteral("Invio avviato."):QStringLiteral("Nessuna route disponibile: messaggio salvato/pending.")); return;
    }
    if(cmd=="group"){
        const int sep=arg.indexOf('|'); if(sep<1){ print("Uso: group <nome>|<peer1,peer2>"); return; }
        const QString name=arg.left(sep).trimmed(); QStringList ids;
        for(const auto &tok:arg.mid(sep+1).split(',',Qt::SkipEmptyParts)){ const QString id=resolvePeer(tok.trimmed()); if(!id.isEmpty()) ids<<id; }
        if(ids.isEmpty()){ print("Nessun peer valido."); return; }
        currentConversationId_=conversations_.createGroup(name,ids); print(QStringLiteral("Gruppo creato: %1").arg(currentConversationId_)); return;
    }
    if(cmd=="route"){ showRoute(); return; }
    if(cmd=="policy"){
        bool ok=false; const auto p=parsePolicy(arg,&ok); if(!ok){ print("Policy non valida."); return; }
        policy_=p; print(QStringLiteral("Policy impostata: %1").arg(arg)); return;
    }
    print("Comando sconosciuto. Usa 'help'.");
}

} // namespace ec
