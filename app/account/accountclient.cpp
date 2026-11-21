#include "accountclient.h"

#include "cryptoutil.h"
#include "devicekey.h"
#include "vault.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtDebug>

#ifndef VERSION_STR
#define VERSION_STR "0.0.0"
#endif
#ifndef LG_BUILD
#define LG_BUILD "0"
#endif

namespace {

const char kRefreshTokenVaultKey[] = "tokens.refresh";

// The app never carries a web cookie. The storefront is a different surface and
// the payment page uses its own scoped capability, so a cookie jar here would
// only be a leak waiting to happen.
class NoCookieJar : public QNetworkCookieJar
{
public:
    explicit NoCookieJar(QObject *parent) :
        QNetworkCookieJar(parent)
    {
    }

    bool insertCookie(const QNetworkCookie &) override { return false; }
    bool updateCookie(const QNetworkCookie &) override { return false; }
    bool deleteCookie(const QNetworkCookie &) override { return true; }
    bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
};

QString defaultMessageForCode(const QString &code)
{
    if (code == QLatin1String("network_error")) {
        return QStringLiteral("Нет соединения. Проверьте интернет.");
    }
    if (code == QLatin1String("timeout")) {
        return QStringLiteral("Сервер не ответил. Попробуйте ещё раз.");
    }
    if (code == QLatin1String("rate_limited")) {
        return QStringLiteral("Слишком много попыток. Подождите минуту.");
    }
    if (code == QLatin1String("client_update_required")) {
        return QStringLiteral("Версия приложения устарела. Установите обновление.");
    }
    if (code == QLatin1String("device_revoked")) {
        return QStringLiteral("Это устройство отключено. Восстановите доступ по коду.");
    }
    if (code == QLatin1String("session_expired") ||
        code == QLatin1String("session_revoked")) {
        return QStringLiteral("Сессия истекла. Войдите снова.");
    }
    if (code == QLatin1String("consent_required")) {
        return QStringLiteral("Нужно подтвердить документы.");
    }
    if (code == QLatin1String("consent_version_mismatch")) {
        return QStringLiteral("Документы обновились. Подтвердите новую версию.");
    }
    if (code == QLatin1String("not_authenticated")) {
        return QStringLiteral("Нужно войти в аккаунт.");
    }
    if (code == QLatin1String("no_device_key")) {
        return QStringLiteral("Ключ устройства не найден.");
    }
    if (code == QLatin1String("vault_unavailable")) {
        return QStringLiteral("Не удалось открыть хранилище ключей. Аккаунт недоступен.");
    }
    return QStringLiteral("Ошибка запроса. Попробуйте ещё раз.");
}

} // namespace

AccountClient::AccountClient(Vault *vault, QObject *parent) :
    QObject(parent),
    m_vault(vault)
{
    m_nam = new QNetworkAccessManager(this);
    m_nam->setStrictTransportSecurityEnabled(true);
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // setCookieJar() takes ownership; no parent to avoid a double delete.
    m_nam->setCookieJar(new NoCookieJar(nullptr));

    connect(m_nam, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply *reply) {
                const auto it = m_pending.find(reply);
                if (it == m_pending.end()) {
                    reply->deleteLater();
                    return;
                }
                const Pending pending = it.value();
                m_pending.erase(it);
                handleReply(reply, pending);
            });
}

AccountClient::~AccountClient() = default;

QString AccountClient::apiBaseUrl()
{
    return QStringLiteral("https://legiongames.ru/app/v1");
}

QString AccountClient::clientVersion()
{
    return QString::fromLatin1(VERSION_STR) + QLatin1Char('+') +
           QString::fromLatin1(LG_BUILD);
}

