import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Notice.js" as Notice
import "Theme.js" as Theme
import ".."

// Sessions: the live session/queue at the top and the finished-session history
// below it. History is read-only and comes from /app/v1/sessions/history.
LegionView {
    id: sessions

    readonly property var session: shell ? shell.session : null
    readonly property var queueEntry: shell ? shell.queueEntry : null
    readonly property bool hasLiveSession: session !== null &&
                                           session.status !== "expired" &&
                                           session.status !== "stopped"

    readonly property var history: shell ? shell.sessionHistory : []
    readonly property bool historyLoading: shell ? shell.sessionHistoryLoading : false
    readonly property string historyError: shell ? shell.sessionHistoryError : ""
    readonly property bool historyDjangoDown: Notice.isDjangoDown(historyError)

    function activate() {
        if (shell) {
            shell.refreshSession()
            shell.refreshQueue()
        }
        loadHistory(false)
    }

    function loadHistory(force) {
        if (shell) {
            shell.refreshSessionHistory(force === true)
        }
    }

    function focusFirst() {
        if (openSessionButton.visible) {
            openSessionButton.forceActiveFocus()
        }
        else if (openQueueButton.visible) {
            openQueueButton.forceActiveFocus()
        }
        else {
            chooseClubButton.forceActiveFocus()
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Сессии")
        subtitle: qsTr("Текущая сессия, очередь и история")
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

            // ---- live state ----------------------------------------------
            NeonFrame {
                width: parent.width
                height: liveColumn.implicitHeight + Theme.spaceXl * 2
                highlighted: sessions.hasLiveSession || sessions.queueEntry !== null

                ColumnLayout {
                    id: liveColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        Layout.fillWidth: true
                        text: sessions.hasLiveSession
                              ? qsTr("Активная сессия")
                              : (sessions.queueEntry !== null
                                 ? qsTr("Очередь")
                                 : qsTr("Нет активных сессий"))
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: sessions.hasLiveSession
                        text: sessions.session
                              ? qsTr("%1 · %2")
                                    .arg(sessions.shell
                                         ? sessions.shell.clubName(sessions.session.club_id)
                                         : qsTr("Клуб"))
                                    .arg(Api.sessionStatusText(sessions.session.status))
                              : ""
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: sessions.hasLiveSession === false && sessions.queueEntry !== null
                        text: sessions.queueEntry
                              ? qsTr("%1 · место %2")
                                    .arg(sessions.shell
                                         ? sessions.shell.clubName(sessions.queueEntry.club_id)
                                         : qsTr("Клуб"))
                                    .arg(sessions.queueEntry.position || "—")
                              : ""
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: sessions.hasLiveSession === false && sessions.queueEntry === null
                        text: qsTr("Когда вы начнёте игру или встанете в очередь, " +
                                   "они появятся здесь.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            id: openSessionButton
                            visible: sessions.hasLiveSession
                            kind: "primary"
                            text: sessions.session && sessions.session.status === "disconnected"
                                  ? qsTr("Переподключиться")
                                  : qsTr("Открыть сессию")
                            onClicked: sessions.go("qrc:/gui/legion/SessionView.qml", {})
                        }

                        LegionButton {
                            id: openQueueButton
                            visible: sessions.hasLiveSession === false && sessions.queueEntry !== null
                            kind: "primary"
                            text: qsTr("Открыть очередь")
                            onClicked: sessions.go("qrc:/gui/legion/QueueView.qml", {})
                        }

                        LegionButton {
                            id: chooseClubButton
                            visible: sessions.hasLiveSession === false && sessions.queueEntry === null
                            kind: "primary"
                            text: qsTr("Выбрать клуб")
                            onClicked: sessions.goSection("clubs")
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- history -------------------------------------------------
            NeonFrame {
                width: parent.width
                height: historyColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: historyColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("История сессий")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: sessions.historyLoading
                        visible: running
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !sessions.historyLoading && sessions.history.length === 0
                        text: qsTr("История пока пуста.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: sessions.history

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 64
                            radius: Theme.radiusS
                            color: Theme.panelAlt
                            border.width: 1
                            border.color: Theme.line

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.spaceM
                                spacing: Theme.spaceM

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.club && modelData.club.name
                                          ? modelData.club.name
                                          : qsTr("Клуб")
                                    color: Theme.text
                                    font.pointSize: Theme.fontBody
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: Theme.dateTimeText(modelData.started_at)
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }

                                Label {
                                    text: Theme.duration(Number(modelData.duration_seconds || 0))
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }

                                Label {
                                    text: modelData.spent !== null && modelData.spent !== undefined
                                          ? Theme.money(modelData.spent)
                                          : "—"
                                    color: Theme.text
                                    font.pointSize: Theme.fontBody
                                    font.bold: true
                                }
                            }
                        }
                    }

                    ErrorBanner {
                        Layout.fillWidth: true
                        visible: sessions.historyError.length > 0 &&
                                 !sessions.historyDjangoDown
                        text: Api.errorText(sessions.historyError, sessions.serverError)
                        detail: sessions.serverError
                        canRetry: true
                        onRetry: sessions.loadHistory(true)
                    }
                }
            }
        }
    }
}
