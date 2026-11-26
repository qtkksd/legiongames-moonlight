#include "autoupdatechecker.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QtDebug>

#ifndef VERSION_STR
#define VERSION_STR "0.0.0"
#endif

// LG_COMMIT / LG_BUILD are injected by the build (see app.pro); fall back to the
// version string / 0 for local or unsupported builds.
#ifndef LG_COMMIT
#define LG_COMMIT VERSION_STR
#endif
#ifndef LG_BUILD
#define LG_BUILD "0"
#endif

// The pkgs manifest is the single source of truth for the update: version,
// monotonic build number, and per-OS asset URL + size + SHA-256. It is signed
// with Ed25519; see account/updatemanifest.h for the exact schema and the
// pinned-key placeholder that must be replaced before release.
#define LG_MANIFEST_URL "https://pkgs.legiongames.ru/moonlight/latest/manifest.json"
#define LG_FALLBACK_URL "https://pkgs.legiongames.ru/moonlight/latest/"

AutoUpdateChecker::AutoUpdateChecker(QObject *parent) :
    QObject(parent)
{
    m_Nam = new QNetworkAccessManager(this);

    // Never communicate over HTTP
    m_Nam->setStrictTransportSecurityEnabled(true);

    // Allow HTTP redirects
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    m_CurrentCommit = QString::fromLatin1(LG_COMMIT);
    m_CurrentBuild = QString::fromLatin1(LG_BUILD).toInt();

#ifdef LG_UPDATES_DISABLED
    m_UpdatesDisabled = true;
#endif
    if (qEnvironmentVariableIsSet("LG_UPDATES_DISABLED")) {
        m_UpdatesDisabled = true;
    }

    qDebug() << "Current LegionGames Moonlight commit:" << m_CurrentCommit
             << "build:" << m_CurrentBuild
             << "updates enabled:" << !m_UpdatesDisabled;
}

bool AutoUpdateChecker::verificationConfigured() const
{
    return UpdateManifestParser::verificationConfigured();
}

void AutoUpdateChecker::start()
{
    if (!m_Nam) {
        Q_ASSERT(m_Nam);
        return;
    }

    clearUpdateState();

#ifdef LG_STAGING_BUILD
    // A staging build must never pull the production installer over itself.
    qInfo() << "Staging build: update checks disabled";
    return;
#endif

    if (m_UpdatesDisabled) {
        qInfo() << "Update checks are disabled (LG_UPDATES_DISABLED)";
        return;
    }

    if (!UpdateManifestParser::verificationConfigured()) {
        // Fail closed. The shipped placeholder key cannot verify anything, so
        // offering an update here would mean executing an unverified binary.
        const QString code = QStringLiteral("update_verification_unavailable");
        qWarning() << "Update verification key is not configured; refusing updates";
        emit onUpdateFailed(code, UpdateManifestParser::errorMessageFor(code));
        return;
    }

    QNetworkRequest request{QUrl(QStringLiteral(LG_MANIFEST_URL))};
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(20000);
#endif

    QNetworkReply *reply = m_Nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleManifestReply(reply);
    });
}

bool AutoUpdateChecker::commitsEqual(const QString &a, const QString &b)
{
    const QString left = a.trimmed().toLower();
    const QString right = b.trimmed().toLower();
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }

    // Tags are short SHAs while the embedded commit may be the full SHA (or
    // vice versa), so accept a prefix match in either direction.
    return left == right || left.startsWith(right) || right.startsWith(left);
}

