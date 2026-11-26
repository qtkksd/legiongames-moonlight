import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// One app row on the product app-selection page. Box art and name come from
// AppModel; the row uses the same hover/current treatment as ComputerCard.
Rectangle {
    id: card

    property string appName: ""
    property url boxArt: ""
    property bool running: false
    property bool hovered: false

    signal activated()

    implicitHeight: 112
    height: implicitHeight
    radius: Theme.radiusM
    color: running ? "#1f1414" : Theme.panel
    border.width: running ? 2 : 1
    border.color: (running || hovered) ? Theme.accent : Theme.line

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: card.hovered = true
        onExited: card.hovered = false
        onClicked: card.activated()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceL

        Rectangle {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 84
            radius: Theme.radiusS
            color: Theme.panelAlt
            clip: true

            Image {
                anchors.fill: parent
                source: card.boxArt.toString().length > 0
                        ? card.boxArt : "qrc:/res/no_app_image.png"
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceXs

            Label {
                Layout.fillWidth: true
                text: card.appName.length > 0 ? card.appName : qsTr("Приложение")
                color: Theme.text
                font.pointSize: Theme.fontSubtitle
                font.bold: true
                elide: Text.ElideRight
            }

            StatusPill {
                visible: card.running
                text: qsTr("Запущено")
                tone: "accent"
            }

            Item { Layout.fillHeight: true }
        }

        LegionButton {
            kind: card.running ? "primary" : "secondary"
            compact: true
            text: card.running ? qsTr("Продолжить") : qsTr("Запустить")
            onClicked: card.activated()
        }
    }
}
