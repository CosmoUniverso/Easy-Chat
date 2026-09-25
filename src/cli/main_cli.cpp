#include "cli/CliController.h"
#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "crypto/CryptoEngine.h"
#include "storage/Database.h"
#include "transport/TransportManager.h"
#include "transport/bluetooth/BluetoothTransport.h"
#include "transport/internet/RelayTransport.h"
#include "transport/lan/LanTransport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStandardPaths>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EasyChat");
    QCoreApplication::setApplicationName("EC");
    const QString legacyDataDir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QCoreApplication::setApplicationName("EChat");
    QCoreApplication::setApplicationVersion("0.6.0-experimental");

    try {
        const QString dataDir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataDir);
        const QString databasePath=dataDir+"/echat.sqlite3";
        const QString legacyDatabasePath=legacyDataDir+"/ec.sqlite3";
        if(!QFile::exists(databasePath) && QFile::exists(legacyDatabasePath)) QFile::copy(legacyDatabasePath,databasePath);
        ec::Database db(databasePath); db.migrate();
        ec::CryptoEngine crypto; ec::LocalIdentity identity;
        if(db.hasIdentity()){
            identity=db.loadIdentity();
            if(!crypto.identityValid(identity)){ identity=crypto.upgradeIdentity(identity); db.saveIdentity(identity); }
        } else {
            std::cout << "Username locale EChat: " << std::flush;
            std::string name; std::getline(std::cin,name); if(name.empty()) return 0;
            identity=crypto.createIdentity(QString::fromStdString(name)); db.saveIdentity(identity);
        }

        ec::PeerManager peers(db); ec::TransportManager transports; ec::BluetoothTransport bluetooth; ec::LanTransport lan; ec::RelayTransport relay;
        transports.addTransport(&bluetooth); transports.addTransport(&lan); transports.addTransport(&relay);
        transports.setIdentity(identity);
        ec::ConversationManager conversations(db,crypto,peers,transports,identity);
        ec::CliController cli(identity,crypto,peers,conversations,transports,bluetooth);

        std::cout << "EChat CLI 0.6.0 - SPERIMENTALE\nDigita 'help'.\n> " << std::flush;
        transports.startAll();

        std::thread input([&cli] {
            std::string line;
            while(std::getline(std::cin,line)){
                const QString qline=QString::fromStdString(line);
                QMetaObject::invokeMethod(&cli,[&cli,qline]{ cli.execute(qline); },Qt::QueuedConnection);
                std::cout << "> " << std::flush;
            }
            QMetaObject::invokeMethod(QCoreApplication::instance(),"quit",Qt::QueuedConnection);
        });
        input.detach();
        const int rc=app.exec(); transports.stopAll(); return rc;
    } catch(const std::exception &e){
        std::cerr << "EChat CLI fatal: " << e.what() << std::endl; return 1;
    }
}
