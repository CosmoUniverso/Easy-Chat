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
#include <QColor>
#include <QDir>
#include <QPalette>
#include <QStyleFactory>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardPaths>


namespace {

void applyGlobalDarkTheme(QApplication &app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#111318"));
    palette.setColor(QPalette::WindowText, QColor("#e9edf3"));
    palette.setColor(QPalette::Base, QColor("#181b22"));
    palette.setColor(QPalette::AlternateBase, QColor("#20242c"));
    palette.setColor(QPalette::ToolTipBase, QColor("#181b22"));
    palette.setColor(QPalette::ToolTipText, QColor("#f7f9fc"));
    palette.setColor(QPalette::Text, QColor("#e9edf3"));
    palette.setColor(QPalette::Button, QColor("#252a34"));
    palette.setColor(QPalette::ButtonText, QColor("#e7ebf1"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Link, QColor("#6874f5"));
    palette.setColor(QPalette::Highlight, QColor("#5865f2"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor("#8e98a8"));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#6f7886"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#6f7886"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6f7886"));
    app.setPalette(palette);

    app.setStyleSheet(R"(
        QDialog, QMessageBox, QInputDialog {
            background:#151820;
            color:#e9edf3;
        }
        QDialog QLabel, QMessageBox QLabel, QInputDialog QLabel {
            background:transparent;
            color:#e9edf3;
        }
        QDialog QLineEdit, QDialog QComboBox,
        QDialog QListWidget, QDialog QTextEdit, QDialog QPlainTextEdit,
        QInputDialog QLineEdit {
            background:#222630;
            color:#eef2f7;
            border:1px solid #323844;
            border-radius:9px;
            padding:9px 11px;
            selection-background-color:#5865f2;
            selection-color:#ffffff;
        }
        QDialog QListWidget::item {
            color:#e9edf3;
            padding:10px;
            border-radius:8px;
        }
        QDialog QListWidget::item:hover {
            background:#252b36;
        }
        QDialog QListWidget::item:selected {
            background:#2f3850;
            color:#ffffff;
        }
        QDialog QPushButton, QMessageBox QPushButton, QInputDialog QPushButton {
            background:#252a34;
            color:#f1f4f8;
            border:1px solid #343a46;
            border-radius:9px;
            padding:9px 14px;
            min-width:84px;
            font-weight:600;
        }
        QDialog QPushButton:hover, QMessageBox QPushButton:hover, QInputDialog QPushButton:hover {
            background:#303641;
        }
        QDialog QPushButton:default, QMessageBox QPushButton:default, QInputDialog QPushButton:default {
            background:#5865f2;
            color:#ffffff;
            border-color:#5865f2;
        }
        QDialog QPushButton:default:hover, QMessageBox QPushButton:default:hover, QInputDialog QPushButton:default:hover {
            background:#6874f5;
        }
        QDialogButtonBox {
            background:transparent;
        }
        QToolTip {
            background:#181b22;
            color:#f7f9fc;
            border:1px solid #323844;
            padding:5px;
        }
    )");
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    applyGlobalDarkTheme(app);
    QApplication::setOrganizationName("EasyChat");
    QApplication::setApplicationName("EC");
    const QString legacyDataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QApplication::setApplicationName("EChat");
    QApplication::setApplicationVersion("0.3.1");

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
