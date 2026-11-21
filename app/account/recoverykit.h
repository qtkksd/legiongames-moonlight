#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// Writes the printable recovery kit to Documents/LegionGames.
//
// The file is plaintext by necessity (the user must be able to read it), so it
// gets a user-only ACL and a warning when the Documents folder is redirected
// into a cloud-synced directory.
class RecoveryKit
{
public:
    struct Result
    {
        bool ok = false;
        QString path;
        bool syncedFolder = false;
        QString errorCode;
        QString message;
    };

    static QString defaultDirectory();
    static QString suggestedPath(const QString &handle);

    // True when the path lives in OneDrive, Dropbox, Google Drive or iCloud
    // Drive (including the macOS "Mobile Documents" container).
    static bool isSyncedFolder(const QString &path);

    static QString buildContents(const QString &handle, const QStringList &words,
                                 const QString &deviceLabel, const QDateTime &date);

    static Result write(const QString &handle, const QStringList &words,
                        const QString &deviceLabel, const QDateTime &date);
};
