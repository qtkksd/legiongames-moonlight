#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class ComputerManager;
class NvComputer;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Orchestrates the LegionGames app-first connect flow inside the client:
//   1. receive the one-shot connect material from the device-session owner
//      (netbird setup key, PC overlay address, short-lived lease token)
//   2. enroll into a dedicated, environment-specific "legiongames[-stg]"
//      NetBird *profile* (so the user's own login/profile is never touched):
//      netbird down, remove any stale session profiles, add a fresh one, then
//      up --setup-key --profile
//   3. add the host, pair (PIN), relay the PIN to Django and wait for the
//      server-side activation.
// On failure the same profile cleanup runs (down + remove the session profile
// + restore the previously active profile).
// The one-shot material itself is persisted by LegionApi (encrypted vault), not
// here: this object only holds the in-flight copy and clears it on teardown.
class LegionConnect : public QObject
{
    Q_OBJECT

public:
    explicit LegionConnect(ComputerManager* manager, QObject* parent = nullptr);

    void startWithPayload(const QVariantMap& data);
    void disconnect();

    // True while the connect state machine owns the NetBird profile or the
    // pairing/PIN steps. A persisted material resume must not start a second
    // flow on top of a live one.
    bool isBusy() const { return m_Stage != Idle; }

    // Blocking teardown for application shutdown. aboutToQuit is emitted after
    // the event loop stops dispatching work, so the asynchronous cleanup used
    // by disconnect() cannot finish there.
    void shutdown();

signals:
    void status(QString message);
    void progress(int percent);
    void failed(QString error);
    void succeeded();
    // The club PC this lease is provisioned on, identified when the host
    // appears on the overlay. Consumers must treat it as the only streamable
    // host for the active lease.
    void hostIdentified(QString uuid);

private:
    enum Stage {
        Idle,
        Preparing,
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
    void applyConnectPayload(const QJsonObject& object);
    bool prepareSetupKeyFile();
    void cleanupSetupKeyFile();
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
    QNetworkReply* m_PinReply = nullptr;
    QProcess* m_Process;
    Stage m_Stage;
    QString m_SessionId;

    QString m_SetupKey;
    QString m_SetupKeyFile;
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
