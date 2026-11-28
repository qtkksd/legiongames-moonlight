#include "legionapi.h"

#include "accountclient.h"
#include "queueeventstream.h"
#include "cryptoutil.h"
#include "devicekey.h"
#include "path.h"
#include "recoverykit.h"
#include "sessionhealthagent.h"
#include "vault.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QtDebug>

namespace {

const char kAccountVaultKey[] = "account";
const char kWordsVaultKey[] = "recovery.words";
const char kConsentVaultKey[] = "consent";
const char kPendingIntentVaultKey[] = "payment.pending";
// Encrypted one-shot /sessions/{id}/connect payload. The setup key and lease
// token are secrets, so they never go to QSettings or a plain file.
const char kConnectVaultKey[] = "session.connect";
// The lease PIN token lives for 10 minutes server-side. Persisting the material
// beyond that cannot succeed and must not keep a stale secret around.
const int kConnectMaterialMaxAgeSeconds = 600;

QString findNetbirdExecutable()
{
    QString exe = QStandardPaths::findExecutable(QStringLiteral("netbird"));
    if (!exe.isEmpty()) {
        return exe;
    }
#ifdef Q_OS_WIN32
    const QString candidate =
        qEnvironmentVariable("ProgramFiles") + QStringLiteral("/NetBird/netbird.exe");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
#elif defined(Q_OS_MACOS)
    const QString candidate =
        QStringLiteral("/Applications/NetBird.app/Contents/MacOS/netbird");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
#else
    for (const QString &candidate : {QStringLiteral("/usr/bin/netbird"),
                                     QStringLiteral("/usr/local/bin/netbird")}) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
#endif
    return QString();
}

const int kMaxIntentPolls = 80;

QString platformOs()
{
#if defined(Q_OS_WIN32)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QByteArray ceremonyMessage(const QString &ceremonyId, const QString &challenge,
                           const QString &action)
{
    // Exact format from web/device_auth.py::challenge_message.
    return QStringLiteral("%1.%2.%3").arg(ceremonyId, challenge, action).toUtf8();
}

QStringList normalizeWords(const QStringList &words)
{
    QStringList normalized;
    normalized.reserve(words.size());
    for (const QString &word : words) {
        const QString trimmed = word.trimmed().toLower();
        if (!trimmed.isEmpty()) {
            normalized.append(trimmed);
        }
    }
    return normalized;
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QString normalizeReferralCode(const QString &value)
{
    const QString trimmed = value.trimmed().toUpper();
    QString normalized;
    normalized.reserve(trimmed.size());
    for (const QChar character : trimmed) {
        const ushort code = character.unicode();
        const bool latinUpper = code >= 'A' && code <= 'Z';
        const bool digit = code >= '0' && code <= '9';
        if (latinUpper || digit ||
                character == QLatin1Char('-') || character == QLatin1Char('_')) {
            normalized.append(character);
        }
        if (normalized.size() >= 32) {
            break;
        }
    }
    return normalized.size() >= 3 ? normalized : QString();
}

} // namespace

LegionApi::LegionApi(QObject *parent) :
    QObject(parent)
{
}

LegionApi::~LegionApi()
{
    stopQueueEvents();
    if (m_healthAgent != nullptr) {
        // The health worker owns a network manager and an event loop in its
        // own thread; it must be joined before QObject teardown.
        m_healthAgent->stop();
        m_healthAgent->disconnect(this);
        delete m_healthAgent;
        m_healthAgent = nullptr;
    }
    if (m_client != nullptr) {
        m_client->setDeviceKey(nullptr);
        delete m_client;
        m_client = nullptr;
    }
    delete m_deviceKey;
    m_deviceKey = nullptr;
    delete m_vault;
    m_vault = nullptr;
}

void LegionApi::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    m_vault = new Vault();
    m_client = new AccountClient(m_vault, this);
    m_queueEvents = new QueueEventStream(this);
    m_healthAgent = new SessionHealthAgent(this);
    connect(m_healthAgent, &SessionHealthAgent::noticeAvailable, this,
            [this](const QVariantMap &notice) {
                emit sessionHealthNotice(
                    notice.value(QStringLiteral("code")).toString(), notice);
            });
    connect(m_healthAgent, &SessionHealthAgent::healthAction, this,
            [this](const QString &action, const QVariantMap &health) {
                if (!action.isEmpty()) {
                    emit sessionHealthAction(action, health);
                }
            });
    connect(m_healthAgent, &SessionHealthAgent::reportFailed, this,
            [](const QString &code, int httpStatus) {
                // Never log the health token or the report body.
                qWarning() << "Session health: reporter stopped:"
                           << code << httpStatus;
            });
    connect(m_queueEvents, &QueueEventStream::event, this,
            [this](const QString &name, const QJsonObject &data) {
                emit queueEvent(name, data.toVariantMap());
            });
    connect(m_queueEvents, &QueueEventStream::failed, this,
            [](const QString &code, const QString &message) {
                qWarning() << "Queue SSE:" << code << message;
            });
    connect(m_client, &AccountClient::tokensChanged, this, [this]() {
        if (m_state == QLatin1String("signed_in")) {
            startQueueEvents();
        }
    });
    qInfo() << "Account: vault backend" << m_vault->backendName();

    connect(m_client, &AccountClient::authenticationLost, this, [this](const QString &code) {
        if (m_logoutInProgress) {
            // finishLogout() owns the terminal state during sign-out; a late
            // auth failure from the revocation request must not re-open the
            // account gate.
            return;
        }
        if (code == QLatin1String("device_revoked") ||
            code == QLatin1String("device_stale")) {
            // A revoked device can no longer own a lease or hold material.
            setLeaseId(QString());
            clearStoredConnectMaterial();
            if (!m_deletionRequestedAt.isEmpty()) {
                // The revocation is the deletion grace period, not a lost
                // device: keep the dedicated restore gate.
                setState(QStringLiteral("deletion_pending"));
            }
            else {
                setState(QStringLiteral("revoked"));
            }
        }
        else if (code == QLatin1String("unknown_device")) {
            if (m_vault != nullptr) {
                m_vault->remove(QString::fromLatin1(kAccountVaultKey));
            }
            setLeaseId(QString());
            clearStoredConnectMaterial();
            setHandle(QString());
            setOnboardingStep(QString());
            setState(QStringLiteral("no_account"));
        }
        else if (AccountClient::isTerminalAuthError(code)) {
            // Terminal authentication loss (expired/revoked session, missing
            // authentication). The tokens are already gone; the device key is
            // kept so the user can log in again or recover. Never stay in
            // signed_in without tokens. Without a known account (a late reply
            // after sign-out) this is simply "no account".
            setLeaseId(QString());
            clearStoredConnectMaterial();
            setBusy(false);
            setState(m_handle.isEmpty() ? QStringLiteral("no_account")
                                        : QStringLiteral("session_expired"));
        }
        else {
            // Any other refresh failure also left the client without tokens.
            // A transport failure is shown as offline; everything else is a
            // dead session and must not keep the app in signed_in.
            setLeaseId(QString());
            clearStoredConnectMaterial();
            setBusy(false);
            if (m_handle.isEmpty()) {
                setState(QStringLiteral("no_account"));
            }
            else if (code == QLatin1String("network_error") ||
                     code == QLatin1String("timeout")) {
                setState(QStringLiteral("offline"));
            }
            else {
                setState(QStringLiteral("session_expired"));
            }
        }
    });

    connect(m_client, &AccountClient::refreshTokenPersistFailed, this,
            [](const QString &code) {
                qWarning() << "Account: cannot persist the refresh token:" << code;
            });

    if (!m_vault->isAvailable()) {
        // Account actions stop here; the public catalog keeps working because
        // it needs no vault and no token.
        setState(QStringLiteral("vault_unavailable"));
        return;
    }

    loadAccountRecord();
    m_pendingPaymentId = loadStoredPendingIntent();
    // Recover the one-shot connect material from a previous run, if it is
    // still inside the lease token TTL. loadConnectMaterial() clears expired or
    // malformed records and updates connectPending.
    loadConnectMaterial();

    QString errorCode;
    m_deviceKey = DeviceKey::load(m_vault, &errorCode);
    if (m_deviceKey != nullptr) {
        m_client->setDeviceKey(m_deviceKey);
        qInfo() << "Account: device key loaded";
    }
    else if (!errorCode.isEmpty()) {
        qWarning() << "Account: device key unavailable on startup:" << errorCode;
        m_lastErrorCode = errorCode;
        m_lastError = QStringLiteral("Ключ устройства не загрузился (%1).").arg(errorCode);
        emit lastErrorChanged();
    }

    const bool haveRefresh = m_client->restoreRefreshToken();
    qInfo() << "Account: refresh token restored:" << haveRefresh;
    if (haveRefresh || m_deviceKey != nullptr) {
        // Keep the onboarding screen hidden while the stored key/token are
        // validated. A brief no_account state would look like a lost account.
        setState(QStringLiteral("checking"));
        signInAfterAuth();
    }
    else {
        setState(QStringLiteral("no_account"));
    }
}

bool LegionApi::ensureClient(const QString &op)
{
    if (m_client != nullptr) {
        return true;
    }
    fail(op, QStringLiteral("not_initialized"),
         QStringLiteral("Приложение не готово. Перезапустите его."));
    return false;
}

bool LegionApi::ensureVault(const QString &op)
{
    if (m_vault != nullptr && m_vault->isAvailable()) {
        return true;
    }
    setState(QStringLiteral("vault_unavailable"));
    fail(op, QStringLiteral("vault_unavailable"), Vault::errorMessageFor(
                                                       QStringLiteral("vault_unavailable")));
    return false;
}

bool LegionApi::ensureAccount(const QString &op)
{
    // Every account capability needs both the API client and the vault (the
    // device key and refresh token live there). The public catalog deliberately
    // does not use this guard.
    if (!ensureVault(op)) {
        return false;
    }
    return ensureClient(op);
}

bool LegionApi::ensureDeviceKey(const QString &op, bool create)
{
    if (m_deviceKey != nullptr) {
        m_client->setDeviceKey(m_deviceKey);
        return true;
    }

    QString errorCode;
    if (create) {
        bool created = false;
        // A corrupt key may be replaced when there is no local account, or when
        // the app is already on the no-account screen: otherwise the user is
        // trapped between "create account" and the unreadable blob.
        const bool allowReplaceCorrupt =
            m_handle.isEmpty() || m_state == QLatin1String("no_account");
        m_deviceKey = DeviceKey::loadOrCreate(m_vault, &created, &errorCode,
                                               allowReplaceCorrupt);
        if (created) {
            qInfo() << "Account: generated a new P-256 device key";
        }
    }
    else {
        m_deviceKey = DeviceKey::load(m_vault, &errorCode);
    }

    if (m_deviceKey == nullptr) {
        qWarning() << "Account: device key unavailable" << op << "create=" << create
                   << "code=" << errorCode;
        const QString message =
            errorCode == QLatin1String("vault_unavailable")
                ? Vault::errorMessageFor(errorCode)
                : (create ? QStringLiteral("Не удалось создать ключ устройства (%1).")
                                .arg(errorCode)
                          : QStringLiteral("Ключ устройства не найден. Восстановите доступ по словам."));
        fail(op, errorCode.isEmpty() ? QStringLiteral("device_key_missing") : errorCode,
             message);
        return false;
    }

    m_client->setDeviceKey(m_deviceKey);
    return true;
}

void LegionApi::replaceDeviceKey(DeviceKey *newKey)
{
    if (newKey == nullptr || newKey == m_deviceKey) {
        return;
    }

    if (m_client != nullptr) {
        m_client->setDeviceKey(nullptr);
    }
    delete m_deviceKey;
    m_deviceKey = newKey;

    if (m_vault != nullptr && m_vault->isAvailable()) {
        const QByteArray der = m_deviceKey->toPkcs8();
        if (der.isEmpty()) {
            qWarning() << "Account: recovered device key cannot be encoded";
        }
        else if (!m_vault->save(DeviceKey::vaultKey(), der)) {
            qWarning() << "Account: cannot persist the recovered device key"
                       << m_vault->lastErrorCode();
        }
    }

    if (m_client != nullptr) {
        m_client->setDeviceKey(m_deviceKey);
    }
}

void LegionApi::setState(const QString &state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    if (state == QLatin1String("signed_in")) {
        startQueueEvents();
    }
    else {
        stopQueueEvents();
    }
    emit stateChanged();
}

void LegionApi::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void LegionApi::startQueueEvents()
{
    if (m_queueEvents == nullptr || m_client == nullptr) {
        return;
    }
    if (!m_client->hasAccessToken()) {
        return;
    }
    m_queueEvents->start(m_client->accessToken());
}

void LegionApi::stopQueueEvents()
{
    if (m_queueEvents != nullptr) {
        m_queueEvents->stop();
    }
}

void LegionApi::setLeaseId(const QString &leaseId)
{
    const QString normalized = leaseId.trimmed();
    if (m_leaseId == normalized) {
        return;
    }
    m_leaseId = normalized;
    // A different/empty lease invalidates the previous health token. The new
    // lease is started explicitly after /connect or /sessions/current.
    stopHealthAgent();
    emit leaseIdChanged();
}