QByteArray AccountClient::proofPayload(const QByteArray &body, const QString &method,
                                       const QString &path, qint64 iat, qint64 exp,
                                       const QByteArray &jtiBase64Url)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("bh"),
                   QString::fromLatin1(CryptoUtil::base64UrlEncode(CryptoUtil::sha256(body))));
    payload.insert(QStringLiteral("exp"), static_cast<double>(exp));
    payload.insert(QStringLiteral("htm"), method.toUpper());
    payload.insert(QStringLiteral("htu"), path);
    payload.insert(QStringLiteral("iat"), static_cast<double>(iat));
    payload.insert(QStringLiteral("jti"), QString::fromLatin1(jtiBase64Url));
    payload.insert(QStringLiteral("v"), 1);
    return CryptoUtil::canonicalJson(payload);
}

QByteArray AccountClient::buildProof(DeviceKey *key, const QByteArray &body,
                                     const QString &method, const QString &path,
                                     QString *errorCode)
{
    if (key == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("no_device_key");
        }
        return QByteArray();
    }

    const qint64 iat = QDateTime::currentSecsSinceEpoch();
    const qint64 exp = iat + 60;
    const QByteArray jti = CryptoUtil::base64UrlEncode(CryptoUtil::randomBytes(12));

    const QByteArray payload = proofPayload(body, method, path, iat, exp, jti);
    const QByteArray payloadBase64 = CryptoUtil::base64UrlEncode(payload);

    // Contract v1.4 §5.4: the signature covers the ASCII bytes of payload_b64.
    const QByteArray signature = key->signRaw(payloadBase64);
    if (signature.size() != 64) {
        if (errorCode) {
            *errorCode = QStringLiteral("proof_sign_failed");
        }
        return QByteArray();
    }

    if (errorCode) {
        errorCode->clear();
    }
    return payloadBase64 + '.' + CryptoUtil::base64UrlEncode(signature);
}

void AccountClient::setDeviceKey(DeviceKey *key)
{
    m_deviceKey = key;
}

void AccountClient::setTokens(const QByteArray &accessToken, const QByteArray &refreshToken,
                              int expiresInSeconds, int refreshExpiresInSeconds)
{
    m_accessToken = accessToken;
    m_accessExpiresAt = QDateTime::currentDateTimeUtc().addSecs(qMax(0, expiresInSeconds));

    if (!refreshToken.isEmpty()) {
        m_refreshToken = refreshToken;
        m_refreshExpiresAt =
            QDateTime::currentDateTimeUtc().addSecs(qMax(0, refreshExpiresInSeconds));
        storeRefreshToken(m_refreshToken, m_refreshExpiresAt);
    }

    emit tokensChanged();
}

void AccountClient::clearTokens()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_accessExpiresAt = QDateTime();
    m_refreshExpiresAt = QDateTime();

    if (m_vault != nullptr && m_vault->isAvailable()) {
        m_vault->remove(QString::fromLatin1(kRefreshTokenVaultKey));
    }

    emit tokensChanged();
}

bool AccountClient::accessTokenExpired() const
{
    if (m_accessToken.isEmpty()) {
        return true;
    }
    if (!m_accessExpiresAt.isValid()) {
        return true;
    }
    return m_accessExpiresAt <= QDateTime::currentDateTimeUtc().addSecs(30);
}

bool AccountClient::storeRefreshToken(const QByteArray &token, const QDateTime &expiresAt)
{
    if (m_vault == nullptr) {
        emit refreshTokenPersistFailed(QStringLiteral("vault_unavailable"));
        return false;
    }
    if (!m_vault->isAvailable()) {
        emit refreshTokenPersistFailed(QStringLiteral("vault_unavailable"));
        return false;
    }

    QJsonObject record;
    record.insert(QStringLiteral("v"), 1);
    record.insert(QStringLiteral("token"), QString::fromUtf8(token));
    record.insert(QStringLiteral("expires_at"),
                  expiresAt.toUTC().toString(Qt::ISODate));

    if (!m_vault->save(QString::fromLatin1(kRefreshTokenVaultKey),
                       QJsonDocument(record).toJson(QJsonDocument::Compact))) {
        emit refreshTokenPersistFailed(m_vault->lastErrorCode());
        return false;
    }
    return true;
}

