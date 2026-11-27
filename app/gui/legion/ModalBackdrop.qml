import QtQuick 2.9
import QtGraphicalEffects 1.0

// Product modal backdrop: a dimmed, blurred snapshot of the app surface.
// Snapshotting instead of live-blurring keeps the cost to one capture per
// popup and avoids repainting the whole window every frame.
Item {
    id: backdrop

    property bool active: false
    property var backgroundSource: null

    anchors.fill: parent
    visible: active
    z: 0

    onActiveChanged: {
        if (active) {
            snapshot.scheduleUpdate()
        }
    }

    ShaderEffectSource {
        id: snapshot
        anchors.fill: parent
        sourceItem: backdrop.backgroundSource
        live: false
        recursive: true
        hideSource: backdrop.active && backdrop.backgroundSource !== null
        textureSize: Qt.size(Math.max(1, Math.round(backdrop.width / 2)),
                             Math.max(1, Math.round(backdrop.height / 2)))
    }

    FastBlur {
        anchors.fill: parent
        source: snapshot
        radius: 10
        visible: backdrop.backgroundSource !== null
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.70
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
    }
}
