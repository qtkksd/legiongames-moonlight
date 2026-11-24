import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme
import ".."

// Streaming settings from the Moonlight core, embedded in the product shell.
// The page keeps the full Moonlight preference surface (video, audio, input,
// host) while the Left rail provides the same navigation as the rest of the app.
LegionView {
    id: stream

    function focusFirst() {
        if (loader.item) {
            loader.item.forceActiveFocus()
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Трансляция")
        subtitle: qsTr("Качество, звук, управление и хост")
    }

    Loader {
        id: loader
        anchors.top: header.bottom
        anchors.topMargin: Theme.spaceL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        anchors.bottomMargin: Theme.spaceL
        source: "qrc:/gui/SettingsView.qml"
    }
}