void LegionApi::startHealthAgent(const QJsonObject &payload, const QString &leaseId)
{
    if (m_healthAgent == nullptr) {
        return;
    }

    const QString lease = leaseId.trimmed();
    if (lease.isEmpty()) {
        stopHealthAgent();
        return;
    }

    const QString token = healthTokenFromJson(payload);
    if (token.isEmpty()) {
        // Old /connect payloads do not carry a health token. Keep the agent
        // idle; the next GET /sessions/current reissue starts it.
        if (m_healthAgent->isActive() && m_healthAgent->leaseId() != lease) {
            stopHealthAgent();
        }
        return;
    }

    const bool alreadyReporting =
        m_healthAgent->isActive() && m_healthAgent->leaseId() == lease;
    const bool sameLease = m_healthAgent->leaseId() == lease;
    if (!alreadyReporting && m_healthAgent->isActive()) {
        stopHealthAgent();
    }

    m_healthAgent->configure(QUrl(AccountClient::apiBaseUrl()), lease,
                             token.toUtf8(), AccountClient::clientVersion(),
                             platformOs());
    if (alreadyReporting) {
        // Same lease: only refresh the token. Turning every /sessions/current
        // poll into an extra report would defeat the server cadence and hit
        // the per-lease rate limit.
        return;
    }
    if (!sameLease) {
        // A new lease starts from a clean stream projection: no ended/stalled
        // state or frame age from the previous lease may leak into the first
        // sample. Restarting the same lease after a transport failure keeps
        // the live stream state and frame clock.
        SessionHealthAgent::resetFrameClock();
        SessionHealthAgent::clearSdlNotices();
        m_healthAgent->setStreamState(QStringLiteral("idle"), false, 0, 0,
                                      false, false, QString(), QString());
    }
    const int nextMs = healthNextReportMs(payload);
    if (nextMs > 0) {
        m_healthAgent->setInitialReportDelayMs(nextMs);
    }
    m_healthAgent->start();
}

void LegionApi::stopHealthAgent()
{
    if (m_healthAgent != nullptr) {
        m_healthAgent->stop();
    }
}

void LegionApi::setHealthPhase(const QString &phase)
{
    if (m_healthAgent != nullptr) {
        m_healthAgent->setPhase(phase);
    }
}

void LegionApi::reportStreamStarted()
{
    if (m_healthAgent == nullptr) {
        return;
    }
    // A new stream must not inherit the previous stream's frame clock.
    SessionHealthAgent::resetFrameClock();
    m_healthAgent->setPhaseStream(QStringLiteral("streaming"),
                                  QStringLiteral("streaming"), false);
    m_healthAgent->submitReport();
}

void LegionApi::reportStreamReconnecting()
{
    if (m_healthAgent == nullptr) {
        return;
    }
    m_healthAgent->setPhaseStream(QStringLiteral("reconnecting"),
                                  QStringLiteral("reconnecting"), false);
    m_healthAgent->submitReport();
}

void LegionApi::reportFirstFrame()
{
    if (m_healthAgent == nullptr) {
        return;
    }
    SessionHealthAgent::noteFrameProduced();
    m_healthAgent->setPhaseStream(QStringLiteral("streaming"),
                                  QStringLiteral("streaming"), true);
    m_healthAgent->submitReport();
}

void LegionApi::reportStreamEnded()
{
    if (m_healthAgent == nullptr) {
        return;
    }
    m_healthAgent->setPhaseStream(QStringLiteral("stopping"),
                                  QStringLiteral("ended"), false);
    m_healthAgent->submitReport();
}

QString LegionApi::healthTokenFromJson(const QJsonObject &json)
{
    QString token = json.value(QStringLiteral("health_token")).toString().trimmed();
    if (!token.isEmpty()) {
        return token;
    }

    // Accept the token one level down so /connect, /reconnect and
    // /sessions/current can evolve their envelope without breaking the app.
    const QJsonObject health = json.value(QStringLiteral("health")).toObject();
    token = health.value(QStringLiteral("health_token")).toString().trimmed();
    if (!token.isEmpty()) {
        return token;
    }
    const QJsonObject material =
        json.value(QStringLiteral("connect_material")).toObject();
    token = material.value(QStringLiteral("health_token")).toString().trimmed();
    if (!token.isEmpty()) {
        return token;
    }
    const QJsonObject session = json.value(QStringLiteral("session")).toObject();
    return session.value(QStringLiteral("health_token")).toString().trimmed();
}

int LegionApi::healthNextReportMs(const QJsonObject &json)
{
    int nextMs = json.value(QStringLiteral("next_report_in_ms")).toInt(0);
    if (nextMs > 0) {
        return nextMs;
    }
    const QJsonObject health = json.value(QStringLiteral("health")).toObject();
    nextMs = health.value(QStringLiteral("next_report_in_ms")).toInt(0);
    if (nextMs > 0) {
        return nextMs;
    }
    const QJsonObject session = json.value(QStringLiteral("session")).toObject();
    return session.value(QStringLiteral("next_report_in_ms")).toInt(0);
}

QVariantMap LegionApi::takeSessionNotice()
{
    // The agent emits sessionHealthNotice asynchronously; the SDL loop uses the
    // separate static slot in SessionHealthAgent. This pull path stays for QML.
    return m_healthAgent != nullptr ? m_healthAgent->takeNotice()
                                    : QVariantMap();
}

void LegionApi::setConnectPending(bool pending)
{
    if (m_connectPending == pending) {
        return;
    }
    m_connectPending = pending;
    emit connectPendingChanged();
}

void LegionApi::persistConnectMaterial(const QString &sessionId, const QJsonObject &payload)
{
    if (sessionId.isEmpty() || payload.isEmpty() ||
            m_vault == nullptr || !m_vault->isAvailable()) {
        setConnectPending(false);
        return;
    }

    QJsonObject record;
    record.insert(QStringLiteral("v"), 1);
    record.insert(QStringLiteral("session_id"), sessionId);
    record.insert(QStringLiteral("issued_at"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    record.insert(QStringLiteral("payload"), payload);

    if (!m_vault->save(QString::fromLatin1(kConnectVaultKey),
                       QJsonDocument(record).toJson(QJsonDocument::Compact))) {
        // Without a vault the in-flight connect still works, but a page change
        // or restart loses the one-shot material. Surface it instead of
        // pretending it is durable.
        qWarning() << "Account: cannot persist connect material"
                   << m_vault->lastErrorCode();
        setConnectPending(false);
        return;
    }
    setConnectPending(true);
}

QJsonObject LegionApi::loadConnectMaterial()
{
    if (m_vault == nullptr || !m_vault->isAvailable()) {
        setConnectPending(false);
        return QJsonObject();
    }

    bool ok = false;
    const QByteArray blob = m_vault->load(QString::fromLatin1(kConnectVaultKey), &ok);
    if (!ok || blob.isEmpty()) {
        setConnectPending(false);
        return QJsonObject();
    }

    const QJsonObject record = QJsonDocument::fromJson(blob).object();
    const QJsonObject payload = record.value(QStringLiteral("payload")).toObject();
    const QString sessionId =
        record.value(QStringLiteral("session_id")).toString().trimmed();
    const QDateTime issuedAt = QDateTime::fromString(
        record.value(QStringLiteral("issued_at")).toString(), Qt::ISODate);

    const bool fresh = issuedAt.isValid() &&
        issuedAt.secsTo(QDateTime::currentDateTimeUtc()) <= kConnectMaterialMaxAgeSeconds;

    if (sessionId.isEmpty() || payload.isEmpty() || !fresh) {
        // Expired, malformed or written by another schema version: drop it so
        // a stale setup key/token never lingers in the vault.
        clearStoredConnectMaterial();
        return QJsonObject();
    }

    setConnectPending(true);
    return record;
}

void LegionApi::clearStoredConnectMaterial()
{
    if (m_vault != nullptr && m_vault->isAvailable()) {
        m_vault->remove(QString::fromLatin1(kConnectVaultKey));
    }
    setConnectPending(false);
}

bool LegionApi::resumeConnectMaterial(const QString &expectedSessionId)
{
    if (m_logoutInProgress || m_state != QLatin1String("signed_in")) {
        // Never hand one-shot material to the network layer outside an active
        // account session.
        return false;
    }

    const QJsonObject record = loadConnectMaterial();
    const QString sessionId = record.value(QStringLiteral("session_id")).toString().trimmed();
    const QJsonObject payload = record.value(QStringLiteral("payload")).toObject();
    if (sessionId.isEmpty() || payload.isEmpty()) {
        return false;
    }

    const QString expected = expectedSessionId.trimmed();
    if (!expected.isEmpty() && expected != sessionId) {
        // The material belongs to another lease (or a session that already
        // ended). Never apply it to the current one.
        clearStoredConnectMaterial();
        return false;
    }

    setLeaseId(sessionId);
    setHealthPhase(QStringLiteral("connecting"));
    startHealthAgent(payload, sessionId);
    emit connectMaterialReady(sessionId, payload.toVariantMap());
    return true;
}

void LegionApi::clearConnectMaterial()
{
    clearStoredConnectMaterial();
}

void LegionApi::clearError()
{
    if (m_lastError.isEmpty() && m_lastErrorCode.isEmpty()) {
        return;
    }
    m_lastError.clear();
    m_lastErrorCode.clear();
    emit lastErrorChanged();
}

void LegionApi::fail(const QString &op, const QString &code, const QString &message,
                     std::function<void()> retry)
{
    setBusy(false);
    m_lastErrorCode = code;
    m_lastError = message;
    m_retry = std::move(retry);
    emit lastErrorChanged();
    emit failed(op, code, message);
}

void LegionApi::failFromResult(const QString &op, const ApiResult &result,
                               std::function<void()> retry)
{
    if (m_logoutInProgress) {
        // A sign-out is in progress; the resulting state is no_account, not a
        // terminal-auth or offline gate. Still report the failed operation.
        fail(op,
             result.errorCode.isEmpty() ? QStringLiteral("server_error") : result.errorCode,
             result.message.isEmpty() ? QStringLiteral("Ошибка запроса. Попробуйте ещё раз.")
                                      : result.message,
             std::move(retry));
        return;
    }

    if (AccountClient::isTerminalAuthError(result.errorCode)) {
        // One reducer for terminal auth: no tokens, no lease, no material, and
        // a dedicated state that the shell turns into a re-login/recovery gate.
        if (m_client != nullptr) {
            m_client->clearTokens();
        }
        setLeaseId(QString());
        clearStoredConnectMaterial();
        setState(m_handle.isEmpty() ? QStringLiteral("no_account")
                                    : QStringLiteral("session_expired"));
    }
    else if (result.errorCode == QLatin1String("device_revoked") ||
        result.errorCode == QLatin1String("device_stale")) {
        if (m_client != nullptr) {
            m_client->clearTokens();
        }
        setLeaseId(QString());
        clearStoredConnectMaterial();
        setState(!m_deletionRequestedAt.isEmpty() ? QStringLiteral("deletion_pending")
                                                  : QStringLiteral("revoked"));
    }
    else if (result.errorCode == QLatin1String("account_deletion_pending")) {
        setState(QStringLiteral("deletion_pending"));
    }
    else if (result.errorCode == QLatin1String("consent_version_mismatch") ||
             result.errorCode == QLatin1String("consent_required")) {
        setState(QStringLiteral("consent"));
        emit consentRequired();
    }
    else if (result.networkError) {
        setState(QStringLiteral("offline"));
    }

    fail(op,
         result.errorCode.isEmpty() ? QStringLiteral("server_error") : result.errorCode,
         result.message.isEmpty() ? QStringLiteral("Ошибка запроса. Попробуйте ещё раз.")
                                  : result.message,
         std::move(retry));
}

void LegionApi::silentFailure(const QString &code, const QString &message)
{
    m_lastErrorCode = code;
    m_lastError = message;
    emit lastErrorChanged();
}

QJsonObject LegionApi::loadVaultObject(const QString &key, bool *ok) const
{
    if (ok) {
        *ok = false;
    }
    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return QJsonObject();
    }

    auto parse = [](const QByteArray &bytes, QJsonObject *object) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            document.object().isEmpty()) {
            return false;
        }
        *object = document.object();
        return true;
    };

    bool loaded = false;
    QJsonObject object;
    const QByteArray primary = m_vault->load(key, &loaded);
    if (loaded && parse(primary, &object)) {
        if (ok) {
            *ok = true;
        }
        return object;
    }

    bool backupLoaded = false;
    const QByteArray backup = m_vault->load(key + QStringLiteral(".bak"), &backupLoaded);
    if (backupLoaded && parse(backup, &object)) {
        qWarning() << "Account: recovered JSON vault record" << key;
        if (ok) {
            *ok = true;
        }
        return object;
    }

    return QJsonObject();
}

void LegionApi::loadAccountRecord()
{
    bool ok = false;
    const QJsonObject record = loadVaultObject(QString::fromLatin1(kAccountVaultKey), &ok);
    if (!ok) {
        return;
    }
    setHandle(record.value(QStringLiteral("handle")).toString());
    setOnboardingStep(record.value(QStringLiteral("onboarding_step")).toString());
    setDeletionState(record.value(QStringLiteral("deletion_requested_at")).toString(),
                     record.value(QStringLiteral("purge_after")).toString());
}

void LegionApi::persistAccount()
{
    if (m_vault == nullptr || !m_vault->isAvailable() || m_handle.isEmpty()) {
        return;
    }

    QJsonObject record;
    record.insert(QStringLiteral("v"), 1);
    record.insert(QStringLiteral("handle"), m_handle);
    record.insert(QStringLiteral("onboarding_step"), m_onboardingStep);
    record.insert(QStringLiteral("deletion_requested_at"),
                  m_deletionRequestedAt.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                  : QJsonValue(m_deletionRequestedAt));
    record.insert(QStringLiteral("purge_after"),
                  m_purgeAfter.isEmpty() ? QJsonValue(QJsonValue::Null)
                                         : QJsonValue(m_purgeAfter));
    record.insert(QStringLiteral("saved_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!m_vault->save(QString::fromLatin1(kAccountVaultKey),
                       QJsonDocument(record).toJson(QJsonDocument::Compact))) {
        qWarning() << "Account: cannot persist the account record"
                   << m_vault->lastErrorCode();
    }
}

QString LegionApi::installPath() const
{
    return QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
}

