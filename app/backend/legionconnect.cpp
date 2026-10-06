#include "legionconnect.h"
#include "computermanager.h"
#include "nvcomputer.h"
#include "nvaddress.h"

#include <QTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QtDebug>

#define LEGION_API_BASE "https://legiongames.ru"
#define LEGION_CONNECT_URL LEGION_API_BASE "/api/client/connect"
#define LEGION_PIN_URL LEGION_API_BASE "/api/client/pin"
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
    // Allow HTTP redirects (e.g. https://legiongames.ru -> www)
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    connect(m_Process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_Stage == Idle) {
            return;
        }

        const QByteArray out = m_Process->readAllStandardOutput();

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
            runNetbird(QStringList() << QStringLiteral("up")
                                     << QStringLiteral("--setup-key") << m_SetupKey
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
            m_Stage = Idle;
            break;

        case NetbirdUp:
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                fail(tr("Не удалось подключиться к NetBird"));
                return;
            }

            emit status(tr("Ожидание сети NetBird..."));
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
        checkFinished();
    });
}

void LegionConnect::start(const QString& code)
{
    if (m_Stage != Idle) {
        emit failed(tr("Подключение уже выполняется"));
        return;
    }

    m_Code = code.trimmed();
    if (m_Code.isEmpty()) {
        emit failed(tr("Введите код"));
        return;
    }

    // Reset per-connection NetBird profile state.
    m_Cleaning = false;
    m_NetbirdTouched = false;
    m_PreviousProfileId.clear();
    m_SessionProfileId.clear();
    m_StaleProfileIds.clear();
    m_StaleProfileActive = false;
    m_RemoveIndex = 0;

    m_Stage = ExchangingCode;
    emit status(tr("Проверка кода..."));

    QJsonObject body;
    body[QStringLiteral("code")] = m_Code;

    QNetworkRequest request{QUrl(QStringLiteral(LEGION_CONNECT_URL))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");

    QNetworkReply* reply = m_Nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        downloadSetupKeyFromReply(reply);
    });
}

void LegionConnect::disconnect()
{
    // Tear down the session NetBird profile and restore the user's own profile.
    if (m_Stage != Idle || !m_NetbirdTouched) {
        return;
    }

    m_Cleaning = true;
    emit status(tr("Отключение NetBird..."));
    runNetbird(QStringList() << QStringLiteral("down"), NetbirdDown);
}

void LegionConnect::downloadSetupKeyFromReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (m_Stage != ExchangingCode) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("Не удалось проверить код. Проверьте соединение с интернетом."));
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        fail(tr("Некорректный ответ сервера"));
        return;
    }

    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("status")).isBool() && !obj.value(QStringLiteral("status")).toBool()) {
        fail(obj.value(QStringLiteral("error")).toString(tr("Код недействителен или истёк")));
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

    if (m_SetupKey.isEmpty() || m_PcIp.isEmpty() || m_Token.isEmpty()) {
        fail(tr("Сервер вернул неполные данные"));
        return;
    }

    emit status(tr("Подключение NetBird..."));
    runNetbird(QStringList() << QStringLiteral("down"), NetbirdDown);
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

    QNetworkRequest request{QUrl(QStringLiteral(LEGION_PIN_URL))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "LegionGames-Moonlight");
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_Token.toUtf8());

    QNetworkReply* reply = m_Nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (m_Stage != Pairing || m_PinDone) {
            return;
        }

        bool ok = false;
        QString errorText = tr("ПК отклонил PIN");

        if (reply->error() != QNetworkReply::NoError) {
            errorText = tr("Не удалось авторизовать PIN (сеть)");
        }
        else {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                ok = doc.object().value(QStringLiteral("status")).toBool();
                const QString serverError = doc.object().value(QStringLiteral("error")).toString();
                if (!serverError.isEmpty()) {
                    errorText = serverError;
                }
            }
        }

        if (ok) {
            m_PinDone = true;
            m_PinOk = true;
            checkFinished();
            return;
        }

        // The host may not have registered the pairing session yet (or is still
        // processing) — retry a few times before giving up.
        if (m_PinAttempts < 6) {
            qInfo() << "PIN relay attempt" << m_PinAttempts << "failed, retrying:" << errorText;
            QTimer::singleShot(1000, this, &LegionConnect::submitPin);
            return;
        }

        m_PinDone = true;
        m_PinOk = false;
        fail(errorText);
    });
}

void LegionConnect::checkFinished()
{
    if (m_Stage != Pairing || !m_PairingDone || !m_PinDone) {
        return;
    }

    if (m_PairingOk && m_PinOk) {
        m_Stage = Idle;
        emit succeeded();
    }
}

void LegionConnect::fail(const QString& error)
{
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
    const bool processRunning = (m_Process->state() != QProcess::NotRunning);
    m_Stage = Idle;
    if (processRunning) {
        m_Process->kill();
    }
    emit failed(error);

    // Best-effort teardown: remove the session profile and restore the user's
    // own profile. Skip it while the process is still running (the kill() above
    // would race with the cleanup); the next connect removes leftovers anyway.
    if (touched && !processRunning) {
        disconnect();
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
