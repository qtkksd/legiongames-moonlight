#include "netbirddiscovery.h"
#include "computermanager.h"
#include "nvaddress.h"

#include <QTimer>
#include <QStandardPaths>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSet>
#include <QtDebug>

// NetBird overlay subnet used by LegionGames: 100.116.0.0/16
static const quint32 kNetbirdNet = 0x64740000u;

static bool isNetbirdSubnetAddress(const QHostAddress& address)
{
    return address.protocol() == QAbstractSocket::IPv4Protocol &&
           (address.toIPv4Address() & 0xFFFF0000u) == kNetbirdNet;
}

static bool isLocalAddress(const QHostAddress& address)
{
    return QNetworkInterface::allAddresses().contains(address);
}

NetbirdDiscovery::NetbirdDiscovery(ComputerManager* manager, QObject* parent) :
    QObject(parent),
    m_Manager(manager),
    m_Timer(new QTimer(this)),
    m_Process(nullptr),
    m_Active(false),
    m_DetailedFallback(false)
{
    // Re-probe periodically so peers that come online later are picked up.
    m_Timer->setInterval(2 * 60 * 1000);
    connect(m_Timer, &QTimer::timeout, this, &NetbirdDiscovery::probe);
}

void NetbirdDiscovery::start()
{
    if (m_Active) {
        return;
    }

    m_Active = true;
    probe();
    m_Timer->start();
}

void NetbirdDiscovery::stop()
{
    m_Active = false;
    m_Timer->stop();

    if (m_Process != nullptr) {
        m_Process->kill();
    }
}

QString NetbirdDiscovery::findNetbirdExecutable() const
{
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("netbird"));
    if (!exe.isEmpty()) {
        return exe;
    }

#ifdef Q_OS_WIN32
    const QString candidate = qEnvironmentVariable("ProgramFiles") + QStringLiteral("/NetBird/netbird.exe");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
#elif defined(Q_OS_DARWIN)
    const QString candidate = QStringLiteral("/Applications/NetBird.app/Contents/MacOS/netbird");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
#else
    const QString candidate = QStringLiteral("/usr/bin/netbird");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
    const QString localCandidate = QStringLiteral("/usr/local/bin/netbird");
    if (QFileInfo::exists(localCandidate)) {
        return localCandidate;
    }
#endif

    return QString();
}

void NetbirdDiscovery::probe()
{
    if (m_Process != nullptr) {
        // A probe is already in flight.
        return;
    }

    const QString exe = findNetbirdExecutable();
    if (exe.isEmpty()) {
        qInfo() << "NetBird CLI not found; skipping NetBird peer discovery";
        return;
    }

    m_Process = new QProcess(this);
    connect(m_Process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NetbirdDiscovery::handleProcessFinished);
    connect(m_Process, &QProcess::errorOccurred,
            this, &NetbirdDiscovery::handleProcessError);

    // Prefer the structured JSON output; some NetBird versions only support
    // the human-readable detailed report, so fall back to that if needed.
    if (m_DetailedFallback) {
        m_Process->start(exe, QStringList() << QStringLiteral("status") << QStringLiteral("-d"));
    }
    else {
        m_Process->start(exe, QStringList() << QStringLiteral("status") << QStringLiteral("--json"));
    }
}

void NetbirdDiscovery::handleProcessError(QProcess::ProcessError error)
{
    qWarning() << "NetBird status process error:" << error;

    if (m_Process != nullptr) {
        m_Process->deleteLater();
        m_Process = nullptr;
    }
}

void NetbirdDiscovery::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess* process = m_Process;
    m_Process = nullptr;

    if (process == nullptr) {
        return;
    }

    const QByteArray output = process->readAllStandardOutput();
    process->deleteLater();

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (!m_DetailedFallback) {
            qInfo() << "netbird status --json failed; retrying with -d";
            m_DetailedFallback = true;
            probe();
            return;
        }

        qWarning() << "netbird status failed with exit code" << exitCode;
        return;
    }

    m_DetailedFallback = false;
    addPeers(output);
}

void NetbirdDiscovery::addPeers(const QByteArray& json)
{
    // Extract every IPv4 address from the NetBird status JSON and keep only
    // those on the NetBird overlay subnet. This is schema-independent, so it
    // keeps working across NetBird versions and fields (netbirdIp, fqdn IPs, ...).
    static const QRegularExpression ipv4(QStringLiteral("\\b(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})\\b"));

    QSet<QString> seen;
    QRegularExpressionMatchIterator it = ipv4.globalMatch(QString::fromUtf8(json));
    while (it.hasNext()) {
        const QString candidate = it.next().captured(1);

        const QHostAddress address(candidate);

        // Only peers on the NetBird subnet, and never ourselves.
        if (!isNetbirdSubnetAddress(address) || isLocalAddress(address)) {
            continue;
        }

        if (seen.contains(candidate)) {
            continue;
        }
        seen.insert(candidate);

        qInfo() << "Discovered NetBird peer:" << candidate;

        // mdns=true keeps this silent (no error dialogs / STUN) and reuses the
        // existing port-47989 probe before the host is added.
        m_Manager->addNewHost(NvAddress(candidate, DEFAULT_HTTP_PORT), true);
    }
}
