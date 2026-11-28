#include "sessionhealthagent.h"

#include "accountclient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QtDebug>

namespace {

// SDL frame clock. The stream loop writes it; the reporter thread reads it.
QMutex g_frameMutex;
qint64 g_lastFrameMs = 0;

// One pending in-stream notice. It is intentionally a single slot: the SDL
// overlay can only show one message at a time, and the server repeats a notice
// while it still applies. Separate from SessionHealthAgent::m_notices so the
// QML signal and the SDL poll do not consume each other's copy.
QMutex g_sdlNoticeMutex;
QVariantMap g_sdlNotice;
bool g_sdlNoticePending = false;

int currentFrameAgeMs()
{
    QMutexLocker locker(&g_frameMutex);
    if (g_lastFrameMs <= 0) {
        return -1;
    }
    return static_cast<int>(
        qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - g_lastFrameMs));
}

// Plan 19, 5.2: retry with 2, 5, 10 and then 30 seconds.
int nextBackoffMs(int *index)
{
    static const int delays[] = { 2000, 5000, 10000, 30000 };
    const int clamped = qBound(0, *index, 3);
    const int delay = delays[clamped];
    if (*index < 3) {
        ++(*index);
    }
    return delay;
}

bool isSupportedPhase(const QString &phase)
{
    return phase == QLatin1String("preparing") ||
        phase == QLatin1String("connecting") ||
        phase == QLatin1String("streaming") ||
        phase == QLatin1String("reconnecting") ||
        phase == QLatin1String("stopping");
}

int defaultIntervalMs(const QString &phase)
{
    if (phase == QLatin1String("preparing") ||
            phase == QLatin1String("connecting") ||
            phase == QLatin1String("reconnecting")) {
        return 10000;
    }
    return 30000;
}

QString defaultOsName()
{
#if defined(Q_OS_WIN32)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

QString terminalCodeForStatus(int status)
{
    switch (status) {
    case 400:
    case 422:
        return QStringLiteral("validation_error");
    case 401:
    case 403:
        return QStringLiteral("health_token_invalid");
    case 404:
        return QStringLiteral("session_not_found");
    case 409:
        return QStringLiteral("session_not_active");
    case 410:
        return QStringLiteral("session_closed");
    default:
        return QStringLiteral("unexpected_status");
    }
}

} // namespace

SessionHealthAgent::SessionHealthAgent(QObject *parent) :
    QThread(parent)
{
    m_monotonic.start();

    m_netbird.insert(QStringLiteral("installed"), false);
    m_netbird.insert(QStringLiteral("daemon"), false);
    m_netbird.insert(QStringLiteral("peer_connected"), false);
    m_netbird.insert(QStringLiteral("profile"), QString());

    m_sunshine.insert(QStringLiteral("reachable"), false);
    m_sunshine.insert(QStringLiteral("paired"), false);

    m_stream.insert(QStringLiteral("state"), QStringLiteral("idle"));
    m_stream.insert(QStringLiteral("first_frame"), false);
    m_stream.insert(QStringLiteral("last_frame_age_ms"), 0);
    m_stream.insert(QStringLiteral("rtt_ms"), 0);
    m_stream.insert(QStringLiteral("audio_ok"), false);
    m_stream.insert(QStringLiteral("input_ok"), false);
    m_stream.insert(QStringLiteral("decoder"), QString());
    m_stream.insert(QStringLiteral("error"), QString());
}

SessionHealthAgent::SessionHealthAgent(const QUrl &apiBaseUrl,
                                       const QString &leaseId,
                                       const QByteArray &healthToken,
                                       const QString &appVersion,
                                       const QString &os,
                                       QObject *parent) :
    SessionHealthAgent(parent)
{
    configure(apiBaseUrl, leaseId, healthToken, appVersion, os);
}

SessionHealthAgent::~SessionHealthAgent()
{
    stop();
    if (isRunning()) {
        // A request has a 10 s transfer timeout and the worker aborts it when
        // interrupted, so this is bounded. The destructor must not leave a
        // running QThread behind.
        wait();
    }
}