QString LegionApi::recoveryKitPath() const
{
    if (!m_recoveryKitPath.isEmpty()) {
        return m_recoveryKitPath;
    }
    if (!m_handle.isEmpty()) {
        return QDir::toNativeSeparators(RecoveryKit::suggestedPath(m_handle));
    }
    return QString();
}

void LegionApi::setHandle(const QString &handle)
{
    if (m_handle == handle) {
        return;
    }
    m_handle = handle;
    emit handleChanged();
}

void LegionApi::setOnboardingStep(const QString &step)
{
    if (m_onboardingStep == step) {
        return;
    }
    m_onboardingStep = step;
    emit onboardingStepChanged();
}

void LegionApi::setDeletionState(const QString &requestedAt, const QString &purgeAfter)
{
    if (m_deletionRequestedAt == requestedAt && m_purgeAfter == purgeAfter) {
        return;
    }
    m_deletionRequestedAt = requestedAt;
    m_purgeAfter = purgeAfter;
    emit deletionStateChanged();
}

void LegionApi::applyDevice(const QJsonObject &device)
{
    if (device.isEmpty()) {
        return;
    }

    QVariantMap map;
    map.insert(QStringLiteral("id"),
               device.value(QStringLiteral("id"))
                   .toString(device.value(QStringLiteral("device_id")).toString()));
    map.insert(QStringLiteral("label"),
               device.value(QStringLiteral("label")).toString());
    map.insert(QStringLiteral("os"), device.value(QStringLiteral("os")).toString());
    map.insert(QStringLiteral("app_version"),
               device.value(QStringLiteral("app_version"))
                   .toString(device.value(QStringLiteral("version")).toString()));
    map.insert(QStringLiteral("kind"),
               device.value(QStringLiteral("kind")).toString(QStringLiteral("app")));

    if (map != m_device) {
        m_device = map;
        emit deviceChanged();
    }
}

void LegionApi::emitLegalDocuments()
{
    QVariantList list;
    for (const LegalDocumentInfo &document : m_documents) {
        QVariantMap entry;
        entry.insert(QStringLiteral("document_id"), document.documentId);
        entry.insert(QStringLiteral("version"), document.version);
        entry.insert(QStringLiteral("sha256"), document.sha256);
        entry.insert(QStringLiteral("url"), document.url);
        entry.insert(QStringLiteral("locale"), document.locale);
        list.append(entry);
    }

    if (list != m_legalDocuments) {
        m_legalDocuments = list;
    }
    emit legalDocumentsChanged();

    QVariantMap data;
    data.insert(QStringLiteral("documents"), m_legalDocuments);
    data.insert(QStringLiteral("age_notice"), m_ageNotice);
    emit response(QStringLiteral("legal_documents"), data);
}

void LegionApi::storePendingIntent(const QString &publicId)
{
    m_pendingPaymentId = publicId;

    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return;
    }

    QJsonObject record;
    record.insert(QStringLiteral("v"), 1);
    record.insert(QStringLiteral("public_id"), publicId);
    record.insert(QStringLiteral("saved_at"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!m_vault->save(QString::fromLatin1(kPendingIntentVaultKey),
                       QJsonDocument(record).toJson(QJsonDocument::Compact))) {
        qWarning() << "Account: cannot persist the pending payment intent"
                   << m_vault->lastErrorCode();
    }
}

QString LegionApi::loadStoredPendingIntent() const
{
    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return QString();
    }

    bool ok = false;
    const QJsonObject record =
        loadVaultObject(QString::fromLatin1(kPendingIntentVaultKey), &ok);
    if (!ok) {
        return QString();
    }

    const QString publicId = record.value(QStringLiteral("public_id")).toString();
    if (publicId.isEmpty()) {
        return QString();
    }

    const QDateTime savedAt = QDateTime::fromString(
        record.value(QStringLiteral("saved_at")).toString(), Qt::ISODate);
    if (savedAt.isValid() &&
        savedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc()) > 3600) {
        return QString();
    }

    return publicId;
}

void LegionApi::clearPendingIntent()
{
    m_pendingPaymentId.clear();
    m_emittedPaymentId.clear();

    if (m_vault != nullptr && m_vault->isAvailable()) {
        m_vault->remove(QString::fromLatin1(kPendingIntentVaultKey));
    }
}

void LegionApi::storeConsentRecord(const QJsonObject &payload)
{
    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return;
    }

    QJsonObject record;
    record.insert(QStringLiteral("v"), 1);
    record.insert(QStringLiteral("agreement"), true);
    record.insert(QStringLiteral("pdn_consent"), true);
    record.insert(QStringLiteral("accepted_at"),
                  payload.value(QStringLiteral("client_accepted_at")).toString());
    record.insert(QStringLiteral("documents"), payload.value(QStringLiteral("documents")));

    if (!m_vault->save(QString::fromLatin1(kConsentVaultKey),
                       QJsonDocument(record).toJson(QJsonDocument::Compact))) {
        qWarning() << "Account: cannot persist the consent record"
                   << m_vault->lastErrorCode();
    }
}

void LegionApi::storeWords(const QStringList &words)
{
    m_words.clear();
    for (const QString &word : words) {
        m_words.append(word);
    }
    emit wordsChanged();

    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return;
    }

    QJsonObject record;
    record.insert(QStringLiteral("v"), 1);
    record.insert(QStringLiteral("handle"), m_handle);
    record.insert(QStringLiteral("words"), toJsonArray(words));
    record.insert(QStringLiteral("saved_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!m_vault->save(QString::fromLatin1(kWordsVaultKey),
                       QJsonDocument(record).toJson(QJsonDocument::Compact))) {
        qWarning() << "Account: cannot persist the recovery words"
                   << m_vault->lastErrorCode();
    }
}

QStringList LegionApi::storedWords() const
{
    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return QStringList();
    }

    bool ok = false;
    const QJsonObject record = loadVaultObject(QString::fromLatin1(kWordsVaultKey), &ok);
    if (!ok) {
        return QStringList();
    }

    QStringList words;
    for (const QJsonValue &value : record.value(QStringLiteral("words")).toArray()) {
        words.append(value.toString());
    }
    return words;
}

void LegionApi::writeRecoveryKit(const QStringList &words)
{
    if (words.size() != 8 || m_handle.isEmpty()) {
        return;
    }

    const RecoveryKit::Result kit = RecoveryKit::write(
        m_handle, words, deviceLabel(QString()), QDateTime::currentDateTime());

    if (!kit.ok) {
        m_lastErrorCode = kit.errorCode;
        m_lastError = kit.message;
        emit lastErrorChanged();
        emit failed(QStringLiteral("kit"), kit.errorCode, kit.message);
        return;
    }

    m_recoveryKitPath = QDir::toNativeSeparators(kit.path);
    emit recoveryKitPathChanged();
    emit recoveryKitWritten(kit.path, kit.syncedFolder);
}

void LegionApi::applyMe(const QJsonObject &json)
{
    clearError();
    const QJsonObject account = json.value(QStringLiteral("account")).toObject();
    if (!account.isEmpty()) {
        const QString handle = account.value(QStringLiteral("handle")).toString();
        if (!handle.isEmpty()) {
            setHandle(handle);
        }
    }

    const QString step = json.value(QStringLiteral("onboarding_step")).toString();
    if (!step.isEmpty()) {
        setOnboardingStep(step);
    }
    applyDevice(json.value(QStringLiteral("device")).toObject());

    const QString deletion = json.value(QStringLiteral("deletion_requested_at")).toString();
    const QString purgeAfter = json.value(QStringLiteral("purge_after")).toString();
    setDeletionState(deletion, purgeAfter);
    persistAccount();
    emit response(QStringLiteral("me"), json.toVariantMap());

    if (!deletion.isEmpty()) {
        setState(QStringLiteral("deletion_pending"));
        return;
    }

    const bool consentCurrent = json.value(QStringLiteral("consent_current")).toBool(true);
    if (!consentCurrent) {
        setState(QStringLiteral("consent"));
        emit consentRequired();
        return;
    }

    if (m_onboardingStep == QLatin1String("words")) {
        const QStringList words = storedWords();
        if (!words.isEmpty()) {
            storeWords(words);
            emit wordsReady(m_handle, words);
        }
        setState(QStringLiteral("onboarding"));
        return;
    }

    setState(QStringLiteral("signed_in"));

    if (!m_pendingPaymentId.isEmpty() && m_client != nullptr) {
        const QString pending = m_pendingPaymentId;
        m_pendingPaymentId.clear();
        notifyPaymentReturn(pending);
    }
}

void LegionApi::applyAuthResponse(const QString &op, const ApiResult &result,
                                  DeviceKey *adoptedKey)
{
    clearError();
    const QJsonObject json = result.json;

    // A successful register/login consumes any onboarding referral code. It is
    // never attached to a later re-consent or recovery ceremony.
    Consent::setPendingReferralCode(QString());
    if (!m_referralCode.isEmpty()) {
        m_referralCode.clear();
        emit referralCodeChanged();
    }

    if (adoptedKey != nullptr) {
        replaceDeviceKey(adoptedKey);
    }
    if (m_deviceKey != nullptr) {
        m_client->setDeviceKey(m_deviceKey);
    }

    const QByteArray access = json.value(QStringLiteral("access_token")).toString().toUtf8();
    const QByteArray refresh = json.value(QStringLiteral("refresh_token")).toString().toUtf8();
    const int expiresIn =
        static_cast<int>(json.value(QStringLiteral("expires_in")).toDouble(900));
    const int refreshExpiresIn =
        static_cast<int>(json.value(QStringLiteral("refresh_expires_in")).toDouble(14 * 24 * 60 * 60));

    if (!access.isEmpty()) {
        m_client->setTokens(access, refresh, expiresIn, refreshExpiresIn);
    }

    const QString responseHandle = json.value(QStringLiteral("handle")).toString();
    if (!responseHandle.isEmpty()) {
        setHandle(responseHandle);
    }

    const QString step = json.value(QStringLiteral("onboarding_step")).toString();
    if (!step.isEmpty()) {
        setOnboardingStep(step);
    }

    const QStringList words = wordsFromJson(json);
    if (!words.isEmpty()) {
        storeWords(words);
        writeRecoveryKit(words);
        emit wordsReady(m_handle, words);
    }

    persistAccount();
    applyDevice(json.value(QStringLiteral("device")).toObject());

    QVariantMap opData;
    opData.insert(QStringLiteral("handle"), m_handle);
    opData.insert(QStringLiteral("onboarding_step"), m_onboardingStep);
    opData.insert(QStringLiteral("device"), m_device);
    emit response(op, opData);

    setBusy(false);

    if (words.isEmpty() && m_onboardingStep == QLatin1String("words")) {
        // Lost registration/login response: the server still has the words in
        // the onboarding window, so ask for a one-time reissue.
        setState(QStringLiteral("onboarding"));
        reissueWords();
        return;
    }

    if (!words.isEmpty()) {
        setState(QStringLiteral("onboarding"));
    }
    else if (m_onboardingStep == QLatin1String("words")) {
        setState(QStringLiteral("onboarding"));
    }
    else {
        // login/complete does not return the account snapshot; fetch /me so
        // the handle, consent freshness and onboarding step are authoritative.
        refreshMe();
    }

    if (!m_pendingPaymentId.isEmpty()) {
        const QString pending = m_pendingPaymentId;
        m_pendingPaymentId.clear();
        notifyPaymentReturn(pending);
    }
}

void LegionApi::handleAuthFailure(const QString &op, const ApiResult &result, bool silent)
{
    if (result.networkError) {
        setState(QStringLiteral("offline"));
        if (silent) {
            silentFailure(result.errorCode, result.message);
        }
        else {
            failFromResult(op, result);
        }
        return;
    }

    if (result.errorCode == QLatin1String("device_revoked") ||
        result.errorCode == QLatin1String("device_stale")) {
        if (m_client != nullptr) {
            m_client->clearTokens();
        }
        setLeaseId(QString());
        clearStoredConnectMaterial();
        if (!m_deletionRequestedAt.isEmpty()) {
            setState(QStringLiteral("deletion_pending"));
        }
        else {
            setState(QStringLiteral("revoked"));
        }
        if (!silent) {
            fail(op, result.errorCode, result.message);
        }
        return;
    }

    if (result.errorCode == QLatin1String("unknown_device")) {
        if (m_vault != nullptr) {
            m_vault->remove(QString::fromLatin1(kAccountVaultKey));
        }
        setLeaseId(QString());
        clearStoredConnectMaterial();
        setHandle(QString());
        setOnboardingStep(QString());
        setState(QStringLiteral("no_account"));
        if (!silent) {
            fail(op, result.errorCode, result.message);
        }
        return;
    }

    if (AccountClient::isTerminalAuthError(result.errorCode)) {
        if (m_client != nullptr) {
            m_client->clearTokens();
        }
        setLeaseId(QString());
        clearStoredConnectMaterial();
        if (m_handle.isEmpty()) {
            setState(QStringLiteral("no_account"));
        }
        else {
            setState(QStringLiteral("session_expired"));
        }
        if (!silent) {
            fail(op, result.errorCode, result.message);
        }
        else {
            silentFailure(result.errorCode, result.message);
        }
        return;
    }

    if (silent) {
        silentFailure(result.errorCode, result.message);
        if (result.errorCode == QLatin1String("invalid_signature") ||
            result.errorCode == QLatin1String("key_revoked") ||
            result.errorCode == QLatin1String("invalid_or_expired_challenge")) {
            // The stored device key can no longer sign a ceremony. The account
            // may still exist, so offer login/recovery instead of pretending
            // there is no account.
            setState(m_handle.isEmpty() ? QStringLiteral("no_account")
                                        : QStringLiteral("session_expired"));
        }
        return;
    }

    failFromResult(op, result);
}

