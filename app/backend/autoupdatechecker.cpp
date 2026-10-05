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

void AutoUpdateChecker::installUpdate(const QString& url)
{
#if defined(Q_OS_WIN32)
    qInfo() << "Downloading update installer:" << url;

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");

    QNetworkReply* reply = m_Nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Installer download failed:" << reply->errorString();
            QDesktopServices::openUrl(QUrl(url));
            return;
        }

        const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("MoonlightSetup.exe"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to open installer path for writing:" << path;
            QDesktopServices::openUrl(QUrl(url));
            return;
        }

        file.write(reply->readAll());
        file.close();

        qInfo() << "Launching silent installer:" << path;
        QProcess::startDetached(path, QStringList() << QStringLiteral("/S"));
        QCoreApplication::quit();
    });
#elif defined(APP_IMAGE)
    // Self-update the running AppImage in place. The AppImage runtime exports
    // the absolute path of the current bundle in $APPIMAGE.
    const QString appImagePath = qEnvironmentVariable("APPIMAGE");
    if (appImagePath.isEmpty() || !QFile::exists(appImagePath)) {
        qWarning() << "APPIMAGE env var not set; falling back to browser download";
        QDesktopServices::openUrl(QUrl(url));
        return;
    }

    qInfo() << "Downloading AppImage update:" << url;

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");

    QNetworkReply* reply = m_Nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, appImagePath]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "AppImage download failed:" << reply->errorString();
            QDesktopServices::openUrl(QUrl(url));
            return;
        }

        // Write alongside the current AppImage so the final rename is atomic.
        const QString newPath = appImagePath + QStringLiteral(".new");
        QFile file(newPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "Failed to open AppImage path for writing:" << newPath;
            QDesktopServices::openUrl(QUrl(url));
            return;
        }

        file.write(reply->readAll());
        file.close();

        // Mark the new bundle executable.
        QFile::setPermissions(newPath,
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadGroup | QFile::ExeGroup |
                              QFile::ReadOther | QFile::ExeOther);

        // Replace the running AppImage. Unlinking the old file (and renaming the
        // new one over it) keeps the running inode valid until we exit.
        if (QFile::exists(appImagePath)) {
            QFile::remove(appImagePath);
        }

        if (!QFile::rename(newPath, appImagePath)) {
            qWarning() << "Failed to replace AppImage:" << appImagePath;
            QDesktopServices::openUrl(QUrl(url));
            return;
        }

        // Relaunch the freshly installed AppImage and quit the old process.
        qInfo() << "Relaunching updated AppImage:" << appImagePath;
        QProcess::startDetached(appImagePath, QStringList());
        QCoreApplication::quit();
    });
#else
    QDesktopServices::openUrl(QUrl(url));
#endif
}
