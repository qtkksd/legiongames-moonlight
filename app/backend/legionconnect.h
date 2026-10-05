#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ComputerManager;
class NvComputer;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Orchestrates the LegionGames one-time-code connect flow inside the client:
//   1. exchange the code for a NetBird setup key + PC address
//   2. netbird down, then up --setup-key
//   3. add the host, pair (PIN) and relay the PIN back to Django
// All state is in memory only; nothing is persisted.
class LegionConnect : public QObject
{
    Q_OBJECT

public:
    explicit LegionConnect(ComputerManager* manager, QObject* parent = nullptr);

    void start(const QString& code);

signals:
    void status(QString message);
    void failed(QString error);
    void succeeded();

private:
    enum Stage {
        Idle,
        ExchangingCode,
        NetbirdDown,
        NetbirdUp,
        WaitingForHost,
        Pairing,
    };

    void fail(const QString& error);
    void runNetbird(const QStringList& args, Stage stage);
    void downloadSetupKeyFromReply(QNetworkReply* reply);
    void addHost();
    void startPairing(NvComputer* computer);
    void submitPin();
    void checkFinished();

    QString findNetbirdExecutable() const;
    NvComputer* findComputerByAddress(const QString& ip) const;

    ComputerManager* m_Manager;
    QNetworkAccessManager* m_Nam;
    QProcess* m_Process;
    Stage m_Stage;
    QString m_Code;

    QString m_SetupKey;
    QString m_ManagementUrl;
    QString m_PcIp;
    QString m_PcName;
    QString m_Token;

    // Pairing / PIN submission run concurrently; both must succeed.
    QString m_Pin;
    bool m_PairingDone;
    bool m_PairingOk;
    bool m_PinDone;
    bool m_PinOk;
    int m_PinAttempts;
    int m_AddRetries;
};
