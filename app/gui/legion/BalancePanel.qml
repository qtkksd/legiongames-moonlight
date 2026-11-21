import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Per-club wallet summary: balance, bonus and the estimated play time.
NeonFrame {
    id: panel

    property string title: qsTr("Баланс клуба")
    property var balance: "0"
    property var bonus: "0"
    property int minutes: -1
    property string clubName: ""

    implicitHeight: 88

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceXl

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: panel.clubName.length > 0 ? panel.clubName : panel.title
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: Theme.money(panel.balance)
                color: Number(panel.balance) > 0 ? Theme.text : Theme.warning
                font.pointSize: Theme.fontHeader
                font.bold: true
            }
        }

        ColumnLayout {
            visible: Number(panel.bonus) > 0
            spacing: 2

            Label {
                text: qsTr("Бонус")
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
            }

            Label {
                text: Theme.money(panel.bonus)
                color: Theme.success
                font.pointSize: Theme.fontSubtitle
                font.bold: true
            }
        }

        ColumnLayout {
            visible: panel.minutes >= 0
            spacing: 2

            Label {
                text: qsTr("Примерно")
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
            }

            Label {
                text: panel.minutes >= 0 ? (panel.minutes + " мин") : "—"
                color: Theme.text
                font.pointSize: Theme.fontSubtitle
                font.bold: true
            }
        }
    }
}
