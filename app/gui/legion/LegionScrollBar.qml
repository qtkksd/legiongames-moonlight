import QtQuick 2.9
import QtQuick.Controls 2.2

import "Theme.js" as Theme

// Product scrollbar: a thin rail pinned to the right edge. It stays visible
// (gray) when the content is scrollable and turns red while the pointer is
// over it or the handle is being dragged. The pages reserve
// Theme.scrollbarGutter so this rail never sits on top of content.
ScrollBar {
    id: control

    policy: ScrollBar.AsNeeded
    visible: policy === ScrollBar.AlwaysOn ||
             (policy === ScrollBar.AsNeeded && size < 0.999)
    width: Theme.scrollbarWidth
    padding: 0

    hoverEnabled: true

    contentItem: Rectangle {
        implicitWidth: 4
        width: 4
        height: Math.max(48, control.size * control.availableHeight)
        radius: 2
        color: (control.hovered || control.pressed)
               ? Theme.scrollbarThumbHover
               : Theme.scrollbarThumb

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }

    background: Rectangle {
        width: 2
        height: control.availableHeight
        anchors.horizontalCenter: parent.horizontalCenter
        color: Theme.line
        opacity: 0.4
    }
}
