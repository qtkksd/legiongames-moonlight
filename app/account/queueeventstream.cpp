#include "queueeventstream.h"

#include "accountclient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

QueueEventStream::QueueEventStream(QObject *parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this)),
      m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_stopping) {
            openConnection();
        }
    });
}

void QueueEventStream::start(const QByteArray &accessToken)
{
    stop();
    m_stopping = false;
    m_token = accessToken.trimmed();
    m_backoffMs = 1000;
    if (m_token.isEmpty()) {
        return;
    }
    openConnection();
}

void QueueEventStream::stop()
{
    m_stopping = true;
    m_reconnectTimer->stop();
    cleanupReply();
    m_token.clear();
    m_buffer.clear();
}

void QueueEventStream::openConnection()
{
    cleanupReply();
    if (m_stopping || m_token.isEmpty()) {
        return;
    }

    QUrl url(AccountClient::apiBaseUrl());
    url.setPath(QStringLiteral("/queue/sse/"));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Cookie", "__Host-lgsession=" + m_token);
    request.setRawHeader("User-Agent", "LegionGames-Moonlight-SSE");

    m_buffer.clear();
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &QueueEventStream::readAvailable);
    connect(m_reply, &QNetworkReply::finished, this, &QueueEventStream::handleFinished);
}

void QueueEventStream::readAvailable()
{
    if (m_reply == nullptr) {
        return;
    }
    m_buffer += m_reply->readAll();

    while (true) {
        const int end = m_buffer.indexOf("\n\n");
        if (end < 0) {
            break;
        }
        const QByteArray block = m_buffer.left(end);
        m_buffer.remove(0, end + 2);

        QString eventName = QStringLiteral("message");
        QByteArray data;
        for (const QByteArray &line : block.split('\n')) {
            if (line.startsWith("event:")) {
                eventName = QString::fromUtf8(line.mid(6).trimmed());
            }
            else if (line.startsWith("data:")) {
                if (!data.isEmpty()) {
                    data += '\n';
                }
                data += line.mid(5).trimmed();
            }
        }
        if (data.isEmpty()) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (document.isObject()) {
            emit event(eventName, document.object());
        }
    }
}

void QueueEventStream::handleFinished()
{
    if (m_reply == nullptr) {
        return;
    }
    const QString error = m_reply->errorString();
    const bool hadError = m_reply->error() != QNetworkReply::NoError;
    cleanupReply();
    if (m_stopping) {
        return;
    }
    if (hadError) {
        emit failed(QStringLiteral("sse_error"), error);
    }
    scheduleReconnect();
}

void QueueEventStream::scheduleReconnect()
{
    if (m_stopping || m_token.isEmpty()) {
        return;
    }
    m_reconnectTimer->start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, 30000);
}

void QueueEventStream::cleanupReply()
{
    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}
