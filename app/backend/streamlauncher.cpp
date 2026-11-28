#include "streamlauncher.h"

StreamLauncher::StreamLauncher(QObject *parent)
    : QObject(parent)
{
}

void StreamLauncher::startStream(Session *session, const QString &appName,
                                 bool isResume, const QString &leaseId)
{
    if (session == nullptr) {
        return;
    }
    emit startRequested(session, appName, isResume, leaseId);
}
