#include "autoupdatechecker.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSysInfo>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QtDebug>

// LG_COMMIT is injected by the build (see app.pro); fall back to the version
// string for local/unsupported builds.
#ifndef LG_COMMIT
#define LG_COMMIT VERSION_STR
#endif

// Releases are tagged with the short commit SHA, which is also what the pkgs
// "version" file contains. Update available == embedded commit != latest.
#define LG_RELEASES_API "https://api.github.com/repos/qtkksd/legiongames-moonlight/releases/latest"
#define LG_PKGS_VERSION_URL "https://pkgs.legiongames.ru/legiongames-moonlight/latest/version"
#define LG_PKGS_BASE "https://pkgs.legiongames.ru/legiongames-moonlight/latest/"
#define LG_FALLBACK_URL "https://github.com/qtkksd/legiongames-moonlight/releases/latest"

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
    qDebug() << "Current LegionGames Moonlight commit:" << m_CurrentCommit;
}

void AutoUpdateChecker::start()
{
    if (!m_Nam) {
        Q_ASSERT(m_Nam);
        return;
    }

#if defined(Q_OS_WIN32) || defined(Q_OS_DARWIN) || defined(STEAM_LINK) || defined(APP_IMAGE)
    QNetworkRequest request{QUrl(QStringLiteral(LG_RELEASES_API))};
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");
    request.setRawHeader("Accept", "application/vnd.github+json");
    m_Nam->get(request);
#endif
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

void AutoUpdateChecker::checkPkgsFallback()
{
    if (!m_Nam) {
        return;
    }

    qInfo() << "Falling back to pkgs version check";
    QNetworkRequest request{QUrl(QStringLiteral(LG_PKGS_VERSION_URL))};
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");
    m_Nam->get(request);
}

void AutoUpdateChecker::finishWithCommit(const QString& latestCommit)
{
    qDebug() << "Latest available commit:" << latestCommit
             << "current commit:" << m_CurrentCommit;

    if (commitsEqual(m_CurrentCommit, latestCommit)) {
        qDebug() << "Moonlight is up to date";
        return;
    }

    const QString downloadUrl = getPlatformDownloadUrl();
    qDebug() << "Update available:" << latestCommit << downloadUrl;
    emit onUpdateAvailable(latestCommit, downloadUrl);
}

QString AutoUpdateChecker::getPlatformDownloadUrl() const
{
    const QString base = QStringLiteral(LG_PKGS_BASE);

#if defined(Q_OS_WIN32)
    return base + QStringLiteral("windows/MoonlightSetup.exe");
#elif defined(Q_OS_DARWIN)
    return base + QStringLiteral("macos/Moonlight.dmg");
#elif defined(APP_IMAGE) || defined(Q_OS_LINUX)
    const QString arch = QSysInfo::buildCpuArchitecture();
    if (arch == QLatin1String("aarch64") || arch == QLatin1String("arm64")) {
        return base + QStringLiteral("linux/Moonlight-aarch64.AppImage");
    }
    return base + QStringLiteral("linux/Moonlight-x86_64.AppImage");
#else
    return QStringLiteral(LG_FALLBACK_URL);
#endif
}

void AutoUpdateChecker::handleRequestFinished(QNetworkReply* reply)
{
    Q_ASSERT(reply->isFinished());

    const bool isGithub = reply->request().url().toString().contains(QStringLiteral("api.github.com"));

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Update check request failed:" << reply->error() << reply->errorString();
        reply->deleteLater();

        // If the GitHub API failed (offline, rate-limited, no releases, ...),
        // try the pkgs version file instead.
        if (isGithub) {
            checkPkgsFallback();
        }
        return;
    }

    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (isGithub) {
        QJsonParseError error;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(payload, &error);
        if (jsonDoc.isNull() || !jsonDoc.isObject()) {
            qWarning() << "GitHub release response malformed:" << error.errorString();
            checkPkgsFallback();
            return;
        }

        const QString tag = jsonDoc.object().value(QStringLiteral("tag_name")).toString().trimmed();
        if (tag.isEmpty()) {
            qWarning() << "GitHub release response missing tag_name";
            checkPkgsFallback();
            return;
        }

        finishWithCommit(tag);
    }
    else {
        // The pkgs version file is a plain-text short commit SHA.
        const QString latest = QString::fromUtf8(payload).trimmed();
        if (latest.isEmpty()) {
            qWarning() << "pkgs version response was empty";
            return;
        }

        finishWithCommit(latest);
    }
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
        // The Windows installer is a WiX Burn bundle: it accepts /quiet (NOT
        // the NSIS-style /S), plus /norestart. This still triggers UAC because
        // the bundle installs per-machine.
        qInfo() << "Launching silent installer:" << targetPath;
        QProcess::startDetached(targetPath, QStringList() << QStringLiteral("/quiet")
                                                          << QStringLiteral("/norestart"));
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
