#include "transport/lan/QuicEndpoint.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <cstring>

#ifndef ECHAT_HAVE_MSQUIC
#define ECHAT_HAVE_MSQUIC 0
#endif

#if ECHAT_HAVE_MSQUIC
#include <msquic.h>
#endif

namespace ec {

class QuicEndpoint::Impl {
public:
    explicit Impl(QuicEndpoint *owner) : q(owner) {}

    QuicEndpoint *q = nullptr;
    mutable QMutex mutex;
    QString error;

#if ECHAT_HAVE_MSQUIC
    struct ConnectionCtx {
        Impl *self = nullptr;
        HQUIC handle = nullptr;
        QString host;
        bool connected = false;
    };

    struct StreamCtx {
        Impl *self = nullptr;
        HQUIC handle = nullptr;
        QString host;
        QByteArray outbound;
        QByteArray inbound;
        QUIC_BUFFER buffer{};
        bool delivered = false;
    };

    const QUIC_API_TABLE *api = nullptr;
    HQUIC registration = nullptr;
    HQUIC serverConfiguration = nullptr;
    HQUIC clientConfiguration = nullptr;
    HQUIC listener = nullptr;
    quint16 listenPort = 0;
    QHash<QString, ConnectionCtx *> byHost;
    QSet<ConnectionCtx *> connections;
    QSet<QString> connectingHosts;

    static constexpr char AlpnText[] = "echat-v3";

    static QString addrString(const QUIC_ADDR *address) {
        if (!address) return {};
        QUIC_ADDR_STR text{};
        if (!QuicAddrIpToString(address, &text)) return {};
        return QString::fromLatin1(text.Address);
    }

    void setError(const QString &message) {
        QMutexLocker lock(&mutex);
        error = message;
    }

    bool loadWindowsServerCredential() {
#ifdef _WIN32
        const QString command = QStringLiteral(
            "$c=Get-ChildItem Cert:\\CurrentUser\\My | Where-Object {$_.FriendlyName -eq 'EChat QUIC'} | Select-Object -First 1;"
            "if(-not $c){$c=New-SelfSignedCertificate -DnsName localhost -CertStoreLocation 'Cert:\\CurrentUser\\My' -FriendlyName 'EChat QUIC' -KeyUsage DigitalSignature -KeyExportPolicy Exportable};"
            "$c.Thumbprint");
        QProcess process;
        process.start(QStringLiteral("powershell.exe"),
                      {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                       QStringLiteral("-Command"), command});
        if (!process.waitForStarted(5000) || !process.waitForFinished(15000) || process.exitCode() != 0) {
            setError(QStringLiteral("QUIC: impossibile creare/trovare il certificato Windows"));
            return false;
        }
        QByteArray hex = process.readAllStandardOutput().trimmed();
        hex.replace("\r", "");
        hex.replace("\n", "");
        const QByteArray hashBytes = QByteArray::fromHex(hex);
        QUIC_CERTIFICATE_HASH certHash{};
        if (hashBytes.size() != static_cast<int>(sizeof(certHash.ShaHash))) {
            setError(QStringLiteral("QUIC: thumbprint certificato Windows non valido"));
            return false;
        }
        memcpy(certHash.ShaHash, hashBytes.constData(), sizeof(certHash.ShaHash));

        QUIC_CREDENTIAL_CONFIG credential{};
        credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH;
        credential.Flags = QUIC_CREDENTIAL_FLAG_NONE;
        credential.CertificateHash = &certHash;
        const QUIC_STATUS status = api->ConfigurationLoadCredential(serverConfiguration, &credential);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC: caricamento certificato Schannel fallito (%1)").arg(status));
            return false;
        }
        return true;
#else
        return false;
#endif
    }