void SessionHealthAgent::configure(const QUrl &apiBaseUrl,
                                   const QString &leaseId,
                                   const QByteArray &healthToken,
                                   const QString &appVersion,
                                   const QString &os)
{
    QMutexLocker locker(&m_mutex);
    m_apiBaseUrl = apiBaseUrl;
    m_leaseId = leaseId.trimmed();
    m_healthToken = healthToken.trimmed();
    m_appVersion = appVersion.trimmed();
    m_os = os.trimmed();
    m_initialReportDelayMs = 0;
    if (m_os.isEmpty()) {
        m_os = defaultOsName();
    }
}

QUrl SessionHealthAgent::apiBaseUrl() const
{
    QMutexLocker locker(&m_mutex);
    return m_apiBaseUrl;
}

QString SessionHealthAgent::leaseId() const
{
    QMutexLocker locker(&m_mutex);
    return m_leaseId;
}

bool SessionHealthAgent::hasHealthToken() const
{
    QMutexLocker locker(&m_mutex);
    return !m_healthToken.isEmpty();
}

bool SessionHealthAgent::isActive() const
{
    QMutexLocker locker(&m_mutex);
    return m_running;
}

void SessionHealthAgent::setInitialReportDelayMs(int milliseconds)
{
    QMutexLocker locker(&m_mutex);
    m_initialReportDelayMs = qBound(0, milliseconds, 300000);
}

void SessionHealthAgent::start()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_leaseId.isEmpty() || m_healthToken.isEmpty()) {
            return;
        }
        if (m_running) {
            // Already reporting: ask for an immediate sample instead of
            // starting a second worker.
            m_submitRequested = true;
            m_forceFreshSample = true;
            if (m_waitLoop != nullptr) {
                QMetaObject::invokeMethod(m_waitLoop, "quit",
                                          Qt::QueuedConnection);
            }
            return;
        }
    }

    if (isRunning() && !wait(1500)) {
        // The previous worker is still unwinding after a terminal response.
        // The next session refresh will retry.
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_running || m_leaseId.isEmpty() || m_healthToken.isEmpty()) {
            return;
        }
        m_running = true;
        m_submitRequested = false;
        m_forceFreshSample = false;
        m_finalReport = false;
    }

    QThread::start();
}

void SessionHealthAgent::stop()
{
    requestInterruption();

    {
        QMutexLocker locker(&m_mutex);
        m_submitRequested = false;
        m_forceFreshSample = false;
        m_finalReport = false;
        if (m_waitLoop != nullptr) {
            QMetaObject::invokeMethod(m_waitLoop, "quit", Qt::QueuedConnection);
        }
    }

    if (isRunning()) {
        wait(3000);
    }

    {
        QMutexLocker locker(&m_mutex);
        m_running = false;
        // Notices belong to the lease that just ended; do not let the SDL
        // loop show a stale low-balance/closing message in the next session.
        m_notices.clear();
    }
    clearSdlNotices();
}

void SessionHealthAgent::finalize(int timeoutMs)
{
    bool running = false;
    {
        QMutexLocker locker(&m_mutex);
        running = m_running;
        if (running) {
            // One last sample with the caller's phase/stream state, then the
            // worker exits by itself. submitReport() may be racing; both only
            // set flags under m_mutex.
            m_finalReport = true;
            m_submitRequested = true;
            m_forceFreshSample = true;
            if (m_waitLoop != nullptr) {
                QMetaObject::invokeMethod(m_waitLoop, "quit",
                                          Qt::QueuedConnection);
            }
        }
    }

    if (running) {
        // Bounded: a dead network must not hold the UI. stop() below aborts
        // the in-flight request and joins the worker.
        wait(qBound(0, timeoutMs, 5000));
    }

    stop();
}

void SessionHealthAgent::submitReport()
{
    QMutexLocker locker(&m_mutex);
    if (!m_running || m_healthToken.isEmpty() || m_leaseId.isEmpty()) {
        return;
    }
    m_submitRequested = true;
    m_forceFreshSample = true;
    if (m_waitLoop != nullptr) {
        QMetaObject::invokeMethod(m_waitLoop, "quit", Qt::QueuedConnection);
    }
}

void SessionHealthAgent::noteFrameProduced()
{
    QMutexLocker locker(&g_frameMutex);
    g_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
}

void SessionHealthAgent::resetFrameClock()
{
    QMutexLocker locker(&g_frameMutex);
    g_lastFrameMs = 0;
}

