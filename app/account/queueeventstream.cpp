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
    // Same transport policy as the account client: no cookies, no redirects,
    // HSTS and the shared certificate pins. The SSE request carries a Bearer
    // access token, so it must never be weaker than the API transport.
    AccountClient::configureNetworkManager(m_nam);

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
    m_lastEventId.clear();
}

void QueueEventStream::openConnection()
{
    cleanupReply();
    if (m_stopping || m_token.isEmpty()) {
        return;
    }

    QUrl url = AccountClient::apiOriginUrl(QStringLiteral("/queue/sse/"));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Authorization", "Bearer " + m_token);
    if (!m_lastEventId.isEmpty()) {
        request.setRawHeader("Last-Event-ID", m_lastEventId);
    }
    request.setRawHeader("X-Client-Version", AccountClient::clientVersion().toUtf8());
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
        QByteArray eventId;
        QByteArray data;
        for (const QByteArray &rawLine : block.split('\n')) {
            if (rawLine.startsWith(':')) {
                continue;
            }
            const int colon = rawLine.indexOf(':');
            const QByteArray field = colon < 0 ? rawLine : rawLine.left(colon);
            QByteArray value = colon < 0 ? QByteArray() : rawLine.mid(colon + 1);
            if (value.startsWith(' ')) {
                value.remove(0, 1);
            }
            if (field == "event") {
                eventName = QString::fromUtf8(value.trimmed());
            }
            else if (field == "id") {
                eventId = value.trimmed();
            }
            else if (field == "data") {
                if (!data.isEmpty()) {
                    data += '\n';
                }
                data += value;
            }
        }
        if (!eventId.isEmpty()) {
            m_lastEventId = eventId;
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
    const int httpStatus = m_reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = m_reply->errorString();
    const bool hadError = m_reply->error() != QNetworkReply::NoError;
    cleanupReply();
    if (m_stopping) {
        return;
    }
    if (httpStatus == 401 || httpStatus == 403) {
        emit failed(QStringLiteral("sse_unauthorized"), error);
    }
    else if (hadError) {
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
