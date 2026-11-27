import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Inline product notice. It is used when an error belongs to one page or
// action: it stays inside the layout, keeps the rest of the screen usable,
// and never competes with the global notification host.
Rectangle {
    id: banner

    property string text: ""
    property string detail: ""
    property string title: ""
    property string tone: "danger"
    property bool canRetry: true

    readonly property color accent: Theme.noticeColor(tone)

    signal retry()

    visible: text.length > 0 || title.length > 0
    implicitHeight: visible ? (layout.implicitHeight + Theme.spaceL * 1.5) : 0
    color: Theme.noticeBackground(tone)
    radius: Theme.radiusS
    border.width: 1
    border.color: accent

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spaceM
        spacing: Theme.spaceM

        Label {
            text: Theme.noticeIcon(banner.tone)
            color: banner.accent
            font.pointSize: Theme.fontSubtitle
            font.bold: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                visible: banner.title.length > 0
                text: banner.title
                color: banner.accent
                font.pointSize: Theme.fontSubtitle
                font.bold: true
                wrapMode: Text.Wrap
            }

            SelectableText {
                Layout.fillWidth: true
                visible: banner.text.length > 0
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