bool SessionHealthAgent::takeSdlNotice(QVariantMap *notice)
{
    QMutexLocker locker(&g_sdlNoticeMutex);
    if (!g_sdlNoticePending) {
        return false;
    }
    if (notice != nullptr) {
        *notice = g_sdlNotice;
    }
    g_sdlNotice.clear();
    g_sdlNoticePending = false;
    return true;
}

void SessionHealthAgent::clearSdlNotices()
{
    QMutexLocker locker(&g_sdlNoticeMutex);
    g_sdlNotice.clear();
    g_sdlNoticePending = false;
}

bool SessionHealthAgent::hasNotice() const
{
    QMutexLocker locker(&m_mutex);
    return !m_notices.isEmpty();
}

QVariantMap SessionHealthAgent::takeNotice()
{
    QMutexLocker locker(&m_mutex);
    if (m_notices.isEmpty()) {
        return QVariantMap();
    }
    return m_notices.takeFirst();
}

void SessionHealthAgent::setPhase(const QString &phase)
{
    if (!isSupportedPhase(phase)) {
        return;
    }
    QMutexLocker locker(&m_mutex);
    m_phase = phase;
}

void SessionHealthAgent::setNetbirdState(bool installed, bool daemon,
                                         bool peerConnected,
                                         const QString &profile)
{
    QJsonObject netbird;
    netbird.insert(QStringLiteral("installed"), installed);
    netbird.insert(QStringLiteral("daemon"), daemon);
    netbird.insert(QStringLiteral("peer_connected"), peerConnected);
    netbird.insert(QStringLiteral("profile"), profile);

    QMutexLocker locker(&m_mutex);
    m_netbird = netbird;
}

void SessionHealthAgent::setSunshineState(bool reachable, bool paired)
{
    QJsonObject sunshine;
    sunshine.insert(QStringLiteral("reachable"), reachable);
    sunshine.insert(QStringLiteral("paired"), paired);

    QMutexLocker locker(&m_mutex);
    m_sunshine = sunshine;
}

void SessionHealthAgent::setStreamState(const QString &state, bool firstFrame,
                                        int lastFrameAgeMs, int rttMs,
                                        bool audioOk, bool inputOk,
                                        const QString &decoder,
                                        const QString &error)
{
    QJsonObject stream;
    stream.insert(QStringLiteral("state"),
                  state.isEmpty() ? QStringLiteral("idle") : state);
    stream.insert(QStringLiteral("first_frame"), firstFrame);
    stream.insert(QStringLiteral("last_frame_age_ms"), qMax(0, lastFrameAgeMs));
    stream.insert(QStringLiteral("rtt_ms"), qMax(0, rttMs));
    stream.insert(QStringLiteral("audio_ok"), audioOk);
    stream.insert(QStringLiteral("input_ok"), inputOk);
    stream.insert(QStringLiteral("decoder"), decoder);
    stream.insert(QStringLiteral("error"), error);

    QMutexLocker locker(&m_mutex);
    m_stream = stream;
}

void SessionHealthAgent::setPhaseStream(const QString &phase,
                                        const QString &state,
                                        bool firstFrame)
{
    if (!isSupportedPhase(phase)) {
        return;
    }
    const QString normalized =
        state.isEmpty() ? QStringLiteral("idle") : state;
    QMutexLocker locker(&m_mutex);
    m_phase = phase;
    m_stream.insert(QStringLiteral("state"), normalized);
    m_stream.insert(QStringLiteral("first_frame"), firstFrame);
    if (normalized == QLatin1String("streaming") && !firstFrame) {
        m_stream.insert(QStringLiteral("last_frame_age_ms"), 0);
        m_stream.insert(QStringLiteral("error"), QString());
    }
}

