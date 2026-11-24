import QtQuick 2.9

import "Theme.js" as Theme

// Panel with the LegionGames neon identity.
//
// The flicker matches the production site's 14-second neon border animation:
// the border briefly dims at the same points instead of drawing a separate
// glow strip around the panel.
Rectangle {
    id: frame

    property bool highlighted: false
    property bool flicker: false
    property color panelColor: Theme.panel
    property real flickerAlpha: 1.0

    color: panelColor
    radius: Theme.radiusM
    border.width: highlighted ? 2 : 1
    border.color: highlighted
                  ? (flicker
                     ? Qt.rgba(201 / 255, 42 / 255, 42 / 255, flickerAlpha)
                     : Theme.accent)
                  : Theme.line

    SequentialAnimation on flickerAlpha {
        running: frame.flicker && frame.highlighted
        loops: Animation.Infinite

        // 14 s cycle, keyframes from the production neonFlicker animation.
        PauseAnimation { duration: 756 }
        NumberAnimation { to: 0.15; duration: 42 }
        NumberAnimation { to: 1.0; duration: 42 }
        PauseAnimation { duration: 84 }
        NumberAnimation { to: 0.15; duration: 42 }
        NumberAnimation { to: 1.0; duration: 28 }
        PauseAnimation { duration: 1162 }
        NumberAnimation { to: 0.15; duration: 42 }
        NumberAnimation { to: 1.0; duration: 42 }
        PauseAnimation { duration: 11760 }

        onStopped: frame.flickerAlpha = 1.0
    }
}
