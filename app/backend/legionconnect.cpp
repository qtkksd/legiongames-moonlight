#include "legionconnect.h"
#include "computermanager.h"
#include "nvcomputer.h"
#include "nvaddress.h"
#include "sessiondeadline.h"

#include <QTimer>
#include <QProcess>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>
#include <QtDebug>

#ifdef LG_STAGING_BUILD
#define LEGION_API_BASE "https://django-stg.legiongames.cell"
#else
#define LEGION_API_BASE "https://legiongames.ru"
#endif
#define LEGION_DEFAULT_MANAGEMENT_URL "https://netbird.legiongames.ru"

// Dedicated NetBird profile used for the session so the user's own NetBird
// login (the "default" profile) is never modified.
#define LEGION_PROFILE_NAME "legiongames"

LegionConnect::LegionConnect(ComputerManager* manager, QObject* parent) :
    QObject(parent),
    m_Manager(manager),
    m_Nam(new QNetworkAccessManager(this)),
    m_Process(new QProcess(this)),
    m_Stage(Idle),
    m_PairingDone(false),
    m_PairingOk(false),
    m_PinDone(false),
    m_PinOk(false),
    m_PinAttempts(0),
    m_AddRetries(0),
    m_Cleaning(false),
    m_NetbirdTouched(false),
    m_StaleProfileActive(false),
    m_RemoveIndex(0)
{
    // The connect and PIN APIs never redirect. Following a redirect here could
    // disclose the one-time bearer token or send the code to another host.
    m_Nam->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);

    connect(m_Process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_Stage == Idle) {
            return;
        }

        const QByteArray out = m_Process->readAllStandardOutput();
        cleanupSetupKeyFile();

        switch (m_Stage) {
        case NetbirdDown:
            // Ignore the result of "down" (may fail if not connected) and list
            // the profiles so we can remove any stale "legiongames" ones.
            runNetbird(QStringList() << QStringLiteral("profile")
                                     << QStringLiteral("list")
                                     << QStringLiteral("--show-id"),
                       NetbirdListProfiles);
            break;

        case NetbirdListProfiles:
            parseProfileList(out);
            startRemovingStaleProfiles();
            break;

        case NetbirdSelectPrev:
        case NetbirdRemoveProfile:
            removeNextStaleProfile();
            break;

        case NetbirdAddProfile:
            parseAddedProfile(out);
            emit status(tr("Подключение NetBird..."));
            emit progress(40);
            if (!prepareSetupKeyFile()) {
                fail(tr("Не удалось защитить ключ подключения NetBird"));
                return;
            }
            runNetbird(QStringList() << QStringLiteral("up")
                                     << QStringLiteral("--setup-key-file") << m_SetupKeyFile
                                     << QStringLiteral("--management-url") << m_ManagementUrl
                                     << QStringLiteral("--profile")
                                     << (m_SessionProfileId.isEmpty()
                                             ? QStringLiteral(LEGION_PROFILE_NAME)
                                             : m_SessionProfileId)
                                     << QStringLiteral("--disable-auto-connect"),
                       NetbirdUp);
            break;

        case NetbirdRestoreProfile:
            m_Cleaning = false;
            m_NetbirdTouched = false;
            m_Stage = Idle;
            break;

        case NetbirdUp:
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                fail(tr("Не удалось подключиться к NetBird"));
                return;
            }

            emit status(tr("Ожидание сети NetBird..."));
            emit progress(60);
            m_Stage = WaitingForHost;
            m_AddRetries = 0;
            // Give the daemon a moment, then add the host (retried on timeout).
            QTimer::singleShot(1500, this, [this]() { addHost(); });
            break;

        default:
            break;
        }
    });

    connect(m_Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Ignore errors after we've already finished/failed (e.g. the kill() we
        // issue in fail() can emit Crashed), and stop cleanup quietly.
        if (m_Stage == Idle) {
            return;
        }
        if (m_Cleaning) {
            m_Stage = Idle;
            return;
        }
        qWarning() << "NetBird process error:" << error;
        fail(tr("Не удалось запустить NetBird. Убедитесь, что он установлен."));
    });

    // Host appears after a successful add.
    connect(m_Manager, &ComputerManager::computerStateChanged, this, [this](NvComputer* computer) {
        if (m_Stage != WaitingForHost) {
            return;
        }
        if (computer->activeAddress.address() == m_PcIp ||
                computer->localAddress.address() == m_PcIp) {
            startPairing(computer);
        }
    });

    connect(m_Manager, &ComputerManager::pairingCompleted, this,
            [this](NvComputer*, QString error) {
        if (m_Stage != Pairing || m_PairingDone) {
            return;
        }
        m_PairingDone = true;
        m_PairingOk = error.isEmpty();
        if (!m_PairingOk) {
            fail(error);
            return;
        }
        // The client is now authenticated with the host (pairing done). This is
        // the real "authed into PC" moment and lands ~2s before the Django
        // PIN relay round-trip returns, so complete the bar here rather
        // than on the slower server response.
        emit progress(100);
        checkFinished();
    });
}