void LegionApi::signInAfterAuth()
{
    if (m_client == nullptr) {
        return;
    }
    if (m_deviceKey == nullptr) {
        setState(QStringLiteral("no_account"));
        return;
    }

    setBusy(true);
    m_client->get(QStringLiteral("/me"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (result.ok) {
                          setBusy(false);
                          applyMe(result.json);
                          return;
                      }

                      if (result.networkError) {
                          setBusy(false);
                          setState(QStringLiteral("offline"));
                          silentFailure(result.errorCode, result.message);
                          return;
                      }

                      if (result.errorCode == QLatin1String("account_deletion_pending")) {
                          setBusy(false);
                          setState(QStringLiteral("deletion_pending"));
                          return;
                      }
                      if ((result.errorCode == QLatin1String("device_revoked") ||
                           result.errorCode == QLatin1String("device_stale")) &&
                              !m_deletionRequestedAt.isEmpty()) {
                          setBusy(false);
                          setState(QStringLiteral("deletion_pending"));
                          return;
                      }

                      // Access/refresh rejected: fall back to the silent login
                      // ceremony with the stored key.
                      performLogin(true);
                  });
}

void LegionApi::performLogin(bool silent)
{
    if (!ensureClient(QStringLiteral("login")) || m_deviceKey == nullptr) {
        return;
    }

    setBusy(true);

    QJsonObject beginBody;
    beginBody.insert(QStringLiteral("public_key_jwk"), m_deviceKey->publicJwk());

    m_client->post(QStringLiteral("/auth/login/begin"), beginBody, false,
                   CryptoUtil::randomUuid(), [this, silent](const ApiResult &begin) {
                       if (!begin.ok) {
                           setBusy(false);
                           handleAuthFailure(QStringLiteral("login"), begin, silent);
                           return;
                       }

                       const QString ceremonyId =
                           begin.json.value(QStringLiteral("ceremony_id")).toString();
                       const QString challenge =
                           begin.json.value(QStringLiteral("challenge")).toString();
                       if (ceremonyId.isEmpty() || challenge.isEmpty()) {
                           setBusy(false);
                           fail(QStringLiteral("login"), QStringLiteral("invalid_response"),
                                QStringLiteral("Сервер вернул неверный ответ. Повторите попытку."));
                           return;
                       }
                       const QByteArray signature =
                           signCeremony(ceremonyId, challenge, QStringLiteral("login"));
                       if (signature.size() != 64) {
                           setBusy(false);
                           fail(QStringLiteral("login"), QStringLiteral("proof_sign_failed"),
                                QStringLiteral("Не удалось подписать вход."));
                           return;
                       }

                       QJsonObject complete;
                       complete.insert(QStringLiteral("ceremony_id"), ceremonyId);
                       complete.insert(QStringLiteral("signature"),
                                       QString::fromLatin1(CryptoUtil::base64UrlEncode(signature)));

                       m_client->post(QStringLiteral("/auth/login/complete"), complete, false,
                                      CryptoUtil::randomUuid(),
                                      [this, silent](const ApiResult &result) {
                                          if (!result.ok) {
                                              setBusy(false);
                                              handleAuthFailure(QStringLiteral("login"), result,
                                                                silent);
                                              return;
                                          }
                                          applyAuthResponse(QStringLiteral("login"), result);
                                      });
                   });
}

void LegionApi::recoverLostRegistration()
{
    // The account exists server-side but the registration response was lost.
    // The key is still in the vault, so a login ceremony restores the session
    // and reissueWords() fetches the words while the onboarding window is open.
    performLogin(true);
}

void LegionApi::setConsent(bool agreement, bool pdnConsent)
{
    m_agreement = agreement;
    m_pdnConsent = pdnConsent;

    if (!agreement || !pdnConsent) {
        setState(QStringLiteral("consent"));
        emit consentRequired();
        return;
    }

    // Re-consent for an existing account whose documents changed. During
    // registration there is no token yet, so this does not fire.
    if (m_client != nullptr && m_client->hasAccessToken() && !m_handle.isEmpty()) {
        submitConsent();
    }
}

void LegionApi::submitConsent()
{
    if (!ensureAccount(QStringLiteral("consent"))) {
        return;
    }
    // Re-consent is never a referral attribution surface.
    Consent::setPendingReferralCode(QString());
    if (!m_agreement || !m_pdnConsent) {
        setState(QStringLiteral("consent"));
        emit consentRequired();
        fail(QStringLiteral("consent"), QStringLiteral("consent_required"),
             QStringLiteral("Подтвердите оба документа."));
        return;
    }
    if (!ensureDeviceKey(QStringLiteral("consent"), false)) {
        return;
    }

    setBusy(true);
    clearError();

    fetchLegalDocuments([this](bool ok) {
        if (!ok) {
            fail(QStringLiteral("consent"), m_lastErrorCode, m_lastError,
                 [this]() { submitConsent(); });
            return;
        }

        QString errorCode;
        // /auth/consent has no ceremony: there is no begin/challenge pair for a
        // re-consent. The payload keeps the field for schema compatibility and
        // the server verifies the signature and the version/hash pair.
        const QJsonObject payload = Consent::buildPayload(
            QStringLiteral("reconsent"), true, true, consentDocuments(), deviceLabel(QString()),
            AccountClient::clientVersion(),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate), &errorCode);
        if (payload.isEmpty()) {
            fail(QStringLiteral("consent"), errorCode,
                 QStringLiteral("Не удалось подтвердить документы (%1).").arg(errorCode));
            return;
        }

        const QByteArray payloadBytes = Consent::signedBytes(payload);
        const QByteArray signature = m_deviceKey->signRaw(payloadBytes);
        if (signature.size() != 64) {
            fail(QStringLiteral("consent"), QStringLiteral("proof_sign_failed"),
                 QStringLiteral("Не удалось подписать согласие."));
            return;
        }

        QJsonObject consent;
        consent.insert(QStringLiteral("payload"), QString::fromUtf8(payloadBytes));
        consent.insert(QStringLiteral("signature"),
                       QString::fromLatin1(CryptoUtil::base64UrlEncode(signature)));

        QJsonObject body;
        body.insert(QStringLiteral("consent"), consent);

        m_client->post(QStringLiteral("/auth/consent"), body, true,
                       CryptoUtil::randomUuid(),
                       [this, payload](const ApiResult &result) {
                           if (!result.ok) {
                               failFromResult(QStringLiteral("consent"), result);
                               return;
                           }
                           storeConsentRecord(payload);
                           setBusy(false);
                           refreshMe();
                       });
    });
}

void LegionApi::registerAccount(const QString &label)
{
    if (!ensureVault(QStringLiteral("register")) || !ensureClient(QStringLiteral("register"))) {
        return;
    }

    if (!m_agreement || !m_pdnConsent) {
        setState(QStringLiteral("consent"));
        emit consentRequired();
        fail(QStringLiteral("register"), QStringLiteral("consent_required"),
             QStringLiteral("Подтвердите оба документа."));
        return;
    }

    setBusy(true);
    clearError();

    fetchLegalDocuments([this, label](bool ok) {
        if (!ok) {
            fail(QStringLiteral("register"), m_lastErrorCode, m_lastError,
                 [this, label]() { registerAccount(label); });
            return;
        }
        if (!ensureDeviceKey(QStringLiteral("register"), true)) {
            return;
        }

        QJsonObject device;
        device.insert(QStringLiteral("label"), deviceLabel(label));
        device.insert(QStringLiteral("os"), platformOs());
        device.insert(QStringLiteral("version"), AccountClient::clientVersion());

        QJsonObject body;
        body.insert(QStringLiteral("public_key_jwk"), m_deviceKey->publicJwk());
        body.insert(QStringLiteral("device"), device);
        body.insert(QStringLiteral("client_version"), AccountClient::clientVersion());

        m_client->post(QStringLiteral("/auth/register/begin"), body, false,
                       CryptoUtil::randomUuid(), [this](const ApiResult &begin) {
                           if (!begin.ok) {
                               if (begin.errorCode == QLatin1String("key_already_registered") ||
                                   begin.errorCode == QLatin1String("key_revoked")) {
                                   recoverLostRegistration();
                                   return;
                               }
                               failFromResult(QStringLiteral("register"), begin,
                                              [this]() { registerAccount(QString()); });
                               return;
                           }

                           const QString ceremonyId =
                               begin.json.value(QStringLiteral("ceremony_id")).toString();
                           const QString challenge =
                               begin.json.value(QStringLiteral("challenge")).toString();
                           if (ceremonyId.isEmpty() || challenge.isEmpty()) {
                               fail(QStringLiteral("register"),
                                    QStringLiteral("invalid_response"),
                                    QStringLiteral("Сервер вернул неверный ответ. Повторите попытку."));
                               return;
                           }

                           QString errorCode;
                           const QJsonObject payload = Consent::buildPayload(
                               ceremonyId, m_agreement, m_pdnConsent, consentDocuments(),
                               deviceLabel(QString()), AccountClient::clientVersion(),
                               QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
                               &errorCode);
                           if (payload.isEmpty()) {
                               fail(QStringLiteral("register"), errorCode,
                                    QStringLiteral("Не удалось подтвердить документы (%1).").arg(errorCode));
                               return;
                           }

                           const QByteArray payloadBytes = Consent::signedBytes(payload);
                           const QByteArray consentSignature =
                               m_deviceKey->signRaw(payloadBytes);
                           const QByteArray ceremonySignature =
                               signCeremony(ceremonyId, challenge, QStringLiteral("register"));

                           if (consentSignature.size() != 64 || ceremonySignature.size() != 64) {
                               fail(QStringLiteral("register"),
                                    QStringLiteral("proof_sign_failed"),
                                    QStringLiteral("Не удалось подписать согласие."));
                               return;
                           }

                           QJsonObject consent;
                           consent.insert(QStringLiteral("payload"),
                                          QString::fromUtf8(payloadBytes));
                           consent.insert(QStringLiteral("signature"),
                                          QString::fromLatin1(
                                              CryptoUtil::base64UrlEncode(consentSignature)));

                           QJsonObject complete;
                           complete.insert(QStringLiteral("ceremony_id"), ceremonyId);
                           complete.insert(QStringLiteral("signature"),
                                           QString::fromLatin1(
                                               CryptoUtil::base64UrlEncode(ceremonySignature)));
                           complete.insert(QStringLiteral("label"), deviceLabel(QString()));
                           complete.insert(QStringLiteral("consent"), consent);
                            if (!m_referralCode.isEmpty()) {
                                complete.insert(QStringLiteral("referral_code"),
                                                m_referralCode);
                            }

                           m_client->post(
                               QStringLiteral("/auth/register/complete"), complete, false,
                               CryptoUtil::randomUuid(),
                               [this, payload](const ApiResult &result) {
                                   if (!result.ok) {
                                       if (result.errorCode ==
                                               QLatin1String("key_already_registered") ||
                                           result.errorCode == QLatin1String("key_revoked")) {
                                           recoverLostRegistration();
                                           return;
                                       }
                                       failFromResult(QStringLiteral("register"), result);
                                       return;
                                   }
                                   storeConsentRecord(payload);
                                   applyAuthResponse(QStringLiteral("register"), result);
                               });
                       });
    });
}

void LegionApi::login()
{
    if (!ensureVault(QStringLiteral("login"))) {
        return;
    }
    if (!ensureDeviceKey(QStringLiteral("login"), false)) {
        return;
    }
    performLogin(false);
}

void LegionApi::logout()
{
    if (m_client == nullptr) {
        setState(QStringLiteral("no_account"));
        return;
    }

    if (m_logoutInProgress) {
        return;
    }
    stopHealthAgent();
    m_logoutInProgress = true;
    const int generation = ++m_logoutGeneration;

    // F5: the session/NetBird teardown starts before any account state is
    // cleared. The signal is synchronous; QML stops the NetBird session
    // profile here while credentials are still valid.
    emit signOutStarted();

    const QString lease = m_leaseId.trimmed();
    if (!lease.isEmpty() &&
            (m_client->hasAccessToken() || m_client->hasRefreshToken())) {
        // Bounded: logout must not hang on a dead network. The server stop
        // endpoint is idempotent, so a timeout followed by the server watchdog
        // is safe.
        m_client->post(QStringLiteral("/sessions/") + lease + QStringLiteral("/stop"),
                       QJsonObject(), true, CryptoUtil::randomUuid(),
                       [this, generation](const ApiResult &) { finishLogout(generation); });
        QTimer::singleShot(3000, this, [this, generation]() { finishLogout(generation); });
        return;
    }

    finishLogout(generation);
}

void LegionApi::finishLogout(int generation)
{
    if (generation != m_logoutGeneration ||
            !m_logoutInProgress || m_logoutFinalizing) {
        return;
    }
    m_logoutFinalizing = true;

    if (m_client == nullptr) {
        m_logoutInProgress = false;
        m_logoutFinalizing = false;
        setState(QStringLiteral("no_account"));
        return;
    }

    // Clearing the local account is the goal; server-side token revocation is
    // best effort and must finish before the tokens are dropped (a refresh in
    // flight would otherwise restore them after the UI signed out).
    auto complete = [this, generation]() {
        if (generation != m_logoutGeneration || !m_logoutFinalizing) {
            return;
        }
        m_logoutFinalizing = false;
        m_logoutInProgress = false;

        m_client->clearTokens();

        if (m_pollTimer != nullptr) {
            m_pollTimer->stop();
        }
        m_pollPublicId.clear();
        clearPendingIntent();
        clearStoredConnectMaterial();
        setLeaseId(QString());

        if (m_vault != nullptr && m_vault->isAvailable()) {
            m_vault->remove(QString::fromLatin1(kAccountVaultKey));
            m_vault->remove(QString::fromLatin1(kWordsVaultKey));
        }

        // The device key stays: the same PC logs back in with a ceremony.
        // Logging out is not a device revocation.
        setHandle(QString());
        setOnboardingStep(QString());
        m_words.clear();
        emit wordsChanged();
        m_recoveryKitPath.clear();
        emit recoveryKitPathChanged();
        setReferralCode(QString());
        setDeletionState(QString(), QString());
        if (!m_gamification.isEmpty()) {
            m_gamification.clear();
            emit gamificationChanged();
        }
        if (!m_accountSummary.isEmpty()) {
            m_accountSummary.clear();
            emit accountSummaryChanged();
        }
        setBusy(false);
        setState(QStringLiteral("no_account"));
    };

    if (m_client->hasAccessToken() || m_client->hasRefreshToken()) {
        // The epoch guard in AccountClient makes this safe even when the
        // request has to refresh first: a late refresh reply cannot restore
        // the tokens cleared below.
        m_client->post(QStringLiteral("/auth/logout"), QJsonObject(), true,
                       CryptoUtil::randomUuid(),
                       [complete](const ApiResult &) { complete(); });
        QTimer::singleShot(1500, this, [complete]() { complete(); });
        return;
    }

    complete();
}

