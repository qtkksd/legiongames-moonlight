import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Modal product notice for decisions or outcomes that need an explicit
// acknowledgement. Routine connectivity failures stay in the banner host.
// The dark/blurred backdrop is provided by ModalBackdrop; this component owns
// only the centered panel so it can grow with its content.
Dialog {
    id: dialog

    property var notice: null
    readonly property string tone: notice && notice.tone ? String(notice.tone) : "danger"
    readonly property color accent: Theme.noticeColor(tone)

    signal dismissed()

    modal: true
    // Do not use the platform dim: NotificationCenter renders the product
    // scrim/blur behind this dialog.
    dim: false
    parent: Overlay.overlay
    width: Math.min(650, parent ? parent.width - Theme.spaceXl * 2 : 650)
    padding: Theme.spaceXl
    closePolicy: Popup.CloseOnEscape
    transformOrigin: Item.Center

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 160
        }
        NumberAnimation {
            property: "scale"
            from: 0.96
            to: 1
            duration: 220
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 120
        }
    }

    function centerInWindow() {
        var overlay = Overlay.overlay
        if (!overlay) {
            return
        }
        if (parent !== overlay) {
            parent = overlay
        }
        x = Math.round((overlay.width - width) / 2)
        y = Math.round((overlay.height - height) / 2)
    }

    onOpened: centerInWindow()
    onWidthChanged: if (visible) centerInWindow()
    onHeightChanged: if (visible) centerInWindow()

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusM
        border.width: 2
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
