import QtQuick 2.9
import QtQuick.Effects

// Qt 6 blur backend. MultiEffect is part of Qt Quick Effects and replaces
// QtGraphicalEffects, which is not shipped with Qt 6.
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

    MultiEffect {
        anchors.fill: parent
        source: snapshot
        visible: backdrop.backgroundSource !== null
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: 32
        blur: 0.5
    }

    onActiveChanged: {
        if (active) {
            snapshot.scheduleUpdate()
        }
    }
}
