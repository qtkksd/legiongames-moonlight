import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Centered empty/placeholder state with one optional action.
Item {
    id: empty

    property string title: ""
    property string text: ""
    property string actionText: ""
    property string iconText: ""

    signal action()

    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spaceXl * 2, 520)
        spacing: Theme.spaceM

        Label {
            visible: empty.iconText.length > 0
            Layout.alignment: Qt.AlignHCenter
            text: empty.iconText
            color: Theme.lineStrong
            font.pointSize: Theme.fontIcon
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: empty.title
            color: Theme.text
            font.pointSize: Theme.fontTitle
            font.bold: true
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: empty.text.length > 0
            text: empty.text
            color: Theme.textMuted
            font.pointSize: Theme.fontBody
            wrapMode: Text.Wrap
        }

        LegionButton {
            Layout.alignment: Qt.AlignHCenter
            visible: empty.actionText.length > 0
            kind: "primary"
            text: empty.actionText
            onClicked: empty.action()
        }
    }
}
