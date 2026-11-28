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
    property string leaseId: ""
    // Q_INVOKABLE getters are not reactive; the lease change signal bumps this.
    property int leaseContextRevision: 0

    readonly property bool ready: appModel !== null
    readonly property var session: shell ? shell.session : null
    readonly property bool leaseValid: {
        // Explicit reads so the binding depends on every input, not on how the
        // engine tracks calls into appLeaseValid().
        var current = session
        var currentLease = leaseId
        var currentUuid = computerUuid
        var revision = leaseContextRevision
        return appLeaseValid()
    }

    // A stream may start only for the active lease, on the machine bound to
    // it. The server enforces the same rule; this guard keeps the UI from
    // offering a launch that would bypass billing.
    function appLeaseValid() {
        // dependency on manager changes
        var revision = leaseContextRevision
        if (leaseId.length === 0 || !session) {
            return false
        }
        if (String(session.id || "") !== leaseId) {
            return false
        }
        if (String(session.status || "") !== "active") {
            return false
        }
        var hostUuid = ""
        try {
            hostUuid = ComputerManager.legionLeaseHostUuid
                       ? String(ComputerManager.legionLeaseHostUuid()) : ""
        }
        catch (error) {
            hostUuid = ""
        }
        if (hostUuid.length > 0 && computerUuid.length > 0 &&
                hostUuid !== computerUuid) {
            return false
        }
        return true
    }

    function activate() {
        clearError()

        var nextIndex = params && params.computerIndex !== undefined
                        ? Number(params.computerIndex) : -1
        var nextName = params && params.computerName
                       ? String(params.computerName) : qsTr("Компьютер")
        var nextUuid = params && params.computerUuid
                       ? String(params.computerUuid) : ""
        var nextLease = params && params.leaseId !== undefined
                        ? String(params.leaseId) : ""

        var changed = nextIndex !== computerIndex ||
                      nextLease !== leaseId ||
                      nextUuid !== computerUuid

        computerIndex = nextIndex
        computerName = nextName
        computerUuid = nextUuid
        leaseId = nextLease

        if (computerIndex < 0) {
            showError("computer_not_selected", qsTr("Компьютер не выбран."), "apps")
            return
        }

        if (appModel !== null && !changed) {
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
        if (!appLeaseValid()) {
            showError("lease_required",
                      qsTr("Запуск доступен только на ПК активной сессии клуба."),
                      "apps")
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

        streamLauncher.startStream(session, name, running === true, leaseId)
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

            ErrorBanner {
                width: parent.width
                visible: !apps.leaseValid
                tone: "warning"
                text: qsTr("Запуск недоступен")
                detail: qsTr("Этот ПК не входит в активную сессию клуба. " +
                             "Начните сессию и подключитесь к её компьютеру.")
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

    Connections {
        target: ComputerManager
        ignoreUnknownSignals: true

        function onLegionLeaseContextChanged() {
            apps.leaseContextRevision += 1
        }
    }
}