QByteArray AccountClient::loadStoredRefreshToken(QDateTime *expiresAt)
{
    if (expiresAt) {
        *expiresAt = QDateTime();
    }
    if (m_vault == nullptr || !m_vault->isAvailable()) {
        return QByteArray();
    }

    bool ok = false;
    const QByteArray blob = m_vault->load(QString::fromLatin1(kRefreshTokenVaultKey), &ok);
    if (!ok) {
        return QByteArray();
    }

    const QJsonObject record = QJsonDocument::fromJson(blob).object();
    const QByteArray token = record.value(QStringLiteral("token")).toString().toUtf8();
    if (token.isEmpty()) {
        return QByteArray();
    }

    QDateTime expiry = QDateTime::fromString(
        record.value(QStringLiteral("expires_at")).toString(), Qt::ISODate);
    if (expiry.isValid()) {
        expiry = expiry.toUTC();
    }

    if (expiry.isValid() && expiry <= QDateTime::currentDateTimeUtc()) {
        m_vault->remove(QString::fromLatin1(kRefreshTokenVaultKey));
        return QByteArray();
    }

    if (expiresAt) {
        *expiresAt = expiry;
    }
    return token;
}

bool AccountClient::restoreRefreshToken()
{
    QDateTime expiry;
    const QByteArray token = loadStoredRefreshToken(&expiry);
    if (token.isEmpty()) {
        return false;
    }

    m_refreshToken = token;
    m_refreshExpiresAt = expiry;
    emit tokensChanged();
    return true;
}

void AccountClient::get(const QString &path, bool authenticated, const QString &requestId,
                        Callback callback)
{
    Pending pending;
    pending.method = QStringLiteral("GET");
    pending.path = path;
    pending.authenticated = authenticated;
    pending.tokenKind = authenticated ? TokenKind::Access : TokenKind::None;
    pending.requestId = requestId;
    pending.callback = std::move(callback);
    send(std::move(pending));
}

void AccountClient::post(const QString &path, const QJsonObject &body, bool authenticated,
                         const QString &requestId, Callback callback)
{
    Pending pending;
    pending.method = QStringLiteral("POST");
    pending.path = path;
    pending.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    pending.authenticated = authenticated;
    pending.tokenKind = authenticated ? TokenKind::Access : TokenKind::None;
    pending.requestId = requestId;
    pending.callback = std::move(callback);
    send(std::move(pending));
}

void AccountClient::patch(const QString &path, const QJsonObject &body, bool authenticated,
                          const QString &requestId, Callback callback)
{
    Pending pending;
    pending.method = QStringLiteral("PATCH");
    pending.path = path;
    pending.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    pending.authenticated = authenticated;
    pending.tokenKind = authenticated ? TokenKind::Access : TokenKind::None;
    pending.requestId = requestId;
    pending.callback = std::move(callback);
    send(std::move(pending));
}

void AccountClient::del(const QString &path, bool authenticated, const QString &requestId,
                        Callback callback)
{
    Pending pending;
    pending.method = QStringLiteral("DELETE");
    pending.path = path;
    pending.authenticated = authenticated;
    pending.tokenKind = authenticated ? TokenKind::Access : TokenKind::None;
    pending.requestId = requestId;
    pending.callback = std::move(callback);
    send(std::move(pending));
}

void AccountClient::send(Pending pending)
{
    if (pending.requestId.isEmpty()) {
        pending.requestId = CryptoUtil::randomUuid();
    }
    dispatch(pending);
}

void AccountClient::dispatch(const Pending &pending)
{
    if (pending.authenticated && pending.tokenKind == TokenKind::Refresh &&
        m_refreshToken.isEmpty()) {
        deliverFailure(pending, QStringLiteral("not_authenticated"),
                       defaultMessageForCode(QStringLiteral("not_authenticated")));
        return;
    }

    if (pending.authenticated && pending.tokenKind == TokenKind::Access) {
        if (m_accessToken.isEmpty() || accessTokenExpired()) {
            if (m_refreshToken.isEmpty() || pending.refreshed) {
                deliverFailure(pending, QStringLiteral("session_expired"),
                               defaultMessageForCode(QStringLiteral("session_expired")));
                return;
            }

            Pending retry = pending;
            retry.refreshed = true;
            refreshThen([this, retry](bool ok) {
                if (!ok) {
                    deliverFailure(retry, QStringLiteral("session_expired"),
                                   defaultMessageForCode(QStringLiteral("session_expired")));
                    return;
                }
                dispatch(retry);
            });
            return;
        }
    }

    startRequest(pending);
}