QString AutoUpdateChecker::platformAssetKey()
{
#if defined(Q_OS_WIN32)
    return QStringLiteral("windows");
#elif defined(Q_OS_DARWIN)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

void AutoUpdateChecker::handleManifestReply(QNetworkReply *reply)
{
    Q_ASSERT(reply->isFinished());

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Update manifest fetch failed:"
                   << reply->error() << reply->errorString();
        const QString code = QStringLiteral("update_download_failed");
        emit onUpdateFailed(code, UpdateManifestParser::errorMessageFor(code));
        reply->deleteLater();
        return;
    }

    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    UpdateManifest manifest;
    QString errorCode;
    if (!UpdateManifestParser::verifyAndParse(payload, &manifest, &errorCode)) {
        qWarning() << "Update manifest rejected:" << errorCode;
        emit onUpdateFailed(errorCode, UpdateManifestParser::errorMessageFor(errorCode));
        return;
    }

    if (manifest.disabled) {
        qInfo() << "Update manifest is signed and marked disabled; no update offered";
        clearUpdateState();
        return;
    }

    const QString latestVersion = manifest.version;
    const QString latestCommit = manifest.commitFull;
    const int latestBuild = manifest.build;

    if (latestBuild < m_CurrentBuild) {
        // Signed, but older than this build. Never downgrade.
        qInfo() << "Ignoring older signed manifest: build" << latestBuild
                << "current" << m_CurrentBuild;
        clearUpdateState();
        return;
    }

    if (latestBuild == m_CurrentBuild && commitsEqual(m_CurrentCommit, latestCommit)) {
        qDebug() << "Moonlight is up to date (build" << m_CurrentBuild << ")";
        clearUpdateState();
        return;
    }

    if (m_CurrentBuild > 0 && m_CurrentBuild < manifest.minSupportedBuild) {
        qWarning() << "This build" << m_CurrentBuild
                   << "is below the supported floor" << manifest.minSupportedBuild
                   << "- a forced update is required";
    }

    const QMap<QString, UpdateAsset> &assets = manifest.assets;
    const QString key = platformAssetKey();
    if (!assets.contains(key)) {
        qInfo() << "Signed manifest has no asset for platform" << key;
        clearUpdateState();
        return;
    }

    m_PendingAsset = assets.value(key);
    m_HasPendingAsset = true;

    m_updateAvailable = true;
    m_updateVersion = latestVersion.isEmpty() ? latestCommit : latestVersion;
    m_updateUrl = m_PendingAsset.url;
    emit updateStateChanged();

    qDebug() << "Update available: version" << latestVersion << "build" << latestBuild
             << m_PendingAsset.url;
    emit onUpdateAvailable(m_updateVersion, m_updateUrl);
}

QString AutoUpdateChecker::randomTempPath()
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        tempDir = QDir::tempPath();
    }

    const QString suffix =
#if defined(Q_OS_WIN32)
        QStringLiteral(".exe");
#else
        QStringLiteral(".AppImage");
#endif

    const QString name =
        QStringLiteral("lgupdate-%1-%2%3")
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'))
            .arg(suffix);

    return QDir(tempDir).filePath(name);
}

void AutoUpdateChecker::rejectUpdate(const QString &code, const QString &message)
{
    qWarning() << "Update rejected:" << code << message;
    clearUpdateState();
    emit onUpdateFailed(code, message.isEmpty() ? UpdateManifestParser::errorMessageFor(code)
                                                : message);
}

void AutoUpdateChecker::clearUpdateState()
{
    if (!m_updateAvailable && m_updateVersion.isEmpty() && m_updateUrl.isEmpty()) {
        return;
    }
    m_updateAvailable = false;
    m_updateVersion.clear();
    m_updateUrl.clear();
    m_HasPendingAsset = false;
    m_PendingAsset = UpdateAsset();
    emit updateStateChanged();
}

void AutoUpdateChecker::installUpdate(const QString &url)
{
    if (m_UpdatesDisabled) {
        return;
    }

    if (!m_HasPendingAsset || m_PendingAsset.url != url) {
        // Only a URL that came out of a verified manifest can ever be executed.
        rejectUpdate(QStringLiteral("update_unverified"),
                     QStringLiteral("Обновление не проверено и не будет установлено."));
        return;
    }

    // Find the asset entry again so a stale signal cannot cross-wire manifests.
    const UpdateAsset asset = m_PendingAsset;

#if !defined(Q_OS_WIN32) && !defined(APP_IMAGE)
    // Nothing executable is downloaded here: the browser handles the file and
    // the platform's own package flow. No verification gap on this path.
    QDesktopServices::openUrl(QUrl(asset.url));
#else
    downloadVerifiedAsset(asset);
#endif
}

