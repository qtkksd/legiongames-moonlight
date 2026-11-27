import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import SystemProperties 1.0
import AutoUpdateChecker 1.0

import "Api.js" as Api
import "Theme.js" as Theme
import ".."

// App settings that belong to the product (not the streaming pipeline):
// updates, application folder, network state, privacy, diagnostics, about.
// The full Moonlight settings page stays one click away.
LegionView {
    id: settings

    readonly property string platformName: {
        if (Qt.platform.os === "windows") {
            return "Windows"
        }
        if (Qt.platform.os === "osx") {
            return "macOS"
        }
        if (Qt.platform.os === "linux") {
            return "Linux"
        }
        return Qt.platform.os
    }

    readonly property string appFolder: {
        var configured = api && api.installPath ? String(api.installPath) : ""
        if (configured.length > 0) {
            return configured
        }
        // Fall back to the executable directory for older builds.
        var args = Qt.application.arguments
        if (!args || args.length === 0) {
            return "—"
        }
        var path = String(args[0])
        var cut = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return cut > 0 ? path.substring(0, cut) : path
    }

    readonly property bool logFolderAvailable: Api.hasOpenLogFolder(api)

    readonly property bool offline: appState === "offline" || lastErrorCode === "offline"
    readonly property bool updateRequired: lastErrorCode === "client_update_required"
    readonly property bool updateAvailable: AutoUpdateChecker.updateAvailable
    readonly property string updateVersion: AutoUpdateChecker.updateVersion
    readonly property string updateUrl: AutoUpdateChecker.updateUrl
    readonly property string netbirdLabel: {
        if (netbirdStatus === "online") {
            return qsTr("NetBird — онлайн")
        }
        if (netbirdStatus === "missing") {
            return qsTr("NetBird — не найден")
        }
        if (netbirdStatus === "offline") {
            return qsTr("NetBird — офлайн")
        }
        return qsTr("NetBird — проверка")
    }
    readonly property string netbirdTone: {
        if (netbirdStatus === "online") {
            return "good"
        }
        if (netbirdStatus === "offline" || netbirdStatus === "missing") {
            return "bad"
        }
        return "neutral"
    }

    function activate() {
        Api.checkNetbirdStatus(api)
        clearError()
    }

    function focusFirst() {
        if (updateButton.visible) {
            updateButton.forceActiveFocus()
        }
        else {
            connectionButton.forceActiveFocus()
        }
    }

    function checkConnection() {
        Api.checkNetbirdStatus(api)
        Api.retryFailedStep(api)
        if (shell) {
            shell.loadCatalog(false, true)
            shell.refreshWallet(true)
            shell.refreshSession(true)
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Настройки")
        subtitle: qsTr("Приложение, стриминг и аккаунт")
    }

    LegionScrollView {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spaceL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        anchors.bottomMargin: Theme.spaceL

        Column {
            width: parent.width
            spacing: Theme.spaceL

            ErrorBanner {
                width: parent.width
                visible: settings.updateRequired
                tone: "danger"
                text: qsTr("Обновите приложение, чтобы продолжить.")
                detail: qsTr("Эта версия больше не поддерживается сервером.")
                canRetry: false
            }

            ErrorBanner {
                width: parent.width
                visible: settings.offline
                tone: "warning"
                text: qsTr("Нет соединения. Проверьте интернет.")
                detail: qsTr("Без сети клубы, сессии и оплата недоступны.")
                canRetry: true
                onRetry: settings.checkConnection()
            }

            // ---- updates -------------------------------------------------
            NeonFrame {
                width: parent.width
                height: updateColumn.implicitHeight + Theme.spaceXl * 2
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: updateColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Обновления")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Версия %1, сборка %2")
                            .arg(SystemProperties.versionString)
                            .arg(SystemProperties.commitString.substring(0, 7))
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Установщик не подписан сертификатом. Обновление проверяется " +
                                   "по подписанному манифесту и контрольной сумме файла.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !settings.updateAvailable
                        text: qsTr("Установлена последняя версия или обновлений сейчас нет.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            id: updateButton
                            kind: "primary"
                            visible: settings.updateAvailable
                            text: settings.updateVersion.length > 0
                                  ? qsTr("Скачать обновление %1").arg(settings.updateVersion)
                                  : qsTr("Скачать обновление")
                            onClicked: AutoUpdateChecker.installUpdate(settings.updateUrl)
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- application --------------------------------------------
            NeonFrame {
                width: parent.width
                height: appColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: appColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Приложение")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Папка приложения: %1").arg(settings.appFolder)
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Платформа: %1").arg(settings.platformName)
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Открыть папку приложения")
                            onClicked: Api.openInstallFolder(settings.api)
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: settings.logFolderAvailable
                            text: qsTr("Открыть папку логов")
                            onClicked: Api.openLogFolder(settings.api)
                        }

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Компьютеры")
                            onClicked: settings.goSection("computers")
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- streaming ----------------------------------------------
            NeonFrame {
                width: parent.width
                height: streamColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: streamColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Стриминг")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Качество, звук, управление и хост.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            id: streamSettingsButton
                            kind: "secondary"
                            text: qsTr("Открыть параметры стриминга")
                            onClicked: settings.go(
                                           "qrc:/gui/legion/StreamSettingsView.qml", {})
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- network -------------------------------------------------
            NeonFrame {
                width: parent.width
                height: networkColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: networkColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Сеть")
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.bold: true
                        }

                        StatusPill {
                            text: settings.offline ? qsTr("LegionGames — офлайн")
                                                   : qsTr("LegionGames — онлайн")
                            tone: settings.offline ? "bad" : "good"
                        }

                        StatusPill {
                            text: settings.netbirdLabel
                            tone: settings.netbirdTone
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Соединение с сервером LegionGames. Стриминг идёт через NetBird и " +
                                   "не зависит от этого статуса.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            id: connectionButton
                            kind: "secondary"
                            text: qsTr("Проверить соединение")
                            onClicked: settings.checkConnection()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- privacy -------------------------------------------------
            NeonFrame {
                width: parent.width
                height: privacyColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: privacyColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Приватность")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Ключ устройства и слова восстановления хранятся только на этом ПК в " +
                                   "защищённом хранилище системы. Сервер хранит хеш слов, а не сами слова.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Публичные страницы клубов и документов открываются без аккаунта " +
                                   "и без передачи персональных данных.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Документы на сайте")
                            onClicked: settings.openUrl("https://legiongames.ru/")
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- diagnostics ---------------------------------------------
            NeonFrame {
                width: parent.width
                height: diagColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: diagColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Диагностика")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Данные для поддержки. Токены, слова и ключи сюда не попадают.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    LegionScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120

                        TextArea {
                            id: diagnosticsText
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextArea.Wrap
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            font.family: "monospace"
                            text: "app: " + SystemProperties.versionString + "+" +
                                  SystemProperties.commitString.substring(0, 7) + "\n" +
                                  "platform: " + settings.platformName + "\n" +
                                  "arch: " + SystemProperties.friendlyNativeArchName + "\n" +
                                  "account: " + (settings.handle.length > 0 ? settings.handle : "none") + "\n" +
                                  "state: " + (settings.appState.length > 0 ? settings.appState : "unknown") + "\n" +
                                  "vault: " + (settings.vaultPath.length > 0 ? settings.vaultPath : "unavailable") + "\n" +
                                  "netbird: " + settings.netbirdStatus + "\n" +
                                  "last_error: " + (settings.serverErrorCode.length > 0 ? settings.serverErrorCode : "none") + "\n" +
                                  "last_error_message: " + (settings.serverError.length > 0 ? settings.serverError : "none")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Скопировать диагностику")
                            onClicked: {
                                diagnosticsText.selectAll()
                                diagnosticsText.copy()
                            }
                        }

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Telegram-бот")
                            onClicked: settings.openUrl("https://t.me/legiongamebot")
                        }

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("rem.legiongames@yandex.com")
                            onClicked: settings.openUrl("mailto:rem.legiongames@yandex.com")
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- about ---------------------------------------------------
            NeonFrame {
                width: parent.width
                height: aboutColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: aboutColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("О приложении")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("LegionGames — клиент для игры на компьютерах клубов. " +
                                   "Аккаунт создаётся в приложении, оплата проходит в браузере.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Клиент основан на Moonlight (GPLv3). Один аккаунт — один ПК. " +
                                   "Сервис предназначен для лиц старше 18 лет.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("ИП Адигамов Р.А., ИНН: 772307873972, ОГРНИП: 325774600216705")
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Адрес: г. Москва, ул. Перерва, д. 39, кв. 196")
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                }
            }

            Label {
                width: parent.width
                text: qsTr("LegionGames. Клиент основан на Moonlight (GPLv3). Один аккаунт — один ПК.")
                color: Theme.textDim
                font.pointSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }
}