void AccountClient::startRequest(const Pending &pending)
{
    if (pending.authenticated && m_deviceKey == nullptr) {
        deliverFailure(pending, QStringLiteral("no_device_key"),
                       defaultMessageForCode(QStringLiteral("no_device_key")));
        return;
    }

    const QUrl url(apiBaseUrl() + pending.path);
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        deliverFailure(pending, QStringLiteral("invalid_url"),
                       defaultMessageForCode(QStringLiteral("invalid_url")));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent",
                         QByteArray("LegionGames-Moonlight/") +
                             clientVersion().toUtf8());
    request.setRawHeader("X-Client-Version", clientVersion().toUtf8());
    request.setRawHeader("X-Request-Id", pending.requestId.toUtf8());
    request.setTransferTimeout(20000);

    if (pending.authenticated) {
        const QByteArray token = (pending.tokenKind == TokenKind::Refresh)
                                     ? m_refreshToken
                                     : m_accessToken;
        QString proofError;
        const QByteArray proof =
            buildProof(m_deviceKey, pending.body, pending.method, url.path(), &proofError);
        if (proof.isEmpty()) {
            deliverFailure(pending, proofError,
                           defaultMessageForCode(proofError));
            return;
        }

        request.setRawHeader("Authorization", "Bearer " + token);
        request.setRawHeader("X-Device-Proof", proof);
    }

    QNetworkReply *reply = m_nam->sendCustomRequest(
        request, pending.method.toUtf8(),
        pending.body.isEmpty() ? QByteArray() : pending.body);

    m_pending.insert(reply, pending);
}

