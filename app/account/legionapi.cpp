#include "legionapi.h"

#include "accountclient.h"
#include "cryptoutil.h"
#include "devicekey.h"
#include "path.h"
#include "recoverykit.h"
#include "vault.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QtDebug>

namespace {

const char kAccountVaultKey[] = "account";
const char kWordsVaultKey[] = "recovery.words";
const char kConsentVaultKey[] = "consent";
const char kPendingIntentVaultKey[] = "payment.pending";

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

} // namespace

LegionApi::LegionApi(QObject *parent) :
    QObject(parent)
{
}

LegionApi::~LegionApi()
{
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

    connect(m_client, &AccountClient::authenticationLost, this, [this](const QString &code) {
        if (code == QLatin1String("device_revoked") ||
            code == QLatin1String("device_stale")) {
            setState(QStringLiteral("revoked"));
        }
        else if (code == QLatin1String("unknown_device")) {
            if (m_vault != nullptr) {
                m_vault->remove(QString::fromLatin1(kAccountVaultKey));
            }
            setHandle(QString());
            setOnboardingStep(QString());
            setState(QStringLiteral("no_account"));
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

    QString errorCode;
    m_deviceKey = DeviceKey::load(m_vault, &errorCode);
    if (m_deviceKey != nullptr) {
        m_client->setDeviceKey(m_deviceKey);
    }

    const bool haveRefresh = m_client->restoreRefreshToken();
    if (haveRefresh || m_deviceKey != nullptr) {
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
        m_deviceKey = DeviceKey::loadOrCreate(m_vault, &created, &errorCode,
                                               m_handle.isEmpty());
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

    if (m_vault != nullptr && m_vault->isAvailable() &&
        !m_vault->save(DeviceKey::vaultKey(), m_deviceKey->toPkcs8())) {
        qWarning() << "Account: cannot persist the recovered device key"
                   << m_vault->lastErrorCode();
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
    if (result.errorCode == QLatin1String("device_revoked") ||
        result.errorCode == QLatin1String("device_stale")) {
        if (m_client != nullptr) {
            m_client->clearTokens();
        }
        setState(QStringLiteral("revoked"));
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

void LegionApi::loadAccountRecord()
{
    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return;
    }

    bool ok = false;
    const QByteArray blob = m_vault->load(QString::fromLatin1(kAccountVaultKey), &ok);
    if (!ok) {
        return;
    }

    const QJsonObject record = QJsonDocument::fromJson(blob).object();
    setHandle(record.value(QStringLiteral("handle")).toString());
    setOnboardingStep(record.value(QStringLiteral("onboarding_step")).toString());
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
    const QByteArray blob = m_vault->load(QString::fromLatin1(kPendingIntentVaultKey), &ok);
    if (!ok) {
        return QString();
    }

    const QJsonObject record = QJsonDocument::fromJson(blob).object();
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
    const QByteArray blob = m_vault->load(QString::fromLatin1(kWordsVaultKey), &ok);
    if (!ok) {
        return QStringList();
    }

    const QJsonObject record = QJsonDocument::fromJson(blob).object();
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

    emit recoveryKitWritten(kit.path, kit.syncedFolder);
}

void LegionApi::applyMe(const QJsonObject &json)
{
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
    persistAccount();
    applyDevice(json.value(QStringLiteral("device")).toObject());
    emit response(QStringLiteral("me"), json.toVariantMap());

    const QString deletion = json.value(QStringLiteral("deletion_requested_at")).toString();
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
    const QJsonObject json = result.json;

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
        setState(QStringLiteral("revoked"));
        if (!silent) {
            fail(op, result.errorCode, result.message);
        }
        return;
    }

    if (result.errorCode == QLatin1String("unknown_device")) {
        if (m_vault != nullptr) {
            m_vault->remove(QString::fromLatin1(kAccountVaultKey));
        }
        setHandle(QString());
        setOnboardingStep(QString());
        setState(QStringLiteral("no_account"));
        if (!silent) {
            fail(op, result.errorCode, result.message);
        }
        return;
    }

    if (silent) {
        silentFailure(result.errorCode, result.message);
        if (result.errorCode == QLatin1String("not_authenticated")) {
            setState(QStringLiteral("no_account"));
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

    if (m_client->hasAccessToken()) {
        m_client->post(QStringLiteral("/auth/logout"), QJsonObject(), true,
                       CryptoUtil::randomUuid(), [](const ApiResult &) {});
    }

    m_client->clearTokens();

    if (m_pollTimer != nullptr) {
        m_pollTimer->stop();
    }
    m_pollPublicId.clear();
    clearPendingIntent();

    if (m_vault != nullptr && m_vault->isAvailable()) {
        m_vault->remove(QString::fromLatin1(kAccountVaultKey));
        m_vault->remove(QString::fromLatin1(kWordsVaultKey));
    }

    // The device key stays: the same PC logs back in with a ceremony. Logging
    // out is not a device revocation.
    setHandle(QString());
    setOnboardingStep(QString());
    m_words.clear();
    emit wordsChanged();
    setBusy(false);
    setState(QStringLiteral("no_account"));
}

void LegionApi::confirmWordsSaved()
{
    if (!ensureClient(QStringLiteral("confirm_words"))) {
        return;
    }

    setBusy(true);
    clearError();

    m_client->post(QStringLiteral("/auth/recovery/confirm"), QJsonObject(), true,
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
                    setState(QStringLiteral("deletion_pending"));

                    QVariantMap data;
                    data.insert(QStringLiteral("deletion_requested_at"),
                                result.json.value(QStringLiteral("deletion_requested_at"))
                                    .toVariant());
                    emit response(QStringLiteral("account_delete"), data);
                });
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
                          if (result.errorCode == QLatin1String("device_revoked") ||
                              result.errorCode == QLatin1String("device_stale")) {
                              m_client->clearTokens();
                              setBusy(false);
                              setState(QStringLiteral("revoked"));
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

    setBusy(true);
    clearError();

    const QString requestId = CryptoUtil::randomUuid();
    QJsonObject body;
    body.insert(QStringLiteral("club_id"), clubId);
    body.insert(QStringLiteral("amount"), normalizedAmount);
    body.insert(QStringLiteral("purchase_type"), QStringLiteral("topup"));
    body.insert(QStringLiteral("promo_code"), QJsonValue::Null);
    body.insert(QStringLiteral("client_request_id"), requestId);

    m_client->post(QStringLiteral("/payments/intents"), body, true, requestId,
                   [this, clubId, normalizedAmount](const ApiResult &intent) {
                       if (!intent.ok) {
                           failFromResult(QStringLiteral("topup"), intent,
                                          [this, clubId, normalizedAmount]() {
                                              createTopup(clubId, normalizedAmount);
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

    m_pollPublicId = publicId;
    m_pollCount = 0;
    pollIntentOnce();
}

void LegionApi::pollIntentOnce()
{
    if (m_client == nullptr || m_pollPublicId.isEmpty()) {
        return;
    }

    const QString publicId = m_pollPublicId;
    m_client->get(QStringLiteral("/payments/intents/") + publicId, true,
                  CryptoUtil::randomUuid(),
                  [this, publicId](const ApiResult &result) {
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
                       emit response(QStringLiteral("session_start"), sessionData(result.json));
                   });
}

void LegionApi::getCurrentSession()
{
    if (!ensureAccount(QStringLiteral("session_current"))) {
        return;
    }

    m_client->get(QStringLiteral("/sessions/current"), true, CryptoUtil::randomUuid(),
                  [this](const ApiResult &result) {
                      if (!result.ok) {
                          if (result.httpStatus == 404) {
                              QVariantMap data;
                              data.insert(QStringLiteral("session"), QVariant());
                              emit response(QStringLiteral("session_current"), data);
                              return;
                          }
                          failFromResult(QStringLiteral("session_current"), result,
                                         [this]() { getCurrentSession(); });
                          return;
                      }
                      emit response(QStringLiteral("session_current"), sessionData(result.json));
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

    m_client->post(QStringLiteral("/sessions/") + id.trimmed() + QStringLiteral("/stop"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [this, id](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("session_stop"), result,
                                          [this, id]() { stopSession(id); });
                           return;
                       }
                       emit response(QStringLiteral("session_stop"), sessionData(result.json));
                   });
}

void LegionApi::extendSession(const QString &id, const QString &amount)
{
    if (!ensureAccount(QStringLiteral("session_extend"))) {
        return;
    }
    if (id.trimmed().isEmpty()) {
        fail(QStringLiteral("session_extend"), QStringLiteral("validation_error"),
             QStringLiteral("Сеанс не найден."));
        return;
    }

    QString normalizedAmount;
    if (!normalizeAmount(amount, &normalizedAmount)) {
        fail(QStringLiteral("session_extend"), QStringLiteral("validation_error"),
             QStringLiteral("Сумма должна быть от 1 до 10 000 рублей."));
        return;
    }

    setBusy(true);
    clearError();

    const QString requestId = CryptoUtil::randomUuid();
    QJsonObject body;
    body.insert(QStringLiteral("amount"), normalizedAmount);
    body.insert(QStringLiteral("client_request_id"), requestId);
    // Contract §9 names the extend key `request_id`; accept both spellings.
    body.insert(QStringLiteral("request_id"), requestId);

    m_client->post(QStringLiteral("/sessions/") + id.trimmed() + QStringLiteral("/extend"),
                   body, true, requestId, [this](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("session_extend"), result);
                           return;
                       }

                       QString publicId =
                           result.json.value(QStringLiteral("public_id")).toString();
                       if (publicId.isEmpty()) {
                           publicId = result.json.value(QStringLiteral("intent"))
                                          .toObject()
                                          .value(QStringLiteral("public_id"))
                                          .toString();
                       }
                       const QString handoffUrl =
                           result.json.value(QStringLiteral("handoff_url")).toString();

                       openHandoffForIntent(
                           publicId, handoffUrl,
                           [this, publicId](const QString &url) {
                               if (!url.isEmpty()) {
                                   openExternal(url);
                               }

                               QVariantMap data;
                               data.insert(QStringLiteral("public_id"), publicId);
                               data.insert(QStringLiteral("handoff_url"), url);
                               emit response(QStringLiteral("session_extend"), data);

                               setBusy(false);
                               if (!publicId.isEmpty()) {
                                   startIntentPolling(publicId);
                               }
                           },
                           [this](const ApiResult &failure) {
                               failFromResult(QStringLiteral("session_extend"), failure);
                           });
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

    m_client->post(QStringLiteral("/sessions/") + id.trimmed() +
                       QStringLiteral("/reconnect"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [this, id](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("session_reconnect"), result,
                                          [this, id]() { reconnectSession(id); });
                           return;
                       }
                       emit response(QStringLiteral("session_reconnect"),
                                     sessionData(result.json));
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

    m_client->post(QStringLiteral("/queue/") + id.trimmed() + QStringLiteral("/claim"),
                   QJsonObject(), true, CryptoUtil::randomUuid(),
                   [this, id](const ApiResult &result) {
                       if (!result.ok) {
                           failFromResult(QStringLiteral("queue_claim"), result,
                                          [this, id]() { claimQueue(id); });
                           return;
                       }
                       emit response(QStringLiteral("queue_claim"), sessionData(result.json));
                   });
}

void LegionApi::openExternal(const QString &url)
{
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();

    if (!parsed.isValid() ||
        (scheme != QLatin1String("https") && scheme != QLatin1String("legiongames")) ||
        parsed.host().isEmpty()) {
        fail(QStringLiteral("open_external"), QStringLiteral("invalid_url"),
             QStringLiteral("Ссылка не поддерживается."));
        return;
    }

    QDesktopServices::openUrl(parsed);
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
    if (json.contains(QStringLiteral("session"))) {
        data.insert(QStringLiteral("session"),
                    json.value(QStringLiteral("session")).toVariant());
    }
    else if (!json.isEmpty()) {
        data.insert(QStringLiteral("session"),
                    QVariant::fromValue(json.toVariantMap()));
    }
    else {
        data.insert(QStringLiteral("session"), QVariant());
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
