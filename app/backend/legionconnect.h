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
//   2. enroll into a dedicated "legiongames" NetBird *profile* (so the user's
//      own login/profile is never touched): netbird down, remove any stale
//      "legiongames" profiles, add a fresh one, then up --setup-key --profile
//   3. add the host, pair (PIN) and relay the PIN back to Django
// On failure the same profile cleanup runs (down + remove "legiongames" +
// restore the previously active profile).
// All state is in memory only; nothing is persisted.
class LegionConnect : public QObject
{
    Q_OBJECT

public:
    explicit LegionConnect(ComputerManager* manager, QObject* parent = nullptr);

    void start(const QString& code);
    void disconnect();

signals:
    void status(QString message);
    void progress(int percent);
    void failed(QString error);
    void succeeded();

private:
    enum Stage {
        Idle,
        ExchangingCode,
        NetbirdDown,
        NetbirdListProfiles,
        NetbirdSelectPrev,
        NetbirdRemoveProfile,
        NetbirdAddProfile,
        NetbirdUp,
        NetbirdRestoreProfile,
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

    // NetBird profile management
    void parseProfileList(const QByteArray& out);
    void parseAddedProfile(const QByteArray& out);
    void startRemovingStaleProfiles();
    void removeNextStaleProfile();
    void addSessionProfile();
    void restorePreviousProfile();

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

    // NetBird profile state
    bool m_Cleaning;               // true while tearing down instead of connecting
    bool m_NetbirdTouched;         // true once we've run at least one netbird command
    QString m_PreviousProfileId;   // active profile before we switched (restore target)
    QString m_SessionProfileId;    // the "legiongames" profile we enrolled
    QStringList m_StaleProfileIds; // existing "legiongames" profiles to remove
    bool m_StaleProfileActive;
    int m_RemoveIndex;
};
