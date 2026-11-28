#pragma once

#include <QObject>
#include <QString>

#include "streaming/session.h"

// Small QML-facing bridge used by the product app-selection page. The page
// owns the AppModel/Session creation; the main window owns the StackView and
// StreamSegue, so it listens to this signal instead of exposing stackView as a
// context property.
//
// The lease id travels with the launch so StreamSegue can end the paid lease
// when the stream finishes. An empty lease id is the legacy/CLI path and does
// not touch billing.
class StreamLauncher : public QObject
{
    Q_OBJECT

public:
    explicit StreamLauncher(QObject *parent = nullptr);

    Q_INVOKABLE void startStream(Session *session, const QString &appName,
                                 bool isResume, const QString &leaseId = QString());

signals:
    void startRequested(Session *session, const QString &appName, bool isResume,
                        const QString &leaseId);
};
