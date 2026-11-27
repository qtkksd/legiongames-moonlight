import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import QtQuick.Controls.Material 2.2

import ComputerManager 1.0
import AutoUpdateChecker 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

import "legion/Theme.js" as Theme

ApplicationWindow {
    property bool pollingActive: false

    // True once the whole window tree exists; used to avoid touching the
    // toolbar from the StackView's early completion callback.
    property bool uiReady: false

    // Splash/loading overlay. The timer totals 1 s including the 180 ms
    // fade-out; a 5 s failsafe prevents a broken shell from freezing startup.
    property bool splashMinimumElapsed: false
    property bool splashTimedOut: false
    property bool splashDismissed: false
    readonly property bool splashEnabled: initialView === "qrc:/gui/PcView.qml"
    readonly property bool splashVisible: splashEnabled && !splashDismissed

    // Set by SettingsView to force the back operation to pop all
    // pages except the initial view. This is required when doing
    // a retranslate() because AppView breaks for some reason.
    property bool clearOnBack: false

    id: window
    width: 1280
    height: 720
    minimumWidth: 1024
    minimumHeight: 600
    color: Theme.bg

    // LegionGames theme (mirrors templates/home.html palette)
    font.family: "Manrope"
    Material.theme: Material.Dark
    Material.accent: "#c92a2a"
    Material.primary: "#c92a2a"
    Material.background: "#0d0d0d"
    Material.foreground: "#ffffff"

    // This function runs prior to creation of the initial StackView item
    function doEarlyInit() {
        // Keep the app background on-brand for Qt 6.5+, which otherwise
        // overrides Material 2 background colors.
        if (SystemProperties.usesMaterial3Theme) {
            Material.background = "#0d0d0d"
        }

        SdlGamepadKeyNavigation.enable()
    }

    Component.onCompleted: {
        // Show the window according to the user's preferences
        if (SystemProperties.hasDesktopEnvironment) {
            if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_MAXIMIZED) {
                window.showMaximized()
            }
            else if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_FULLSCREEN) {
                window.showFullScreen()
            }
            else {
                window.show()
            }
        } else {
            window.showFullScreen()
        }

        // Display any modal dialogs for configuration warnings
        if (runConfigChecks) {
            if (SystemProperties.isWow64) {
                wow64Dialog.open()
            }

            // Hardware acceleration and unmapped gamepads are checked asynchronously
            SystemProperties.hasHardwareAccelerationChanged.connect(hasHardwareAccelerationChanged)
            SystemProperties.unmappedGamepadsChanged.connect(hasUnmappedGamepadsChanged)
            SystemProperties.startAsyncLoad()
        }

        // The initial StackView push happens before the header exists, so the
        // chrome state is applied once here.
        uiReady = true
        updateChrome()
        tryDismissSplash()
    }

    onSplashVisibleChanged: {
        if (!splashVisible) {
            updateChrome()
        }
    }

    Connections {
        target: streamLauncher
        ignoreUnknownSignals: true

        function onStartRequested(session, appName, isResume) {
            if (!session) {
                return
            }
            var component = Qt.createComponent("qrc:/gui/StreamSegue.qml")
            if (component.status !== Component.Ready) {
                console.error("Cannot load StreamSegue:", component.errorString())
                return
            }
            var segue = component.createObject(stackView, {
                "session": session,
                "appName": appName,
                "isResume": isResume === true
            })
            if (segue) {
                stackView.push(segue)
            }
        }
    }

    function tryDismissSplash() {
        if (!splashEnabled || splashDismissed || splashFadeOut.running) {
            return
        }
        if (!splashMinimumElapsed && !splashTimedOut) {
            return
        }
        var shell = stackView.currentItem
        if (!splashTimedOut && (!uiReady || shell === null ||
                                shell.objectName !== "legion-shell" ||
                                !shell.startupSettled)) {
            return
        }
        splashFadeOut.start()
    }

    function hasHardwareAccelerationChanged() {
        if (!SystemProperties.hasHardwareAcceleration && StreamingPreferences.videoDecoderSelection !== StreamingPreferences.VDS_FORCE_SOFTWARE) {
            if (SystemProperties.isRunningXWayland) {
                xWaylandDialog.open()
            }
            else {
                noHwDecoderDialog.open()
            }
        }
    }

    function hasUnmappedGamepadsChanged() {
        if (SystemProperties.unmappedGamepads) {
            unmappedGamepadDialog.unmappedGamepads = SystemProperties.unmappedGamepads
            unmappedGamepadDialog.open()
        }
    }

    // It would be better to use TextMetrics here, but it always lays out
    // the text slightly more compactly than real Text does in ToolTip,
    // causing unexpected line breaks to be inserted
    Text {
        id: tooltipTextLayoutHelper
        visible: false
        font: ToolTip.toolTip.font
        text: ToolTip.toolTip.text
    }

    // This configures the maximum width of the singleton attached QML ToolTip. If left unconstrained,
    // it will never insert a line break and just extend on forever.
    ToolTip.toolTip.contentWidth: Math.min(tooltipTextLayoutHelper.width, 400)

    function goBack() {
        if (clearOnBack) {
            // Pop all items except the first one
            stackView.pop(null)
            clearOnBack = false
        }
        else {
            stackView.pop()
        }
    }

    function startView() {
        // Only the normal start goes through the product shell. Command-line
        // modes (--stream/--quit/--pair) own their initial page and must not
        // be replaced.
        //
        // legionApi is installed in the root context by main.cpp before this
        // QML document is loaded. Do not make the app-first entry point
        // depend on a QML `typeof` probe: on some Qt/platform combinations
        // that probe returns false for context properties during the initial
        // StackView push, sending the user to the old Moonlight PC grid
        // (usually an empty screen with only the gear button).
        if (initialView === "qrc:/gui/PcView.qml") {
            return "qrc:/gui/legion/LegionShell.qml"
        }
        return initialView
    }

    // The product shell draws its own header and rail.
    function legionShellShown() {
        return stackView.currentItem !== null &&
               stackView.currentItem.objectName === "legion-shell"
    }

    // Keep the legacy header in layout while the splash is visible. Opacity
    // hides it visually; changing visibility/height here would move the
    // centered splash content and caused the startup freeze in earlier fixes.
    function updateChrome() {
        if (!uiReady || !toolBar) {
            return
        }
        if (splashVisible) {
            toolBar.visible = true
            return
        }
        toolBar.visible = !legionShellShown()
    }

    StackView {
        id: stackView
        anchors.fill: parent
        focus: true
        // The splash is an opaque launch screen. Do not render the
        // startup/checking page underneath it for the first frame.
        visible: !window.splashVisible

        Component.onCompleted: {
            // Perform our early initialization before constructing
            // the initial view and pushing it to the StackView
            doEarlyInit()
            var view = startView()
            console.info("Initial view: " + view)
            push(view)
        }

        onCurrentItemChanged: {
            // Ensure focus travels to the next view when going back
            if (currentItem) {
                currentItem.forceActiveFocus()
            }

            // Hide the window toolbar while the product shell is on screen:
            // it provides its own rail and header. StreamSegue toggles the
            // same toolbar directly, so this is reassigned on every change.
            if (uiReady) {
                updateChrome()
            }
            tryDismissSplash()
        }

        Keys.onEscapePressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                quitConfirmationDialog.open()
            }
        }

        Keys.onBackPressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                quitConfirmationDialog.open()
            }
        }

        Keys.onMenuPressed: {
            settingsButton.clicked()
        }

        // This is a keypress we've reserved for letting the
        // SdlGamepadKeyNavigation object tell us to show settings
        // when Menu is consumed by a focused control.
        Keys.onHangupPressed: {
            settingsButton.clicked()
        }
    }

    // If the product shell fails to instantiate, do not leave the user with an
    // empty window and only the legacy toolbar gear. Show a readable fallback
    // with the version and a hint to check the application log.
    Rectangle {
        anchors.fill: parent
        color: "#0d0d0d"
        visible: uiReady && stackView.currentItem === null
        z: 100

        Column {
            anchors.centerIn: parent
            spacing: 10

            Label {
                text: qsTr("LegionGames UI не загрузился")
                color: "#ff6b6b"
                font.pointSize: 22
                font.bold: true
            }

            Label {
                text: qsTr("Версия %1. Откройте журнал приложения и проверьте ошибки QML.")
                      .arg(SystemProperties.versionString)
                color: "#aaaaaa"
                font.pointSize: 14
            }
        }
    }

    // ---- splash ---------------------------------------------------------
    //
    // The wheel and alien are the same split production artwork used by the
    // legacy PC view. QtSvg does not execute the CSS keyframes in the original
    // logo.svg, so the wheel is rotated here in QML. Minimum splash time is
    // 1 s total: 820 ms hold + 180 ms fade-out; if the shell is slower, the
    // fade waits for it (5 s failsafe).
    Rectangle {
        id: splash
        anchors.fill: parent
        z: 1000
        color: Theme.bg
        visible: window.splashEnabled && !window.splashDismissed
        // Start fully opaque: a fade-in would expose the app's checking gate
        // for one or more frames before the launch screen covers it.
        opacity: 1

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            anchors.centerIn: parent
            spacing: 18

            Item {
                width: 190
                height: 190
                anchors.horizontalCenter: parent.horizontalCenter

                LogoWheel {
                    id: splashWheel
                    anchors.fill: parent
                    initialHoldMs: 0
                }

                Image {
                    anchors.fill: parent
                    source: "qrc:/res/logo_alien.svg"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("ЛЕГИОН ИГР")
                color: Theme.accent
                font.pointSize: Theme.fontPageTitle
                font.bold: true
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Загрузка…")
                color: Theme.textMuted
                font.pointSize: Theme.fontBody
            }
        }
    }

    NumberAnimation {
        id: splashFadeOut
        target: splash
        property: "opacity"
        to: 0
        duration: 180
        easing.type: Easing.InCubic
        onStopped: window.splashDismissed = true
    }

    Timer {
        id: splashMinimumTimer
        interval: 820
        running: window.splashEnabled
        onTriggered: {
            window.splashMinimumElapsed = true
            window.tryDismissSplash()
        }
    }

    Timer {
        id: splashFailsafeTimer
        interval: 5000
        running: window.splashEnabled
        onTriggered: {
            window.splashTimedOut = true
            window.tryDismissSplash()
        }
    }

    // This timer keeps us polling for 5 minutes of inactivity
    // to allow the user to work with Moonlight on a second display
    // while dealing with configuration issues. This will ensure
    // machines come online even if the input focus isn't on Moonlight.
    Timer {
        id: inactivityTimer
        interval: 5 * 60000
        onTriggered: {
            if (!active && pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
    }

    onVisibleChanged: {
        // When we become invisible while streaming is going on,
        // stop polling immediately.
        if (!visible) {
            inactivityTimer.stop()

            if (pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
        else if (active) {
            // When we become visible and active again, start polling
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }

        // Poll for gamepad input only when the window is in focus
        SdlGamepadKeyNavigation.notifyWindowFocus(visible && active)
    }

    onActiveChanged: {
        if (active) {
            // Stop the inactivity timer
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }
        else {
            // Start the inactivity timer to stop polling
            // if focus does not return within a few minutes.
            inactivityTimer.restart()
        }

        // Poll for gamepad input only when the window is in focus
        SdlGamepadKeyNavigation.notifyWindowFocus(visible && active)
    }

    function navigateTo(url, objectType)
    {
        var existingItem = stackView.find(function(item, index) {
            return item instanceof objectType
        })

        if (existingItem !== null) {
            // Pop to the existing item
            stackView.pop(existingItem)
        }
        else {
            // Create a new item
            stackView.push(url)
        }
    }

    header: ToolBar {
        id: toolBar
        height: 60
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        // Keep the legacy chrome in the layout, but hide it visually while the
        // launch screen is up. Changing height/visibility here caused a startup
        // freeze before the StackView completed.
        opacity: window.splashVisible ? 0 : 1
        enabled: !window.splashVisible

        Label {
            id: titleLabel
            visible: toolBar.width > 700
            anchors.fill: parent
            text: stackView.currentItem.objectName
            font.pointSize: 20
            elide: Label.ElideRight
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
        }

        RowLayout {
            spacing: 10
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.fill: parent

            NavigableToolButton {
                // Only make the button visible if the user has navigated somewhere.
                visible: stackView.depth > 1

                iconSource: "qrc:/res/arrow_left.svg"

                onClicked: goBack()

                Keys.onDownPressed: {
                    stackView.currentItem.forceActiveFocus(Qt.TabFocus)
                }
            }

            // This label will appear when the window gets too small and
            // we need to ensure the toolbar controls don't collide
            Label {
                id: titleRowLabel
                font.pointSize: titleLabel.font.pointSize
                elide: Label.ElideRight
                horizontalAlignment: Qt.AlignHCenter
                verticalAlignment: Qt.AlignVCenter
                Layout.fillWidth: true

                // We need this label to always be visible so it can occupy
                // the remaining space in the RowLayout. To "hide" it, we
                // just set the text to empty string.
                text: !titleLabel.visible ? stackView.currentItem.objectName : ""
            }

            Label {
                id: versionLabel
                visible: stackView.currentItem instanceof SettingsView
                text: qsTr("%1 · Version %2").arg(SystemProperties.commitString.substring(0, 7)).arg(SystemProperties.versionString)
                font.pointSize: 12
                horizontalAlignment: Qt.AlignRight
                verticalAlignment: Qt.AlignVCenter
            }

            NavigableToolButton {
                id: addPcButton
                visible: stackView.currentItem instanceof PcView

                iconSource:  "qrc:/res/ic_add_to_queue_white_48px.svg"

                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add PC manually") + (newPcShortcut.nativeText ? (" ("+newPcShortcut.nativeText+")") : "")

                Shortcut {
                    id: newPcShortcut
                    sequence: StandardKey.New
                    onActivated: addPcButton.clicked()
                }

                onClicked: {
                    addPcDialog.open()
                }

                Keys.onDownPressed: {
                    stackView.currentItem.forceActiveFocus(Qt.TabFocus)
                }
            }

            NavigableToolButton {
                property string browserUrl: ""

                id: updateButton

                iconSource: "qrc:/res/update.svg"

                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered || visible

                // Invisible until we get a callback notifying us that
                // an update is available
                visible: false

                onClicked: {
                    updateDialog.open()
                }

                function updateAvailable(version, url)
                {
                    ToolTip.text = qsTr("Update available for Moonlight: Version %1").arg(version)
                    updateButton.browserUrl = url
                    updateButton.visible = true
                }

                Component.onCompleted: {
                    AutoUpdateChecker.onUpdateAvailable.connect(updateAvailable)
                    AutoUpdateChecker.start()
                }

                Keys.onDownPressed: {
                    stackView.currentItem.forceActiveFocus(Qt.TabFocus)
                }
            }

            NavigableToolButton {
                // TODO: Implement gamepad mapping then unhide this button
                visible: false

                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Gamepad Mapper")

                iconSource: "qrc:/res/ic_videogame_asset_white_48px.svg"

                onClicked: navigateTo("qrc:/gui/GamepadMapper.qml", GamepadMapper)

                Keys.onDownPressed: {
                    stackView.currentItem.forceActiveFocus(Qt.TabFocus)
                }
            }

            NavigableToolButton {
                id: settingsButton

                iconSource:  "qrc:/res/settings.svg"

                onClicked: navigateTo("qrc:/gui/SettingsView.qml", SettingsView)

                Keys.onDownPressed: {
                    stackView.currentItem.forceActiveFocus(Qt.TabFocus)
                }

                Shortcut {
                    id: settingsShortcut
                    sequence: StandardKey.Preferences
                    onActivated: settingsButton.clicked()
                }

                ToolTip.delay: 1000
                ToolTip.timeout: 3000
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Settings") + (settingsShortcut.nativeText ? (" ("+settingsShortcut.nativeText+")") : "")
            }
        }
    }

    ErrorMessageDialog {
        id: noHwDecoderDialog
        text: qsTr("No functioning hardware accelerated video decoder was detected by Moonlight. " +
                   "Your streaming performance may be severely degraded in this configuration.")
        helpText: qsTr("Click the Help button for more information on solving this problem.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    ErrorMessageDialog {
        id: xWaylandDialog
        text: qsTr("Hardware acceleration doesn't work on XWayland. Continuing on XWayland may result in poor streaming performance. " +
                   "Try running with QT_QPA_PLATFORM=wayland or switch to X11.")
        helpText: qsTr("Click the Help button for more information.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    NavigableMessageDialog {
        id: wow64Dialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        text: qsTr("This version of Moonlight isn't optimized for your PC. Please download the '%1' version of Moonlight for the best streaming performance.").arg(SystemProperties.friendlyNativeArchName)
        onAccepted: {
            Qt.openUrlExternally("https://github.com/qtkksd/legiongames-moonlight/releases");
        }
    }

    ErrorMessageDialog {
        id: unmappedGamepadDialog
        property string unmappedGamepads : ""
        text: qsTr("Moonlight detected gamepads without a mapping:") + "\n" + unmappedGamepads
        helpTextSeparator: "\n\n"
        helpText: qsTr("Click the Help button for information on how to map your gamepads.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Gamepad-Mapping"
    }

    // This dialog appears when an update is available and asks the user
    // whether to install it now.
    NavigableMessageDialog {
        id: updateDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Доступно обновление Moonlight. Установить сейчас?")
        onAccepted: {
            AutoUpdateChecker.installUpdate(updateButton.browserUrl)
        }
    }

    // This dialog appears when quitting via keyboard or gamepad button
    NavigableMessageDialog {
        id: quitConfirmationDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Are you sure you want to quit?")
        // For keyboard/gamepad navigation
        onAccepted: Qt.quit()
    }

    // HACK: This belongs in StreamSegue but keeping a dialog around after the parent
    // dies can trigger bugs in Qt 5.12 that cause the app to crash. For now, we will
    // host this dialog in a QML component that is never destroyed.
    //
    // To repro: Start a stream, cut the network connection to trigger the "Connection
    // terminated" dialog, wait until the app grid times out back to the PC grid, then
    // try to dismiss the dialog.
    ErrorMessageDialog {
        id: streamSegueErrorDialog

        property bool quitAfter: false

        onClosed: {
            if (quitAfter) {
                Qt.quit()
            }

            // StreamSegue assumes its dialog will be re-created each time we
            // start streaming, so fake it by wiping out the text each time.
            text = ""
        }
    }

    NavigableDialog {
        id: addPcDialog
        property string label: qsTr("Enter the IP address of your host PC:")

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
                ComputerManager.addNewHostManually(editText.text.trim())
            }
        }

        ColumnLayout {
            Label {
                text: addPcDialog.label
                font.bold: true
            }

            TextField {
                id: editText
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    addPcDialog.accept()
                }

                Keys.onEnterPressed: {
                    addPcDialog.accept()
                }
            }
        }
    }
}