void LegionApi::shutdownForQuit()
{
    if (m_client == nullptr) {
        return;
    }

    stopHealthAgent();
    // Let QML stop the NetBird session profile while the app is still alive.
    emit signOutStarted();
    stopQueueEvents();

    if (m_leaseId.isEmpty() ||
            (!m_client->hasAccessToken() && !m_client->hasRefreshToken())) {
        return;
    }

    // aboutToQuit has no usable event loop for asynchronous work. Flush the
    // stop with a short local loop; the server watchdog remains the backstop.
    QEventLoop loop;
    QPointer<QEventLoop> loopGuard(&loop);
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(2500);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    m_client->post(QStringLiteral("/sessions/") + m_leaseId + QStringLiteral("/stop"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [loopGuard](const ApiResult &) {
                       // The reply may arrive after the timeout destroyed the
                       // local loop; never touch a dangling object.
                       if (loopGuard) {
                           loopGuard->quit();
                       }
                   });
    timer.start();
    loop.exec();
}

void LegionApi::confirmWordsSaved()
{
    if (!ensureClient(QStringLiteral("confirm_words"))) {
        return;
    }

    setBusy(true);
    clearError();

    QJsonObject confirmBody;
    confirmBody.insert(QStringLiteral("saved"), true);
    m_client->post(QStringLiteral("/auth/recovery/confirm"), confirmBody, true,
                   CryptoUtil::randomUuid(), [this](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("confirm_words"), result,
                                          [this]() { confirmWordsSaved(); });
                           return;
                       }

                       const QString step =
                           result.json.value(QStringLiteral("onboarding_step")).toString();
                       setOnboardingStep(step.isEmpty() ? QStringLiteral("complete") : step);

                       m_words.clear();
                       emit wordsChanged();
                       persistAccount();
                       setBusy(false);
                       setState(QStringLiteral("signed_in"));

                       QVariantMap data;
                       data.insert(QStringLiteral("onboarding_step"), m_onboardingStep);
                       emit response(QStringLiteral("confirm_words"), data);

                       if (!m_pendingPaymentId.isEmpty()) {
                           const QString pending = m_pendingPaymentId;
                           m_pendingPaymentId.clear();
                           notifyPaymentReturn(pending);
                       }
                   });
}

void LegionApi::reissueWords()
{
    if (!ensureClient(QStringLiteral("reissue_words")) ||
        !ensureVault(QStringLiteral("reissue_words"))) {
        return;
    }
    if (!ensureDeviceKey(QStringLiteral("reissue_words"), false)) {
        return;
    }

    setBusy(true);
    clearError();

    m_client->post(QStringLiteral("/auth/recovery/reissue"), QJsonObject(), true,
                   CryptoUtil::randomUuid(), [this](const ApiResult &result) {
                       if (result.ok) {
                           const QStringList words = wordsFromJson(result.json);
                           if (words.size() != 8) {
                               fail(QStringLiteral("reissue_words"),
                                    QStringLiteral("words_missing"),
                                    QStringLiteral("Сервер не вернул слова. Обратитесь в поддержку."));
                               return;
                           }
                           storeWords(words);
                           writeRecoveryKit(words);
                           setBusy(false);
                           setState(QStringLiteral("onboarding"));
                           emit wordsReady(m_handle, words);
                           return;
                       }

                       if (result.errorCode != QLatin1String("control_required")) {
                           failFromResult(QStringLiteral("reissue_words"), result);
                           return;
                       }

                       const QStringList words = storedWords();
                       if (words.size() != 8) {
                           fail(QStringLiteral("reissue_words"),
                                QStringLiteral("control_required"),
                                QStringLiteral("Введите слова восстановления, чтобы получить новые."));
                           return;
                       }

                       QJsonObject body;
                       body.insert(QStringLiteral("recovery_code"),
                                   words.join(QLatin1Char(' ')));
                       body.insert(QStringLiteral("words"), toJsonArray(words));

                       m_client->post(
                           QStringLiteral("/auth/control/verify"), body, true,
                           CryptoUtil::randomUuid(), [this](const ApiResult &control) {
                               if (!control.ok) {
                                   failFromResult(QStringLiteral("reissue_words"), control);
                                   return;
                               }

                               m_client->post(
                                   QStringLiteral("/auth/recovery/reissue"), QJsonObject(), true,
                                   CryptoUtil::randomUuid(),
                                   [this](const ApiResult &result) {
                                       if (!result.ok) {
                                           failFromResult(QStringLiteral("reissue_words"),
                                                          result);
                                           return;
                                       }
                                       const QStringList words = wordsFromJson(result.json);
                                       if (words.size() != 8) {
                                           fail(QStringLiteral("reissue_words"),
                                                QStringLiteral("words_missing"),
                                                QStringLiteral("Сервер не вернул слова."));
                                           return;
                                       }
                                       storeWords(words);
                                       writeRecoveryKit(words);
                                       setBusy(false);
                                       setState(QStringLiteral("onboarding"));
                                       emit wordsReady(m_handle, words);
                                   });
                           });
                   });
}

void LegionApi::recoverPrepare(const QString &handle, const QStringList &words)
{
    if (!ensureClient(QStringLiteral("recover_prepare"))) {
        return;
    }

    const QString normalizedHandle = handle.trimmed().toLower();
    const QStringList normalizedWords = normalizeWords(words);
    if (normalizedHandle.isEmpty() || normalizedWords.size() != 8) {
        fail(QStringLiteral("recover_prepare"), QStringLiteral("recovery_invalid"),
             QStringLiteral("Введите логин и все 8 слов."));
        return;
    }

    setBusy(true);
    clearError();

    QJsonObject body;
    body.insert(QStringLiteral("handle"), normalizedHandle);
    body.insert(QStringLiteral("words"), toJsonArray(normalizedWords));
    // The Django-side recovery primitive takes the words as one normalized
    // space-separated string; send both forms so the server can accept either.
    body.insert(QStringLiteral("recovery_code"), normalizedWords.join(QLatin1Char(' ')));

    m_client->post(QStringLiteral("/auth/recover/prepare"), body, false,
                   CryptoUtil::randomUuid(),
                   [this, normalizedHandle, normalizedWords](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("recover_prepare"), result);
                           return;
                       }

                       m_pendingRecoveryTicket =
                           result.json.value(QStringLiteral("ticket")).toString();
                       m_pendingRecoveryHandle = normalizedHandle;
                       m_pendingRecoveryWords = normalizedWords;

                       setBusy(false);
                       setState(QStringLiteral("consent"));
                       emit consentRequired();
                   });
}

void LegionApi::recoverComplete(const QString &handle, const QStringList &words,
                                const QString &label)
{
    if (!ensureVault(QStringLiteral("recover")) || !ensureClient(QStringLiteral("recover"))) {
        return;
    }

    // Recovery is never a referral attribution surface.
    Consent::setPendingReferralCode(QString());

    if (!m_agreement || !m_pdnConsent) {
        setState(QStringLiteral("consent"));
        emit consentRequired();
        fail(QStringLiteral("recover"), QStringLiteral("consent_required"),
             QStringLiteral("Подтвердите оба документа."));
        return;
    }

    const QString normalizedHandle = handle.trimmed().toLower();
    const QStringList normalizedWords = normalizeWords(words);
    if (normalizedHandle.isEmpty() || normalizedWords.size() != 8) {
        fail(QStringLiteral("recover"), QStringLiteral("recovery_invalid"),
             QStringLiteral("Введите логин и все 8 слов."));
        return;
    }

    setBusy(true);
    clearError();

    fetchLegalDocuments([this, normalizedHandle, normalizedWords, label](bool ok) {
        if (!ok) {
            fail(QStringLiteral("recover"), m_lastErrorCode, m_lastError,
                 [this, normalizedHandle, normalizedWords, label]() {
                     recoverComplete(normalizedHandle, normalizedWords, label);
                 });
            return;
        }

        QJsonObject beginBody;
        beginBody.insert(QStringLiteral("handle"), normalizedHandle);
        beginBody.insert(QStringLiteral("words"), toJsonArray(normalizedWords));
        beginBody.insert(QStringLiteral("recovery_code"),
                         normalizedWords.join(QLatin1Char(' ')));

        m_client->post(
            QStringLiteral("/auth/recover/begin"), beginBody, false,
            CryptoUtil::randomUuid(),
            [this, label, normalizedHandle, normalizedWords](const ApiResult &begin) {
                if (!begin.ok) {
                    failFromResult(QStringLiteral("recover"), begin,
                                   [this, label, normalizedHandle, normalizedWords]() {
                                       recoverComplete(normalizedHandle, normalizedWords, label);
                                   });
                    return;
                }

                const QString ceremonyId =
                    begin.json.value(QStringLiteral("ceremony_id")).toString();
                const QString challenge =
                    begin.json.value(QStringLiteral("challenge")).toString();
                if (ceremonyId.isEmpty() || challenge.isEmpty()) {
                    fail(QStringLiteral("recover"), QStringLiteral("invalid_response"),
                         QStringLiteral("Сервер вернул неверный ответ. Повторите попытку."));
                    return;
                }

                DeviceKey *newKey = DeviceKey::generate();
                if (newKey == nullptr) {
                    fail(QStringLiteral("recover"), QStringLiteral("key_generation_failed"),
                         QStringLiteral("Не удалось создать ключ устройства."));
                    return;
                }

                QString errorCode;
                const QJsonObject payload = Consent::buildPayload(
                    ceremonyId, true, true, consentDocuments(), deviceLabel(label),
                    AccountClient::clientVersion(),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate), &errorCode);
                if (payload.isEmpty()) {
                    delete newKey;
                    fail(QStringLiteral("recover"), errorCode,
                         QStringLiteral("Не удалось подтвердить документы (%1).").arg(errorCode));
                    return;
                }

                const QByteArray payloadBytes = Consent::signedBytes(payload);
                const QByteArray consentSignature = newKey->signRaw(payloadBytes);
                const QByteArray ceremonySignature =
                    newKey->signRaw(ceremonyMessage(ceremonyId, challenge,
                                                    QStringLiteral("recover")));

                if (consentSignature.size() != 64 || ceremonySignature.size() != 64) {
                    delete newKey;
                    fail(QStringLiteral("recover"), QStringLiteral("proof_sign_failed"),
                         QStringLiteral("Не удалось подписать восстановление."));
                    return;
                }

                QJsonObject consent;
                consent.insert(QStringLiteral("payload"), QString::fromUtf8(payloadBytes));
                consent.insert(QStringLiteral("signature"),
                               QString::fromLatin1(
                                   CryptoUtil::base64UrlEncode(consentSignature)));

                QJsonObject complete;
                complete.insert(QStringLiteral("ceremony_id"), ceremonyId);
                complete.insert(QStringLiteral("signature"),
                                QString::fromLatin1(
                                    CryptoUtil::base64UrlEncode(ceremonySignature)));
                complete.insert(QStringLiteral("public_key_jwk"), newKey->publicJwk());
                complete.insert(QStringLiteral("label"), deviceLabel(label));
                complete.insert(QStringLiteral("consent"), consent);

                m_client->post(
                    QStringLiteral("/auth/recover/complete"), complete, false,
                    CryptoUtil::randomUuid(),
                    [this, newKey, payload](const ApiResult &result) {
                        if (!result.ok) {
                            delete newKey;
                            failFromResult(QStringLiteral("recover"), result);
                            return;
                        }

                        // Ownership moves to LegionApi inside applyAuthResponse.
                        storeConsentRecord(payload);
                        applyAuthResponse(QStringLiteral("recover"), result, newKey);
                    });
            });
    });
}

void LegionApi::reprintRecoveryKit()
{
    const QStringList words = storedWords();
    if (words.size() != 8 || m_handle.isEmpty()) {
        fail(QStringLiteral("reprint_kit"), QStringLiteral("recovery_words_missing"),
             QStringLiteral("Слова восстановления не сохранены на этом компьютере."));
        return;
    }

    writeRecoveryKit(words);
}

