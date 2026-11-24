#include "recoverykit.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QtDebug>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <sddl.h>
#endif

namespace {

QString sanitizedHandle(const QString &handle)
{
    QString out;
    out.reserve(handle.size());
    for (const QChar ch : handle) {
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
            (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
            (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
            ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            out += ch;
        }
    }
    if (out.isEmpty()) {
        out = QStringLiteral("account");
    }
    return out;
}

#ifdef Q_OS_WIN32
bool restrictToCurrentUser(const QString &path)
{
    // Protected DACL with a single ACE for the file owner. Same rule as the
    // vault blob: nobody else on the machine can read the words.
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;FA;;;OW)", SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }

    const BOOL ok = SetFileSecurityW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                     DACL_SECURITY_INFORMATION |
                                         PROTECTED_DACL_SECURITY_INFORMATION,
                                     sd);
    LocalFree(sd);
    return ok != FALSE;
}
#endif

} // namespace

QString RecoveryKit::defaultDirectory()
{
    // Prefer the installation directory when it is writable (portable/custom
    // installs). Program Files is read-only for a normal user, so fall back to
    // the per-user application data directory instead of Documents, which is
    // often synced to a cloud drive.
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty() && QFileInfo(appDir).isWritable()) {
        return appDir;
    }

    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.legiongames");
    }
    return base + QStringLiteral("/recovery");
}

QString RecoveryKit::suggestedPath(const QString &handle)
{
    return defaultDirectory() + QStringLiteral("/legiongames-recovery-") +
           sanitizedHandle(handle) + QStringLiteral(".txt");
}

bool RecoveryKit::isSyncedFolder(const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(path).toLower();

    const QStringList markers = {
        QStringLiteral("/onedrive"),
        QStringLiteral("/dropbox"),
        QStringLiteral("/google drive"),
        QStringLiteral("/googledrive"),
        QStringLiteral("/icloud drive"),
        QStringLiteral("/library/mobile documents"),
        QStringLiteral("/com~apple~clouddocs"),
    };

    for (const QString &marker : markers) {
        if (normalized.contains(marker)) {
            return true;
        }
    }

    // Cloud clients also let the user move the folder and point an environment
    // variable at it, which the path check alone would miss.
    const QStringList environment = {
        QStringLiteral("OneDrive"),
        QStringLiteral("OneDriveConsumer"),
        QStringLiteral("OneDriveCommercial"),
        QStringLiteral("DROPBOX_PATH"),
        QStringLiteral("GOOGLE_DRIVE_PATH"),
    };

    for (const QString &name : environment) {
        const QString value = qEnvironmentVariable(name.toUtf8().constData());
        if (value.isEmpty()) {
            continue;
        }
        const QString envPath = QDir::fromNativeSeparators(value).toLower();
        if (!envPath.isEmpty() && normalized.startsWith(envPath)) {
            return true;
        }
    }

    return false;
}

QString RecoveryKit::buildContents(const QString &handle, const QStringList &words,
                                   const QString &deviceLabel, const QDateTime &date)
{
    QString contents;
    QTextStream stream(&contents);

    stream << "LegionGames - памятка для восстановления доступа\n";
    stream << "=================================================\n\n";
    stream << "Аккаунт: " << handle << "\n";
    stream << "Дата: " << date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) << "\n";
    if (!deviceLabel.isEmpty()) {
        stream << "Устройство: " << deviceLabel << "\n";
    }
    stream << "\n";
    stream << "Слова восстановления (8):\n";
    stream << "-------------------------\n";
    for (int i = 0; i < words.size(); ++i) {
        stream << QStringLiteral("%1. %2\n").arg(i + 1).arg(words.at(i));
    }
    stream << "\n";
    stream << "Как восстановить доступ:\n";
    stream << "1. Установите Moonlight на новый компьютер.\n";
    stream << "2. На экране входа выберите «Восстановить доступ».\n";
    stream << "3. Введите логин и все 8 слов в том же порядке.\n";
    stream << "\n";
    stream << "Поддержка: https://legiongames.ru/help/\n";
    stream << "\n";
    stream << "ВНИМАНИЕ: любой, кто знает эти слова, получает полный доступ к аккаунту\n";
    stream << "и балансу. Никому их не передавайте. Храните файл в надёжном месте.\n";

    return contents;
}

RecoveryKit::Result RecoveryKit::write(const QString &handle, const QStringList &words,
                                       const QString &deviceLabel, const QDateTime &date)
{
    Result result;

    if (handle.isEmpty() || words.size() != 8) {
        result.errorCode = QStringLiteral("recovery_kit_invalid");
        result.message = QStringLiteral("Не удалось сохранить памятку: нет данных аккаунта.");
        return result;
    }

    const QString directory = defaultDirectory();
    if (!QDir().mkpath(directory)) {
        result.errorCode = QStringLiteral("recovery_kit_write_failed");
        result.message = QStringLiteral("Не удалось создать папку для памятки.");
        return result;
    }

    const QString path = directory + QStringLiteral("/legiongames-recovery-") +
                         sanitizedHandle(handle) + QStringLiteral(".txt");

    // Re-printing must replace the previous kit. Removing it first also covers
    // filesystems where QSaveFile cannot atomically replace an existing file.
    if (QFile::exists(path)) {
        QFile::remove(path);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.errorCode = QStringLiteral("recovery_kit_write_failed");
        result.message = QStringLiteral("Не удалось перезаписать файл памятки.");
        qWarning() << "RecoveryKit: cannot open" << path << file.errorString();
        return result;
    }

    const QByteArray contents =
        buildContents(handle, words, deviceLabel, date).toUtf8();
    if (file.write(contents) != contents.size() || !file.commit()) {
        result.errorCode = QStringLiteral("recovery_kit_write_failed");
        result.message = QStringLiteral("Не удалось записать памятку. Освободите место на диске.");
        qWarning() << "RecoveryKit: write failed" << path;
        return result;
    }

#ifdef Q_OS_WIN32
    if (!restrictToCurrentUser(path)) {
        // DPAPI protects the vault, but this file is plain text. The app data
        // directory is already per-user; do not fail a successful write just
        // because a custom install directory rejects an explicit DACL.
        qWarning() << "RecoveryKit: failed to set user-only ACL on" << path;
    }
#else
    // 0600: owner read/write only.
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        qWarning() << "RecoveryKit: failed to set 0600 on" << path;
    }
#endif

    result.ok = true;
    result.path = path;
    result.syncedFolder = isSyncedFolder(QFileInfo(path).absolutePath());
    return result;
}
