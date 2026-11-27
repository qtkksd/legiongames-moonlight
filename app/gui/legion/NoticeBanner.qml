import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Floating product banner. One at a time, with an accent border and a
// short colored title/icon; body copy stays readable.
Rectangle {
    id: banner

    property var notice: null
    readonly property string tone: notice && notice.tone ? String(notice.tone) : "danger"
    readonly property color accent: Theme.noticeColor(tone)

    signal actionClicked()
    signal dismissed()

    visible: notice !== null
    implicitHeight: visible ? (layout.implicitHeight + Theme.spaceL) : 0
    color: Theme.panel
    radius: Theme.radiusM
    border.width: 1
    border.color: accent

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceM

        Label {
            text: Theme.noticeIcon(banner.tone)
            color: banner.accent
            font.pointSize: Theme.fontTitle
            font.bold: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs

            Label {
                Layout.fillWidth: true
                text: banner.notice && banner.notice.title
                      ? String(banner.notice.title) : ""
                color: banner.accent
                font.pointSize: Theme.fontSubtitle
                font.bold: true
                wrapMode: Text.Wrap
            }

            SelectableText {
                Layout.fillWidth: true
                visible: !!(banner.notice && banner.notice.text)
                text: banner.notice && banner.notice.text
                      ? String(banner.notice.text) : ""
                color: Theme.text
                font.pointSize: Theme.fontBody
                wrapMode: TextEdit.Wrap
            }

            SelectableText {
                Layout.fillWidth: true
                visible: !!(banner.notice && banner.notice.detail)
                text: banner.notice && banner.notice.detail
                      ? String(banner.notice.detail) : ""
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                wrapMode: TextEdit.Wrap
            }
        }

        ColumnLayout {
            spacing: Theme.spaceXs

            LegionButton {
                visible: !!(banner.notice && banner.notice.actionText)
                kind: "secondary"
                compact: true
                text: banner.notice && banner.notice.actionText
                      ? String(banner.notice.actionText) : ""
                onClicked: banner.actionClicked()
            }

            LegionButton {
                kind: "ghost"
                compact: true
                text: qsTr("Закрыть")
                onClicked: banner.dismissed()
            }
        }
    }
}
