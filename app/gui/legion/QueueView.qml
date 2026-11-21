import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme
import ".."

// Queue: joining, waiting, slot ready with a claim countdown, expired/left.
// The claim is the only step that reserves a PC; there is no auto-connect.
LegionView {
    id: queue

    property bool joining: false
    property bool claiming: false
    property bool lastNotified: false
    property int claimSeconds: 0

    readonly property var entry: shell ? shell.queueEntry : null
    readonly property string status: entry ? String(entry.status) : ""
    readonly property string clubName: entry && shell ? shell.clubName(entry.club_id) : ""
    readonly property bool waiting: status === "waiting"
    readonly property bool ready: status === "notified"
    readonly property bool expired: status === "expired"

    function activate() {
        joining = false
        claiming = false
        if (shell) {
            shell.refreshQueue()
            if (shell.catalog.length === 0) {
                shell.loadCatalog(false)
            }
        }
        syncClaim()
        checkReady()
    }

    function focusFirst() {
        if (ready) {
            claimButton.forceActiveFocus()
        }
        else if (params && params.clubId !== undefined && entry === null) {
            joinButton.forceActiveFocus()
        }
        else {
            leaveButton.forceActiveFocus()
        }
    }

    function syncClaim() {
        if (!entry || !entry.claim_deadline) {
            claimSeconds = 0
            return
        }
        var deadline = Theme.parseDate(entry.claim_deadline)
        claimSeconds = deadline === null ? 0
                                         : Math.max(0, Math.floor((deadline.getTime() - new Date().getTime()) / 1000))
    }

    function checkReady() {
        if (ready && !lastNotified) {
            lastNotified = true
            // Taskbar flash only; focus is never stolen.
            try {
                window.alert(0)
            }
            catch (error) {
            }
        }
        if (!ready) {
            lastNotified = false
        }
    }

    function join() {
        if (!params || params.clubId === undefined || joining) {
            return
        }
        clearError()
        joining = true
        Api.joinQueue(api, params.clubId)
    }

    function leave() {
        if (!entry) {
            return
        }
        clearError()
        Api.leaveQueue(api, entry.id)
    }

    function claim() {
        if (!entry || claiming) {
            return
        }
        clearError()
        claiming = true
        Api.claimQueue(api, entry.id)
    }

    Connections {
        target: queue.shell
        ignoreUnknownSignals: true
        function onQueueEntryChanged() {
            queue.syncClaim()
            queue.checkReady()
        }
    }

    Connections {
        target: queue.api
        ignoreUnknownSignals: true

        function onResponse(op, data) {
            if (op === "queue_join") {
                queue.joining = false
            }
            else if (op === "queue_leave") {
                queue.joining = false
                queue.claiming = false
            }
            else if (op === "queue_claim") {
                queue.claiming = false
                queue.go("qrc:/gui/legion/SessionView.qml", {})
            }
        }

        function onFailed(op, code, message) {
            if (op === "queue_join" || op === "queue_leave" || op === "queue_claim") {
                queue.joining = false
                queue.claiming = false
                queue.showError(code, message)
                if (queue.shell) {
                    queue.shell.refreshQueue()
                }
            }
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: queue.ready
        onTriggered: queue.syncClaim()
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        showBack: true
        backText: qsTr("Назад")
        title: qsTr("Очередь")
        subtitle: queue.clubName.length > 0 ? queue.clubName : qsTr("Свободных ПК нет")
        onBack: queue.goBack()
    }

    ScrollView {
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

        Column {
            width: parent.width
            spacing: Theme.spaceL

            NeonFrame {
                width: parent.width
                height: mainColumn.implicitHeight + Theme.spaceXl * 2
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: mainColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: {
                                if (queue.entry === null) {
                                    return qsTr("Вы не в очереди")
                                }
                                if (queue.ready) {
                                    return qsTr("Слот готов")
                                }
                                if (queue.expired) {
                                    return qsTr("Время на подключение истекло")
                                }
                                if (queue.status === "left") {
                                    return qsTr("Вы вышли из очереди")
                                }
                                if (queue.status === "claimed") {
                                    return qsTr("Слот получен")
                                }
                                return qsTr("Ожидание в очереди")
                            }
                            color: Theme.text
                            font.pointSize: Theme.fontHero
                            font.bold: true
                            wrapMode: Text.Wrap
                        }

                        StatusPill {
                            visible: queue.entry !== null
                            text: Api.queueStatusText(queue.entry)
                            tone: Api.queueTone(queue.entry)
                        }
                    }

                    // Waiting
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: queue.waiting
                        spacing: Theme.spaceS

                        Label {
                            text: queue.entry ? qsTr("Место в очереди: %1").arg(queue.entry.position || "—") : ""
                            color: Theme.text
                            font.pointSize: Theme.fontHeader
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Когда слот освободится, у вас будет 10 минут на подключение. " +
                                       "Позиция обновляется сервером.")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }
                    }

                    // Ready
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: queue.ready
                        spacing: Theme.spaceS

                        Label {
                            text: Theme.clockShort(queue.claimSeconds)
                            color: Theme.accent
                            font.pointSize: 44
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Осталось времени на подключение. Не закрывайте приложение.")
                            color: Theme.text
                            font.pointSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }
                    }

                    // Expired / left / claimed
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: queue.expired || queue.status === "left" || queue.status === "claimed"
                        spacing: Theme.spaceS

                        Label {
                            Layout.fillWidth: true
                            text: queue.status === "left"
                                  ? qsTr("Вы покинули очередь. Можно встать снова, когда появится место.")
                                  : qsTr("Слот освободился, но подключение не началось. " +
                                         "Встаньте в очередь снова или выберите другой клуб.")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }
                    }

                    // Not in a queue but arrived from a club
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: queue.entry === null && (params !== null) &&
                                 params.clubId !== undefined
                        spacing: Theme.spaceS

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Свободных компьютеров нет. Встаньте в очередь: " +
                                       "место резервируется только при подключении.")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }
                    }

                    ErrorBanner {
                        Layout.fillWidth: true
                        visible: queue.hasError
                        text: queue.errorText
                        detail: queue.serverError
                        canRetry: true
                        onRetry: {
                            if (queue.entry === null) {
                                queue.join()
                            }
                            else if (queue.shell) {
                                queue.shell.refreshQueue()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        BusyIndicator {
                            visible: queue.joining || queue.claiming
                            running: visible
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                        }

                        Item { Layout.fillWidth: true }

                        LegionButton {
                            kind: "secondary"
                            visible: queue.entry !== null && (queue.waiting || queue.ready)
                            text: queue.ready ? qsTr("Пропустить слот") : qsTr("Покинуть очередь")
                            onClicked: leaveDialog.open()
                        }

                        LegionButton {
                            id: claimButton
                            kind: "primary"
                            visible: queue.ready
                            focus: visible
                            enabled: !queue.claiming
                            text: queue.claiming ? qsTr("Подключаем…") : qsTr("Подключиться")
                            onClicked: queue.claim()
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: queue.expired || queue.status === "left" || queue.status === "claimed"
                            text: qsTr("Выбрать клуб")
                            onClicked: queue.goSection("clubs")
                        }

                        LegionButton {
                            id: joinButton
                            kind: "primary"
                            visible: queue.entry === null && (params !== null) &&
                                     params.clubId !== undefined
                            focus: visible
                            enabled: !queue.joining
                            text: queue.joining ? qsTr("Встаём в очередь…") : qsTr("Встать в очередь")
                            onClicked: queue.join()
                        }

                        LegionButton {
                            id: leaveButton
                            kind: "ghost"
                            visible: queue.entry === null && (!params || params.clubId === undefined)
                            text: qsTr("К клубам")
                            onClicked: queue.goSection("clubs")
                        }

                        LegionButton {
                            kind: "ghost"
                            text: qsTr("Обновить")
                            onClicked: {
                                if (queue.shell) {
                                    queue.shell.refreshQueue()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    NavigableMessageDialog {
        id: leaveDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: queue.ready
              ? qsTr("Пропустить слот? Место перейдёт следующему в очереди.")
              : qsTr("Покинуть очередь?")
        onAccepted: queue.leave()
    }
}
