import QtQuick 2.9

// LegionGames wheel. The production logo.svg uses a 72 s CSS animation:
// six 60-degree turns over 2 s, separated by 10 s holds, with the CSS default
// ease cubic-bezier(0.25, 0.1, 0.25, 1). QtSvg does not run CSS animations,
// so the same sequence is reproduced here.
Image {
    id: wheel

    // Ambient watermark keeps the production hold. The launch screen sets
    // this to 0 so the first turn begins immediately.
    property int initialHoldMs: 10000

    source: "qrc:/res/logo_wheel.svg"
    fillMode: Image.PreserveAspectFit
    smooth: true

    SequentialAnimation on rotation {
        loops: Animation.Infinite
        running: wheel.visible

        PauseAnimation { duration: wheel.initialHoldMs }
        NumberAnimation { from: 0;   to: 60;  duration: 2000; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1.0, 1.0, 1.0] }
        PauseAnimation { duration: 10000 }
        NumberAnimation { from: 60;  to: 120; duration: 2000; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1.0, 1.0, 1.0] }
        PauseAnimation { duration: 10000 }
        NumberAnimation { from: 120; to: 180; duration: 2000; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1.0, 1.0, 1.0] }
        PauseAnimation { duration: 10000 }
        NumberAnimation { from: 180; to: 240; duration: 2000; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1.0, 1.0, 1.0] }
        PauseAnimation { duration: 10000 }
        NumberAnimation { from: 240; to: 300; duration: 2000; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1.0, 1.0, 1.0] }
        PauseAnimation { duration: 10000 }
        NumberAnimation { from: 300; to: 360; duration: 2000; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1.0, 1.0, 1.0] }
    }
}
