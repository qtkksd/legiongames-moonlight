import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import AppModel 1.0
import ComputerManager 1.0

import "Theme.js" as Theme
import ".."

// App selection for one discovered/paired PC. This page replaces Moonlight's
// AppView in the normal product flow; the actual stream launch is delegated
// to the main window bridge because StreamSegue owns the main StackView.
LegionView {
    id: apps

    property var appModel: null
    property int computerIndex: -1
    property string computerName: ""
    property string computerUuid: ""

    readonly property bool ready: appModel !== null

    function activate() {
        clearError()
        if (appModel !== null) {
            return
        }

        computerIndex = params && params.computerIndex !== undefined
                        ? Number(params.computerIndex) : -1
        computerName = params && params.computerName
                       ? String(params.computerName) : qsTr("Компьютер")
        computerUuid = params && params.computerUuid
                       ? String(params.computerUuid) : ""

        if (computerIndex < 0) {
            showError("computer_not_selected", qsTr("Компьютер не выбран."), "apps")
            return
        }

        var model = Qt.createQmlObject(
            'import AppModel 1.0; AppModel {}', parent, '')
        model.initialize(ComputerManager, computerIndex, false)
        appModel = model
    }

    function focusFirst() {
        if (appRepeater.count > 0 && appRepeater.itemAt(0) !== null) {
            appRepeater.itemAt(0).forceActiveFocus()
        }
        else {
            apps.forceActiveFocus()
        }
    }

    function launch(index, name, running) {
        if (!appModel) {
            return
        }
        var runningId = appModel.getRunningAppId()
        if (runningId !== 0 && !running) {
            showError("app_already_running",
                      qsTr("Сначала завершите запущенное приложение."), "apps")
            return
        }

        var session = appModel.createSessionForApp(index)
        if (!session) {
            showError("app_launch_failed",
                      qsTr("Не удалось создать сессию приложения."), "apps")
            return
        }

        if (typeof streamLauncher === "undefined" || !streamLauncher) {
            showError("stream_launcher_missing",
                      qsTr("Не удалось запустить поток. Перезапустите приложение."), "apps")
            return
        }

        streamLauncher.startStream(session, name, running === true)
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        showBack: true
        backText: qsTr("Компьютеры")
        title: apps.computerName
        subtitle: qsTr("Выберите приложение")
        onBack: {
            if (apps.shell && apps.shell.pageHistory.length > 0) {
                apps.goBack()
            }
            else {
                apps.goSection("computers")
            }
        }
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
                visible: apps.hasInlineError
                tone: apps.errorTone
                text: apps.errorText
                canRetry: false
            }

            Label {
                width: parent.width
                visible: apps.computerUuid.length > 0
                text: qsTr("ПК: %1").arg(apps.computerName)
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                elide: Text.ElideRight
            }

            Column {
                width: parent.width
                spacing: Theme.spaceS

                Repeater {
                    id: appRepeater
                    model: apps.appModel

                    delegate: ComputerAppCard {
                        width: parent.width
                        appName: model.name ? String(model.name) : ""
                        boxArt: model.boxart ? model.boxart : ""
                        running: model.running === true
                        onActivated: apps.launch(
                                         index,
                                         String(model.name || ""),
                                         model.running === true)
                    }
                }
            }

            EmptyState {
                width: parent.width
                visible: apps.ready && appRepeater.count === 0
                title: qsTr("Приложения не найдены")
                text: qsTr("На этом компьютере пока нет доступных приложений.")
                actionText: qsTr("К компьютерам")
                onAction: apps.goSection("computers")
            }
        }
    }

    Connections {
        target: apps.appModel
        ignoreUnknownSignals: true

        function onComputerLost() {
            apps.goSection("computers")
        }
    }
}
