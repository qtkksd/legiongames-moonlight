import QtQuick 2.9
import QtQuick.Controls 2.2

import "Theme.js" as Theme

// Product scrollbar: a full-height track with a proportional handle. It fades
// away when the pointer is far, appears gray on hover and turns red while
// dragging.
ScrollBar {
    id: control

    policy: ScrollBar.AsNeeded
    width: 8
    padding: 0

    opacity: (hovered || pressed || active) ? 1.0 : 0.0
    Behavior on opacity {
        NumberAnimation { duration: 160 }
    }

    contentItem: Rectangle {
        implicitWidth: 6
        width: 6
        height: Math.max(48, control.size * control.availableHeight)
        radius: 3
        color: (control.pressed || control.hovered) ? Theme.accent : Theme.lineStrong
    }

    background: Rectangle {
        width: 2
        height: control.availableHeight
        anchors.horizontalCenter: parent.horizontalCenter
        color: Theme.line
        opacity: 0.5
    }
}