    bool loadPosixServerCredential() {
#ifndef _WIN32
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/quic");
        QDir().mkpath(dir);
        const QString certPath = dir + QStringLiteral("/echat-quic-cert.pem");
        const QString keyPath = dir + QStringLiteral("/echat-quic-key.pem");
        if (!QFileInfo::exists(certPath) || !QFileInfo::exists(keyPath)) {
            QProcess process;
            process.start(QStringLiteral("openssl"),
                          {QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
                           QStringLiteral("rsa:2048"), QStringLiteral("-sha256"), QStringLiteral("-nodes"),
                           QStringLiteral("-keyout"), keyPath, QStringLiteral("-out"), certPath,
                           QStringLiteral("-days"), QStringLiteral("3650"), QStringLiteral("-subj"),
                           QStringLiteral("/CN=localhost")});
            if (!process.waitForStarted(5000) || !process.waitForFinished(15000) || process.exitCode() != 0) {
                setError(QStringLiteral("QUIC: openssl non disponibile per creare il certificato locale"));
                return false;
            }
        }

        const QByteArray cert = QFile::encodeName(certPath);
        const QByteArray key = QFile::encodeName(keyPath);
        QUIC_CERTIFICATE_FILE files{};
        files.CertificateFile = cert.constData();
        files.PrivateKeyFile = key.constData();
        QUIC_CREDENTIAL_CONFIG credential{};
        credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
        credential.Flags = QUIC_CREDENTIAL_FLAG_NONE;
        credential.CertificateFile = &files;
        const QUIC_STATUS status = api->ConfigurationLoadCredential(serverConfiguration, &credential);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC: caricamento certificato OpenSSL fallito (%1)").arg(status));
            return false;
        }
        return true;
#else
        return false;
#endif
    }

    bool initialize(quint16 port) {
        if (api) return true;
        QUIC_STATUS status = MsQuicOpen2(&api);
        if (QUIC_FAILED(status) || !api) {
            setError(QStringLiteral("MsQuicOpen2 fallito (%1)").arg(status));
            api = nullptr;
            return false;
        }

        const QUIC_REGISTRATION_CONFIG registrationConfig = {
            "EChat", QUIC_EXECUTION_PROFILE_LOW_LATENCY
        };
        status = api->RegistrationOpen(&registrationConfig, &registration);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC RegistrationOpen fallito (%1)").arg(status));
            cleanup();
            return false;
        }

        QUIC_SETTINGS settings{};
        settings.IdleTimeoutMs = 30000;
        settings.IsSet.IdleTimeoutMs = TRUE;
        settings.PeerBidiStreamCount = 128;
        settings.IsSet.PeerBidiStreamCount = TRUE;
        settings.DatagramReceiveEnabled = TRUE;
        settings.IsSet.DatagramReceiveEnabled = TRUE;

        QUIC_BUFFER alpn{static_cast<uint32_t>(sizeof(AlpnText) - 1),
                         reinterpret_cast<uint8_t *>(const_cast<char *>(AlpnText))};
        status = api->ConfigurationOpen(registration, &alpn, 1, &settings, sizeof(settings),
                                        nullptr, &serverConfiguration);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC server ConfigurationOpen fallito (%1)").arg(status));
            cleanup();
            return false;
        }
        status = api->ConfigurationOpen(registration, &alpn, 1, &settings, sizeof(settings),
                                        nullptr, &clientConfiguration);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC client ConfigurationOpen fallito (%1)").arg(status));
            cleanup();
            return false;
        }

