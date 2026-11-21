import QtQuick 2.9

import "Theme.js" as Theme

// Panel with the LegionGames neon identity.
//
// The flicker is deliberately limited to `highlighted` panels (the primary
// panel of a screen), never to every control, and it is a slow breathing
// outline rather than a strobing effect.
Rectangle {
    id: frame

    property bool highlighted: false
    property bool flicker: false
    property color panelColor: Theme.panel

    color: panelColor
    radius: Theme.radiusM
    border.width: highlighted ? 2 : 1
    border.color: highlighted ? Theme.accent : Theme.line

    Rectangle {
        id: glow
        anchors.fill: parent
        anchors.margins: -3
        radius: frame.radius + 3
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0.0
        visible: frame.flicker

        SequentialAnimation on opacity {
            running: frame.flicker
            loops: Animation.Infinite

            NumberAnimation { from: 0.0;  to: 0.42; duration: 900 }
            PauseAnimation { duration: 2600 }
            NumberAnimation { from: 0.42; to: 0.16; duration: 140 }
            PauseAnimation { duration: 90 }
            NumberAnimation { from: 0.16; to: 0.38; duration: 120 }
            PauseAnimation { duration: 2200 }
            NumberAnimation { from: 0.38; to: 0.0;  duration: 800 }
        }
    }
}