void AccountClient::handleReply(QNetworkReply *reply, const Pending &pending)
{
    ApiResult result;

    const QVariant statusAttribute =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    result.httpStatus = statusAttribute.toInt();
    result.requestId = QString::fromUtf8(reply->rawHeader("X-Request-Id"));

    const QByteArray payload = reply->readAll();
    const bool transportFailure =
        reply->error() != QNetworkReply::NoError && !statusAttribute.isValid();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (document.isObject()) {
        result.json = document.object();
    }

    if (result.requestId.isEmpty()) {
        result.requestId = result.json.value(QStringLiteral("request_id")).toString();
    }

    result.networkError = transportFailure;
    result.ok = !transportFailure && result.httpStatus >= 200 && result.httpStatus < 300;

    if (!result.ok) {
        const QJsonValue errorValue = result.json.value(QStringLiteral("error"));
        if (errorValue.isObject()) {
            result.errorCode = errorValue.toObject().value(QStringLiteral("code")).toString();
        }
        else if (errorValue.isString()) {
            result.errorCode = errorValue.toString();
        }
        result.message = result.json.value(QStringLiteral("message")).toString();

        if (result.errorCode.isEmpty()) {
            if (transportFailure) {
                result.errorCode =
                    reply->error() == QNetworkReply::OperationCanceledError
                        ? QStringLiteral("timeout")
                        : QStringLiteral("network_error");
            }
            else {
                switch (result.httpStatus) {
                case 400:
                    result.errorCode = QStringLiteral("validation_error");
                    break;
                case 401:
                    result.errorCode = QStringLiteral("session_expired");
                    break;
                case 403:
                    result.errorCode = QStringLiteral("forbidden");
                    break;
                case 404:
                    result.errorCode = QStringLiteral("not_found");
                    break;
                case 409:
                    result.errorCode = QStringLiteral("conflict");
                    break;
                case 410:
                    result.errorCode = QStringLiteral("handoff_expired");
                    break;
                case 426:
                    result.errorCode = QStringLiteral("client_update_required");
                    break;
                case 429:
                    result.errorCode = QStringLiteral("rate_limited");
                    break;
                default:
                    result.errorCode = QStringLiteral("server_error");
                    break;
                }
            }
        }
        if (result.message.isEmpty()) {
            result.message = defaultMessageForCode(result.errorCode);
        }
    }

    reply->deleteLater();

    if (pending.isRefresh) {
        if (result.ok) {
            const QByteArray access =
                result.json.value(QStringLiteral("access_token")).toString().toUtf8();
            const QByteArray refresh =
                result.json.value(QStringLiteral("refresh_token")).toString().toUtf8();
            const int expiresIn =
                static_cast<int>(result.json.value(QStringLiteral("expires_in"))
                                     .toDouble(900));
            const int refreshExpiresIn =
                static_cast<int>(result.json.value(QStringLiteral("refresh_expires_in"))
                                     .toDouble(14 * 24 * 60 * 60));

            if (access.isEmpty()) {
                finishRefresh(false, QStringLiteral("session_expired"));
                return;
            }

            setTokens(access, refresh.isEmpty() ? m_refreshToken : refresh, expiresIn,
                      refreshExpiresIn);
            finishRefresh(true, QString());
        }
        else {
            finishRefresh(false, result.errorCode);
        }
        return;
    }

    // One retry after a 401, with the same request id and body. The refresh is
    // single-flight, so parallel requests do not rotate the token twice.
    if (result.httpStatus == 401 && pending.authenticated && !pending.retried &&
        !pending.refreshed && !m_refreshToken.isEmpty()) {
        Pending retry = pending;
        retry.retried = true;
        retry.refreshed = true;
        refreshThen([this, retry](bool ok) {
            if (!ok) {
                deliverFailure(retry, QStringLiteral("session_expired"),
                               defaultMessageForCode(QStringLiteral("session_expired")), 401);
                return;
            }
            dispatch(retry);
        });
        return;
    }

    if (result.httpStatus == 401 && pending.authenticated &&
        (pending.retried || pending.refreshed)) {
        clearTokens();
        emit authenticationLost(result.errorCode);
    }

    if (pending.callback) {
        pending.callback(result);
    }
}

void AccountClient::refreshThen(std::function<void(bool)> done)
{
    if (done) {
        m_refreshWaiters.append(std::move(done));
    }

    if (m_refreshing) {
        return;
    }
    if (m_refreshToken.isEmpty()) {
        finishRefresh(false, QStringLiteral("not_authenticated"));
        return;
    }

    m_refreshing = true;
    doRefresh();
}

void AccountClient::doRefresh()
{
    Pending pending;
    pending.method = QStringLiteral("POST");
    pending.path = QStringLiteral("/auth/refresh");
    pending.authenticated = true;
    pending.tokenKind = TokenKind::Refresh;
    pending.isRefresh = true;
    pending.requestId = CryptoUtil::randomUuid();
    // The refresh response is consumed here, not by a caller.
    pending.callback = nullptr;
    startRequest(pending);
}

void AccountClient::finishRefresh(bool ok, const QString &errorCode)
{
    m_refreshing = false;

    QList<std::function<void(bool)>> waiters;
    waiters.swap(m_refreshWaiters);

    if (!ok) {
        const bool hadSession = !m_refreshToken.isEmpty();
        clearTokens();
        if (hadSession) {
            emit authenticationLost(errorCode.isEmpty() ? QStringLiteral("session_revoked")
                                                        : errorCode);
        }
    }

    for (const auto &waiter : waiters) {
        if (waiter) {
            waiter(ok);
        }
    }
}

void AccountClient::deliverFailure(const Pending &pending, const QString &code,
                                   const QString &message, int httpStatus)
{
    ApiResult result;
    result.ok = false;
    result.httpStatus = httpStatus;
    result.errorCode = code;
    result.message = message;
    result.requestId = pending.requestId;
    if (pending.callback) {
        pending.callback(result);
    }
}
