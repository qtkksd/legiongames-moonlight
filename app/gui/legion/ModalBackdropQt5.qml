import QtQuick 2.9
import QtGraphicalEffects 1.0

// Qt 5 blur backend. Snapshot once per popup; the wrapper owns the dim layer.
Item {
    id: backdrop

    property bool active: false
    property var backgroundSource: null

    anchors.fill: parent

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

    onActiveChanged: {
        if (active) {
            snapshot.scheduleUpdate()
        }
    }
}
