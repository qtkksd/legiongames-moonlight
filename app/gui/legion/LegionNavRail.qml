import QtQuick 2.9
import QtQuick.Controls 2.2

import "Theme.js" as Theme

// Persistent section rail: Главная, Сессии, Компьютеры, Клубы, Кошелёк,
// Аккаунт, Настройки. QA is appended only in staging builds. Streaming
// preferences live inside Settings.
//
// Badges only report server state (active session, queue position), never a
// client-side guess. Focus enters with Menu and leaves with Right/Escape so the
// leftmost content column stays reachable; nothing grabs focus on load.
Rectangle {
    id: rail

    property string current: "home"
    property int queuePosition: 0
    property bool queueReady: false
    property bool sessionActive: false
    property string handle: ""
    property bool qaEnabled: false

    signal navigate(string key)
    signal contentRequested()

    implicitWidth: 220
    color: Theme.panelAlt

    function moveFocus(index) {
        if (navItems.count === 0) {
            return
        }
        var next = Math.max(0, Math.min(navItems.count - 1, index))
        var entry = navItems.itemAt(next)
        if (entry) {
            entry.forceActiveFocus()
        }
    }

    Column {
        id: navColumn
        anchors.top: parent.top
        anchors.topMargin: Theme.spaceXl
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spaceXs

        Repeater {
            id: navItems
            model: {
                var items = [
                    { key: "home", title: qsTr("Главная") },
                    { key: "sessions", title: qsTr("Сессии") },
                    { key: "computers", title: qsTr("Компьютеры") },
                    { key: "clubs", title: qsTr("Клубы") },
                    { key: "wallet", title: qsTr("Кошелёк") },
                    { key: "account", title: qsTr("Аккаунт") },
                    { key: "settings", title: qsTr("Настройки") }
                ]
                if (rail.qaEnabled) {
                    items.push({ key: "qa", title: qsTr("QA") })
                }
                return items
            }

            delegate: Item {
                id: entry

                property bool isCurrent: rail.current === modelData.key
                property string badge: {
                    if (modelData.key === "home" && rail.sessionActive) {
                        return qsTr("сессия")
                    }
                    if (modelData.key === "clubs" && rail.queueReady) {
                        return qsTr("слот готов")
                    }
                    if (modelData.key === "clubs" && rail.queuePosition > 0) {
                        return qsTr("очередь %1").arg(rail.queuePosition)
                    }
                    return ""
                }

                width: rail.width
                height: 52
                activeFocusOnTab: true

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceS
                    anchors.rightMargin: Theme.spaceS
                    radius: Theme.radiusS
                    color: entry.isCurrent ? "#221414"
                                              : (entryMouse.containsMouse ? Theme.panel : "transparent")
                }

                Rectangle {
                    visible: entry.isCurrent
                    width: 3
                    height: entry.height - Theme.spaceL
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spaceS
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: Theme.accent
                }

                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spaceXl
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.title
                    color: entry.isCurrent ? Theme.text : Theme.textMuted
                    font.pointSize: Theme.fontSubtitle
                    font.bold: entry.isCurrent
                }

                StatusPill {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spaceL
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.badge.length > 0
                    text: entry.badge
                    tone: (modelData.key === "clubs" && rail.queueReady) ? "accent" : "neutral"
                }

                MouseArea {
                    id: entryMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rail.navigate(modelData.key)
                }

                Keys.onUpPressed: rail.moveFocus(index - 1)
                Keys.onDownPressed: rail.moveFocus(index + 1)
                Keys.onRightPressed: rail.contentRequested()
                Keys.onReturnPressed: rail.navigate(modelData.key)
                Keys.onEnterPressed: rail.navigate(modelData.key)
                Keys.onEscapePressed: rail.contentRequested()
            }
        }
    }

    Column {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spaceXl
        anchors.left: parent.left
        anchors.leftMargin: Theme.spaceXl
        anchors.right: parent.right
        anchors.rightMargin: Theme.spaceL
        spacing: Theme.spaceXs

        Label {
            width: parent.width
            text: rail.handle.length > 0 ? rail.handle : qsTr("Гость")
            color: Theme.text
            font.pointSize: Theme.fontBody
            elide: Text.ElideRight
        }

        Label {
            width: parent.width
            text: qsTr("Один аккаунт — один ПК")
            color: Theme.textDim
            font.pointSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }
}