void LegionConnect::startWithPayload(const QVariantMap& data)
{
    if (m_Stage != Idle) {
        emit failed(tr("Подключение уже выполняется"));
        return;
    }

    m_Token.clear();
    m_SessionId = data.value(QStringLiteral("session_id")).toString().trimmed();
    m_Cleaning = false;
    m_NetbirdTouched = false;
    m_PreviousProfileId.clear();
    m_SessionProfileId.clear();
    m_StaleProfileIds.clear();
    m_StaleProfileActive = false;
    m_RemoveIndex = 0;
    m_PinReply = nullptr;
    m_Stage = Preparing;

    applyConnectPayload(QJsonObject::fromVariantMap(data));
}

void LegionConnect::disconnect()
{
    cleanupSetupKeyFile();
    // Tear down the session NetBird profile and restore the user's own profile.
    if (!m_NetbirdTouched || m_Cleaning) {
        return;
    }

    // Leave the connect state machine before killing an in-flight command.
    // Otherwise its finished handler may start the next connect stage while we
    // are trying to tear the same process down.
    m_Stage = Idle;
    m_Cleaning = true;

    if (m_Process->state() != QProcess::NotRunning) {
        m_Process->kill();
        m_Process->waitForFinished(1000);
    }

    emit status(tr("Отключение NetBird..."));
    runNetbird(QStringList() << QStringLiteral("down"), NetbirdDown);
    if (m_Stage == Idle) {
        // The executable was missing, so no cleanup command could start.
        m_Cleaning = false;
    }
}

void LegionConnect::shutdown()
{
    if (!m_NetbirdTouched) {
        return;
    }

    m_Stage = Idle;
    m_Cleaning = false;
    if (m_Process->state() != QProcess::NotRunning) {
        m_Process->kill();
        m_Process->waitForFinished(1000);
    }

    const QString exe = findNetbirdExecutable();
    if (exe.isEmpty()) {
        m_NetbirdTouched = false;
        return;
    }

    // The event loop is already shutting down, so run the same cleanup
    // sequence synchronously and ignore individual command failures.
    auto run = [&exe](const QStringList& args) {
        QProcess process;
        process.start(exe, args);
        if (!process.waitForFinished(3000)) {
            process.kill();
            process.waitForFinished(1000);
        }
        return process.readAllStandardOutput();
    };

    run(QStringList() << QStringLiteral("down"));
    parseProfileList(run(QStringList() << QStringLiteral("profile")
                                       << QStringLiteral("list")
                                       << QStringLiteral("--show-id")));

    if (m_StaleProfileActive) {
        const QString target = m_PreviousProfileId.isEmpty()
                                   ? QStringLiteral("default")
                                   : m_PreviousProfileId;
        run(QStringList() << QStringLiteral("profile")
                          << QStringLiteral("select") << target);
    }

    for (const QString& id : m_StaleProfileIds) {
        run(QStringList() << QStringLiteral("profile")
                          << QStringLiteral("remove") << id);
    }

    if (!m_PreviousProfileId.isEmpty()) {
        run(QStringList() << QStringLiteral("profile")
                          << QStringLiteral("select") << m_PreviousProfileId);
    }

    m_NetbirdTouched = false;
    m_SessionProfileId.clear();
}

