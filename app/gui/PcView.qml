import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerModel 1.0

import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

CenteredGridView {
    property ComputerModel computerModel : createModel()

    id: pcGrid
    focus: true
    activeFocusOnTab: true
    topMargin: 20
    bottomMargin: 5
    cellWidth: 310; cellHeight: 330;
    objectName: qsTr("Computers")

    // Faded LegionGames logo sitting behind the list of PCs: the wheel spins
    // while the alien stays put. Split into two SVGs because QtSvg < 6.7
    // ignores <mask> (so the radial lines wouldn't be cut) and can't run CSS
    // animations, so the wheel is rotated here in QML instead.
    Item {
        id: backgroundLogo
        width: Math.min(pcGrid.width, pcGrid.height) * 0.7
        height: width
        x: (pcGrid.width - width) / 2
        y: (pcGrid.height - height) / 2
        z: -1
        opacity: 0.08

        Image {
            id: backgroundWheel
            anchors.fill: parent
            source: "qrc:/res/logo_wheel.svg"
            fillMode: Image.PreserveAspectFit
            smooth: true

            // Matches the website keyframes exactly: hold ~10s, then snap 60°
            // with the CSS default `ease` timing (cubic-bezier(0.25, 0.1, 0.25, 1)),
            // six times (one full turn every 72s).
            SequentialAnimation on rotation {
                loops: Animation.Infinite
                PauseAnimation { duration: 10000 }
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

        Image {
            anchors.fill: parent
            source: "qrc:/res/logo_alien.svg"
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
    }

    Component.onCompleted: {
        // Don't show any highlighted item until interacting with them.
        // We do this here instead of onActivated to avoid losing the user's
        // selection when backing out of a different page of the app.
        currentIndex = -1
    }

    // Note: Any initialization done here that is critical for streaming must
    // also be done in CliStartStreamSegue.qml, since this code does not run
    // for command-line initiated streams.
    StackView.onActivated: {
        // Setup signals on CM
        ComputerManager.computerAddCompleted.connect(addComplete)

        // Highlight the first item if a gamepad is connected
        if (currentIndex === -1 && SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
            currentIndex = 0
        }
    }

    StackView.onDeactivating: {
        ComputerManager.computerAddCompleted.disconnect(addComplete)
    }

    function pairingComplete(error)
    {
        // Close the PIN dialog
        pairDialog.close()

        // Display a failed dialog if we got an error
        if (error !== undefined) {
            errorDialog.text = error
            errorDialog.helpText = ""
            errorDialog.open()
        }
    }

    function addComplete(success, detectedPortBlocking)
    {
        if (!success) {
            errorDialog.text = qsTr("Unable to connect to the specified PC.")

            if (detectedPortBlocking) {
                errorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.")
            }
            else {
                errorDialog.helpText = qsTr("Click the Help button for possible solutions.")
            }

            errorDialog.open()
        }
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import ComputerModel 1.0; ComputerModel {}', parent, '')
        model.initialize(ComputerManager)
        model.pairingCompleted.connect(pairingComplete)
        model.connectionTestCompleted.connect(testConnectionDialog.connectionTestComplete)
        return model
    }

    function openConnectDialog()
    {
        connectCodeDialog.open()
    }

    Column {
        anchors.centerIn: parent
        spacing: 20
        visible: pcGrid.count === 0

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 5

            BusyIndicator {
                id: searchSpinner
                visible: StreamingPreferences.enableMdns
                running: visible
            }

            Label {
                height: searchSpinner.height
                elide: Label.ElideRight
                text: qsTr("Поиск ПК на вашей сети...")
                font.pointSize: 20
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
            }
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Ввести код подключения")
            onClicked: pcGrid.openConnectDialog()
        }
    }

    model: computerModel

    delegate: NavigableItemDelegate {
        width: 300; height: 320;
        grid: pcGrid

        property alias pcContextMenu : pcContextMenuLoader.item

        Image {
            id: pcIcon
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/res/desktop_windows-48px.svg"
            sourceSize {
                width: 200
                height: 200
            }
        }

        Image {
            // TODO: Tooltip
            id: stateIcon
            anchors.horizontalCenter: pcIcon.horizontalCenter
            anchors.verticalCenter: pcIcon.verticalCenter
            anchors.verticalCenterOffset: !model.online ? -18 : -16
            visible: !model.statusUnknown && (!model.online || !model.paired)
            source: !model.online ? "qrc:/res/warning_FILL1_wght300_GRAD200_opsz24.svg" : "qrc:/res/baseline-lock-24px.svg"
            sourceSize {
                width: !model.online ? 75 : 70
                height: !model.online ? 75 : 70
            }
        }

        BusyIndicator {
            id: statusUnknownSpinner
            anchors.horizontalCenter: pcIcon.horizontalCenter
            anchors.verticalCenter: pcIcon.verticalCenter
            anchors.verticalCenterOffset: -15
            width: 75
            height: 75
            visible: model.statusUnknown
            running: visible
        }

        Label {
            id: pcNameText
            text: model.name

            width: parent.width
            anchors.top: pcIcon.bottom
            anchors.bottom: parent.bottom
            font.pointSize: 36
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            elide: Text.ElideRight
        }

        Loader {
            id: pcContextMenuLoader
            asynchronous: true
            sourceComponent: NavigableMenu {
                id: pcContextMenu
                initiator: pcContextMenuLoader.parent
                MenuItem {
                    text: qsTr("PC Status: %1").arg(model.online ? qsTr("Online") : qsTr("Offline"))
                    font.bold: true
                    enabled: false
                }
                NavigableMenuItem {
                    text: qsTr("View All Apps")
                    onTriggered: {
                        var component = Qt.createComponent("AppView.qml")
                        var appView = component.createObject(stackView, {"computerIndex": index, "objectName": model.name, "showHiddenGames": true})
                        stackView.push(appView)
                    }
                    visible: model.online && model.paired
                }
                NavigableMenuItem {
                    text: qsTr("Wake PC")
                    onTriggered: computerModel.wakeComputer(index)
                    visible: !model.online && model.wakeable
                }
                NavigableMenuItem {
                    text: qsTr("Test Network")
                    onTriggered: {
                        computerModel.testConnectionForComputer(index)
                        testConnectionDialog.open()
                    }
                }

                NavigableMenuItem {
                    text: qsTr("Rename PC")
                    onTriggered: {
                        renamePcDialog.pcIndex = index
                        renamePcDialog.originalName = model.name
                        renamePcDialog.open()
                    }
                }
                NavigableMenuItem {
                    text: qsTr("Delete PC")
                    onTriggered: {
                        deletePcDialog.pcIndex = index
                        deletePcDialog.pcName = model.name
                        deletePcDialog.open()
                    }
                }
                NavigableMenuItem {
                    text: qsTr("View Details")
                    onTriggered: {
                        showPcDetailsDialog.pcDetails = model.details
                        showPcDetailsDialog.open()
                    }
                }
            }
        }

        onClicked: {
            if (model.online) {
                if (!model.serverSupported) {
                    errorDialog.text = qsTr("The version of GeForce Experience on %1 is not supported by this build of Moonlight. You must update Moonlight to stream from %1.").arg(model.name)
                    errorDialog.helpText = ""
                    errorDialog.open()
                }
                else if (model.paired) {
                    // go to game view
                    var component = Qt.createComponent("AppView.qml")
                    var appView = component.createObject(stackView, {"computerIndex": index, "objectName": model.name})
                    stackView.push(appView)
                }
                else {
                    var pin = computerModel.generatePinString()

                    // Kick off pairing in the background
                    computerModel.pairComputer(index, pin)

                    // Display the pairing dialog
                    pairDialog.pin = pin
                    pairDialog.open()
                }
            } else if (!model.online) {
                // Using open() here because it may be activated by keyboard
                pcContextMenu.open()
            }
        }

        onPressAndHold: {
            // popup() ensures the menu appears under the mouse cursor
            if (pcContextMenu.popup) {
                pcContextMenu.popup()
            }
            else {
                // Qt 5.9 doesn't have popup()
                pcContextMenu.open()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton;
            onClicked: {
                parent.pressAndHold()
            }
        }

        Keys.onMenuPressed: {
            // We must use open() here so the menu is positioned on
            // the ItemDelegate and not where the mouse cursor is
            pcContextMenu.open()
        }

        Keys.onDeletePressed: {
            deletePcDialog.pcIndex = index
            deletePcDialog.pcName = model.name
            deletePcDialog.open()
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        // Using Setup-Guide here instead of Troubleshooting because it's likely that users
        // will arrive here by forgetting to enable GameStream or not forwarding ports.
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide"
    }

    NavigableMessageDialog {
        id: pairDialog
        closePolicy: Popup.CloseOnEscape

        // don't allow edits to the rest of the window while open
        property string pin : "0000"
        text:qsTr("Please enter %1 on your host PC. This dialog will close when pairing is completed.").arg(pin)+"\n\n"+
             qsTr("If your host PC is running Sunshine, navigate to the Sunshine web UI to enter the PIN.")
        standardButtons: Dialog.Cancel
        onRejected: {
            // FIXME: We should interrupt pairing here
        }
    }

    NavigableMessageDialog {
        id: deletePcDialog
        // don't allow edits to the rest of the window while open
        property int pcIndex : -1
        property string pcName : ""
        text: qsTr("Are you sure you want to remove '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No

        onAccepted: {
            computerModel.deleteComputer(pcIndex)
        }
    }

    NavigableMessageDialog {
        id: testConnectionDialog
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        onAboutToShow: {
            testConnectionDialog.text = qsTr("Moonlight is testing your network connection to determine if any required ports are blocked.") + "\n\n" + qsTr("This may take a few seconds…")
            showSpinner = true
        }

        function connectionTestComplete(result, blockedPorts)
        {
            if (result === -1) {
                text = qsTr("The network test could not be performed because none of Moonlight's connection testing servers were reachable from this PC. Check your Internet connection or try again later.")
                imageSrc = "qrc:/res/baseline-warning-24px.svg"
            }
            else if (result === 0) {
                text = qsTr("This network does not appear to be blocking Moonlight. If you still have trouble connecting, check your PC's firewall settings.") + "\n\n" + qsTr("If you are trying to stream over the Internet, install the Moonlight Internet Hosting Tool on your gaming PC and run the included Internet Streaming Tester to check your gaming PC's Internet connection.")
                imageSrc = "qrc:/res/baseline-check_circle_outline-24px.svg"
            }
            else {
                text = qsTr("Your PC's current network connection seems to be blocking Moonlight. Streaming over the Internet may not work while connected to this network.") + "\n\n" + qsTr("The following network ports were blocked:") + "\n"
                text += blockedPorts
                imageSrc = "qrc:/res/baseline-error_outline-24px.svg"
            }

            // Stop showing the spinner and show the image instead
            showSpinner = false
        }
    }

    NavigableDialog {
        id: renamePcDialog
        property string label: qsTr("Enter the new name for this PC:")
        property string originalName
        property int pcIndex : -1;

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            // Force keyboard focus on the textbox so keyboard navigation works
            editText.forceActiveFocus()
        }

        onClosed: {
            editText.clear()
        }

        onAccepted: {
            if (editText.text) {
                computerModel.renameComputer(pcIndex, editText.text)
            }
        }

        ColumnLayout {
            Label {
                text: renamePcDialog.label
                font.bold: true
            }

            TextField {
                id: editText
                placeholderText: renamePcDialog.originalName
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    renamePcDialog.accept()
                }

                Keys.onEnterPressed: {
                    renamePcDialog.accept()
                }
            }
        }
    }

    NavigableMessageDialog {
        id: showPcDetailsDialog
        property string pcDetails : "";
        text: showPcDetailsDialog.pcDetails
        imageSrc: "qrc:/res/baseline-help_outline-24px.svg"
        standardButtons: Dialog.Ok
    }

    // LegionGames one-time-code connect flow
    NavigableDialog {
        id: connectCodeDialog

        property bool connecting: false
        property bool done: false
        property int progress: 0

        title: qsTr("Подключение по коду")
        standardButtons: connecting ? Dialog.NoButton : (Dialog.Ok | Dialog.Cancel)
        closePolicy: connecting ? Popup.NoAutoClose : Popup.CloseOnEscape

        onOpened: {
            connectCodeField.text = ""
            connectCodeStatus.text = ""
            connectCodeStatus.color = "#f0a500"
            connecting = false
            done = false
            progress = 0
            connectCodeField.forceActiveFocus()
        }

        onAccepted: {
            if (connectCodeField.text.length > 0) {
                connecting = true
                connectCodeStatus.color = "#f0a500"
                connectCodeStatus.text = qsTr("Проверка кода...")
                ComputerManager.startLegionConnect(connectCodeField.text)
            }
        }

        ColumnLayout {
            spacing: 12

            Label {
                visible: !connectCodeDialog.connecting
                text: qsTr("Введите код подключения с сайта LegionGames:")
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            TextField {
                id: connectCodeField
                visible: !connectCodeDialog.connecting
                Layout.fillWidth: true
                focus: true
                enabled: !connectCodeDialog.connecting
                Keys.onReturnPressed: connectCodeDialog.accept()
                Keys.onEnterPressed: connectCodeDialog.accept()
            }

            // Larger spinner logo shown while connecting: same alien + spinning
            // wheel as the list background, only scaled down — the animation is
            // identical (hold ~10s, snap 60° six times).
            Item {
                visible: connectCodeDialog.connecting
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 120
                Layout.preferredHeight: 120

                Image {
                    anchors.fill: parent
                    source: "qrc:/res/logo_wheel.svg"
                    fillMode: Image.PreserveAspectFit
                    smooth: true

                    SequentialAnimation on rotation {
                        loops: Animation.Infinite
                        PauseAnimation { duration: 10000 }
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

                Image {
                    anchors.fill: parent
                    source: "qrc:/res/logo_alien.svg"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
            }

            // Progress bar in the LegionGames palette (accent #c92a2a on a
            // dark track). Reaches 100% only when Django returns 200.
            ProgressBar {
                id: connectProgressBar
                visible: connectCodeDialog.connecting
                Layout.fillWidth: true
                Layout.preferredHeight: 8
                from: 0
                to: 100
                value: connectCodeDialog.progress

                background: Rectangle {
                    implicitHeight: 8
                    radius: 4
                    color: "#1a1a1a"
                    border.color: "#3a3a3a"
                    border.width: 1
                }

                contentItem: Item {
                    Rectangle {
                        width: connectProgressBar.visualPosition * parent.width
                        height: parent.height
                        radius: 4
                        color: "#c92a2a"
                    }
                }
            }

            Label {
                id: connectCodeStatus
                color: "#f0a500"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                horizontalAlignment: connectCodeDialog.connecting
                                     ? Text.AlignHCenter : Text.AlignLeft
            }
        }

        Connections {
            target: ComputerManager

            function onLegionConnectStatus(message) {
                connectCodeStatus.text = message
            }

            function onLegionConnectProgress(percent) {
                connectCodeDialog.progress = percent
            }

            function onLegionConnectFailed(error) {
                connectCodeDialog.connecting = false
                connectCodeStatus.color = "#c92a2a"
                connectCodeStatus.text = error
            }

            function onLegionConnectSucceeded() {
                // 200 from Django: complete the bar, then close.
                connectCodeDialog.done = true
                connectCodeDialog.progress = 100
                connectCloseTimer.start()
            }
        }

        Timer {
            id: connectCloseTimer
            interval: 400
            onTriggered: connectCodeDialog.close()
        }
    }

    ScrollBar.vertical: ScrollBar {}
}