void SessionHealthAgent::run()
{
    // The manager is created in the worker thread: QNetworkAccessManager is
    // not thread-safe and must only be used from the thread it belongs to.
    QNetworkAccessManager manager;
    AccountClient::configureNetworkManager(&manager);

    int delay = 0;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_running) {
            return;
        }
        delay = qBound(0, m_initialReportDelayMs, 300000);
    }

    QByteArray pendingBody;
    int backoffIndex = 0;

    while (!isInterruptionRequested()) {
        {
            QMutexLocker locker(&m_mutex);
            if (!m_running || m_healthToken.isEmpty() || m_leaseId.isEmpty()) {
                break;
            }
        }

        if (!waitForWake(delay)) {
            break;
        }

        bool forceFresh = false;
        {
            QMutexLocker locker(&m_mutex);
            forceFresh = m_forceFreshSample;
            m_forceFreshSample = false;
        }
        if (forceFresh) {
            // A transition event is newer than a report waiting for a retry.
            pendingBody.clear();
        }

        delay = postReport(&manager, &pendingBody, &backoffIndex);
        if (delay < 0) {
            break;
        }

        {
            QMutexLocker locker(&m_mutex);
            if (m_finalReport) {
                break;
            }
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        m_running = false;
        m_submitRequested = false;
        m_finalReport = false;
    }
}

bool SessionHealthAgent::waitForWake(int milliseconds)
{
    QMutexLocker locker(&m_mutex);
    if (isInterruptionRequested()) {
        return false;
    }
    if (m_submitRequested) {
        m_submitRequested = false;
        return true;
    }
    if (milliseconds <= 0) {
        return true;
    }

    QEventLoop loop;
    m_waitLoop = &loop;
    locker.unlock();

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(milliseconds);
    loop.exec();

    locker.relock();
    m_waitLoop = nullptr;
    return !isInterruptionRequested();
}

