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
class QTimer;
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
    Q_INVOKABLE void deleteAccount(const QString &handle, const QStringList &words);

    Q_INVOKABLE void getLegalDocuments();
    Q_INVOKABLE void reissueHandoff(const QString &publicId);

    Q_INVOKABLE void getClubs(bool availableOnly = true);
    Q_INVOKABLE void getClub(const QString &slug);
    Q_INVOKABLE void getWallet();
    Q_INVOKABLE void getWalletHistory(const QString &cursor);
    Q_INVOKABLE void createTopup(int clubId, const QString &amount);
    Q_INVOKABLE void pollIntent(const QString &publicId);

    Q_INVOKABLE void startSession(int clubId);
    Q_INVOKABLE void getCurrentSession();
    Q_INVOKABLE void stopSession(const QString &id);
    Q_INVOKABLE void extendSession(const QString &id, const QString &amount);
    Q_INVOKABLE void reconnectSession(const QString &id);

    Q_INVOKABLE void getQueue();
    Q_INVOKABLE void joinQueue(int clubId);
    Q_INVOKABLE void leaveQueue(const QString &id);
    Q_INVOKABLE void claimQueue(const QString &id);

    Q_INVOKABLE void openExternal(const QString &url);
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void retryFailedStep();

    // Called by main.cpp when a legiongames://payment/<uuid> launch or a
    // forwarded second-instance URL arrives. Only triggers a poll; the deep
    // link never credits anything.
    Q_SLOT void notifyPaymentReturn(const QString &publicId);

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
    void wordsReady(const QString &handle, const QStringList &words);
    void consentRequired();
    void paymentReturned(const QString &publicId);
    void response(const QString &op, const QVariantMap &data);
    void failed(const QString &op, const QString &code, const QString &message);

    // Additive: lets the words screen require an explicit acknowledgement when
    // the recovery kit landed in a cloud-synced Documents folder.
    void recoveryKitWritten(const QString &path, bool syncedFolder);

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

    static QVariantMap sessionData(const QJsonObject &json);
    static QVariantMap entryData(const QJsonObject &json);
    static QStringList wordsFromJson(const QJsonObject &json);
    static bool normalizeAmount(const QString &input, QString *output);

    Vault *m_vault = nullptr;
    AccountClient *m_client = nullptr;
    DeviceKey *m_deviceKey = nullptr;
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

    QString m_pollPublicId;
    int m_pollCount = 0;

    std::function<void()> m_retry;
};
