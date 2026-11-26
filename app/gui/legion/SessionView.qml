import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerManager 1.0

import "Api.js" as Api
import "Theme.js" as Theme
import ".."

// Session control: creating, provisioning, connecting, active, disconnected,
// expired. The stream itself is the existing StreamSegue/AppView flow; this
// page only drives the connect code and the server state.
LegionView {
    id: sessionView

    property int remaining: 0
    property string trackedSessionId: ""
    property bool connecting: false
    property int connectProgress: 0
    property string connectStage: ""
    property string connectError: ""
    property bool stopping: false
    property bool extending: false
    property bool extendOpen: false
    property int extendAmount: 200
    property bool summaryVisible: false

    readonly property var session: shell ? shell.session : null
    readonly property string status: session ? String(session.status) : ""
    readonly property bool hasSession: session !== null
    readonly property bool active: status === "active"
    readonly property bool pending: status === "pending"
    readonly property bool disconnected: status === "disconnected"
    readonly property bool finished: status === "expired" || status === "stopped"
    readonly property string clubName: session && shell ? shell.clubName(session.club_id) : qsTr("Клуб")
    readonly property var walletRow: session && shell ? shell.walletFor(session.club_id) : null
    readonly property string code: session && session.code ? String(session.code) : ""

    function activate() {
        summaryVisible = false
        stopping = false
        extending = false
        connectError = ""
        if (shell) {
            shell.refreshSession()
            shell.refreshWallet()
        }
        syncSession()
    }

    function focusFirst() {
        if (connectButton.visible) {
            connectButton.forceActiveFocus()
        }
        else if (refreshButton.visible) {
            refreshButton.forceActiveFocus()
        }
    }

    function syncSession() {
        if (!session) {
            remaining = 0
            trackedSessionId = ""
            return
        }
        if (String(session.id) !== trackedSessionId) {
            trackedSessionId = String(session.id)
            remaining = Number(session.remaining_seconds || 0)
        }
    }

    function connectNow() {
        if (!session) {
            return
        }
        if (code.length === 0) {
            Api.reconnectSession(api, session.id)
            return
        }
        connectError = ""
        connectProgress = 0
        connectStage = qsTr("Проверяем код подключения…")
        var proof = Api.connectProof(api, code)
        if (proof.length === 0) {
            connectStage = ""
            connectError = qsTr("Не удалось подтвердить устройство. Перезапустите приложение.")
            connecting = false
            return
        }
        connecting = true
        ComputerManager.startLegionConnect(code, proof)
    }

    function openComputerList() {
        try {
            stackView.push("qrc:/gui/PcView.qml")
        }
        catch (error) {
            connectError = qsTr("Не удалось открыть список компьютеров.")
        }
    }

    function stop() {
        if (!session || stopping) {
            return
        }
        stopping = true
        ComputerManager.stopLegionConnect()
        Api.stopSession(api, session.id)
    }

    function extend() {
        if (!session || extending) {
            return
        }
        extending = true
        Api.extendSession(api, session.id, String(extendAmount) + ".00")
    }

    Connections {
        target: sessionView.shell
        ignoreUnknownSignals: true
        function onSessionChanged() {
            sessionView.syncSession()
            if (sessionView.finished) {
                sessionView.summaryVisible = true
            }
        }
    }

    Connections {
        target: sessionView.api
        ignoreUnknownSignals: true

        function onResponse(op, data) {
            if (op === "session_stop") {
                sessionView.stopping = false
                ComputerManager.stopLegionConnect()
                sessionView.summaryVisible = true
                if (sessionView.shell) {
                    sessionView.shell.refreshWallet(true)
                }
            }
            else if (op === "session_extend") {
                sessionView.extending = false
                var publicId = data && data.public_id ? String(data.public_id) : ""
                var handoff = data && data.handoff_url ? String(data.handoff_url) : ""
                if (publicId.length > 0) {
                    sessionView.go("qrc:/gui/legion/PaymentView.qml",
                                   { publicId: publicId, handoffUrl: handoff, purpose: "extend",
                                     clubId: sessionView.session ? sessionView.session.club_id : -1 })
                }
            }
            else if (op === "session_reconnect") {
                sessionView.connectError = ""
            }
        }

        function onFailed(op, code, message) {
            if (op === "session_stop") {
                sessionView.stopping = false
                sessionView.showError(code, message, op)
                if (sessionView.shell) {
                    sessionView.shell.refreshSession(true)
                }
            }
            else if (op === "session_extend") {
                sessionView.extending = false
                sessionView.showError(code, message, op)
            }
            else if (op === "session_reconnect" || op === "session_current") {
                sessionView.showError(code, message, op)
            }
        }
    }

    Connections {
        target: ComputerManager

        function onLegionConnectStatus(message) {
            sessionView.connectStage = message
        }

        function onLegionConnectProgress(percent) {
            sessionView.connectProgress = percent
        }

        function onLegionConnectFailed(error) {
            sessionView.connecting = false
            sessionView.connectError = error
        }

        function onLegionConnectSucceeded() {
            sessionView.connecting = false
            sessionView.connectProgress = 100
            sessionView.connectStage = qsTr("Подключено")
            sessionView.openComputerList()
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: sessionView.remaining > 0
        onTriggered: sessionView.remaining = Math.max(0, sessionView.remaining - 1)
    }

    // Pending sessions must not spin forever without an explanation.
    property int pendingSeconds: 0
    Timer {
        interval: 1000
        repeat: true
        running: sessionView.pending
        onTriggered: sessionView.pendingSeconds = sessionView.pendingSeconds + 1
    }
    onStatusChanged: pendingSeconds = 0

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        showBack: true
        backText: qsTr("Главная")
        title: qsTr("Сессия")
        subtitle: sessionView.hasSession
                  ? qsTr("%1 · %2").arg(sessionView.clubName).arg(Api.sessionStatusText(sessionView.status))
                  : qsTr("Нет активной сессии")
        onBack: sessionView.goBack()
    }

    ScrollView {
        contentWidth: availableWidth
        anchors.top: header.bottom
        anchors.topMargin: Theme.spaceL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        anchors.bottomMargin: Theme.spaceL
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: LegionScrollBar { }

        Column {
            width: parent.width
            spacing: Theme.spaceL

            // ---- no session ---------------------------------------------
            NeonFrame {
                width: parent.width
                height: 220
                visible: !sessionView.hasSession
                highlighted: true
                flicker: true

                EmptyState {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    title: qsTr("Пока нет активной сессии")
                    text: qsTr("Выберите клуб, пополните баланс и запустите игру. " +
                               "Время начнёт списываться после запуска.")
                    actionText: qsTr("Выбрать клуб")
                    onAction: sessionView.goSection("clubs")
                }
            }

            // ---- session panel ------------------------------------------
            NeonFrame {
                width: parent.width
                height: panelColumn.implicitHeight + Theme.spaceXl * 2
                visible: sessionView.hasSession
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: panelColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: sessionView.clubName
                            color: Theme.text
                            font.pointSize: Theme.fontHeader
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        StatusPill {
                            text: Api.sessionStatusText(sessionView.status)
                            tone: Api.sessionTone(sessionView.status)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceXxl

                        ColumnLayout {
                            spacing: 0

                            Label {
                                text: Theme.clock(sessionView.remaining)
                                color: Theme.text
                                font.pointSize: 44
                                font.bold: true
                            }

                            Label {
                                text: qsTr("осталось по данным сервера")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }
                        }

                        ColumnLayout {
                            spacing: Theme.spaceXs

                            Label {
                                text: sessionView.session && sessionView.session.pc &&
                                      sessionView.session.pc.name
                                      ? qsTr("Компьютер: %1").arg(sessionView.session.pc.name)
                                      : qsTr("Компьютер: готовится")
                                color: Theme.text
                                font.pointSize: Theme.fontBody
                            }

                            Label {
                                visible: sessionView.code.length > 0
                                text: qsTr("Код подключения: %1").arg(sessionView.code)
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }

                            Label {
                                visible: sessionView.walletRow !== null
                                text: sessionView.walletRow
                                      ? qsTr("Баланс клуба: %1").arg(Theme.money(sessionView.walletRow.balance))
                                      : ""
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Provisioning
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: sessionView.pending
                        spacing: Theme.spaceS

                        RowLayout {
                            spacing: Theme.spaceM

                            BusyIndicator {
                                running: visible
                                visible: sessionView.pending && !sessionView.connecting
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Готовим компьютер. Обычно это занимает до минуты.")
                                color: Theme.text
                                font.pointSize: Theme.fontBody
                                wrapMode: Text.Wrap
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: sessionView.pendingSeconds > 120
                            text: qsTr("Компьютер долго не отвечает. Попробуйте повторить или завершите сессию.")
                            color: Theme.warning
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }
                    }

                    // Connecting progress
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: sessionView.connecting
                        spacing: Theme.spaceS

                        Label {
                            Layout.fillWidth: true
                            text: sessionView.connectStage.length > 0
                                  ? sessionView.connectStage
                                  : qsTr("Подключаемся…")
                            color: Theme.text
                            font.pointSize: Theme.fontBody
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 8
                            radius: 4
                            color: Theme.panelAlt
                            border.width: 1
                            border.color: Theme.lineStrong

                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(100, sessionView.connectProgress)) / 100
                                height: parent.height
                                radius: 4
                                color: Theme.accent
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Прогресс подключения NetBird и Sunshine. Окно стрима откроется автоматически.")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: sessionView.disconnected
                        text: qsTr("Соединение прервано. Время может продолжать списываться — " +
                                   "подключитесь снова или завершите сессию.")
                        color: Theme.warning
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: sessionView.finished
                        text: qsTr("Сессия завершена. Итоговое списание считает сервер.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    ErrorBanner {
                        Layout.fillWidth: true
                        visible: sessionView.connectError.length > 0
                        text: qsTr("Не удалось подключиться")
                        detail: sessionView.connectError
                        canRetry: true
                        onRetry: {
                            Api.retryFailedStep(sessionView.api)
                            sessionView.connectNow()
                        }
                    }

                    ErrorBanner {
                        Layout.fillWidth: true
                        visible: sessionView.hasError
                        text: sessionView.errorText
                        detail: sessionView.serverError
                        canRetry: true
                        onRetry: sessionView.activate()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            id: connectButton
                            kind: "primary"
                            visible: sessionView.active || sessionView.disconnected || sessionView.pending
                            enabled: !sessionView.connecting && !sessionView.stopping
                            focus: visible
                            text: sessionView.disconnected ? qsTr("Подключиться снова")
                                                           : qsTr("Подключиться")
                            onClicked: sessionView.connectNow()
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: sessionView.disconnected
                            text: qsTr("Повторить")
                            onClicked: {
                                Api.reconnectSession(sessionView.api, sessionView.session.id)
                            }
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: sessionView.active
                            enabled: !sessionView.extending && !sessionView.stopping
                            text: qsTr("Продлить")
                            onClicked: sessionView.extendOpen = !sessionView.extendOpen
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: sessionView.hasSession && !sessionView.finished
                            enabled: !sessionView.stopping
                            text: sessionView.stopping ? qsTr("Завершаем…") : qsTr("Завершить")
                            onClicked: stopDialog.open()
                        }

                        Item { Layout.fillWidth: true }

                        LegionButton {
                            kind: "ghost"
                            visible: sessionView.finished || !sessionView.hasSession
                            text: qsTr("К клубам")
                            onClicked: sessionView.goSection("clubs")
                        }

                        LegionButton {
                            id: refreshButton
                            kind: "ghost"
                            text: qsTr("Обновить")
                            onClicked: {
                                if (sessionView.shell) {
                                    sessionView.shell.refreshSession(true)
                                    sessionView.shell.refreshWallet(true)
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: sessionView.extendOpen && sessionView.active
                        spacing: Theme.spaceS

                        Label {
                            text: qsTr("Продлить на")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                        }

                        Repeater {
                            model: [100, 200, 500]

                            delegate: LegionButton {
                                kind: sessionView.extendAmount === modelData ? "primary" : "secondary"
                                compact: true
                                text: qsTr("%1 ₽").arg(modelData)
                                onClicked: sessionView.extendAmount = modelData
                            }
                        }

                        Item { Layout.fillWidth: true }

                        LegionButton {
                            kind: "primary"
                            enabled: !sessionView.extending
                            text: sessionView.extending ? qsTr("Переходим к оплате…") : qsTr("Перейти к оплате")
                            onClicked: sessionView.extend()
                        }
                    }
                }
            }

            // ---- summary -------------------------------------------------
            NeonFrame {
                width: parent.width
                height: summaryColumn.implicitHeight + Theme.spaceXl * 2
                visible: sessionView.summaryVisible && sessionView.finished

                ColumnLayout {
                    id: summaryColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Итоги сессии")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Сессия завершена. Остаток баланса и итоговое списание " +
                                   "показаны по данным сервера.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    BalancePanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 88
                        clubName: sessionView.clubName
                        balance: sessionView.walletRow ? sessionView.walletRow.balance : "0"
                        bonus: sessionView.walletRow ? sessionView.walletRow.bonus : "0"
                        minutes: sessionView.walletRow ? Number(sessionView.walletRow.minutes || 0) : -1
                    }
                }
            }
        }
    }

    NavigableMessageDialog {
        id: stopDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Завершить сессию? Списание остановится, остаток баланса сохранится.")
        onAccepted: sessionView.stop()
    }
}
