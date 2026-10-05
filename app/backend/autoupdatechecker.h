#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

class AutoUpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit AutoUpdateChecker(QObject *parent = nullptr);

    Q_INVOKABLE void start();

    // Downloads and silently launches the installer on Windows, or opens the
    // download URL in the browser on other platforms.
    Q_INVOKABLE void installUpdate(const QString& url);

signals:
    void onUpdateAvailable(QString newVersion, QString url);

private slots:
    void handleRequestFinished(QNetworkReply* reply);

private:
    void checkPkgsFallback();
    void finishWithCommit(const QString& latestCommit);
    QString getPlatformDownloadUrl() const;
    static bool commitsEqual(const QString& a, const QString& b);

    QString m_CurrentCommit;
    QNetworkAccessManager* m_Nam;
};
