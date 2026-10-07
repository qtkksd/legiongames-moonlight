#include "autoupdatechecker.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QtDebug>

// LG_COMMIT / LG_BUILD are injected by the build (see app.pro); fall back to the
// version string / 0 for local or unsupported builds.
#ifndef LG_COMMIT
#define LG_COMMIT VERSION_STR
#endif
#ifndef LG_BUILD
#define LG_BUILD "0"
#endif

// The pkgs manifest is the single source of truth for the update: version,
// monotonic build number, and per-OS asset URL + sha256.
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

    connect(m_Nam, &QNetworkAccessManager::finished,
            this, &AutoUpdateChecker::handleRequestFinished);

    m_CurrentCommit = QString::fromLatin1(LG_COMMIT);
    m_CurrentBuild = QString::fromLatin1(LG_BUILD).toInt();
    qDebug() << "Current LegionGames Moonlight commit:" << m_CurrentCommit
             << "build:" << m_CurrentBuild;
}

void AutoUpdateChecker::start()
{
    if (!m_Nam) {
        Q_ASSERT(m_Nam);
        return;
    }

    QNetworkRequest request{QUrl(QStringLiteral(LG_MANIFEST_URL))};
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");
    m_Nam->get(request);
}

bool AutoUpdateChecker::commitsEqual(const QString& a, const QString& b)
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

void AutoUpdateChecker::handleRequestFinished(QNetworkReply* reply)
{
    Q_ASSERT(reply->isFinished());

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Update manifest fetch failed:"
                   << reply->error() << reply->errorString();
        reply->deleteLater();
        return;
    }

    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    QJsonParseError error;
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(payload, &error);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        qWarning() << "Update manifest malformed:" << error.errorString();
        return;
    }

    const QJsonObject manifest = jsonDoc.object();
    const int latestBuild = manifest.value(QStringLiteral("build")).toInt(0);
    const QString latestCommit = manifest.value(QStringLiteral("commit_full")).toString(
        manifest.value(QStringLiteral("commit")).toString());
    const QString latestVersion = manifest.value(QStringLiteral("version")).toString();

    // Pick the asset for this platform.
    QString assetUrl;
    const QJsonObject assets = manifest.value(QStringLiteral("assets")).toObject();
    const QJsonObject asset = assets.value(platformAssetKey()).toObject();
    assetUrl = asset.value(QStringLiteral("url")).toString();

    if (assetUrl.isEmpty()) {
        qWarning() << "Update manifest missing asset for platform" << platformAssetKey();
        return;
    }

    // Decide whether we're outdated: prefer the monotonic build number, fall
    // back to commit equality (prefix match).
    bool outdated;
    if (latestBuild > 0 && m_CurrentBuild > 0) {
        outdated = latestBuild > m_CurrentBuild;
    } else {
        outdated = !latestCommit.isEmpty() && !commitsEqual(m_CurrentCommit, latestCommit);
    }

    if (!outdated) {
        qDebug() << "Moonlight is up to date (build" << m_CurrentBuild
                 << ", latest" << latestBuild << ")";
        return;
    }

    qDebug() << "Update available: version" << latestVersion
             << "build" << latestBuild << assetUrl;
    emit onUpdateAvailable(latestVersion.isEmpty() ? latestCommit : latestVersion, assetUrl);
}

// Anything smaller than this is almost certainly an error page, not a build.
#define MIN_UPDATE_SIZE (1024 * 1024)

void AutoUpdateChecker::installUpdate(const QString& url)
{
#if !defined(Q_OS_WIN32) && !defined(APP_IMAGE)
    QDesktopServices::openUrl(QUrl(url));
#else
#if defined(Q_OS_WIN32)
    const QString targetPath = QDir(QDir::tempPath()).filePath(QStringLiteral("MoonlightSetup.exe"));
#else
    // Self-update the running AppImage in place. The AppImage runtime exports
    // the absolute path of the current bundle in $APPIMAGE.
    const QString appImagePath = qEnvironmentVariable("APPIMAGE");
    if (appImagePath.isEmpty() || !QFile::exists(appImagePath)) {
        qWarning() << "APPIMAGE env var not set; falling back to browser download";
        QDesktopServices::openUrl(QUrl(url));
        return;
    }
    const QString targetPath = appImagePath + QStringLiteral(".new");
#endif

    qInfo() << "Downloading update to" << targetPath << "from" << url;

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");

    QFile* file = new QFile(targetPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open update path for writing:" << targetPath;
        delete file;
        QDesktopServices::openUrl(QUrl(url));
        return;
    }

    QNetworkReply* reply = m_Nam->get(request);

    // Stream chunks to disk so the UI never blocks on a large download.
    connect(reply, &QNetworkReply::readyRead, file, [reply, file]() {
        file->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, url, targetPath]() {
        file->close();
        const qint64 size = file->size();
        file->deleteLater();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError || size < MIN_UPDATE_SIZE) {
            qWarning() << "Update download failed or too small:"
                       << reply->errorString() << "size=" << size;
            QFile::remove(targetPath);
            QDesktopServices::openUrl(QUrl(url));
            return;
        }

#if defined(Q_OS_WIN32)
        // Run the WiX Burn bundle interactively (NOT /quiet) so the user sees
        // the standard installer UI (progress + finish page) instead of a silent
        // update. The bundle's LaunchTarget adds a default-checked "Launch
        // Moonlight" action on the finish page, so the app reopens once the
        // install completes. /norestart suppresses any reboot prompt. Quit now
        // so the installer can replace the running binary.
        qInfo() << "Launching installer (interactive):" << targetPath;
        QProcess::startDetached(targetPath, QStringList() << QStringLiteral("/norestart"));
        QCoreApplication::quit();
#else
        // AppImage: make the new bundle executable, replace the running one,
        // then relaunch it.
        QFile::setPermissions(targetPath,
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadGroup | QFile::ExeGroup |
                              QFile::ReadOther | QFile::ExeOther);

        const QString appImagePath = qEnvironmentVariable("APPIMAGE");
        if (QFile::exists(appImagePath)) {
            QFile::remove(appImagePath);
        }
        if (!QFile::rename(targetPath, appImagePath)) {
            qWarning() << "Failed to replace AppImage:" << appImagePath;
            QDesktopServices::openUrl(QUrl(url));
            return;
        }

        qInfo() << "Relaunching updated AppImage:" << appImagePath;
        QProcess::startDetached(appImagePath, QStringList());
        QCoreApplication::quit();
#endif
    });
#endif
}
