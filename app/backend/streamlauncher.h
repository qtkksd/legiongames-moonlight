#pragma once

#include <QObject>
#include <QString>

#include "streaming/session.h"

// Small QML-facing bridge used by the product app-selection page. The page
// owns the AppModel/Session creation; the main window owns the StackView and
// StreamSegue, so it listens to this signal instead of exposing stackView as a
// context property.
class StreamLauncher : public QObject
{
    Q_OBJECT

public:
    explicit StreamLauncher(QObject *parent = nullptr);

    Q_INVOKABLE void startStream(Session *session, const QString &appName,
                                 bool isResume);

signals:
    void startRequested(Session *session, const QString &appName, bool isResume);
};
