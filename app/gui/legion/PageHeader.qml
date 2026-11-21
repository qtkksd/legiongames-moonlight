import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Page heading with an explicit back control. The window toolbar is hidden
// while the product shell is on screen, so detail pages carry their own way
// back for mouse, keyboard and controller users.
Item {
    id: header

    property string title: ""
    property string subtitle: ""
    property bool showBack: false
    property string backText: qsTr("Назад")

    signal back()

    implicitHeight: Math.max(56, titleColumn.implicitHeight)

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spaceM

        LegionButton {
            visible: header.showBack
            kind: "secondary"
            compact: true
            text: "< " + header.backText
            onClicked: header.back()
        }

        ColumnLayout {
            id: titleColumn
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: header.title
                color: Theme.text
                font.pointSize: Theme.fontHeader
                font.bold: true
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                visible: header.subtitle.length > 0
                text: header.subtitle
                color: Theme.textMuted
                font.pointSize: Theme.fontBody
                elide: Text.ElideRight
            }
        }
    }
}
