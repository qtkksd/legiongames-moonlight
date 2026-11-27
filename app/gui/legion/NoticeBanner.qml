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
    readonly property string title: notice && notice.title ? String(notice.title) : ""
    readonly property string text: notice && notice.text ? String(notice.text) : ""
    readonly property string detail: notice && notice.detail ? String(notice.detail) : ""
    readonly property string actionText: notice && notice.actionText
                                         ? String(notice.actionText) : ""
    readonly property color accent: Theme.noticeColor(tone)

    signal actionClicked()
    signal dismissed()

    visible: notice !== null
    implicitWidth: Math.max(360, Math.min(560,
                        content.implicitWidth + Theme.spaceL * 2))
    implicitHeight: visible ? (content.implicitHeight + Theme.spaceL * 2) : 0
    color: Theme.panel
    radius: Theme.radiusM
    border.width: 1
    border.color: accent

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceM

            Label {
                text: Theme.noticeIcon(banner.tone)
                color: banner.accent
                font.pointSize: Theme.fontTitle
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                text: banner.title
                color: banner.accent
                font.pointSize: Theme.fontSubtitle
                font.bold: true
                wrapMode: Text.Wrap
            }

            LegionButton {
                kind: "ghost"
                compact: true
                text: qsTr("Закрыть")
                onClicked: banner.dismissed()
            }
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

        RowLayout {
            Layout.fillWidth: true
            visible: banner.actionText.length > 0
            spacing: Theme.spaceM

            Item { Layout.fillWidth: true }

            LegionButton {
                kind: "secondary"
                compact: true
                text: banner.actionText
                onClicked: banner.actionClicked()
            }
        }
    }
}