void AutoUpdateChecker::downloadVerifiedAsset(const UpdateAsset &asset)
{
    const QString targetPath = randomTempPath();
    if (targetPath.isEmpty()) {
        rejectUpdate(QStringLiteral("update_download_failed"),
                     QStringLiteral("Не удалось выбрать папку для загрузки."));
        return;
    }

    qInfo() << "Downloading verified update to" << targetPath << "from" << asset.url;

    QNetworkRequest request{QUrl(asset.url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(600000);
#endif

    QFile *file = new QFile(targetPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open update path for writing:" << targetPath;
        delete file;
        rejectUpdate(QStringLiteral("update_download_failed"),
                     QStringLiteral("Не удалось сохранить файл обновления."));
        return;
    }

    // Owner-only permissions on Unix; on Windows the per-user temp directory
    // already restricts access and the ACL is tightened by the installer flow.
    QFile::setPermissions(targetPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QNetworkReply *reply = m_Nam->get(request);

    // Stream chunks to disk so the UI never blocks on a large download.
    connect(reply, &QNetworkReply::readyRead, file, [reply, file]() {
        file->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, targetPath, asset]() {
                file->close();
                const qint64 size = file->size();
                file->deleteLater();
                reply->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    qWarning() << "Update download failed:" << reply->errorString();
                    QFile::remove(targetPath);
                    rejectUpdate(QStringLiteral("update_download_failed"),
                                 QStringLiteral("Не удалось скачать обновление."));
                    return;
                }

                if (size != asset.size) {
                    qWarning() << "Update size mismatch: got" << size << "expected"
                               << asset.size;
                    QFile::remove(targetPath);
                    rejectUpdate(QStringLiteral("update_size_mismatch"),
                                 UpdateManifestParser::errorMessageFor(
                                     QStringLiteral("update_size_mismatch")));
                    return;
                }

                QFile downloaded(targetPath);
                if (!downloaded.open(QIODevice::ReadOnly)) {
                    QFile::remove(targetPath);
                    rejectUpdate(QStringLiteral("update_download_failed"),
                                 QStringLiteral("Не удалось проверить файл обновления."));
                    return;
                }

                QCryptographicHash hash(QCryptographicHash::Sha256);
                const bool hashed = hash.addData(&downloaded);
                downloaded.close();

                if (!hashed ||
                    QString::fromLatin1(hash.result().toHex()).compare(asset.sha256,
                                                                       Qt::CaseInsensitive) != 0) {
                    qWarning() << "Update SHA-256 mismatch for" << targetPath;
                    QFile::remove(targetPath);
                    rejectUpdate(QStringLiteral("update_hash_mismatch"),
                                 UpdateManifestParser::errorMessageFor(
                                     QStringLiteral("update_hash_mismatch")));
                    return;
                }

#if defined(Q_OS_WIN32)
                // Run the WiX Burn bundle interactively (NOT /quiet) so the user
                // sees the standard installer UI. The bundle's LaunchTarget adds
                // a default-checked "Launch Moonlight" action. /norestart
                // suppresses any reboot prompt. Quit now so the installer can
                // replace the running binary.
                qInfo() << "Launching verified installer:" << targetPath;
                QProcess::startDetached(targetPath,
                                        QStringList() << QStringLiteral("/norestart"));
                QCoreApplication::quit();
#else
                // AppImage: the manifest and the artifact are both verified, so
                // replacing the running bundle is safe.
                QFile::setPermissions(targetPath,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                          QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                          QFileDevice::ExeOther);

                const QString appImagePath = qEnvironmentVariable("APPIMAGE");
                if (appImagePath.isEmpty() || !QFile::exists(appImagePath)) {
                    qWarning() << "APPIMAGE env var not set; keeping the download at"
                               << targetPath;
                    QDesktopServices::openUrl(QUrl(QStringLiteral(LG_FALLBACK_URL)));
                    return;
                }

                if (!QFile::rename(targetPath, appImagePath)) {
                    // rename() is atomic on the same filesystem; if the temp dir
                    // is elsewhere, fall back to remove+rename and accept the
                    // small window.
                    QFile::remove(appImagePath);
                    if (!QFile::rename(targetPath, appImagePath)) {
                        qWarning() << "Failed to replace AppImage:" << appImagePath;
                        QDesktopServices::openUrl(QUrl(QStringLiteral(LG_FALLBACK_URL)));
                        return;
                    }
                }

                qInfo() << "Relaunching updated AppImage:" << appImagePath;
                QProcess::startDetached(appImagePath, QStringList());
                QCoreApplication::quit();
#endif
            });
}
