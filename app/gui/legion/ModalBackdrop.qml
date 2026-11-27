import QtQuick 2.9

// Product modal backdrop: dimmed background plus a blurred snapshot.
// Qt 5 uses QtGraphicalEffects FastBlur; Qt 6 uses the built-in MultiEffect.
// The implementation is in a Loader so a Qt 6 build never imports the Qt 5
// effects module (and vice versa).
Item {
    id: backdrop

    property bool active: false
    property var backgroundSource: null

    anchors.fill: parent
    visible: active
    z: 0

    Loader {
        id: blurLoader
        anchors.fill: parent
        source: (typeof lgQt6 !== "undefined" && lgQt6)
                ? "qrc:/gui/legion/ModalBackdropQt6.qml"
                : "qrc:/gui/legion/ModalBackdropQt5.qml"

        onLoaded: {
            item.active = Qt.binding(function() { return backdrop.active })
            item.backgroundSource = Qt.binding(function() { return backdrop.backgroundSource })
        }
    }

    // Always present: this is also the safe fallback when the blur backend
    // fails to load or the platform has no usable shader path.
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
