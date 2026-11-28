#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "consent.h"

class AccountClient;
class DeviceKey;
class QueueEventStream;
class QProcess;
class QTimer;
class SessionHealthAgent;
class Vault;
struct ApiResult;

// QML context property `legionApi` (see notes/build-interfaces.md §4).
//
// One deliberate naming deviation: the interface document calls the
// registration method `register`, which is a C++ keyword and cannot be an
// invokable name. It is exposed as `registerAccount(label)`; everything else
// matches the documented name and order.
class LegionApi : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString handle READ handle NOTIFY handleChanged)
    Q_PROPERTY(QString onboardingStep READ onboardingStep NOTIFY onboardingStepChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY lastErrorChanged)
    Q_PROPERTY(QVariantList words READ words NOTIFY wordsChanged)
    Q_PROPERTY(QString catalogUpdatedAt READ catalogUpdatedAt NOTIFY catalogUpdatedAtChanged)
    Q_PROPERTY(QVariantMap device READ device NOTIFY deviceChanged)
    Q_PROPERTY(QString suggestedDeviceLabel READ suggestedDeviceLabel CONSTANT)
    Q_PROPERTY(QVariantList legalDocuments READ legalDocuments NOTIFY legalDocumentsChanged)
    Q_PROPERTY(QString ageNotice READ ageNotice NOTIFY legalDocumentsChanged)
    Q_PROPERTY(QString installPath READ installPath CONSTANT)
    Q_PROPERTY(QString recoveryKitPath READ recoveryKitPath NOTIFY recoveryKitPathChanged)
    Q_PROPERTY(QString netbirdStatus READ netbirdStatus NOTIFY netbirdStatusChanged)
    // Server id of the lease the device is bound to (also the /sessions id).
    Q_PROPERTY(QString leaseId READ leaseId NOTIFY leaseIdChanged)
    // True while the encrypted one-shot connect material is waiting to be
    // consumed. The material survives page changes and app restarts.
    Q_PROPERTY(bool connectPending READ connectPending NOTIFY connectPendingChanged)
    // Optional referral code from onboarding or a legiongames://ref/<code>
    // deep link. It is sent only with the registration consent payload.
    Q_PROPERTY(QString referralCode READ referralCode NOTIFY referralCodeChanged)
    // Server timestamps of the account deletion grace period. They survive a
    // restart through the account vault record so the deletion gate can render
    // even when the revoked device can no longer call /me.
    Q_PROPERTY(QString deletionRequestedAt READ deletionRequestedAt NOTIFY deletionStateChanged)
    Q_PROPERTY(QString purgeAfter READ purgeAfter NOTIFY deletionStateChanged)
    // Latest optional read models. The generic response(op, data) signal is
    // still authoritative for views; these properties keep bindings simple.
    Q_PROPERTY(QVariantMap gamification READ gamification NOTIFY gamificationChanged)
    Q_PROPERTY(QVariantMap accountSummary READ accountSummary NOTIFY accountSummaryChanged)