void LegionConnect::applyConnectPayload(const QJsonObject& obj)
{
    if (obj.contains(QStringLiteral("ok")) &&
            !obj.value(QStringLiteral("ok")).toBool()) {
        const QString message = obj.value(QStringLiteral("message")).toString();
        const QString error = obj.value(QStringLiteral("error")).toString();
        fail(!message.isEmpty() ? message
                                : (error.isEmpty() ? tr("Сервер отклонил подключение") : error));
        return;
    }
    if (m_SessionId.isEmpty()) {
        fail(tr("Сервер не вернул идентификатор сессии"));
        return;
    }

    m_SetupKey = obj.value(QStringLiteral("setup_key")).toString();
    m_PcIp = obj.value(QStringLiteral("pc_netbird_ip")).toString();
    m_PcName = obj.value(QStringLiteral("pc_name")).toString();
    m_Token = obj.value(QStringLiteral("token")).toString();
    m_ManagementUrl = obj.value(QStringLiteral("management_url")).toString();
    if (m_ManagementUrl.isEmpty()) {
        m_ManagementUrl = QStringLiteral(LEGION_DEFAULT_MANAGEMENT_URL);
    }
    const QUrl managementUrl(m_ManagementUrl, QUrl::StrictMode);
    const QString managementHost = managementUrl.host().toLower();
    const bool allowedManagementHost =
        managementHost == QLatin1String("netbird.legiongames.ru") ||
        managementHost.endsWith(QStringLiteral(".legiongames.cell"));
    if (!managementUrl.isValid() ||
        managementUrl.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0 ||
        !allowedManagementHost) {
        fail(tr("Сервер вернул недопустимый адрес NetBird"));
        return;
    }

    // End-of-booking notification deadline (optional). Drives the in-stream
    // overlay warnings; purely local countdown, nothing is polled afterwards.
    const QJsonObject deadlineObj =
        obj.value(QStringLiteral("session_deadline")).toObject();
    if (!deadlineObj.isEmpty()) {
        const QDateTime end = QDateTime::fromString(
            deadlineObj.value(QStringLiteral("end_of_booking")).toString(),
            Qt::ISODate);
        if (end.isValid()) {
            SessionDeadline::set(
                end.toMSecsSinceEpoch(),
                deadlineObj.value(QStringLiteral("grace_minutes")).toInt());
        } else {
            // Present but unparseable — don't keep a stale value around.
            SessionDeadline::clear();
        }
    } else {
        SessionDeadline::clear();
    }

    if (m_SetupKey.isEmpty() || m_PcIp.isEmpty() || m_Token.isEmpty()) {
        fail(tr("Сервер вернул неполные данные"));
        return;
    }

    emit status(tr("Подключение NetBird..."));
    emit progress(20);
    runNetbird(QStringList() << QStringLiteral("down"), NetbirdDown);
}


bool LegionConnect::prepareSetupKeyFile()
{
    cleanupSetupKeyFile();
    if (m_SetupKey.isEmpty()) {
        return false;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QTemporaryFile file(QDir(dir).filePath(QStringLiteral("legiongames-netbird-XXXXXX")));
    file.setAutoRemove(false);
    if (!file.open()) {
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(m_SetupKey.toUtf8()) != m_SetupKey.toUtf8().size()) {
        file.close();
        file.remove();
        return false;
    }
    file.flush();
    file.close();
    m_SetupKeyFile = file.fileName();
    return !m_SetupKeyFile.isEmpty();
}

void LegionConnect::cleanupSetupKeyFile()
{
    if (m_SetupKeyFile.isEmpty()) {
        return;
    }
    QFile::remove(m_SetupKeyFile);
    m_SetupKeyFile.clear();
}

void LegionConnect::runNetbird(const QStringList& args, Stage stage)
{
    const QString exe = findNetbirdExecutable();
    if (exe.isEmpty()) {
        fail(tr("NetBird не найден. Установите NetBird и попробуйте снова."));
        return;
    }

    m_NetbirdTouched = true;
    m_Stage = stage;
    m_Process->start(exe, args);
}

void LegionConnect::parseProfileList(const QByteArray& out)
{
    m_StaleProfileIds.clear();
    m_StaleProfileActive = false;

    // Output of `netbird profile list --show-id`:
    //   ID        NAME         ACTIVE
    //   default   default      ✓
    //   b21dedd1  legiongames
    const QString activeMark = QStringLiteral("\u2713"); // ✓
    const QStringList lines = QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (fields.size() < 2) {
            continue;
        }
        const QString id = fields.at(0);
        if (id == QLatin1String("ID")) {
            continue; // header
        }
        const QString name = fields.at(1);
        const bool active = line.contains(activeMark);
        if (name == QLatin1String(LEGION_PROFILE_NAME)) {
            m_StaleProfileIds.append(id);
            if (active) {
                m_StaleProfileActive = true;
            }
        } else if (active && m_PreviousProfileId.isEmpty()) {
            // Remember the user's own profile so we can restore it later.
            m_PreviousProfileId = id;
        }
    }
}

