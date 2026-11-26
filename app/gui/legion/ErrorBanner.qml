import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Inline error with a retry action. The raw server code is never the only text.
Rectangle {
    id: banner

    property string text: ""
    property string detail: ""
    property bool canRetry: true

    signal retry()

    visible: text.length > 0
    implicitHeight: visible ? (layout.implicitHeight + Theme.spaceL * 1.5) : 0
    color: "#221414"
    radius: Theme.radiusS
    border.width: 1
    border.color: Theme.error

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spaceM
        spacing: Theme.spaceM

        Label {
            text: "!"
            color: Theme.error
            font.pointSize: Theme.fontSubtitle
            font.bold: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            SelectableText {
                Layout.fillWidth: true
                text: banner.text
                color: Theme.text
                font.pointSize: Theme.fontBody
                wrapMode: TextEdit.Wrap
            }

            SelectableText {
                Layout.fillWidth: true
                visible: banner.detail.length > 0
                text: banner.detail
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                wrapMode: TextEdit.Wrap
            }
        }

        LegionButton {
            visible: banner.canRetry
            kind: "secondary"
            compact: true
            text: qsTr("Повторить")
            onClicked: banner.retry()
        }
    }
}