public:
    explicit LegionApi(QObject *parent = nullptr);
    ~LegionApi() override;

    QString state() const { return m_state; }
    QString handle() const { return m_handle; }
    QString onboardingStep() const { return m_onboardingStep; }
    bool busy() const { return m_busy; }
    QString lastError() const { return m_lastError; }
    QString lastErrorCode() const { return m_lastErrorCode; }
    QVariantList words() const { return m_words; }
    QString catalogUpdatedAt() const { return m_catalogUpdatedAt; }
    QVariantMap device() const { return m_device; }
    QString suggestedDeviceLabel() const { return deviceLabel(QString()); }
    QVariantList legalDocuments() const { return m_legalDocuments; }
    QString ageNotice() const { return m_ageNotice; }
    QString installPath() const;
    QString recoveryKitPath() const;
    QString netbirdStatus() const { return m_netbirdStatus; }
    QString leaseId() const { return m_leaseId; }
    bool connectPending() const { return m_connectPending; }
    QString referralCode() const { return m_referralCode; }
    QString deletionRequestedAt() const { return m_deletionRequestedAt; }
    QString purgeAfter() const { return m_purgeAfter; }
    QVariantMap gamification() const { return m_gamification; }
    QVariantMap accountSummary() const { return m_accountSummary; }

    Q_INVOKABLE void initialize();

    Q_INVOKABLE void setConsent(bool agreement, bool pdnConsent);
    Q_INVOKABLE void submitConsent();
    Q_INVOKABLE void registerAccount(const QString &label);
    Q_INVOKABLE void login();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void confirmWordsSaved();
    Q_INVOKABLE void reissueWords();
    Q_INVOKABLE void recoverPrepare(const QString &handle, const QStringList &words);
    Q_INVOKABLE void recoverComplete(const QString &handle, const QStringList &words,
                                     const QString &label);
    Q_INVOKABLE void refreshMe();
    Q_INVOKABLE void reprintRecoveryKit();
    Q_INVOKABLE void resetLocalAccount();
    Q_INVOKABLE void checkNetbirdStatus();
    Q_INVOKABLE void deleteAccount(const QString &handle, const QStringList &words);
    Q_INVOKABLE void restoreAccount(const QString &handle, const QStringList &words);

    Q_INVOKABLE void setReferralCode(const QString &code);

    Q_INVOKABLE void getGamification();
    Q_INVOKABLE void getAccountSummary();

    Q_INVOKABLE void getLegalDocuments();
    Q_INVOKABLE void reissueHandoff(const QString &publicId);

    Q_INVOKABLE void getClubs(bool availableOnly = true);
    Q_INVOKABLE void getClub(const QString &slug);
    Q_INVOKABLE void getWallet();
    Q_INVOKABLE void getWalletHistory(const QString &cursor);
    Q_INVOKABLE void createTopup(int clubId, const QString &amount);
    Q_INVOKABLE void createTopupWithPromo(int clubId, const QString &amount,
                                          const QString &promoCode);
    Q_INVOKABLE void pollIntent(const QString &publicId);

    Q_INVOKABLE void startSession(int clubId);
    Q_INVOKABLE void getCurrentSession();
    Q_INVOKABLE void getSessionHistory();
    Q_INVOKABLE void stopSession(const QString &id);
    Q_INVOKABLE void reconnectSession(const QString &id);
    Q_INVOKABLE void connectSession(const QString &id);

    // Re-emits persisted one-shot connect material (vault-backed). When
    // expectedSessionId is non-empty the material must belong to that lease.
    // Returns true when valid material was found and connectMaterialReady was
    // emitted.
    Q_INVOKABLE bool resumeConnectMaterial(const QString &expectedSessionId = QString());
    // Drops the persisted material and the pending marker. Called when the
    // lease changes, stops, is consumed, or the server says it is gone.
    Q_INVOKABLE void clearConnectMaterial();

    // Application shutdown hook. Emits signOutStarted (so QML can tear down
    // the NetBird session profile) and sends a bounded lease stop before the
    // process exits. The server watchdog remains the backstop.
    Q_INVOKABLE void shutdownForQuit();

    Q_INVOKABLE void getQueue();
    Q_INVOKABLE void joinQueue(int clubId);
    Q_INVOKABLE void leaveQueue(const QString &id);
    Q_INVOKABLE void claimQueue(const QString &id);

    // Latest lease health notice for the SDL loop. The agent also emits
    // sessionHealthNotice; takeSessionNotice() is the pull side and consumes
    // the oldest queued notice. Empty when nothing is pending.
    Q_INVOKABLE QVariantMap takeSessionNotice();

    Q_INVOKABLE void openExternal(const QString &url);
    Q_INVOKABLE void openInstallFolder();
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void openVaultFolder();
    Q_INVOKABLE QString vaultPath() const;
    Q_INVOKABLE void retryFailedStep();

    // Called by main.cpp when a legiongames://payment/<uuid> launch or a
    // forwarded second-instance URL arrives. Only triggers a poll; the deep
    // link never credits anything.
    Q_SLOT void notifyPaymentReturn(const QString &publicId);

    // Called by main.cpp for legiongames://ref/<code>. Only pre-fills the
    // onboarding field; the server validates the code at registration.
    Q_SLOT void notifyReferralCode(const QString &code);

signals:
    void stateChanged();
    void handleChanged();
    void onboardingStepChanged();
    void busyChanged();
    void lastErrorChanged();
    void catalogUpdatedAtChanged();
    void deviceChanged();
    void legalDocumentsChanged();
    void wordsChanged();
    void recoveryKitPathChanged();
    void netbirdStatusChanged();
    void leaseIdChanged();
    void connectPendingChanged();
    void referralCodeChanged();
    void deletionStateChanged();
    void gamificationChanged();
    void accountSummaryChanged();
    void wordsReady(const QString &handle, const QStringList &words);
    void consentRequired();
    void paymentReturned(const QString &publicId);
    void response(const QString &op, const QVariantMap &data);
    void failed(const QString &op, const QString &code, const QString &message);
    // Emitted synchronously at the start of logout()/shutdownForQuit(), before
    // tokens or the local account record are cleared. QML uses it to stop the
    // NetBird session profile while the lease is still stoppable.
    void signOutStarted();
    // The persisted connect material is ready for ComputerManager. Emitted on
    // a fresh /connect response, on resume, and after a restart.
    void connectMaterialReady(const QString &sessionId, const QVariantMap &data);

    // Additive: lets the words screen require an explicit acknowledgement when
    // the recovery kit landed in a cloud-synced Documents folder.
    void recoveryKitWritten(const QString &path, bool syncedFolder);
    void queueEvent(const QString &name, const QVariantMap &data);
    // Lease health, delivered by the dedicated worker thread. The notice
    // carries code, minutes_left, close_at and message, plus action and
    // health_state when the server supplied them.
    void sessionHealthNotice(const QString &code, const QVariantMap &notice);
    void sessionHealthAction(const QString &action, const QVariantMap &health);

