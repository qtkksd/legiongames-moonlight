#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Small SSE client for the queue notification stream. The app authenticates
// with its short-lived access token in the Authorization header; tokens never
// appear in URLs or cookies. Events are advisory: the shell refreshes the
// authoritative queue/session/wallet state after each hint.
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
    QByteArray m_lastEventId;
    int m_backoffMs = 1000;
    bool m_stopping = false;
};
