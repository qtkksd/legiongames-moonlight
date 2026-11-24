import QtQuick 2.9
import QtQuick.Controls 2.2

import "Theme.js" as Theme

// Product scrollbar: narrow, muted gray, red while hovered or dragged.
ScrollBar {
    id: control

    policy: ScrollBar.AsNeeded
    width: 6
    padding: 0

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 40
        radius: 3
        color: (control.pressed || control.hovered) ? Theme.accent : Theme.lineStrong
        opacity: control.active ? 1.0 : 0.65
    }

    background: Rectangle {
        color: "transparent"
    }
}