void LegionApi::checkNetbirdStatus()
{
    if (m_netbirdProcess != nullptr) {
        return;
    }

    const QString exe = findNetbirdExecutable();
    if (exe.isEmpty()) {
        if (m_netbirdStatus != QLatin1String("missing")) {
            m_netbirdStatus = QStringLiteral("missing");
            emit netbirdStatusChanged();
        }
        return;
    }

    QProcess *process = new QProcess(this);
    m_netbirdProcess = process;

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                if (m_netbirdProcess != process) {
                    return;
                }
                QString text = QString::fromUtf8(process->readAllStandardOutput() +
                                                 process->readAllStandardError());
                QString compact = text;
                compact.remove(QRegularExpression(QStringLiteral("\\s+")));
                const bool online =
                    status == QProcess::NormalExit && exitCode == 0 &&
                    (compact.contains(QStringLiteral("\"connected\":true")) ||
                     compact.contains(QStringLiteral("Management:Connected")));
                const QString next = online ? QStringLiteral("online")
                                            : QStringLiteral("offline");
                if (m_netbirdStatus != next) {
                    m_netbirdStatus = next;
                    emit netbirdStatusChanged();
                }
                m_netbirdProcess = nullptr;
                process->deleteLater();
            });

    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                if (m_netbirdProcess != process) {
                    return;
                }
                if (m_netbirdStatus != QLatin1String("offline")) {
                    m_netbirdStatus = QStringLiteral("offline");
                    emit netbirdStatusChanged();
                }
                m_netbirdProcess = nullptr;
                process->deleteLater();
            });

    QStringList arguments;
    arguments << QStringLiteral("status") << QStringLiteral("--json");
    process->start(exe, arguments);
}

void LegionApi::resetLocalAccount()
{
    // A local reset drops the device key and tokens without the logout
    // ceremony. Stop the session network first; the shell may still stop the
    // lease while the access token is usable.
    setLeaseId(QString());
    emit signOutStarted();
    clearStoredConnectMaterial();

    if (m_client != nullptr) {
        m_client->clearTokens();
        m_client->setDeviceKey(nullptr);
    }

    delete m_deviceKey;
    m_deviceKey = nullptr;

    if (m_vault != nullptr && m_vault->isAvailable()) {
        m_vault->remove(DeviceKey::vaultKey());
        m_vault->remove(QString::fromLatin1(kAccountVaultKey));
        m_vault->remove(QString::fromLatin1(kWordsVaultKey));
        m_vault->remove(QString::fromLatin1(kConsentVaultKey));
        m_vault->remove(QString::fromLatin1(kPendingIntentVaultKey));
        m_vault->remove(QString::fromLatin1(kConnectVaultKey));
    }

    if (m_pollTimer != nullptr) {
        m_pollTimer->stop();
    }
    m_pollPublicId.clear();
    m_pollRequestInFlight = false;
    m_pendingPaymentId.clear();
    m_pendingRecoveryTicket.clear();
    m_pendingRecoveryHandle.clear();
    m_pendingRecoveryWords.clear();

    m_words.clear();
    emit wordsChanged();
    m_recoveryKitPath.clear();
    emit recoveryKitPathChanged();

    setHandle(QString());
    setOnboardingStep(QString());
    setReferralCode(QString());
    setDeletionState(QString(), QString());
    if (!m_gamification.isEmpty()) {
        m_gamification.clear();
        emit gamificationChanged();
    }
    if (!m_accountSummary.isEmpty()) {
        m_accountSummary.clear();
        emit accountSummaryChanged();
    }
    clearError();
    setBusy(false);
    setState(QStringLiteral("no_account"));
    emit response(QStringLiteral("local_reset"), QVariantMap());
}

void LegionApi::deleteAccount(const QString &handle, const QStringList &words)
{
    if (!ensureAccount(QStringLiteral("account_delete"))) {
        return;
    }

    const QString normalizedHandle = handle.trimmed().toLower();
    const QStringList normalizedWords = normalizeWords(words);
    if (normalizedHandle.isEmpty() || normalizedWords.size() != 8) {
        fail(QStringLiteral("account_delete"), QStringLiteral("deletion_invalid"),
             QStringLiteral("Введите логин и все 8 слов."));
        return;
    }
    if (!ensureDeviceKey(QStringLiteral("account_delete"), false)) {
        return;
    }

    setBusy(true);
    clearError();

    QJsonObject controlBody;
    controlBody.insert(QStringLiteral("recovery_code"),
                       normalizedWords.join(QLatin1Char(' ')));
    controlBody.insert(QStringLiteral("words"), toJsonArray(normalizedWords));

    m_client->post(
        QStringLiteral("/auth/control/verify"), controlBody, true,
        CryptoUtil::randomUuid(),
        [this, normalizedHandle, normalizedWords](const ApiResult &control) {
            if (!control.ok) {
                failFromResult(QStringLiteral("account_delete"), control,
                               [this, normalizedHandle, normalizedWords]() {
                                   deleteAccount(normalizedHandle, normalizedWords);
                               });
                return;
            }

            QJsonObject body;
            body.insert(QStringLiteral("handle"), normalizedHandle);
            body.insert(QStringLiteral("recovery_code"),
                        normalizedWords.join(QLatin1Char(' ')));
            body.insert(QStringLiteral("words"), toJsonArray(normalizedWords));

            m_client->post(
                QStringLiteral("/account/delete"), body, true,
                CryptoUtil::randomUuid(), [this](const ApiResult &result) {
                    if (!result.ok) {
                        failFromResult(QStringLiteral("account_delete"), result);
                        return;
                    }

                    if (m_pollTimer != nullptr) {
                        m_pollTimer->stop();
                    }
                    m_pollPublicId.clear();
                    clearPendingIntent();

                    // The grace period still needs /me, so tokens stay. The
                    // words are no longer shown and the app is read-only.
                    m_words.clear();
                    emit wordsChanged();

                    setBusy(false);
                    setDeletionState(
                        result.json.value(QStringLiteral("deletion_requested_at")).toString(),
                        result.json.value(QStringLiteral("purge_after")).toString());
                    persistAccount();
                    setState(QStringLiteral("deletion_pending"));

                    QVariantMap data;
                    data.insert(QStringLiteral("deletion_requested_at"),
                                result.json.value(QStringLiteral("deletion_requested_at"))
                                    .toVariant());
                    data.insert(QStringLiteral("purge_after"),
                                result.json.value(QStringLiteral("purge_after")).toVariant());
                    emit response(QStringLiteral("account_delete"), data);
                });
        });
}

void LegionApi::restoreAccount(const QString &handle, const QStringList &words)
{
    if (!ensureClient(QStringLiteral("account_restore")) ||
            !ensureVault(QStringLiteral("account_restore"))) {
        return;
    }

    const QString normalizedHandle = handle.trimmed().toLower();
    const QStringList normalizedWords = normalizeWords(words);
    if (normalizedHandle.isEmpty() || normalizedWords.size() != 8) {
        fail(QStringLiteral("account_restore"), QStringLiteral("deletion_invalid"),
             QStringLiteral("Введите имя аккаунта и все 8 слов."));
        return;
    }
    if (!ensureDeviceKey(QStringLiteral("account_restore"), false)) {
        return;
    }

    setBusy(true);
    clearError();

    QJsonObject beginBody;
    beginBody.insert(QStringLiteral("handle"), normalizedHandle);
    beginBody.insert(QStringLiteral("words"), toJsonArray(normalizedWords));
    beginBody.insert(QStringLiteral("recovery_code"),
                     normalizedWords.join(QLatin1Char(' ')));

    m_client->post(
        QStringLiteral("/account/restore/begin"), beginBody, false,
        CryptoUtil::randomUuid(),
        [this, normalizedHandle, normalizedWords](const ApiResult &begin) {
            if (!begin.ok) {
                failFromResult(QStringLiteral("account_restore"), begin,
                               [this, normalizedHandle, normalizedWords]() {
                                   restoreAccount(normalizedHandle, normalizedWords);
                               });
                return;
            }

            const QString ceremonyId =
                begin.json.value(QStringLiteral("ceremony_id")).toString();
            const QString challenge =
                begin.json.value(QStringLiteral("challenge")).toString();
            if (ceremonyId.isEmpty() || challenge.isEmpty()) {
                fail(QStringLiteral("account_restore"),
                     QStringLiteral("invalid_response"),
                     QStringLiteral("Сервер вернул неверный ответ. Повторите попытку."));
                return;
            }

            const QByteArray signature =
                signCeremony(ceremonyId, challenge, QStringLiteral("restore"));
            if (signature.size() != 64) {
                fail(QStringLiteral("account_restore"),
                     QStringLiteral("proof_sign_failed"),
                     QStringLiteral("Не удалось подтвердить устройство."));
                return;
            }

            QJsonObject completeBody;
            completeBody.insert(QStringLiteral("ceremony_id"), ceremonyId);
            completeBody.insert(QStringLiteral("signature"),
                                QString::fromLatin1(CryptoUtil::base64UrlEncode(signature)));
            completeBody.insert(QStringLiteral("handle"), normalizedHandle);

            m_client->post(
                QStringLiteral("/account/restore/complete"), completeBody, false,
                CryptoUtil::randomUuid(),
                [this](const ApiResult &result) {
                    if (!result.ok) {
                        failFromResult(QStringLiteral("account_restore"), result);
                        return;
                    }

                    setDeletionState(QString(), QString());
                    applyAuthResponse(QStringLiteral("account_restore"), result);
                });
        });
}

void LegionApi::setReferralCode(const QString &code)
{
    const QString normalized = normalizeReferralCode(code);
    if (m_referralCode == normalized) {
        Consent::setPendingReferralCode(normalized);
        return;
    }
    m_referralCode = normalized;
    Consent::setPendingReferralCode(normalized);
    emit referralCodeChanged();
}

void LegionApi::notifyReferralCode(const QString &code)
{
    // A deep link only pre-fills the onboarding field. It never creates an
    // account or bypasses the signed consent request.
    setReferralCode(code);
}

void LegionApi::getGamification()
{
    if (!ensureAccount(QStringLiteral("gamification"))) {
        return;
    }

    m_client->get(QStringLiteral("/gamification"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("gamification"), result,
                                         [this]() { getGamification(); });
                          return;
                      }
                      m_gamification = result.json.toVariantMap();
                      emit gamificationChanged();
                      emit response(QStringLiteral("gamification"), m_gamification);
                  });
}

void LegionApi::getAccountSummary()
{
    if (!ensureAccount(QStringLiteral("account_summary"))) {
        return;
    }

    m_client->get(QStringLiteral("/account/summary"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("account_summary"), result,
                                         [this]() { getAccountSummary(); });
                          return;
                      }
                      m_accountSummary = result.json.toVariantMap();
                      emit accountSummaryChanged();
                      emit response(QStringLiteral("account_summary"), m_accountSummary);
                  });
}

void LegionApi::getLegalDocuments()
{
    if (!ensureClient(QStringLiteral("legal_documents"))) {
        return;
    }

    fetchLegalDocuments([this](bool ok) {
        if (!ok) {
            fail(QStringLiteral("legal_documents"), m_lastErrorCode, m_lastError,
                 [this]() { getLegalDocuments(); });
            return;
        }
        emitLegalDocuments();
    });
}

void LegionApi::reissueHandoff(const QString &publicId)
{
    if (!ensureAccount(QStringLiteral("handoff"))) {
        return;
    }

    const QString id = publicId.trimmed();
    if (id.isEmpty()) {
        fail(QStringLiteral("handoff"), QStringLiteral("validation_error"),
             QStringLiteral("Платёж не найден."));
        return;
    }

    setBusy(true);
    clearError();

    openHandoffForIntent(
        id, QString(),
        [this, id](const QString &url) {
            openExternal(url);

            QVariantMap data;
            data.insert(QStringLiteral("public_id"), id);
            data.insert(QStringLiteral("handoff_url"), url);
            emit response(QStringLiteral("handoff"), data);

            setBusy(false);
            storePendingIntent(id);
        },
        [this, id](const ApiResult &failure) {
            failFromResult(QStringLiteral("handoff"), failure,
                           [this, id]() { reissueHandoff(id); });
        });
}

void LegionApi::refreshMe()
{
    if (!ensureClient(QStringLiteral("refresh_me"))) {
        return;
    }
    if (!ensureVault(QStringLiteral("refresh_me"))) {
        return;
    }

    setBusy(true);
    m_client->get(QStringLiteral("/me"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (!result.ok) {
                          if (result.errorCode == QLatin1String("account_deletion_pending")) {
                              setBusy(false);
                              setState(QStringLiteral("deletion_pending"));
                              return;
                          }
                          if (result.errorCode == QLatin1String("device_revoked") ||
                              result.errorCode == QLatin1String("device_stale")) {
                              m_client->clearTokens();
                              setBusy(false);
                              if (!m_deletionRequestedAt.isEmpty()) {
                                  setState(QStringLiteral("deletion_pending"));
                              }
                              else {
                                  setState(QStringLiteral("revoked"));
                              }
                              return;
                          }
                          failFromResult(QStringLiteral("refresh_me"), result,
                                         [this]() { refreshMe(); });
                          return;
                      }

                      setBusy(false);
                      applyMe(result.json);
                  });
}

void LegionApi::fetchLegalDocuments(std::function<void(bool)> done)
{
    // The app links to the documents on the website and does not download
    // them. The consent payload carries the document IDs and production URLs;
    // the server records the current version/hash when it accepts the consent.
    m_documents = Consent::staticDocuments();
    if (done) {
        done(true);
    }
}

QByteArray LegionApi::signCeremony(const QString &ceremonyId, const QString &challenge,
                                   const QString &action)
{
    if (m_deviceKey == nullptr || ceremonyId.isEmpty() || challenge.isEmpty()) {
        return QByteArray();
    }
    return m_deviceKey->signRaw(ceremonyMessage(ceremonyId, challenge, action));
}

QString LegionApi::deviceLabel(const QString &requested) const
{
    QString label = requested.trimmed();
    if (label.isEmpty()) {
        label = QSysInfo::machineHostName().trimmed();
    }
    if (label.isEmpty()) {
        label = QStringLiteral("PC");
    }
    if (label.size() > 64) {
        label.truncate(64);
    }
    return label;
}

QList<LegalDocumentInfo> LegionApi::consentDocuments() const
{
    // Static URLs are the source of truth; a failed/old /legal/documents call
    // must never leave registration without the two gating documents.
    return m_documents.isEmpty() ? Consent::staticDocuments() : m_documents;
}

