import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerManager 1.0

import AutoUpdateChecker 1.0

import "Api.js" as Api
import "Theme.js" as Theme
import ".."

// Status root: the active session, the queue, or "выберите клуб".
LegionView {
    id: home

    property int remaining: 0
    property string trackedSessionId: ""

    readonly property var session: shell ? shell.session : null
    readonly property var queueEntry: shell ? shell.queueEntry : null
    readonly property var wallet: shell ? shell.wallet : []
    readonly property var catalog: shell ? shell.catalog : []
    readonly property var gamification: shell ? shell.gamification : ({})
    readonly property var streak: Api.streakOf(gamification)
    readonly property var personalTasks: Api.personalTasksOf(gamification)
    readonly property var communityTasks: Api.communityTasksOf(gamification)
    readonly property string updatedAt: shell ? shell.catalogAt : ""
    readonly property bool stale: Theme.isStale(updatedAt.length > 0 ? updatedAt : catalogUpdatedAt)
    readonly property bool offline: appState === "offline"
    readonly property var freeClubs: {
        var result = []
        for (var i = 0; i < catalog.length && result.length < 3; i++) {
            if (Api.isClubOpen(catalog[i]) && Number(catalog[i].free_units || 0) > 0) {
                result.push(catalog[i])
            }
        }
        return result
    }

    function activate() {
        if (shell) {
            shell.refreshAll(false)
            shell.refreshGamification(false)
        }
        syncSession()
    }

    function focusFirst() {
        if (queueButton.visible) {
            queueButton.forceActiveFocus()
        }
        else if (openSessionButton.visible) {
            openSessionButton.forceActiveFocus()
        }
        else if (primaryAction.visible) {
            primaryAction.forceActiveFocus()
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

    Connections {
        target: home.shell
        ignoreUnknownSignals: true
        function onSessionChanged() {
            home.syncSession()
        }
    }

    Connections {
        target: home.api
        ignoreUnknownSignals: true
        function onResponse(op, data) {
            if (op === "session_start") {
                home.go("qrc:/gui/legion/SessionView.qml", {})
            }
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: home.remaining > 0
        onTriggered: home.remaining = Math.max(0, home.remaining - 1)
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Главная")
        subtitle: home.handle.length > 0 ? qsTr("Аккаунт %1").arg(home.handle) : qsTr("Аккаунт")
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
            id: body
            width: parent.width
            spacing: Theme.spaceL

            // ---- update notice ------------------------------------------
            NeonFrame {
                width: parent.width
                visible: AutoUpdateChecker.updateAvailable
                height: updateNotice.implicitHeight + Theme.spaceXl * 2
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: updateNotice
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Доступно обновление")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: AutoUpdateChecker.updateVersion.length > 0
                              ? qsTr("Версия %1 уже готова к установке.")
                                    .arg(AutoUpdateChecker.updateVersion)
                              : qsTr("Новая версия уже готова к установке.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    LegionButton {
                        kind: "primary"
                        text: qsTr("Скачать обновление")
                        onClicked: AutoUpdateChecker.installUpdate(AutoUpdateChecker.updateUrl)
                    }
                }
            }

            // ---- primary panel ------------------------------------------
            NeonFrame {
                width: parent.width
                height: primaryColumn.implicitHeight + Theme.spaceXl * 2
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: primaryColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: {
                                if (home.session) {
                                    return home.clubLabel(home.session.club_id)
                                }
                                if (home.queueEntry) {
                                    return home.clubLabel(home.queueEntry.club_id)
                                }
                                return qsTr("Выберите клуб, чтобы начать")
                            }
                            color: Theme.text
                            font.pointSize: Theme.fontHeader
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        StatusPill {
                            visible: home.session !== null
                            text: Api.sessionStatusText(home.session ? home.session.status : "")
                            tone: Api.sessionTone(home.session ? home.session.status : "")
                        }

                        StatusPill {
                            visible: home.session === null && home.queueEntry !== null
                            text: Api.queueStatusText(home.queueEntry)
                            tone: Api.queueTone(home.queueEntry)
                        }
                    }

                    // Active session
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: home.session !== null
                        spacing: Theme.spaceS

                        RowLayout {
                            spacing: Theme.spaceXl

                            ColumnLayout {
                                spacing: 0

                                Label {
                                    text: Theme.clock(home.remaining)
                                    color: Theme.text
                                    font.pointSize: Theme.fontDisplay
                                    font.bold: true
                                }

                                Label {
                                    text: qsTr("осталось")
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }
                            }

                            ColumnLayout {
                                spacing: 2

                                Label {
                                    text: home.session && home.session.pc && home.session.pc.name
                                          ? qsTr("Компьютер: %1").arg(home.session.pc.name)
                                          : qsTr("Компьютер готовится")
                                    color: Theme.text
                                    font.pointSize: Theme.fontBody
                                }

                                Label {
                                    text: qsTr("Остаток времени и списание считает сервер.")
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }
                            }
                        }
                    }

                    // Queue
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: home.session === null && home.queueEntry !== null
                        spacing: Theme.spaceS

                        Label {
                            text: home.queueEntry
                                  ? qsTr("Место в очереди: %1").arg(home.queueEntry.position || "—")
                                  : ""
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.bold: true
                        }

                        Label {
                            text: home.queueEntry && home.queueEntry.status === "notified"
                                  ? qsTr("Слот готов. Успейте подключиться, пока не истекло время.")
                                  : qsTr("Когда слот освободится, у вас будет 10 минут на подключение.")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }
                    }

                    // No session, no queue
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: home.session === null && home.queueEntry === null
                        spacing: Theme.spaceS

                        Label {
                            text: qsTr("Свободные клубы обновлены %1").arg(Theme.ageText(home.updatedAt))
                            color: home.stale ? Theme.warning : Theme.textMuted
                            font.pointSize: Theme.fontBody
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: Theme.spaceS

                            Repeater {
                                model: home.freeClubs

                                delegate: Rectangle {
                                    width: parent.width
                                    height: 60
                                    radius: Theme.radiusS
                                    color: rowMouse.containsMouse || clubAction.hovered
                                           ? Theme.panelHover : Theme.panelAlt
                                    border.width: 1
                                    border.color: Theme.line

                                    // Keep the click area behind the row so the
                                    // action button still receives its own clicks.
                                    MouseArea {
                                        id: rowMouse
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.spaceL + clubAction.width
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: home.openClub(modelData)
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.spaceL
                                        anchors.rightMargin: Theme.spaceL
                                        spacing: Theme.spaceM

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: Theme.text
                                            font.pointSize: Theme.fontSubtitle
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            text: Theme.pricePerHour(modelData.price_per_hour)
                                            color: Theme.text
                                            font.pointSize: Theme.fontBody
                                        }

                                        StatusPill {
                                            text: Api.clubAvailabilityText(modelData)
                                            tone: Api.clubTone(modelData)
                                        }

                                        LegionButton {
                                            id: clubAction
                                            readonly property real clubBalance:
                                                Api.balanceFor(home.wallet, modelData.id)
                                            readonly property real clubMinutePrice: {
                                                var minute = Number(modelData.price_per_minute || 0)
                                                if (minute > 0) {
                                                    return minute
                                                }
                                                return Number(modelData.price_per_hour || 0) / 60
                                            }
                                            readonly property bool enough:
                                                clubBalance > 0 &&
                                                (clubMinutePrice <= 0 ||
                                                 clubBalance >= clubMinutePrice)

                                            compact: true
                                            kind: enough ? "primary" : "secondary"
                                            text: enough ? qsTr("Играть") : qsTr("Пополнить")
                                            enabled: !home.serverBusy
                                            onClicked: {
                                                if (enough) {
                                                    Api.startSession(home.api, modelData.id)
                                                }
                                                else {
                                                    home.go("qrc:/gui/legion/PaymentView.qml",
                                                            { clubId: modelData.id,
                                                              clubName: modelData.name })
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        EmptyState {
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            visible: home.freeClubs.length === 0
                            title: home.offline ? qsTr("Нет соединения")
                                                : qsTr("Пока нет свободных клубов")
                            text: home.offline
                                  ? qsTr("Список клубов обновится после подключения к сети.")
                                  : qsTr("Откройте список клубов, чтобы встать в очередь.")
                            actionText: qsTr("Все клубы")
                            onAction: home.goSection("clubs")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        Item { Layout.fillWidth: true }

                        LegionButton {
                            visible: home.queueEntry !== null && home.session === null
                            kind: "secondary"
                            text: qsTr("Покинуть очередь")
                            onClicked: Api.leaveQueue(home.api, home.queueEntry.id)
                        }

                        LegionButton {
                            id: queueButton
                            visible: home.queueEntry !== null && home.session === null
                            kind: "primary"
                            text: home.queueEntry && home.queueEntry.status === "notified"
                                  ? qsTr("Подключиться")
                                  : qsTr("Открыть очередь")
                            onClicked: home.go("qrc:/gui/legion/QueueView.qml", {})
                        }

                        LegionButton {
                            visible: home.session !== null
                            kind: "secondary"
                            text: qsTr("Завершить")
                            onClicked: stopDialog.open()
                        }

                        LegionButton {
                            id: primaryAction
                            visible: home.session === null && home.queueEntry === null
                            kind: "primary"
                            focus: visible
                            text: qsTr("Все клубы")
                            onClicked: home.goSection("clubs")
                        }

                        LegionButton {
                            id: openSessionButton
                            visible: home.session !== null
                            kind: "primary"
                            text: home.session && home.session.status === "disconnected"
                                  ? qsTr("Подключиться снова")
                                  : qsTr("Открыть сессию")
                            onClicked: home.go("qrc:/gui/legion/SessionView.qml", {})
                        }
                    }
                }
            }

            // ---- daily streak and tasks ---------------------------------
            Row {
                width: parent.width
                spacing: Theme.spaceL

                NeonFrame {
                    width: (parent.width - Theme.spaceL) / 3
                    height: streakColumn.implicitHeight + Theme.spaceL * 2

                    ColumnLayout {
                        id: streakColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spaceL
                        spacing: Theme.spaceXs

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Серия дней")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 %2")
                                .arg(Number(home.streak.current || 0))
                                .arg(Theme.plural(Number(home.streak.current || 0),
                                                  "день", "дня", "дней"))
                            color: Theme.text
                            font.pointSize: Theme.fontHeader
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Лучшая: %1").arg(Number(home.streak.longest || 0))
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                        }

                        Label {
                            Layout.fillWidth: true
                            text: home.streak.today_connected === true
                                  ? qsTr("Сегодня уже играли")
                                  : qsTr("Сегодня ещё не играли")
                            color: home.streak.today_connected === true ? Theme.success : Theme.textDim
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: home.streak.freeze_available === true
                            text: qsTr("Доступна заморозка серии")
                            color: Theme.warning
                            font.pointSize: Theme.fontSmall
                        }
                    }
                }

                NeonFrame {
                    width: parent.width - ((parent.width - Theme.spaceL) / 3) - Theme.spaceL
                    height: tasksColumn.implicitHeight + Theme.spaceL * 2

                    ColumnLayout {
                        id: tasksColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spaceL
                        spacing: Theme.spaceXs

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceM

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Задания")
                                color: Theme.text
                                font.pointSize: Theme.fontTitle
                                font.bold: true
                            }

                            StatusPill {
                                text: qsTr("Скоро")
                                tone: "neutral"
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: home.shell !== null &&
                                     home.shell.gamificationError !== undefined &&
                                     String(home.shell.gamificationError).length > 0 &&
                                     home.personalTasks.length === 0 &&
                                     home.communityTasks.length === 0
                            text: qsTr("Задания временно недоступны.")
                            color: Theme.warning
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: home.personalTasks.length === 0 &&
                                     home.communityTasks.length === 0
                            text: qsTr("Ежедневные и общие задания появятся позже. " +
                                       "Следите за обновлениями.")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }

                        Repeater {
                            model: home.personalTasks

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceM

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.title ? String(modelData.title)
                                                          : qsTr("Ежедневное задание")
                                    color: Theme.text
                                    font.pointSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: Number(modelData.target || 0) > 0
                                          ? qsTr("%1 / %2").arg(Number(modelData.progress || 0))
                                                .arg(Number(modelData.target || 0))
                                          : qsTr("Личное")
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }
                            }
                        }

                        Repeater {
                            model: home.communityTasks

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceM

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.title ? String(modelData.title)
                                                          : qsTr("Общее задание")
                                    color: Theme.text
                                    font.pointSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: Number(modelData.target || 0) > 0
                                          ? qsTr("%1 / %2").arg(Number(modelData.progress || 0))
                                                .arg(Number(modelData.target || 0))
                                          : qsTr("Общее")
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }
                            }
                        }
                    }
                }
            }

            // ---- wallet -------------------------------------------------
            Row {
                width: parent.width
                spacing: Theme.spaceL

                BalancePanel {
                    width: (parent.width - Theme.spaceL) / 3
                    title: qsTr("Всего на балансе")
                    balance: Api.totalBalance(home.wallet)
                    bonus: "0"
                    minutes: -1
                }

                NeonFrame {
                    width: parent.width - ((parent.width - Theme.spaceL) / 3) - Theme.spaceL
                    height: walletColumn.implicitHeight + Theme.spaceL * 2

                    ColumnLayout {
                        id: walletColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spaceL
                        spacing: Theme.spaceXs

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceM

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Кошелёк по клубам")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }

                            LegionButton {
                                kind: "secondary"
                                compact: true
                                text: qsTr("Пополнить")
                                onClicked: home.goSection("wallet")
                            }
                        }

                        Repeater {
                            model: home.wallet

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceM

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.club && modelData.club.name
                                          ? modelData.club.name
                                          : home.clubLabel(modelData.club_id)
                                    color: Theme.text
                                    font.pointSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }

                                ColumnLayout {
                                    spacing: 0

                                    Label {
                                        Layout.alignment: Qt.AlignRight
                                        text: Theme.money(Api.clubMain(modelData))
                                        color: Api.clubMain(modelData) > 0 ? Theme.text : Theme.warning
                                        font.pointSize: Theme.fontSmall
                                        font.bold: true
                                    }

                                    Label {
                                        Layout.alignment: Qt.AlignRight
                                        visible: Api.clubBonus(modelData) > 0
                                        text: qsTr("+ бонус %1").arg(Theme.money(Api.clubBonus(modelData)))
                                        color: Theme.success
                                        font.pointSize: Theme.fontSmall
                                    }
                                }
                            }
                        }

                        Label {
                            visible: home.wallet.length === 0
                            text: qsTr("Баланс пока пуст")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                        }
                    }
                }
            }

            ErrorBanner {
                width: parent.width
                visible: home.stale && home.catalog.length > 0
                tone: "warning"
                text: qsTr("Данные устарели. Обновите список.")
                detail: qsTr("Цены и доступность показаны по состоянию на %1.").arg(Theme.timeText(home.updatedAt))
                canRetry: true
                onRetry: {
                    if (home.shell) {
                        home.shell.loadCatalog(false, true)
                    }
                }
            }
        }
    }

    NavigableMessageDialog {
        id: stopDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Завершить сессию? Списание остановится, остаток баланса сохранится.")
        onAccepted: {
            if (home.session) {
                ComputerManager.stopLegionConnect()
                Api.stopSession(home.api, home.session.id)
            }
        }
    }

    function clubLabel(clubId) {
        var name = shell ? shell.clubName(clubId) : ""
        return name.length > 0 ? name : qsTr("Клуб")
    }

    function openClub(club) {
        if (!club) {
            return
        }
        home.go("qrc:/gui/legion/ClubDetailView.qml",
                { slug: club.slug, clubId: club.id, name: club.name })
    }
}
