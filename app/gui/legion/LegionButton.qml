import QtQuick 2.9
import QtQuick.Controls 2.2

import "Theme.js" as Theme

// Single button style for the whole product UI.
// kind: primary | secondary | ghost | danger
Button {
    id: control

    property string kind: "secondary"
    property bool compact: false

    implicitHeight: compact ? 36 : 48
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    leftPadding: compact ? 14 : 20
    rightPadding: compact ? 14 : 20
    topPadding: compact ? 6 : 10
    bottomPadding: compact ? 6 : 10
    font.pointSize: compact ? Theme.fontSmall : Theme.fontBody
    font.bold: kind === "primary"
    opacity: enabled ? 1.0 : 0.45
    focusPolicy: Qt.StrongFocus

    contentItem: Label {
        text: control.text
        font: control.font
        color: {
            if (control.kind === "primary") {
                return Theme.textOnAccent
            }
            if (control.kind === "danger") {
                return Theme.error
            }
            return Theme.text
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        wrapMode: Text.NoWrap
    }

    background: Rectangle {
        radius: Theme.radiusS
        color: {
            if (!control.enabled) {
                return Theme.panelAlt
            }
            if (control.kind === "primary") {
                return control.pressed ? Theme.accentPressed
                                       : (control.hovered ? Theme.accentHover : Theme.accent)
            }
            if (control.kind === "danger") {
                return control.hovered ? "#2a1414" : Theme.panel
            }
            if (control.kind === "ghost") {
                return control.hovered ? "#22ffffff" : "transparent"
            }
            return control.hovered ? Theme.panelHover : Theme.panelAlt
        }
        border.width: 1
        border.color: {
            if (control.kind === "primary") {
                return Theme.accent
            }
            if (control.kind === "danger") {
                return Theme.error
            }
            if (control.kind === "ghost") {
                return "transparent"
            }
            return Theme.lineStrong
        }
    }

    // Visible focus ring for keyboard and controller navigation.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Theme.radiusS + 3
        color: "transparent"
        border.width: 3
        border.color: Theme.focus
        visible: control.activeFocus
    }
}
