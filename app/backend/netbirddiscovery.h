#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class ComputerManager;
class QTimer;

// LegionGames: discovers club hosts on the NetBird overlay. mDNS multicast does
// not traverse the NetBird (WireGuard) tunnel, so instead of listening for
// mDNS advertisements we ask the local NetBird daemon for its peer list and
// probe each peer over unicast.
class NetbirdDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit NetbirdDiscovery(ComputerManager* manager, QObject* parent = nullptr);

    void start();
    void stop();

private slots:
    void probe();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);

private:
    QString findNetbirdExecutable() const;
    void addPeers(const QByteArray& json);

    ComputerManager* m_Manager;
    QTimer* m_Timer;
    QProcess* m_Process;
    bool m_Active;
    bool m_DetailedFallback;
};
