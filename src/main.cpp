#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "crypto/CryptoEngine.h"
#include "storage/Database.h"
#include "transport/TransportManager.h"
#include "transport/bluetooth/BluetoothTransport.h"
#include "transport/internet/RelayTransport.h"
#include "transport/lan/LanTransport.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("EasyChat");
    QApplication::setApplicationName("EC");
    const QString legacyDataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QApplication::setApplicationName("EChat");
    QApplication::setApplicationVersion("0.3.0");

    try {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataDir);
        const QString databasePath = dataDir + "/echat.sqlite3";
        const QString legacyDatabasePath = legacyDataDir + "/ec.sqlite3";
        if (!QFile::exists(databasePath) && QFile::exists(legacyDatabasePath))
            QFile::copy(legacyDatabasePath, databasePath);

        ec::Database db(databasePath);
        db.migrate();
        ec::CryptoEngine crypto;

        ec::LocalIdentity identity;
        if (db.hasIdentity()) {
            identity = db.loadIdentity();
            if (!crypto.identityValid(identity)) {
                identity = crypto.upgradeIdentity(identity);
                db.saveIdentity(identity);
            }
        } else {
            bool ok = false;
            const QString username = QInputDialog::getText(nullptr, "EChat", "Scegli username locale:",
                                                           QLineEdit::Normal, {}, &ok).trimmed();
            if (!ok || username.isEmpty()) return 0;
            identity = crypto.createIdentity(username);
            db.saveIdentity(identity);
        }

        ec::PeerManager peers(db);
        ec::TransportManager transports;
        ec::BluetoothTransport bluetooth;
        ec::LanTransport lan;
        ec::RelayTransport relay;

        bluetooth.setIdentity(identity);
        lan.setIdentity(identity);
        transports.addTransport(&bluetooth);
        transports.addTransport(&lan);
        transports.addTransport(&relay);

        ec::ConversationManager conversations(db, crypto, peers, transports, identity);
        ec::MainWindow window(identity, crypto, peers, conversations, transports, bluetooth);
        window.show();

        transports.startAll();
        const int rc = app.exec();
        transports.stopAll();
        return rc;
    } catch (const std::exception &e) {
        QMessageBox::critical(nullptr, "EChat", QStringLiteral("Errore fatale: %1").arg(QString::fromUtf8(e.what())));
        return 1;
    }
}
