import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// One computer row on the product "Компьютеры" page. It deliberately does
// not expose Moonlight's AppView: streaming must go through the club session
// and billing flow. Only the active session has an "open session" action.
Rectangle {
    id: card

    property string computerName: ""
    property string address: ""
    property bool online: false
    property bool paired: false
    property bool busy: false
    property bool wakeable: false
    property bool statusUnknown: false
    property bool legion: false
    property bool currentSession: false

    signal openSession()
    signal wake()
    signal forget()

    implicitHeight: Math.max(92, content.implicitHeight + Theme.spaceL * 2)
    height: implicitHeight
    radius: Theme.radiusM
    color: currentSession ? "#1f1414" : Theme.panel
    border.width: currentSession ? 2 : 1
    border.color: currentSession ? Theme.accent : Theme.line

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceL

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceXs

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceS

                Label {
                    Layout.fillWidth: true
                    text: card.computerName.length > 0
                          ? card.computerName
                          : qsTr("Компьютер")
                    color: Theme.text
                    font.pointSize: Theme.fontSubtitle
                    font.bold: true
                    elide: Text.ElideRight
                }

                StatusPill {
                    visible: card.currentSession
                    text: qsTr("Текущая сессия")
                    tone: "accent"
                }
            }

            Label {
                Layout.fillWidth: true
                visible: card.address.length > 0
                text: card.address
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                elide: Text.ElideRight
            }

            RowLayout {
                spacing: Theme.spaceXs

                StatusPill {
                    text: {
                        if (card.statusUnknown) {
                            return qsTr("Поиск")
                        }
                        return card.online ? qsTr("В сети") : qsTr("Не в сети")
                    }
                    tone: card.statusUnknown ? "neutral"
                                             : (card.online ? "good" : "bad")
                }

                StatusPill {
                    visible: card.online
                    text: card.paired ? qsTr("Сопряжён") : qsTr("Не сопряжён")
                    tone: card.paired ? "good" : "warn"
                }

                StatusPill {
                    visible: card.busy
                    text: qsTr("Занят")
                    tone: "warn"
                }

                StatusPill {
                    visible: card.legion
                    text: qsTr("Клубный ПК")
                    tone: "neutral"
                }
            }
        }

        RowLayout {
            spacing: Theme.spaceS

            LegionButton {
                kind: "primary"
                compact: true
                visible: card.currentSession
                text: qsTr("Открыть сессию")
                onClicked: card.openSession()
            }

            LegionButton {
                kind: "secondary"
                compact: true
                visible: !card.online && card.wakeable
                text: qsTr("Разбудить")
                onClicked: card.wake()
            }

            LegionButton {
                kind: "ghost"
                compact: true
                visible: !card.currentSession
                text: qsTr("Забыть")
                onClicked: card.forget()
            }
        }
    }
}