private:
    bool ensureClient(const QString &op);
    bool ensureVault(const QString &op);
    bool ensureAccount(const QString &op);
    bool ensureDeviceKey(const QString &op, bool create);
    void replaceDeviceKey(DeviceKey *newKey);

    void setState(const QString &state);
    void setBusy(bool busy);
    void clearError();
    void fail(const QString &op, const QString &code, const QString &message,
              std::function<void()> retry = nullptr);
    void failFromResult(const QString &op, const ApiResult &result,
                        std::function<void()> retry = nullptr);
    void silentFailure(const QString &code, const QString &message);

    void loadAccountRecord();
    QJsonObject loadVaultObject(const QString &key, bool *ok = nullptr) const;
    void persistAccount();
    void storeConsentRecord(const QJsonObject &payload);
    void storeWords(const QStringList &words);
    void setHandle(const QString &handle);
    void setOnboardingStep(const QString &step);
    void applyDevice(const QJsonObject &device);
    void emitLegalDocuments();
    void storePendingIntent(const QString &publicId);
    QString loadStoredPendingIntent() const;
    void clearPendingIntent();
    QStringList storedWords() const;
    void writeRecoveryKit(const QStringList &words);
    void applyMe(const QJsonObject &json);
    void applyAuthResponse(const QString &op, const ApiResult &result,
                           DeviceKey *adoptedKey = nullptr);
    void handleAuthFailure(const QString &op, const ApiResult &result, bool silent);
    void performLogin(bool silent);
    void recoverLostRegistration();
    void signInAfterAuth();

    void fetchLegalDocuments(std::function<void(bool)> done);
    QByteArray signCeremony(const QString &ceremonyId, const QString &challenge,
                            const QString &action);
    QString deviceLabel(const QString &requested) const;
    QList<LegalDocumentInfo> consentDocuments() const;

    void openHandoffForIntent(const QString &publicId, const QString &handoffUrl,
                              std::function<void(const QString &url)> done,
                              std::function<void(const ApiResult &)> failed);
    void startIntentPolling(const QString &publicId);
    void pollIntentOnce();
    void startQueueEvents();
    void stopQueueEvents();

    void startHealthAgent(const QJsonObject &payload, const QString &leaseId);
    void stopHealthAgent();
    static QString healthTokenFromJson(const QJsonObject &json);
    static int healthNextReportMs(const QJsonObject &json);

    void setLeaseId(const QString &leaseId);
    void setConnectPending(bool pending);
    void setDeletionState(const QString &requestedAt, const QString &purgeAfter);
    void persistConnectMaterial(const QString &sessionId, const QJsonObject &payload);
    QJsonObject loadConnectMaterial();
    void clearStoredConnectMaterial();
    void finishLogout(int generation);

    QVariantMap sessionData(const QJsonObject &json);
    static QVariantMap entryData(const QJsonObject &json);
    static QStringList wordsFromJson(const QJsonObject &json);
    static bool normalizeAmount(const QString &input, QString *output);

    Vault *m_vault = nullptr;
    AccountClient *m_client = nullptr;
    DeviceKey *m_deviceKey = nullptr;
    QueueEventStream *m_queueEvents = nullptr;
    SessionHealthAgent *m_healthAgent = nullptr;
    QTimer *m_pollTimer = nullptr;

    QString m_state = QStringLiteral("no_account");
    QString m_handle;
    QString m_onboardingStep;
    bool m_busy = false;
    QString m_lastError;
    QString m_lastErrorCode;
    QVariantList m_words;
    QString m_catalogUpdatedAt;
    QVariantMap m_device;
    QVariantList m_legalDocuments;
    QVariantMap m_gamification;
    QVariantMap m_accountSummary;
    QString m_referralCode;
    QString m_deletionRequestedAt;
    QString m_purgeAfter;

    bool m_initialized = false;
    bool m_agreement = false;
    bool m_pdnConsent = false;
    QList<LegalDocumentInfo> m_documents;
    QString m_ageNotice;

    QString m_pendingPaymentId;
    QString m_emittedPaymentId;
    QString m_pendingRecoveryTicket;
    QString m_pendingRecoveryHandle;
    QStringList m_pendingRecoveryWords;
    QString m_recoveryKitPath;
    QString m_netbirdStatus = QStringLiteral("unknown");
    QProcess *m_netbirdProcess = nullptr;

    QString m_pollPublicId;
    int m_pollCount = 0;
    bool m_pollRequestInFlight = false;

    QString m_leaseId;
    bool m_connectPending = false;
    bool m_logoutInProgress = false;
    bool m_logoutFinalizing = false;
    int m_logoutGeneration = 0;
    int m_sessionMutationSeq = 0;

    std::function<void()> m_retry;
};
