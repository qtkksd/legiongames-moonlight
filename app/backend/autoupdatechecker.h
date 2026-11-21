#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

#include "account/updatemanifest.h"

class QNetworkReply;

class AutoUpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit AutoUpdateChecker(QObject *parent = nullptr);

    Q_INVOKABLE void start();

    // Downloads, verifies and launches the installer on Windows, or replaces
    // the running AppImage on Linux. Other platforms open the download page.
    Q_INVOKABLE void installUpdate(const QString &url);

    // False when updates were explicitly disabled at build/run time
    // (LG_UPDATES_DISABLED). A disabled updater never executes anything.
    Q_INVOKABLE bool updatesEnabled() const { return !m_UpdatesDisabled; }

    // False while the pinned Ed25519 key is still the placeholder. In that
    // state the updater fails closed: no manifest is trusted, no installer runs.
    Q_INVOKABLE bool verificationConfigured() const;

signals:
    void onUpdateAvailable(QString newVersion, QString url);
    void onUpdateFailed(QString code, QString message);

private:
    void handleManifestReply(QNetworkReply *reply);
    void downloadVerifiedAsset(const UpdateAsset &asset);
    void rejectUpdate(const QString &code, const QString &message);

    static bool commitsEqual(const QString &a, const QString &b);
    static QString platformAssetKey();
    static QString randomTempPath();

    QString m_CurrentCommit;
    int m_CurrentBuild;
    QNetworkAccessManager *m_Nam;

    UpdateAsset m_PendingAsset;
    bool m_HasPendingAsset = false;
    bool m_UpdatesDisabled = false;
};