#ifdef _WIN32
        if (!loadWindowsServerCredential()) {
#else
        if (!loadPosixServerCredential()) {
#endif
            cleanup();
            return false;
        }

        QUIC_CREDENTIAL_CONFIG clientCredential{};
        clientCredential.Type = QUIC_CREDENTIAL_TYPE_NONE;
        clientCredential.Flags = static_cast<QUIC_CREDENTIAL_FLAGS>(
            QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION);
        status = api->ConfigurationLoadCredential(clientConfiguration, &clientCredential);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC client credential fallito (%1)").arg(status));
            cleanup();
            return false;
        }

        status = api->ListenerOpen(registration, listenerCallback, this, &listener);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC ListenerOpen fallito (%1)").arg(status));
            cleanup();
            return false;
        }

        QUIC_ADDR address{};
        QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
        QuicAddrSetPort(&address, port);
        status = api->ListenerStart(listener, &alpn, 1, &address);
        if (QUIC_FAILED(status)) {
            setError(QStringLiteral("QUIC ListenerStart UDP/%1 fallito (%2)").arg(port).arg(status));
            cleanup();
            return false;
        }
        listenPort = port;
        return true;
    }

    void cleanup() {
        if (!api) return;
        if (listener) {
            api->ListenerClose(listener);
            listener = nullptr;
        }

        QList<ConnectionCtx *> copy;
        {
            QMutexLocker lock(&mutex);
            copy = connections.values();
            byHost.clear();
            connectingHosts.clear();
            connections.clear();
        }
        for (auto *ctx : copy) {
            if (!ctx) continue;
            if (ctx->handle) api->ConnectionClose(ctx->handle);
            ctx->handle = nullptr;
            delete ctx;
        }

        if (serverConfiguration) {
            api->ConfigurationClose(serverConfiguration);
            serverConfiguration = nullptr;
        }
        if (clientConfiguration) {
            api->ConfigurationClose(clientConfiguration);
            clientConfiguration = nullptr;
        }
        if (registration) {
            api->RegistrationClose(registration);
            registration = nullptr;
        }
        MsQuicClose(api);
        api = nullptr;
        listenPort = 0;
    }

    void connectHost(const QString &host, quint16 port) {
        if (!api || !registration || !clientConfiguration || host.isEmpty()) return;
        {
            QMutexLocker lock(&mutex);
            if (byHost.contains(host) || connectingHosts.contains(host)) return;
            connectingHosts.insert(host);
        }

        auto *ctx = new ConnectionCtx;
        ctx->self = this;
        ctx->host = host;
        QUIC_STATUS status = api->ConnectionOpen(registration, connectionCallback, ctx, &ctx->handle);
        if (QUIC_FAILED(status)) {
            QMutexLocker lock(&mutex);
            connectingHosts.remove(host);
            delete ctx;
            return;
        }
        {
            QMutexLocker lock(&mutex);
            connections.insert(ctx);
        }
        const QByteArray hostBytes = host.toUtf8();
        status = api->ConnectionStart(ctx->handle, clientConfiguration,
                                      QUIC_ADDRESS_FAMILY_UNSPEC, hostBytes.constData(), port);
        if (QUIC_FAILED(status)) {
            {
                QMutexLocker lock(&mutex);
                connectingHosts.remove(host);
                connections.remove(ctx);
            }
            api->ConnectionClose(ctx->handle);
            delete ctx;
            emit q->statusMessage(QStringLiteral("QUIC connessione a %1 fallita (%2), uso fallback TCP")
                                  .arg(host).arg(status));
        }
    }

    bool isConnected(const QString &host) const {
        QMutexLocker lock(&mutex);
        auto *ctx = byHost.value(host, nullptr);
        return ctx && ctx->connected && ctx->handle;
    }

    bool sendTo(const QString &host, const QByteArray &bytes) {
        ConnectionCtx *connection = nullptr;
        {
            QMutexLocker lock(&mutex);
            connection = byHost.value(host, nullptr);
            if (!connection || !connection->connected || !connection->handle) return false;
        }

        auto *ctx = new StreamCtx;
        ctx->self = this;
        ctx->host = host;
        ctx->outbound = bytes;
        ctx->buffer.Length = static_cast<uint32_t>(ctx->outbound.size());
        ctx->buffer.Buffer = reinterpret_cast<uint8_t *>(ctx->outbound.data());

        QUIC_STATUS status = api->StreamOpen(connection->handle, QUIC_STREAM_OPEN_FLAG_NONE,
                                             streamCallback, ctx, &ctx->handle);
        if (QUIC_FAILED(status)) {
            delete ctx;
            return false;
        }
        status = api->StreamSend(ctx->handle, &ctx->buffer, 1,
                                 static_cast<QUIC_SEND_FLAGS>(QUIC_SEND_FLAG_START | QUIC_SEND_FLAG_FIN),
                                 nullptr);
        if (QUIC_FAILED(status)) {
            api->StreamClose(ctx->handle);
            delete ctx;
            return false;
        }
        return true;
    }

    int rtt(const QString &host) const {
        ConnectionCtx *connection = nullptr;
        {
            QMutexLocker lock(&mutex);
            connection = byHost.value(host, nullptr);
            if (!connection || !connection->connected || !connection->handle) return -1;
        }
        QUIC_STATISTICS stats{};
        uint32_t size = sizeof(stats);
        if (QUIC_FAILED(api->GetParam(connection->handle, QUIC_PARAM_CONN_STATISTICS, &size, &stats))) return -1;
        return static_cast<int>(stats.Rtt / 1000);
    }

    static QUIC_STATUS QUIC_API listenerCallback(HQUIC, void *context, QUIC_LISTENER_EVENT *event) {
        auto *self = static_cast<Impl *>(context);
        if (!self || event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION) return QUIC_STATUS_SUCCESS;

        auto *ctx = new ConnectionCtx;
        ctx->self = self;
        ctx->handle = event->NEW_CONNECTION.Connection;
        ctx->host = addrString(event->NEW_CONNECTION.Info ? event->NEW_CONNECTION.Info->RemoteAddress : nullptr);
        if (ctx->host.isEmpty()) ctx->host = QStringLiteral("unknown");
        {
            QMutexLocker lock(&self->mutex);
            self->connections.insert(ctx);
        }
        self->api->SetCallbackHandler(ctx->handle, reinterpret_cast<void *>(connectionCallback), ctx);
        const QUIC_STATUS status = self->api->ConnectionSetConfiguration(ctx->handle, self->serverConfiguration);
        if (QUIC_FAILED(status)) {
            {
                QMutexLocker lock(&self->mutex);
                self->connections.remove(ctx);
            }
            self->api->ConnectionClose(ctx->handle);
            ctx->handle = nullptr;
            delete ctx;
        }
        return status;
    }

    static QUIC_STATUS QUIC_API connectionCallback(HQUIC connection, void *context, QUIC_CONNECTION_EVENT *event) {
        auto *ctx = static_cast<ConnectionCtx *>(context);
        if (!ctx || !ctx->self) return QUIC_STATUS_SUCCESS;
        auto *self = ctx->self;

        switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            {
                QMutexLocker lock(&self->mutex);
                ctx->connected = true;
                self->connectingHosts.remove(ctx->host);
                self->byHost[ctx->host] = ctx;
            }
            emit self->q->connectionChanged(ctx->host, true);
            break;
        }
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            auto *stream = new StreamCtx;
            stream->self = self;
            stream->host = ctx->host;
            stream->handle = event->PEER_STREAM_STARTED.Stream;
            self->api->SetCallbackHandler(stream->handle, reinterpret_cast<void *>(streamCallback), stream);
            break;
        }
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            emit self->q->statusMessage(QStringLiteral("QUIC %1: trasporto chiuso (%2), fallback TCP disponibile")
                                        .arg(ctx->host).arg(event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status));
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
            bool noReplacement = false;
            {
                QMutexLocker lock(&self->mutex);
                self->connectingHosts.remove(ctx->host);
                if (self->byHost.value(ctx->host) == ctx) {
                    self->byHost.remove(ctx->host);
                    noReplacement = true;
                }
                self->connections.remove(ctx);
            }
            if (noReplacement) emit self->q->connectionChanged(ctx->host, false);
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
                self->api->ConnectionClose(connection);
                ctx->handle = nullptr;
                delete ctx;
            }
            break;
        }
        default:
            break;
        }
        return QUIC_STATUS_SUCCESS;
    }

    static QUIC_STATUS QUIC_API streamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *event) {
        auto *ctx = static_cast<StreamCtx *>(context);
        if (!ctx || !ctx->self) return QUIC_STATUS_SUCCESS;
        auto *self = ctx->self;

        switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE:
            for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const auto &buffer = event->RECEIVE.Buffers[i];
                if (ctx->inbound.size() + static_cast<int>(buffer.Length) > 16 * 1024 * 1024) {
                    self->api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
                    return QUIC_STATUS_SUCCESS;
                }
                ctx->inbound.append(reinterpret_cast<const char *>(buffer.Buffer), static_cast<int>(buffer.Length));
            }
            break;
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
            if (!ctx->delivered && !ctx->inbound.isEmpty()) {
                ctx->delivered = true;
                emit self->q->bytesReceived(ctx->host, ctx->inbound);
            }
            self->api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
            break;
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
            self->api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
            break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) self->api->StreamClose(stream);
            ctx->handle = nullptr;
            delete ctx;
            break;
        default:
            break;
        }
        return QUIC_STATUS_SUCCESS;
    }