QJsonObject SessionHealthAgent::buildReport()
{
    QMutexLocker locker(&m_mutex);

    QJsonObject app;
    app.insert(QStringLiteral("version"), m_appVersion);
    app.insert(QStringLiteral("os"), m_os);

    QJsonObject report;
    report.insert(QStringLiteral("v"), 1);
    report.insert(QStringLiteral("seq"), static_cast<double>(m_sequence++));
    report.insert(QStringLiteral("phase"), m_phase);
    report.insert(QStringLiteral("client_time"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    report.insert(QStringLiteral("monotonic_ms"),
                  static_cast<double>(m_monotonic.isValid()
                                          ? m_monotonic.elapsed()
                                          : 0));
    report.insert(QStringLiteral("netbird"), m_netbird);
    report.insert(QStringLiteral("sunshine"), m_sunshine);

    // The SDL loop owns the real frame clock. A rendered frame is proof that
    // the stream has started, so report first_frame even when the QML signal
    // was missed (CLI launch or a page change).
    QJsonObject stream = m_stream;
    const int frameAgeMs = currentFrameAgeMs();
    stream.insert(QStringLiteral("streaming"),
                  frameAgeMs >= 0 && frameAgeMs <= 2000);
    if (frameAgeMs >= 0) {
        stream.insert(QStringLiteral("last_frame_age_ms"), frameAgeMs);
        if (stream.value(QStringLiteral("state")).toString() ==
                QLatin1String("streaming")) {
            stream.insert(QStringLiteral("first_frame"), true);
        }
    }
    report.insert(QStringLiteral("stream"), stream);
    report.insert(QStringLiteral("app"), app);

    return report;
}

int SessionHealthAgent::postReport(QNetworkAccessManager *manager,
                                   QByteArray *pendingBody, int *backoffIndex)
{
    QUrl url;
    QByteArray token;
    QString appVersion;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_running || m_healthToken.isEmpty() || m_leaseId.isEmpty()) {
            return -1;
        }
        url = healthUrlLocked();
        token = m_healthToken;
        appVersion = m_appVersion;
    }

    if (!url.isValid() || url.isEmpty()) {
        emit reportFailed(QStringLiteral("invalid_api_url"), 0);
        return -1;
    }

    // Keep the last unsent report across retries. The server accepts a
    // duplicate seq idempotently; the sample timestamps stay the original ones.
    if (pendingBody->isEmpty()) {
        *pendingBody = QJsonDocument(buildReport()).toJson(QJsonDocument::Compact);
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", "Bearer " + token);
    request.setRawHeader("User-Agent", "LegionGames-Moonlight-Health");
    if (!appVersion.isEmpty()) {
        request.setRawHeader("X-Client-Version", appVersion.toUtf8());
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    // SteamLink ships Qt 5.14, where QNetworkRequest has no transfer timeout.
    request.setTransferTimeout(10000);
#endif

    QNetworkReply *reply = manager->post(request, *pendingBody);

    QEventLoop loop;
    QTimer abortTimer;
    abortTimer.setInterval(250);
    QObject::connect(&abortTimer, &QTimer::timeout, &loop, [this, reply]() {
        if (isInterruptionRequested()) {
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    abortTimer.start();
    loop.exec();
    abortTimer.stop();

    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QByteArray retryAfter = reply->rawHeader("Retry-After");
    reply->deleteLater();

    if (isInterruptionRequested()) {
        return -1;
    }

    if (networkError != QNetworkReply::NoError && status == 0) {
        return nextBackoffMs(backoffIndex);
    }

    if (status == 200) {
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
                !document.isObject()) {
            // A malformed 200 is a server-side fault; keep the report and
            // retry with the normal backoff.
            return nextBackoffMs(backoffIndex);
        }

        const QJsonObject response = document.object();
        const QJsonObject health =
            response.value(QStringLiteral("health")).toObject();
        const QJsonObject notice =
            response.value(QStringLiteral("notice")).toObject();
        const QString action =
            response.value(QStringLiteral("action")).toString();

        QVariantMap queuedNotice;
        if (!notice.isEmpty()) {
            QMutexLocker locker(&m_mutex);
            queuedNotice = queueNoticeLocked(
                notice, action, health.value(QStringLiteral("state")).toString());
        }
        if (!queuedNotice.isEmpty()) {
            emit noticeAvailable(queuedNotice);
        }
        if (!action.isEmpty() || !health.isEmpty()) {
            emit healthAction(action, health.toVariantMap());
        }

        pendingBody->clear();
        *backoffIndex = 0;

        QString phase;
        {
            QMutexLocker locker(&m_mutex);
            phase = m_phase;
        }
        int nextMs =
            response.value(QStringLiteral("next_report_in_ms")).toInt(0);
        if (nextMs <= 0) {
            nextMs = defaultIntervalMs(phase);
        }
        return qBound(5000, nextMs, 300000);
    }

    if (status == 429 || status == 408 || status >= 500) {
        int delay = nextBackoffMs(backoffIndex);
        if (status == 429) {
            bool ok = false;
            const int seconds = retryAfter.toInt(&ok);
            if (ok && seconds > 0) {
                delay = qBound(1000, seconds * 1000, 120000);
            }
        }
        return delay;
    }

    // 401/403/404/409/410 and other 4xx are terminal for this lease. Do not
    // retry a dead token or a closed session, and never log the token.
    emit reportFailed(terminalCodeForStatus(status), status);
    return -1;
}

QUrl SessionHealthAgent::healthUrlLocked() const
{
    if (m_apiBaseUrl.isEmpty() || m_leaseId.isEmpty()) {
        return QUrl();
    }

    QString base = m_apiBaseUrl.toString();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    if (base.isEmpty()) {
        return QUrl();
    }

    const QString encodedLease =
        QString::fromLatin1(QUrl::toPercentEncoding(m_leaseId));
    return QUrl(base + QStringLiteral("/sessions/") + encodedLease +
                QStringLiteral("/health"));
}

QVariantMap SessionHealthAgent::queueNoticeLocked(const QJsonObject &notice,
                                                  const QString &action,
                                                  const QString &healthState)
{
    if (notice.isEmpty()) {
        return QVariantMap();
    }

    QVariantMap map = notice.toVariantMap();
    if (!action.isEmpty()) {
        map.insert(QStringLiteral("action"), action);
    }
    if (!healthState.isEmpty()) {
        map.insert(QStringLiteral("health_state"), healthState);
    }

    // A repeated identical notice is not a new QML event. A changed message or
    // minutes_left is queued normally. The in-stream slot is refreshed anyway:
    // the server repeats a notice while it still applies, and the SDL overlay
    // shows it for 8 s per report cycle.
    if (!m_notices.isEmpty() && m_notices.last() == map) {
        QMutexLocker sdlLocker(&g_sdlNoticeMutex);
        g_sdlNotice = map;
        g_sdlNoticePending = true;
        return QVariantMap();
    }

    while (m_notices.size() >= 8) {
        m_notices.removeFirst();
    }
    m_notices.append(map);

    // The SDL loop polls a separate single slot: the QML side may not consume
    // notices and the overlay must still see the server message mid-stream.
    {
        QMutexLocker sdlLocker(&g_sdlNoticeMutex);
        g_sdlNotice = map;
        g_sdlNoticePending = true;
    }

    return map;
}
