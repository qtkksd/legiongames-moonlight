import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Modal product notice for decisions or outcomes that need an explicit
// acknowledgement. Routine connectivity failures stay in the banner host.
Dialog {
    id: dialog

    property var notice: null
    readonly property string tone: notice && notice.tone ? String(notice.tone) : "danger"
    readonly property color accent: Theme.noticeColor(tone)

    signal dismissed()

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 480
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusM
        border.width: 1
        border.color: dialog.accent
    }

    contentItem: ColumnLayout {
        width: dialog.availableWidth
        spacing: Theme.spaceM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceM

            Label {
                text: Theme.noticeIcon(dialog.tone)
                color: dialog.accent
                font.pointSize: Theme.fontTitle
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                text: dialog.notice && dialog.notice.title
                      ? String(dialog.notice.title) : ""
                color: dialog.accent
                font.pointSize: Theme.fontTitle
                font.bold: true
                wrapMode: Text.Wrap
            }
        }

        SelectableText {
            Layout.fillWidth: true
            visible: !!(dialog.notice && dialog.notice.text)
            text: dialog.notice && dialog.notice.text
                  ? String(dialog.notice.text) : ""
            color: Theme.text
            font.pointSize: Theme.fontBody
            wrapMode: TextEdit.Wrap
        }

        SelectableText {
            Layout.fillWidth: true
            visible: !!(dialog.notice && dialog.notice.detail)
            text: dialog.notice && dialog.notice.detail
                  ? String(dialog.notice.detail) : ""
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
            wrapMode: TextEdit.Wrap
        }
    }

    footer: RowLayout {
        width: dialog.availableWidth
        spacing: Theme.spaceM

        Item { Layout.fillWidth: true }

        LegionButton {
            kind: "secondary"
            text: qsTr("Закрыть")
            onClicked: dialog.close()
        }

        LegionButton {
            visible: !!(dialog.notice && dialog.notice.actionText)
            kind: "primary"
            text: dialog.notice && dialog.notice.actionText
                  ? String(dialog.notice.actionText) : ""
            onClicked: {
                var action = dialog.notice ? dialog.notice.action : null
                dialog.close()
                if (typeof action === "function") {
                    action()
                }
            }
        }
    }

    onClosed: dismissed()
}
