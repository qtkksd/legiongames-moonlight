#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class Vault;
class DeviceKey;

struct ApiResult
{
    bool ok = false;
    int httpStatus = 0;
    QJsonObject json;
    QString errorCode;
    QString message;
    QString requestId;
    bool networkError = false;
    // Transport metadata for the request-policy layer. A GET may be retried
    // automatically; a mutation must use the same idempotency key instead.
    bool retryable = false;
    int retryAfterSeconds = 0;
};

// JSON client for the LegionGames app API. The base URL defaults to
// production; this staging branch uses django-stg.legiongames.cell and
// LG_API_BASE_URL can override either build.
//
// - Bearer access token stays in memory (15 minutes).
// - Rotating refresh token lives in the OS vault (14 days).
// - Every authenticated request carries a v1 X-Device-Proof: sorted-key JSON,
//   base64url, bh = SHA-256 of the exact body bytes, iat/exp window and a
//   random jti; the signature is over the base64url payload bytes.
// - One retry after a 401, using a single-flight refresh, reusing the same
//   X-Request-Id so a mutation is not applied twice.
class AccountClient : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const ApiResult &)>;

    explicit AccountClient(Vault *vault, QObject *parent = nullptr);
    ~AccountClient() override;

    static QString apiBaseUrl();
    static QString clientVersion();

    // Absolute URL for a path below the account API base (which already ends
    // in /app/v1), e.g. apiUrl("/sessions/<id>/pin"). Every credential-bearing
    // caller (account API, lease PIN relay) must resolve its URL here so a
    // LG_API_BASE_URL test/staging override applies to all of them.
    static QUrl apiUrl(const QString &path);

    // Absolute URL on the same origin but outside the versioned API prefix.
    // Used by the queue SSE endpoint, which is mounted at the site root.
    static QUrl apiOriginUrl(const QString &path);

    // Applies the one transport policy shared by the account API, the queue
    // SSE stream and the lease PIN relay: no cookies, no redirects, HSTS and
    // the same certificate pins. Callers own the manager.
    static void configureNetworkManager(QNetworkAccessManager *manager);

    static QByteArray proofPayload(const QByteArray &body, const QString &method,
                                   const QString &path, qint64 iat, qint64 exp,
                                   const QByteArray &jtiBase64Url);
    static QByteArray buildProof(DeviceKey *key, const QByteArray &body, const QString &method,
                                 const QString &path, QString *errorCode);
    static bool isRetryableRequest(const QByteArray &method, bool networkError, int httpStatus);
    static bool isPinnedCertificate(const QByteArray &digest);

    // Terminal authentication failures: the stored device key may still be
    // valid, but the session cannot be used again without a new login
    // ceremony. The UI must leave signed_in and offer login/recovery.
    static bool isTerminalAuthError(const QString &code);

    void setDeviceKey(DeviceKey *key);
    DeviceKey *deviceKey() const { return m_deviceKey; }

    bool hasAccessToken() const { return !m_accessToken.isEmpty(); }
    bool hasRefreshToken() const { return !m_refreshToken.isEmpty(); }
    QByteArray accessToken() const { return m_accessToken; }

    void setTokens(const QByteArray &accessToken, const QByteArray &refreshToken,
                   int expiresInSeconds, int refreshExpiresInSeconds = 14 * 24 * 60 * 60);
    void clearTokens();
    bool restoreRefreshToken();

    void get(const QString &path, bool authenticated, const QString &requestId, Callback callback);
    void post(const QString &path, const QJsonObject &body, bool authenticated,
              const QString &requestId, Callback callback);
    void patch(const QString &path, const QJsonObject &body, bool authenticated,
               const QString &requestId, Callback callback);
    void del(const QString &path, bool authenticated, const QString &requestId, Callback callback);

signals:
    void tokensChanged();
    void authenticationLost(const QString &code);
    void refreshTokenPersistFailed(const QString &code);

private:
    enum class TokenKind {
        None,
        Access,
        Refresh,
    };

    struct Pending
    {
        QString method;
        QString path;
        QByteArray body;
        bool authenticated = false;
        TokenKind tokenKind = TokenKind::None;
        bool isRefresh = false;
        bool retried = false;
        bool refreshed = false;
        int tokenEpoch = 0;
        int refreshGeneration = 0;
        QString requestId;
        Callback callback;
    };

    void send(Pending pending);
    void dispatch(const Pending &pending);
    void startRequest(const Pending &pending);
    void handleReply(QNetworkReply *reply, const Pending &pending);

    void refreshThen(std::function<void(bool)> done);
    void doRefresh();
    void finishRefresh(bool ok, const QString &errorCode);

    bool accessTokenExpired() const;
    bool storeRefreshToken(const QByteArray &token, const QDateTime &expiresAt);
    QByteArray loadStoredRefreshToken(QDateTime *expiresAt);

    static void deliverFailure(const Pending &pending, const QString &code, const QString &message,
                               int httpStatus = 0);

    QNetworkAccessManager *m_nam = nullptr;
    Vault *m_vault = nullptr;
    DeviceKey *m_deviceKey = nullptr;

    QByteArray m_accessToken;
    QByteArray m_refreshToken;
    QDateTime m_accessExpiresAt;
    QDateTime m_refreshExpiresAt;

    bool m_refreshing = false;
    QList<std::function<void(bool)>> m_refreshWaiters;

    // Incremented on every clearTokens(). A refresh reply that belongs to an
    // older epoch is discarded so a logout cannot be undone by a slow refresh.
    int m_tokenEpoch = 0;
    int m_refreshGeneration = 0;

    QHash<QNetworkReply *, Pending> m_pending;
};