void LegionApi::getClubs(bool availableOnly)
{
    if (!ensureClient(QStringLiteral("clubs"))) {
        return;
    }

    const QString path = availableOnly ? QStringLiteral("/clubs?available=1")
                                       : QStringLiteral("/clubs");

    m_client->get(path, false, CryptoUtil::randomUuid(),
                  [this, availableOnly](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("clubs"), result,
                                         [this, availableOnly]() { getClubs(availableOnly); });
                          return;
                      }

                      const QString updated =
                          result.json.value(QStringLiteral("updated_at")).toString();
                      if (updated != m_catalogUpdatedAt) {
                          m_catalogUpdatedAt = updated;
                          emit catalogUpdatedAtChanged();
                      }

                      emit response(QStringLiteral("clubs"), result.json.toVariantMap());
                  });
}

void LegionApi::getClub(const QString &slug)
{
    if (!ensureClient(QStringLiteral("club"))) {
        return;
    }

    const QString trimmed = slug.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/'))) {
        fail(QStringLiteral("club"), QStringLiteral("validation_error"),
             QStringLiteral("Клуб не найден."));
        return;
    }

    m_client->get(QStringLiteral("/clubs/") + trimmed, false, CryptoUtil::randomUuid(),
                  [this, trimmed](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("club"), result,
                                         [this, trimmed]() { getClub(trimmed); });
                          return;
                      }

                      QVariantMap data;
                      data.insert(QStringLiteral("club"),
                                  QVariant::fromValue(result.json.toVariantMap()));
                      emit response(QStringLiteral("club"), data);
                  });
}

void LegionApi::getWallet()
{
    if (!ensureAccount(QStringLiteral("wallet"))) {
        return;
    }

    m_client->get(QStringLiteral("/wallet"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("wallet"), result,
                                         [this]() { getWallet(); });
                          return;
                      }
                      emit response(QStringLiteral("wallet"), result.json.toVariantMap());
                  });
}

void LegionApi::getWalletHistory(const QString &cursor)
{
    if (!ensureAccount(QStringLiteral("wallet_history"))) {
        return;
    }

    QString path = QStringLiteral("/wallet/history");
    if (!cursor.trimmed().isEmpty()) {
        path += QStringLiteral("?cursor=") +
                QString::fromLatin1(QUrl::toPercentEncoding(cursor.trimmed()));
    }

    m_client->get(path, true, CryptoUtil::randomUuid(),
                  [this, cursor](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("wallet_history"), result,
                                         [this, cursor]() { getWalletHistory(cursor); });
                          return;
                      }
                      emit response(QStringLiteral("wallet_history"),
                                    result.json.toVariantMap());
                  });
}

void LegionApi::createTopup(int clubId, const QString &amount)
{
    createTopupWithPromo(clubId, amount, QString());
}

void LegionApi::createTopupWithPromo(int clubId, const QString &amount,
                                     const QString &promoCode)
{
    if (!ensureAccount(QStringLiteral("topup"))) {
        return;
    }
    if (clubId <= 0) {
        fail(QStringLiteral("topup"), QStringLiteral("validation_error"),
             QStringLiteral("Выберите клуб."));
        return;
    }

    QString normalizedAmount;
    if (!normalizeAmount(amount, &normalizedAmount)) {
        fail(QStringLiteral("topup"), QStringLiteral("validation_error"),
             QStringLiteral("Сумма должна быть от 1 до 10 000 рублей."));
        return;
    }

    const QString promo = promoCode.trimmed();
    setBusy(true);
    clearError();

    const QString requestId = CryptoUtil::randomUuid();
    QJsonObject body;
    body.insert(QStringLiteral("club_id"), clubId);
    body.insert(QStringLiteral("amount"), normalizedAmount);
    body.insert(QStringLiteral("purchase_type"), QStringLiteral("topup"));
    body.insert(QStringLiteral("promo_code"),
                promo.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(promo));
    body.insert(QStringLiteral("client_request_id"), requestId);

    m_client->post(QStringLiteral("/payments/intents"), body, true, requestId,
                   [this, clubId, normalizedAmount, promo](const ApiResult &intent) {
                       if (!intent.ok) {
                           failFromResult(QStringLiteral("topup"), intent,
                                          [this, clubId, normalizedAmount, promo]() {
                                              createTopupWithPromo(clubId, normalizedAmount, promo);
                                          });
                           return;
                       }

                       const QString publicId =
                           intent.json.value(QStringLiteral("public_id")).toString();
                       if (publicId.isEmpty()) {
                           fail(QStringLiteral("topup"), QStringLiteral("intent_invalid"),
                                QStringLiteral("Сервер не вернул платёж."));
                           return;
                       }

                       openHandoffForIntent(
                           publicId, QString(),
                           [this, publicId, intent](const QString &url) {
                               openExternal(url);

                               QVariantMap data;
                               data.insert(QStringLiteral("public_id"), publicId);
                               data.insert(QStringLiteral("handoff_url"), url);
                               data.insert(QStringLiteral("status"),
                                           intent.json.value(QStringLiteral("status"))
                                               .toVariant());
                               emit response(QStringLiteral("topup"), data);

                               setBusy(false);
                               startIntentPolling(publicId);
                           },
                           [this](const ApiResult &failure) {
                               failFromResult(QStringLiteral("topup"), failure);
                           });
                   });
}

void LegionApi::openHandoffForIntent(const QString &publicId, const QString &handoffUrl,
                                     std::function<void(const QString &)> done,
                                     std::function<void(const ApiResult &)> failed)
{
    if (!handoffUrl.isEmpty()) {
        done(handoffUrl);
        return;
    }

    if (publicId.isEmpty()) {
        ApiResult result;
        result.errorCode = QStringLiteral("handoff_missing");
        result.message = QStringLiteral("Не удалось открыть страницу оплаты.");
        failed(result);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("purpose"), QStringLiteral("payment"));
    body.insert(QStringLiteral("payment_intent_id"), publicId);

    m_client->post(QStringLiteral("/handoffs"), body, true, CryptoUtil::randomUuid(),
                   [done, failed](const ApiResult &result) {
                       if (!result.ok) {
                           failed(result);
                           return;
                       }

                       const QString url =
                           result.json.value(QStringLiteral("url")).toString();
                       if (url.isEmpty()) {
                           ApiResult failure;
                           failure.errorCode = QStringLiteral("handoff_invalid");
                           failure.message = QStringLiteral("Сервер не вернул ссылку на оплату.");
                           failed(failure);
                           return;
                       }
                       done(url);
                   });
}

void LegionApi::pollIntent(const QString &publicId)
{
    if (!ensureAccount(QStringLiteral("intent"))) {
        return;
    }
    if (publicId.trimmed().isEmpty()) {
        fail(QStringLiteral("intent"), QStringLiteral("validation_error"),
             QStringLiteral("Платёж не найден."));
        return;
    }

    startIntentPolling(publicId.trimmed());
}

void LegionApi::startIntentPolling(const QString &publicId)
{
    if (m_pollTimer == nullptr) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(3000);
        connect(m_pollTimer, &QTimer::timeout, this, &LegionApi::pollIntentOnce);
    }

    if (m_pollTimer != nullptr) {
        m_pollTimer->stop();
    }
    m_pollPublicId = publicId;
    m_pollCount = 0;
    m_pollRequestInFlight = false;
    pollIntentOnce();
}

void LegionApi::pollIntentOnce()
{
    if (m_client == nullptr || m_pollPublicId.isEmpty() || m_pollRequestInFlight) {
        return;
    }

    const QString publicId = m_pollPublicId;
    m_pollRequestInFlight = true;
    m_client->get(QStringLiteral("/payments/intents/") + publicId, true,
                  CryptoUtil::randomUuid(),
                  [this, publicId](const ApiResult &result) {
                      m_pollRequestInFlight = false;
                      if (publicId != m_pollPublicId) {
                          return;
                      }
                      if (!result.ok) {
                          if (m_pollTimer != nullptr) {
                              m_pollTimer->stop();
                          }
                          failFromResult(QStringLiteral("intent"), result);
                          return;
                      }

                      emit response(QStringLiteral("intent"), result.json.toVariantMap());

                      const QString status =
                          result.json.value(QStringLiteral("status")).toString();
                      if (status == QLatin1String("pending") && m_pollPublicId == publicId &&
                          ++m_pollCount < kMaxIntentPolls) {
                          if (m_pollTimer != nullptr) {
                              m_pollTimer->start();
                          }
                      }
                      else {
                          if (m_pollTimer != nullptr) {
                              m_pollTimer->stop();
                          }
                          if (status != QLatin1String("pending")) {
                              // Terminal (succeeded/failed/canceled/refunded):
                              // nothing left to wait for on the next cold start.
                              clearPendingIntent();
                              m_pollPublicId.clear();
                          }
                      }
                  });
}

void LegionApi::startSession(int clubId)
{
    if (!ensureAccount(QStringLiteral("session_start"))) {
        return;
    }
    if (clubId <= 0) {
        fail(QStringLiteral("session_start"), QStringLiteral("validation_error"),
             QStringLiteral("Выберите клуб."));
        return;
    }

    setBusy(true);
    clearError();

    const QString requestId = CryptoUtil::randomUuid();
    QJsonObject body;
    body.insert(QStringLiteral("club_id"), clubId);
    body.insert(QStringLiteral("request_id"), requestId);

    m_client->post(QStringLiteral("/sessions"), body, true, requestId,
                   [this](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("session_start"), result,
                                          [this]() { getCurrentSession(); });
                           return;
                       }
                       setBusy(false);
                       ++m_sessionMutationSeq;
                       emit response(QStringLiteral("session_start"), sessionData(result.json));
                       // Creating the lease is the "preparing" phase: the
                       // machine is reserved and provisioned, not connected.
                       setHealthPhase(QStringLiteral("preparing"));
                       startHealthAgent(result.json, m_leaseId);
                   });
}

void LegionApi::getCurrentSession()
{
    if (!ensureAccount(QStringLiteral("session_current"))) {
        return;
    }

    const int readSeq = m_sessionMutationSeq;
    QString currentPath = QStringLiteral("/sessions/current");
    if (m_healthAgent == nullptr || !m_healthAgent->isActive()
            || !m_healthAgent->hasHealthToken()) {
        // The agent has no usable token (cold start, vault loss, terminal
        // 401). Ask the server to reissue one; the normal poll must not
        // rotate a token the running agent still holds.
        currentPath += QStringLiteral("?health_token=1");
    }
    m_client->get(currentPath, true, CryptoUtil::randomUuid(),
                  [this, readSeq](const ApiResult &result) {
                      if (!result.ok) {
                          if (result.httpStatus == 404) {
                              if (readSeq == m_sessionMutationSeq) {
                                  // This read is newer than every mutation, so
                                  // the lease really is gone.
                                  setLeaseId(QString());
                                  clearStoredConnectMaterial();
                              }
                              QVariantMap data;
                              data.insert(QStringLiteral("session"), QVariant());
                              emit response(QStringLiteral("session_current"), data);
                              return;
                          }
                          failFromResult(QStringLiteral("session_current"), result,
                                         [this]() { getCurrentSession(); });
                          return;
                      }
                      if (readSeq != m_sessionMutationSeq) {
                          // A start/stop/connect/claim completed after this read
                          // started. Do not let the older snapshot overwrite
                          // the lease id; the mutation payload is authoritative.
                          QVariantMap data;
                          data.insert(QStringLiteral("session"),
                                      result.json.value(QStringLiteral("session"))
                                          .toVariant());
                          emit response(QStringLiteral("session_current"), data);
                          return;
                      }
                      emit response(QStringLiteral("session_current"), sessionData(result.json));
                      // A current read may reissue the health token for a
                      // live lease. Starting a stopped agent resets nothing
                      // (the sequence lives in the agent instance); an already
                      // active reporter is only re-configured with the new
                      // token and does not send an extra report.
                      const bool alreadyReporting = m_healthAgent != nullptr &&
                          m_healthAgent->isActive() &&
                          m_healthAgent->leaseId() == m_leaseId;
                      if (!alreadyReporting && !m_leaseId.isEmpty()) {
                          QJsonObject session =
                              result.json.value(QStringLiteral("session")).toObject();
                          if (session.isEmpty() &&
                                  result.json.contains(QStringLiteral("id"))) {
                              session = result.json;
                          }
                          const QString status =
                              session.value(QStringLiteral("status")).toString();
                          setHealthPhase(status == QLatin1String("pending")
                                             ? QStringLiteral("preparing")
                                             : QStringLiteral("connecting"));
                      }
                      startHealthAgent(result.json, m_leaseId);
                  });
}

void LegionApi::getSessionHistory()
{
    if (!ensureAccount(QStringLiteral("session_history"))) {
        return;
    }

    m_client->get(QStringLiteral("/sessions/history"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("session_history"), result,
                                         [this]() { getSessionHistory(); });
                          return;
                      }
                      emit response(QStringLiteral("session_history"),
                                    result.json.toVariantMap());
                  });
}

