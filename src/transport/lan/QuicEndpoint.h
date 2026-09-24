#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

namespace ec {

class QuicEndpoint : public QObject {
    Q_OBJECT
public:
    explicit QuicEndpoint(QObject *parent = nullptr);
    ~QuicEndpoint() override;

    bool compiledIn() const;
    bool start(quint16 port);
    void stop();

    void connectToHost(const QString &host, quint16 port);
    bool connected(const QString &host) const;
    bool send(const QString &host, const QByteArray &bytes);
    int rttMs(const QString &host) const;
    QString lastError() const;

signals:
    void bytesReceived(const QString &host, const QByteArray &bytes);
    void connectionChanged(const QString &host, bool connected);
    void statusMessage(const QString &message);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ec