#else
    void setError(const QString &message) {
        QMutexLocker lock(&mutex);
        error = message;
    }
#endif
};

QuicEndpoint::QuicEndpoint(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)) {}

QuicEndpoint::~QuicEndpoint() { stop(); }

bool QuicEndpoint::compiledIn() const {
#if ECHAT_HAVE_MSQUIC
    return true;
#else
    return false;
#endif
}

bool QuicEndpoint::start(quint16 port) {
#if ECHAT_HAVE_MSQUIC
    if (impl_->initialize(port)) {
        emit statusMessage(QStringLiteral("EChat QUIC attivo su UDP/%1").arg(port));
        return true;
    }
    emit statusMessage(QStringLiteral("QUIC non disponibile: %1").arg(lastError()));
    return false;
#else
    Q_UNUSED(port)
    impl_->setError(QStringLiteral("EChat compilato senza MsQuic"));
    return false;
#endif
}

void QuicEndpoint::stop() {
#if ECHAT_HAVE_MSQUIC
    impl_->cleanup();
#endif
}

void QuicEndpoint::connectToHost(const QString &host, quint16 port) {
#if ECHAT_HAVE_MSQUIC
    impl_->connectHost(host, port);
#else
    Q_UNUSED(host)
    Q_UNUSED(port)
#endif
}

bool QuicEndpoint::connected(const QString &host) const {
#if ECHAT_HAVE_MSQUIC
    return impl_->isConnected(host);
#else
    Q_UNUSED(host)
    return false;
#endif
}

bool QuicEndpoint::send(const QString &host, const QByteArray &bytes) {
#if ECHAT_HAVE_MSQUIC
    return impl_->sendTo(host, bytes);
#else
    Q_UNUSED(host)
    Q_UNUSED(bytes)
    return false;
#endif
}

int QuicEndpoint::rttMs(const QString &host) const {
#if ECHAT_HAVE_MSQUIC
    return impl_->rtt(host);
#else
    Q_UNUSED(host)
    return -1;
#endif
}

QString QuicEndpoint::lastError() const {
    QMutexLocker lock(&impl_->mutex);
    return impl_->error;
}

} // namespace ec
