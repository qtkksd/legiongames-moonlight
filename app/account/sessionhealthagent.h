#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

class QNetworkAccessManager;

// Lease-scoped health reporter for an active Moonlight session.
//
// The agent runs a dedicated QThread with its own QNetworkAccessManager and
// event loop so it keeps reporting while the SDL/QML main loop is suspended
// inside Session::exec(). It never touches QML, the SDL loop or the account
// access token. The only credential is the lease-scoped health token returned
// by /connect (or reissued by GET /sessions/current).
//
// Ownership: LegionApi creates one agent and keeps it idle until a health
// token is available. start() is cheap; if the agent has no lease or token it
// returns without starting a thread.
class SessionHealthAgent : public QThread
{
    Q_OBJECT

public:
    explicit SessionHealthAgent(QObject *parent = nullptr);
    SessionHealthAgent(const QUrl &apiBaseUrl, const QString &leaseId,
                       const QByteArray &healthToken, const QString &appVersion,
                       const QString &os, QObject *parent = nullptr);
    ~SessionHealthAgent() override;

    // Replaces the lease scope. Safe to call while the worker is running: the
    // next report uses the new values. A different lease should normally be
    // applied after stop().
    void configure(const QUrl &apiBaseUrl, const QString &leaseId,
                   const QByteArray &healthToken, const QString &appVersion,
                   const QString &os);

    QUrl apiBaseUrl() const;
    QString leaseId() const;
    bool hasHealthToken() const;
    bool isActive() const;

    // First delay before the initial report. The server's next_report_in_ms
    // takes over after the first response. 0 means report immediately.
    void setInitialReportDelayMs(int milliseconds);

    // Starts the worker if a lease and token are present. Calling it while the
    // worker is already running requests an immediate sample instead of
    // starting a second thread.
    void start();

    // Requests a clean stop and waits briefly for an in-flight request to
    // abort. Idempotent. Safe to call from the UI thread.
    void stop();

    // Sends one final report (phase/stream state set by the caller) and then
    // stops. Best effort: if the network does not answer within timeoutMs the
    // request is aborted and the worker is joined, so the caller is bounded.
    void finalize(int timeoutMs = 1500);

    // Asks the worker for an immediate report (stream start/stop, first frame,
    // decoder error, stream end). Never blocks and never performs I/O on the
    // calling thread. The next request is rebuilt from the current state even
    // if a previous report is still waiting for a retry.
    void submitReport();

    // Frame clock shared with the SDL loop. The loop calls noteFrameProduced()
    // for every rendered frame; buildReport() reads the age without touching
    // QML or the stream thread. No allocation, no I/O, no Qt event delivery.
    static void noteFrameProduced();
    static void resetFrameClock();

    // Server notices for the SDL loop. The agent queues them under a mutex;
    // Session::checkSessionDeadline() polls at most once per second. This is
    // separate from the QML notice list so both surfaces see the same notice
    // without consuming each other's copy.
    static bool takeSdlNotice(QVariantMap *notice);
    static void clearSdlNotices();

    bool hasNotice() const;
    // Pops the oldest server notice. The returned map is the server notice
    // object plus `action` and `health_state` when the server supplied them.
    QVariantMap takeNotice();

    void setPhase(const QString &phase);
    void setNetbirdState(bool installed, bool daemon, bool peerConnected,
                         const QString &profile);
    void setSunshineState(bool reachable, bool paired);
    void setStreamState(const QString &state, bool firstFrame,
                        int lastFrameAgeMs = 0, int rttMs = 0,
                        bool audioOk = false, bool inputOk = false,
                        const QString &decoder = QString(),
                        const QString &error = QString());
    // Atomically sets the phase and the stream flags so a report cannot be
    // built between the two updates.
    void setPhaseStream(const QString &phase, const QString &state,
                        bool firstFrame);

signals:
    // Emitted from the worker thread; the receiver must live in another
    // thread (LegionApi does) so Qt delivers it as a queued connection.
    void noticeAvailable(const QVariantMap &notice);
    void healthAction(const QString &action, const QVariantMap &health);
    // Terminal transport/protocol failure. No token is included.
    void reportFailed(const QString &code, int httpStatus);

protected:
    void run() override;

private:
    QJsonObject buildReport();
    int postReport(QNetworkAccessManager *manager, QByteArray *pendingBody,
                   int *backoffIndex);
    bool waitForWake(int milliseconds);
    QUrl healthUrlLocked() const;
    QVariantMap queueNoticeLocked(const QJsonObject &notice,
                                  const QString &action,
                                  const QString &healthState);

    mutable QMutex m_mutex;
    QUrl m_apiBaseUrl;
    QString m_leaseId;
    QByteArray m_healthToken;
    QString m_appVersion;
    QString m_os;
    QString m_phase = QStringLiteral("preparing");
    QJsonObject m_netbird;
    QJsonObject m_sunshine;
    QJsonObject m_stream;
    QList<QVariantMap> m_notices;
    // Owned by run(); set only while the worker waits in its own event loop.
    QEventLoop *m_waitLoop = nullptr;
    bool m_running = false;
    bool m_submitRequested = false;
    bool m_forceFreshSample = false;
    bool m_finalReport = false;
    int m_initialReportDelayMs = 0;
    QElapsedTimer m_monotonic;
    qint64 m_sequence = 0;
};