void LegionApi::stopSession(const QString &id)
{
    if (!ensureAccount(QStringLiteral("session_stop"))) {
        return;
    }
    if (id.trimmed().isEmpty()) {
        fail(QStringLiteral("session_stop"), QStringLiteral("validation_error"),
             QStringLiteral("Сеанс не найден."));
        return;
    }

    // Plan 19, 5.8: send a final stopping report, then stop. finalize() is
    // bounded and leaves the server watchdog as the backstop. A stale page
    // stopping an older lease must not kill the reporter of the live one.
    if (m_healthAgent != nullptr &&
            (m_leaseId.isEmpty() || id.trimmed() == m_leaseId)) {
        m_healthAgent->setPhaseStream(QStringLiteral("stopping"),
                                      QStringLiteral("ended"), false);
        m_healthAgent->finalize(1000);
    }
    m_client->post(QStringLiteral("/sessions/") + id.trimmed() + QStringLiteral("/stop"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [this, id](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("session_stop"), result,
                                          [this, id]() { stopSession(id); });
                           return;
                       }
                       const QString currentLease = m_leaseId;
                       ++m_sessionMutationSeq;
                       const QVariantMap payload = sessionData(result.json);
                       if (currentLease == id.trimmed()) {
                           setLeaseId(QString());
                           // The lease is over: its one-shot material is dead.
                           clearStoredConnectMaterial();
                       }
                       else if (!currentLease.isEmpty()) {
                           // A stale page stopped an older lease; keep tracking
                           // the live one and its material.
                           setLeaseId(currentLease);
                       }
                       else {
                           const QJsonObject material = loadConnectMaterial();
                           if (material.value(QStringLiteral("session_id")).toString() ==
                                   id.trimmed()) {
                               clearStoredConnectMaterial();
                           }
                           // The response may echo the stopped session; do not
                           // resume tracking a lease this call just ended.
                           setLeaseId(currentLease);
                       }
                       emit response(QStringLiteral("session_stop"), payload);
                   });
}

void LegionApi::connectSession(const QString &id)
{
    if (!ensureAccount(QStringLiteral("session_connect"))) {
        return;
    }
    const QString leaseId = id.trimmed();
    if (leaseId.isEmpty()) {
        fail(QStringLiteral("session_connect"), QStringLiteral("validation_error"),
             QStringLiteral("Сеанс не найден."));
        return;
    }
    // The user is connecting to an already prepared lease.
    setHealthPhase(QStringLiteral("connecting"));
    setBusy(true);
    clearError();

    // Never ask the server for a second material while a valid copy is still
    // in the vault: /connect is one-shot and a replay burns the lease.
    const QJsonObject pending = loadConnectMaterial();
    if (pending.value(QStringLiteral("session_id")).toString().trimmed() == leaseId) {
        const QJsonObject payload = pending.value(QStringLiteral("payload")).toObject();
        if (!payload.isEmpty()) {
            setBusy(false);
            setLeaseId(leaseId);
            ++m_sessionMutationSeq;
            // The persisted copy can carry the lease health token from the
            // original /connect response, so resume reporting before the
            // network layer starts.
            startHealthAgent(payload, leaseId);
            emit response(QStringLiteral("session_connect"), payload.toVariantMap());
            emit connectMaterialReady(leaseId, payload.toVariantMap());
            return;
        }
    }

    m_client->post(QStringLiteral("/sessions/") + leaseId + QStringLiteral("/connect"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [this, leaseId](const ApiResult &result) {
                       setBusy(false);
                       if (!result.ok) {
                           if (result.errorCode == QLatin1String("connect_material_consumed")) {
                               // The server will not issue a second key for
                               // this lease. Drop our copy so a retry cannot
                               // replay a consumed token.
                               clearStoredConnectMaterial();
                           }
                           failFromResult(QStringLiteral("session_connect"), result);
                           return;
                       }

                       if (m_logoutInProgress || m_state != QLatin1String("signed_in")) {
                           // The user signed out while /connect was in flight.
                           // Drop the material instead of starting NetBird for a
                           // signed-out account; the lease stop is already in
                           // flight and the watchdog is the backstop.
                           clearStoredConnectMaterial();
                           return;
                       }

                       const QString sessionId = result.json
                           .value(QStringLiteral("session_id")).toString(leaseId);
                       setLeaseId(sessionId);
                       // Persist before any UI sees it: a page change, a QML
                       // error or an app restart must not lose the only copy of
                       // the one-shot material.
                       persistConnectMaterial(sessionId, result.json);
                       startHealthAgent(result.json, sessionId);
                       ++m_sessionMutationSeq;

                       // The shell is the persistent owner of the connect
                       // command; pages only render the progress.
                       emit response(QStringLiteral("session_connect"),
                                     result.json.toVariantMap());
                       emit connectMaterialReady(sessionId, result.json.toVariantMap());
                   });
}

void LegionApi::reconnectSession(const QString &id)
{
    if (!ensureAccount(QStringLiteral("session_reconnect"))) {
        return;
    }
    if (id.trimmed().isEmpty()) {
        fail(QStringLiteral("session_reconnect"), QStringLiteral("validation_error"),
             QStringLiteral("Сеанс не найден."));
        return;
    }

    // Reconnect is an explicit transition that the server should see at once.
    setHealthPhase(QStringLiteral("reconnecting"));
    const bool wasReporting = m_healthAgent != nullptr &&
        m_healthAgent->isActive() &&
        m_healthAgent->leaseId() == id.trimmed();

    m_client->post(QStringLiteral("/sessions/") + id.trimmed() +
                       QStringLiteral("/reconnect"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [this, id, wasReporting](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("session_reconnect"), result,
                                          [this, id]() { reconnectSession(id); });
                           return;
                       }
                       ++m_sessionMutationSeq;
                       emit response(QStringLiteral("session_reconnect"),
                                     sessionData(result.json));
                       startHealthAgent(result.json, m_leaseId);
                       if (wasReporting && m_healthAgent != nullptr) {
                           m_healthAgent->submitReport();
                       }
                   });
}

void LegionApi::getQueue()
{
    if (!ensureAccount(QStringLiteral("queue"))) {
        return;
    }

    m_client->get(QStringLiteral("/queue"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (!result.ok) {
                          if (result.httpStatus == 404) {
                              QVariantMap data;
                              data.insert(QStringLiteral("entry"), QVariant());
                              emit response(QStringLiteral("queue"), data);
                              return;
                          }
                          failFromResult(QStringLiteral("queue"), result,
                                         [this]() { getQueue(); });
                          return;
                      }
                      emit response(QStringLiteral("queue"), entryData(result.json));
                  });
}

void LegionApi::joinQueue(int clubId)
{
    if (!ensureAccount(QStringLiteral("queue_join"))) {
        return;
    }
    if (clubId <= 0) {
        fail(QStringLiteral("queue_join"), QStringLiteral("validation_error"),
             QStringLiteral("Выберите клуб."));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("club_id"), clubId);

    m_client->post(QStringLiteral("/queue"), body, true, CryptoUtil::randomUuid(),
                   [this](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("queue_join"), result,
                                          [this]() { getQueue(); });
                           return;
                       }
                       emit response(QStringLiteral("queue_join"), entryData(result.json));
                   });
}

void LegionApi::leaveQueue(const QString &id)
{
    if (!ensureAccount(QStringLiteral("queue_leave"))) {
        return;
    }
    if (id.trimmed().isEmpty()) {
        fail(QStringLiteral("queue_leave"), QStringLiteral("validation_error"),
             QStringLiteral("Очередь не найдена."));
        return;
    }

    m_client->del(QStringLiteral("/queue/") + id.trimmed(), true, CryptoUtil::randomUuid(),
                  [this, id](const ApiResult &result) {
                      if (!result.ok) {
                          failFromResult(QStringLiteral("queue_leave"), result,
                                         [this, id]() { leaveQueue(id); });
                          return;
                      }
                      QVariantMap data;
                      data.insert(QStringLiteral("left"), true);
                      emit response(QStringLiteral("queue_leave"), data);
                  });
}

void LegionApi::claimQueue(const QString &id)
{
    if (!ensureAccount(QStringLiteral("queue_claim"))) {
        return;
    }
    if (id.trimmed().isEmpty()) {
        fail(QStringLiteral("queue_claim"), QStringLiteral("validation_error"),
             QStringLiteral("Очередь не найдена."));
        return;
    }

    // The claim creates a lease and starts provisioning.
    m_client->post(QStringLiteral("/queue/") + id.trimmed() + QStringLiteral("/claim"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [this, id](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("queue_claim"), result,
                                          [this, id]() { claimQueue(id); });
                           return;
                       }
                       ++m_sessionMutationSeq;
                       emit response(QStringLiteral("queue_claim"), sessionData(result.json));
                       setHealthPhase(QStringLiteral("preparing"));
                       startHealthAgent(result.json, m_leaseId);
                   });
}

void LegionApi::openExternal(const QString &url)
{
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    const bool isWeb = scheme == QLatin1String("https") ||
                       scheme == QLatin1String("legiongames");
    const bool isMail = scheme == QLatin1String("mailto") && !parsed.path().isEmpty();

    if (!parsed.isValid() || (!isWeb && !isMail) ||
        (isWeb && parsed.host().isEmpty())) {
        fail(QStringLiteral("open_external"), QStringLiteral("invalid_url"),
             QStringLiteral("Ссылка не поддерживается."));
        return;
    }

    QDesktopServices::openUrl(parsed);
}

void LegionApi::openInstallFolder()
{
    const QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        fail(QStringLiteral("open_install"),
             QStringLiteral("open_failed"),
             QStringLiteral("Не удалось открыть папку приложения."));
    }
}

void LegionApi::openLogFolder()
{
    const QString logDir = Path::getLogDir();
    if (logDir.isEmpty()) {
        fail(QStringLiteral("open_logs"), QStringLiteral("logs_unavailable"),
             QStringLiteral("Папка с логами недоступна."));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(logDir))) {
        fail(QStringLiteral("open_logs"), QStringLiteral("logs_unavailable"),
             QStringLiteral("Не удалось открыть папку с логами."));
    }
}

QString LegionApi::vaultPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        return QString();
    }
    return QDir::toNativeSeparators(base + QStringLiteral("/vault"));
}

void LegionApi::openVaultFolder()
{
    const QString path = vaultPath();
    if (path.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        fail(QStringLiteral("open_vault"),
             QStringLiteral("open_failed"),
             QStringLiteral("Не удалось открыть папку хранилища."));
    }
}

void LegionApi::retryFailedStep()
{
    if (!m_retry) {
        return;
    }
    const std::function<void()> retry = m_retry;
    m_retry = nullptr;
    retry();
}

void LegionApi::notifyPaymentReturn(const QString &publicId)
{
    if (publicId.isEmpty()) {
        return;
    }

    // Persist first: a cold start after the browser hand-off must still show
    // the payment screen, and the deep link is only a hint, never a credit.
    storePendingIntent(publicId);

    if (m_client == nullptr ||
        (!m_client->hasAccessToken() && !m_client->hasRefreshToken())) {
        // Cold start or app still onboarding: keep it until the account is
        // usable.
        return;
    }

    if (m_emittedPaymentId != publicId) {
        m_emittedPaymentId = publicId;
        emit paymentReturned(publicId);
    }

    pollIntent(publicId);
}

QVariantMap LegionApi::sessionData(const QJsonObject &json)
{
    QVariantMap data;
    QJsonObject session;
    if (json.contains(QStringLiteral("session"))) {
        session = json.value(QStringLiteral("session")).toObject();
        data.insert(QStringLiteral("session"),
                    json.value(QStringLiteral("session")).toVariant());
    }
    else if (!json.isEmpty()) {
        session = json;
        data.insert(QStringLiteral("session"),
                    QVariant::fromValue(json.toVariantMap()));
    }
    else {
        data.insert(QStringLiteral("session"), QVariant());
    }

    // Track the server lease id so logout/quit can stop exactly this lease and
    // StreamSegue can end it when the stream finishes. A terminal session is
    // not stoppable and must not keep a stale id around.
    const QString sessionId = session.value(QStringLiteral("id")).toString();
    const QString status = session.value(QStringLiteral("status")).toString();
    if (sessionId.isEmpty() || status == QLatin1String("expired") ||
            status == QLatin1String("stopped")) {
        setLeaseId(QString());
    }
    else {
        setLeaseId(sessionId);
        if (status == QLatin1String("active") && m_connectPending) {
            // An active lease has already consumed its one-shot material. Drop
            // a leftover copy from a crash after the PIN relay.
            const QJsonObject material = loadConnectMaterial();
            if (material.value(QStringLiteral("session_id")).toString() == sessionId) {
                clearStoredConnectMaterial();
            }
        }
    }
    return data;
}

QVariantMap LegionApi::entryData(const QJsonObject &json)
{
    QVariantMap data;
    if (json.contains(QStringLiteral("entry"))) {
        data.insert(QStringLiteral("entry"),
                    json.value(QStringLiteral("entry")).toVariant());
    }
    else if (!json.isEmpty()) {
        data.insert(QStringLiteral("entry"),
                    QVariant::fromValue(json.toVariantMap()));
    }
    else {
        data.insert(QStringLiteral("entry"), QVariant());
    }
    return data;
}

QStringList LegionApi::wordsFromJson(const QJsonObject &json)
{
    QJsonValue value = json.value(QStringLiteral("recovery_code"));

    // The API returns an array, but the Django recovery primitive returns one
    // normalized space-separated string. Accept both.
    if (value.isString()) {
        const QStringList parts =
            value.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() == 8) {
            QStringList words;
            for (const QString &part : parts) {
                words.append(part.trimmed().toLower());
            }
            return words;
        }
    }

    if (!value.isArray()) {
        value = json.value(QStringLiteral("recovery_phrase"));
        if (value.isString()) {
            const QStringList parts =
                value.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() == 8) {
                QStringList words;
                for (const QString &part : parts) {
                    words.append(part.trimmed().toLower());
                }
                return words;
            }
        }
    }

    if (!value.isArray()) {
        value = json.value(QStringLiteral("words"));
    }

    QStringList words;
    if (!value.isArray()) {
        return words;
    }

    for (const QJsonValue &item : value.toArray()) {
        const QString word = item.toString();
        if (!word.isEmpty()) {
            words.append(word);
        }
    }
    return words;
}

bool LegionApi::normalizeAmount(const QString &input, QString *output)
{
    QString text = input.trimmed();
    text.replace(QLatin1Char(','), QLatin1Char('.'));

    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || value < 1.0 || value > 10000.0) {
        return false;
    }

    if (output) {
        *output = QString::number(value, 'f', 2);
    }
    return true;
}