void LegionConnect::parseAddedProfile(const QByteArray& out)
{
    // "Profile added: <id>  legiongames"
    const QString text = QString::fromUtf8(out);
    const QString marker = QStringLiteral("Profile added:");
    const int idx = text.indexOf(marker);
    if (idx < 0) {
        return;
    }
    const QStringList fields = text.mid(idx + marker.size())
                                   .split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);
    if (!fields.isEmpty()) {
        m_SessionProfileId = fields.at(0);
    }
}

void LegionConnect::startRemovingStaleProfiles()
{
    m_RemoveIndex = 0;

    if (m_StaleProfileIds.isEmpty()) {
        removeNextStaleProfile();
        return;
    }

    if (m_StaleProfileActive) {
        // Can't remove the active profile: switch to the user's previous one
        // (or "default") first.
        const QString target = m_PreviousProfileId.isEmpty()
                                   ? QStringLiteral("default")
                                   : m_PreviousProfileId;
        runNetbird(QStringList() << QStringLiteral("profile")
                                 << QStringLiteral("select") << target,
                   NetbirdSelectPrev);
        return;
    }

    removeNextStaleProfile();
}

void LegionConnect::removeNextStaleProfile()
{
    if (m_RemoveIndex < m_StaleProfileIds.size()) {
        const QString id = m_StaleProfileIds.at(m_RemoveIndex++);
        runNetbird(QStringList() << QStringLiteral("profile")
                                 << QStringLiteral("remove") << id,
                   NetbirdRemoveProfile);
        return;
    }

    if (m_Cleaning) {
        restorePreviousProfile();
    } else {
        addSessionProfile();
    }
}

void LegionConnect::addSessionProfile()
{
    runNetbird(QStringList() << QStringLiteral("profile")
                             << QStringLiteral("add") << QStringLiteral(LEGION_PROFILE_NAME),
               NetbirdAddProfile);
}

void LegionConnect::restorePreviousProfile()
{
    const QString target = m_PreviousProfileId.isEmpty()
                               ? QStringLiteral("default")
                               : m_PreviousProfileId;
    runNetbird(QStringList() << QStringLiteral("profile")
                             << QStringLiteral("select") << target,
               NetbirdRestoreProfile);
}

void LegionConnect::addHost()
{
    if (m_Stage != WaitingForHost) {
        return;
    }

    if (m_AddRetries++ >= 3) {
        fail(tr("Игровой ПК недоступен. Попробуйте ещё раз."));
        return;
    }

    emit status(tr("Добавление ПК..."));
    emit progress(75);

    // mdns=true keeps this silent (no error dialogs / STUN); we react to
    // computerStateChanged and retry on a timer.
    m_Manager->addNewHost(NvAddress(m_PcIp, DEFAULT_HTTP_PORT), true, m_PcName);

    QTimer::singleShot(6000, this, [this]() {
        if (m_Stage == WaitingForHost) {
            addHost();
        }
    });
}

void LegionConnect::startPairing(NvComputer* computer)
{
    if (m_Stage != WaitingForHost) {
        return;
    }

    m_Stage = Pairing;
    m_Pin = m_Manager->generatePinString();
    m_PairingDone = false;
    m_PairingOk = false;
    m_PinDone = false;
    m_PinOk = false;

    m_PinAttempts = 0;
    emit status(tr("Сопряжение с ПК..."));
    emit progress(85);

    // Start the client's pairing handshake first. The host holds the
    // getservercert request open until the PIN arrives, so we give it a moment
    // to register the pairing session before relaying the PIN (avoids a race
    // where /api/pin reaches Sunshine before the session exists).
    m_Manager->pairHost(computer, m_Pin);
    QTimer::singleShot(1000, this, &LegionConnect::submitPin);
}

