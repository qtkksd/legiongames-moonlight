#include "deeplink.h"

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSharedMemory>
#include <QUrl>
#include <QtDebug>

namespace {

const char kScheme[] = "legiongames";
const char kPaymentHost[] = "payment";
const int kPaymentHostLength = 7;

bool isPaymentUuid(const QString &value)
{
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
                       "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return uuid.match(value).hasMatch();
}

} // namespace

DeepLink::DeepLink(QObject *parent) :
    QObject(parent)
{
}

DeepLink::~DeepLink()
{
    if (m_server != nullptr) {
        m_server->close();
    }
}

QString DeepLink::paymentIdFromUrl(const QString &url)
{
    if (url.isEmpty()) {
        return QString();
    }

    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    if (!parsed.isValid() ||
        parsed.scheme().compare(QLatin1String(kScheme), Qt::CaseInsensitive) != 0) {
        return QString();
    }

    QString candidate;
    if (parsed.host().compare(QLatin1String(kPaymentHost), Qt::CaseInsensitive) == 0) {
        candidate = parsed.path();
        while (candidate.startsWith(QLatin1Char('/'))) {
            candidate.remove(0, 1);
        }
    }
    else {
        // Also accept the opaque form legiongames:payment/<uuid>.
        QString path = parsed.path();
        while (path.startsWith(QLatin1Char('/'))) {
            path.remove(0, 1);
        }
        QString paymentPrefix = QString::fromLatin1(kPaymentHost);
        paymentPrefix += QLatin1Char('/');
        if (path.startsWith(paymentPrefix)) {
            candidate = path.mid(kPaymentHostLength + 1);
        }
    }

    return isPaymentUuid(candidate) ? candidate.toLower() : QString();
}

QString DeepLink::canonicalPaymentUrl(const QString &paymentId)
{
    if (!isPaymentUuid(paymentId)) {
        return QString();
    }

    QString url = QStringLiteral("legiongames://");
    url += QLatin1String(kPaymentHost);
    url += QLatin1Char('/');
    url += paymentId.toLower();
    return url;
}

bool DeepLink::isDeepLinkLaunch(const QStringList &arguments)
{
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument.startsWith(QLatin1String("legiongames:"), Qt::CaseInsensitive)) {
            return true;
        }
        if (argument == QLatin1String("--url") || argument.startsWith(QLatin1String("--url="))) {
            return true;
        }
    }
    return false;
}

QString DeepLink::paymentIdFromArguments(const QStringList &arguments)
{
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);

        if (argument == QLatin1String("--url") && i + 1 < arguments.size()) {
            const QString id = paymentIdFromUrl(arguments.at(i + 1));
            if (!id.isEmpty()) {
                return id;
            }
            continue;
        }

        if (argument.startsWith(QLatin1String("--url="))) {
            const QString id = paymentIdFromUrl(argument.mid(6));
            if (!id.isEmpty()) {
                return id;
            }
            continue;
        }

        if (argument.startsWith(QLatin1String("legiongames:"), Qt::CaseInsensitive)) {
            const QString id = paymentIdFromUrl(argument);
            if (!id.isEmpty()) {
                return id;
            }
        }
    }

    return QString();
}

QString DeepLink::serverName()
{
    return QStringLiteral("ru.legiongames.moonlight.deeplink");
}

QString DeepLink::sharedMemoryKey()
{
    return QStringLiteral("ru.legiongames.moonlight.deeplink.v1");
}

QString DeepLink::takePendingPaymentId()
{
    const QString pending = m_pendingPaymentId;
    m_pendingPaymentId.clear();
    return pending;
}

bool DeepLink::forwardToPrimary(const QString &url, int timeoutMs)
{
    if (url.isEmpty()) {
        return false;
    }

    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }

    QByteArray payload = url.toUtf8();
    payload.append('\n');
    socket.write(payload);
    socket.flush();

    const bool written = socket.waitForBytesWritten(timeoutMs);
    socket.disconnectFromServer();
    return written;
}

bool DeepLink::startPrimary()
{
    if (m_primary) {
        return true;
    }

    m_shared = new QSharedMemory(sharedMemoryKey(), this);

    bool ownsMarker = m_shared->create(1);
    if (!ownsMarker) {
        // Live primary? The socket is the liveness source of truth.
        QLocalSocket probe;
        probe.connectToServer(serverName());
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            return false;
        }

        // A crashed primary can leave the shared segment behind. Detaching as
        // the last process removes it on Unix; on Windows the segment dies
        // with the process.
        if (m_shared->attach()) {
            m_shared->detach();
        }
        ownsMarker = m_shared->create(1);
    }

    if (ownsMarker) {
        // Only remove a stale socket file when we own the marker; otherwise a
        // live primary could still be starting up.
        QLocalServer::removeServer(serverName());
    }

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(serverName())) {
        qWarning() << "DeepLink: cannot listen on" << serverName()
                   << m_server->errorString();
        if (m_shared->isAttached()) {
            m_shared->detach();
        }
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QLocalServer::newConnection, this, &DeepLink::handleConnection);
    m_primary = true;
    return true;
}

void DeepLink::handleConnection()
{
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (socket == nullptr) {
            return;
        }

        m_buffers.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            handleSocketData(socket);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            handleSocketData(socket);
            m_buffers.remove(socket);
            socket->deleteLater();
        });

        // Data may already be buffered before the readyRead connection.
        if (socket->bytesAvailable() > 0) {
            handleSocketData(socket);
        }
    }
}

void DeepLink::handleSocketData(QLocalSocket *socket)
{
    if (socket == nullptr) {
        return;
    }

    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();

    int newline = buffer.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);

        const QString paymentId = paymentIdFromUrl(QString::fromUtf8(line));
        if (!paymentId.isEmpty()) {
            m_pendingPaymentId = paymentId;
            emit paymentUrlReceived(paymentId);
        }

        newline = buffer.indexOf('\n');
    }
}
