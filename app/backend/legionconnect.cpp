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
#include <QUrl>
#include <QtDebug>

#define LEGION_API_BASE "https://legiongames.ru"
#define LEGION_CONNECT_URL LEGION_API_BASE "/api/client/connect"
#define LEGION_PIN_URL LEGION_API_BASE "/api/client/pin"
#define LEGION_DEFAULT_MANAGEMENT_URL "https://netbird.legiongames.ru"

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
    m_AddRetries(0)
{
    // Allow HTTP redirects (e.g. https://legiongames.ru -> www)
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    connect(m_Process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_Stage == Idle) {
            return;
        }

        if (m_Stage == NetbirdDown) {
            // Ignore the result of "down" (may fail if not connected) and go up.
            runNetbird(QStringList() << QStringLiteral("up")
                                     << QStringLiteral("--setup-key") << m_SetupKey
                                     << QStringLiteral("--management-url") << m_ManagementUrl,
                       NetbirdUp);
        }
        else if (m_Stage == NetbirdUp) {
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                fail(tr("Не удалось подключиться к NetBird"));
                return;
            }

            emit status(tr("Ожидание сети NetBird..."));
            m_Stage = WaitingForHost;
            m_AddRetries = 0;

            // Give the daemon a moment, then add the host (retried on timeout).
            QTimer::singleShot(4000, this, [this]() { addHost(); });
        }
    });

    connect(m_Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Ignore errors after we've already finished/failed (e.g. the kill() we
        // issue in fail() can emit Crashed).
        if (m_Stage == Idle) {
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

    m_Stage = stage;
    m_Process->start(exe, args);
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

    qWarning() << "LegionConnect failed:" << error;

    m_Stage = Idle;
    if (m_Process->state() != QProcess::NotRunning) {
        m_Process->kill();
    }
    emit failed(error);
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