void LegionConnect::submitPin()
{
    if (m_Stage != Pairing || m_PinDone) {
        return;
    }

    m_PinAttempts++;

    QJsonObject body;
    body[QStringLiteral("pin")] = m_Pin;

    QNetworkRequest request{QUrl(
        QStringLiteral(LEGION_API_BASE "/app/v1/sessions/") + m_SessionId +
        QStringLiteral("/pin"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_Token.toUtf8());

    QNetworkReply* reply = m_Nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_PinReply = reply;

    // Bound the relay: if Django/Sunshine don't answer (mesh flap, slow verify)
    // abort so the flow retries instead of hanging the modal progress dialog.
    QTimer::singleShot(8000, this, [this, reply]() {
        if (reply == m_PinReply && reply->isRunning()) {
            qWarning() << "LegionConnect: PIN relay timeout — aborting";
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply == m_PinReply) {
            m_PinReply = nullptr;
        }

        if (m_Stage != Pairing || m_PinDone) {
            return;
        }

        // Completion is gated on HTTP 200 from Django, which is only returned
        // once the connection is verified (and activated) server-side.
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        bool ok = false;
        bool retryable = false;
        QString errorText = tr("ПК отклонил PIN");
        QString serverCode;

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject()) {
            const QJsonObject envelope = doc.object();
            serverCode = envelope.value(QStringLiteral("error")).toString();
            ok = httpStatus == 200 &&
                 envelope.value(QStringLiteral("ok")).toBool();
            if (!ok) {
                const QString serverMessage =
                    envelope.value(QStringLiteral("message")).toString();
                if (!serverMessage.isEmpty()) {
                    errorText = serverMessage;
                }
                else if (!serverCode.isEmpty()) {
                    errorText = serverCode;
                }
                // Retry only races: Sunshine publishes the paired client
                // asynchronously, and the host may briefly be unavailable.
                retryable =
                    serverCode == QLatin1String("pin_verification_failed") ||
                    serverCode == QLatin1String("sunshine_unavailable");
            }
        }
        else if (reply->error() != QNetworkReply::NoError || httpStatus != 200) {
            errorText = tr("Не удалось авторизовать PIN (сеть)");
            retryable = httpStatus == 0 || httpStatus == 429 || httpStatus >= 500;
        }

        if (ok) {
            m_PinDone = true;
            m_PinOk = true;
            checkFinished();
            return;
        }

        // The host may not have registered the pairing session yet (or is still
        // processing) — retry a few times before giving up.
        if (retryable && m_PinAttempts < 6) {
            qInfo() << "PIN relay attempt" << m_PinAttempts << "failed, retrying:" << errorText;
            QTimer::singleShot(1000, this, &LegionConnect::submitPin);
            return;
        }

        m_PinDone = true;
        m_PinOk = false;
        // Pairing alone is not enough: Django must activate the connection and
        // record the Sunshine PIN before the session can be considered ready.
        // Otherwise the client can stream while the server still bills/holds a
        // pending session and cleanup never runs correctly.
        fail(errorText);
    });
}

void LegionConnect::checkFinished()
{
    if (m_Stage != Pairing || !m_PairingDone || !m_PinDone) {
        return;
    }

    // Both sides must agree before the app opens the host. The server-side PIN
    // relay activates the Django connection and is not optional bookkeeping.
    if (m_PairingOk && m_PinOk) {
        m_Stage = Idle;
        emit succeeded();
    }
}

void LegionConnect::fail(const QString& error)
{
    cleanupSetupKeyFile();
    // Only surface the first error; later callbacks from a killed process are ignored.
    if (m_Stage == Idle) {
        return;
    }

    // A cleanup command failing is not worth surfacing — just stop cleaning.
    if (m_Cleaning) {
        m_Stage = Idle;
        return;
    }

    qWarning() << "LegionConnect failed:" << error;

    const bool touched = m_NetbirdTouched;
    m_Stage = Idle;
    if (m_Process != nullptr && m_Process->state() != QProcess::NotRunning) {
        m_Process->kill();
        m_Process->waitForFinished(1000);
    }
    emit failed(error);

    // Best-effort teardown: remove the session profile and restore the user's
    // own profile even when the failure happened during a CLI command.
    if (touched) {
        disconnect();
        if (m_Stage == Idle && m_Cleaning) {
            // The teardown could not even start (for example NetBird was
            // uninstalled while a session was active).
            m_Cleaning = false;
            m_NetbirdTouched = false;
        }
    }
}

QString LegionConnect::findNetbirdExecutable() const
{
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("netbird"));
    if (!exe.isEmpty()) {
        return exe;
    }

#ifdef Q_OS_WIN32
    const QString candidate = qEnvironmentVariable("ProgramFiles") + QStringLiteral("/NetBird/netbird.exe");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
#elif defined(Q_OS_DARWIN)
    const QString candidate = QStringLiteral("/Applications/NetBird.app/Contents/MacOS/netbird");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
#else
    const QString candidate = QStringLiteral("/usr/bin/netbird");
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
    const QString localCandidate = QStringLiteral("/usr/local/bin/netbird");
    if (QFileInfo::exists(localCandidate)) {
        return localCandidate;
    }
#endif

    return QString();
}

NvComputer* LegionConnect::findComputerByAddress(const QString& ip) const
{
    for (NvComputer* computer : m_Manager->getComputers()) {
        if (computer->activeAddress.address() == ip || computer->localAddress.address() == ip) {
            return computer;
        }
    }
    return nullptr;
}
