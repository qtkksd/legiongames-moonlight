#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;
class QLocalSocket;
class QSharedMemory;

// legiongames:// deep links and single-instance forwarding.
//
// A protocol launch (payment return) is the only case where a second process
// must hand its URL to the running app and exit. A normal second launch keeps
// the historical multi-window behaviour, so the guard is scoped to deep links.
class DeepLink : public QObject
{
    Q_OBJECT

public:
    explicit DeepLink(QObject *parent = nullptr);
    ~DeepLink() override;

    // Returns the payment UUID for a legiongames://payment/<uuid> URL, or an
    // empty string when the URL is not a payment link.
    static QString paymentIdFromUrl(const QString &url);
    // Returns the referral code for legiongames://ref/<code>, or an empty
    // string. The code is only pre-filled in onboarding; the server validates
    // it during registration.
    static QString referralCodeFromUrl(const QString &url);
    static bool isDeepLinkLaunch(const QStringList &arguments);
    static QString paymentIdFromArguments(const QStringList &arguments);
    static QString referralCodeFromArguments(const QStringList &arguments);
    static QString canonicalPaymentUrl(const QString &paymentId);
    static QString canonicalReferralUrl(const QString &code);

    // Secondary instance: send the URL to the running primary. Returns true
    // when the URL was delivered and this process should exit.
    bool forwardToPrimary(const QString &url, int timeoutMs = 500);

    // Primary instance: claim the single-instance marker and listen for
    // forwarded URLs.
    bool startPrimary();

    bool isPrimary() const { return m_primary; }

    // Cold-start URL handed over before the account API was ready.
    void setPendingPaymentId(const QString &paymentId) { m_pendingPaymentId = paymentId; }
    QString takePendingPaymentId();
    void setPendingReferralCode(const QString &code) { m_pendingReferralCode = code; }
    QString takePendingReferralCode();

signals:
    void paymentUrlReceived(const QString &paymentId);
    void referralCodeReceived(const QString &code);

private:
    void handleConnection();
    void handleSocketData(QLocalSocket *socket);
    static QString serverName();
    static QString sharedMemoryKey();

    QSharedMemory *m_shared = nullptr;
    QLocalServer *m_server = nullptr;
    QHash<QLocalSocket *, QByteArray> m_buffers;
    QString m_pendingPaymentId;
    QString m_pendingReferralCode;
    bool m_primary = false;
};
