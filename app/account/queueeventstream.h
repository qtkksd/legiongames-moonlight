#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Small SSE client for the queue notification stream. The existing queue SSE
// service authenticates a cookie token, so the app presents its short-lived
// access token as __Host-lgsession for this connection only.
class QueueEventStream : public QObject
{
    Q_OBJECT

public:
    explicit QueueEventStream(QObject *parent = nullptr);

    void start(const QByteArray &accessToken);
    void stop();

signals:
    void event(const QString &name, const QJsonObject &data);
    void failed(const QString &code, const QString &message);

private:
    void openConnection();
    void readAvailable();
    void handleFinished();
    void scheduleReconnect();
    void cleanupReply();

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_reconnectTimer;
    QByteArray m_token;
    QByteArray m_buffer;
    int m_backoffMs = 1000;
    bool m_stopping = false;
};
