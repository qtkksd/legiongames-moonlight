import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerModel 1.0
import ComputerManager 1.0

import "Api.js" as Api
import "Theme.js" as Theme
import ".."

// Product computer list: the active club session first, then the PCs found by
// ComputerManager (NetBird/mDNS/manual). It intentionally uses Legion cards
// instead of Moonlight's PcView/AppView so the app-first flow stays in one UI.
LegionView {
    id: computers

    property ComputerModel computerModel: createModel()
    property string pendingForgetUuid: ""
    property string pendingForgetName: ""
    property int pairingIndex: -1
    property string pairingName: ""
    property bool openAppsAfterPair: false
    // Q_INVOKABLE getters are not reactive; the manager's change signal bumps
    // this so the lease guard re-evaluates.
    property int leaseContextRevision: 0

    readonly property var session: shell ? shell.session : null
    readonly property bool sessionLive: session !== null &&
                                        (session.status === "pending" ||
                                         session.status === "active")
    // Only an activated lease has a streamable host. A pending lease is still
    // being provisioned and must not open the app list.
    readonly property bool leaseActive: session !== null && session.status === "active"
    readonly property string leaseId: sessionLive && session
                                      ? String(session.id || "") : ""
    readonly property string leaseHostUuid: {
        // touch the revision so the binding re-evaluates on manager changes
        var revision = leaseContextRevision
        try {
            return ComputerManager.legionLeaseHostUuid
                   ? String(ComputerManager.legionLeaseHostUuid()) : ""
        }
        catch (error) {
            return ""
        }
    }
    readonly property string leaseAddress: {
        // The connected host address is known from the connect material even
        // when the host uuid has not been observed yet (crash mid-connect).
        var revision = leaseContextRevision
        try {
            if (ComputerManager.legionLeaseHostAddress) {
                var stored = String(ComputerManager.legionLeaseHostAddress())
                if (stored.length > 0) {
                    return stored
                }
            }
        }
        catch (error) {
        }
        if (!sessionLive || !session || !session.pc) {
            return ""
        }
        return session.pc.ip ? String(session.pc.ip) : ""
    }
    readonly property string activePcLabel: {
        if (!sessionLive || !session || !session.pc) {
            return qsTr("Компьютер готовится")
        }
        if (session.pc.pc_number !== undefined &&
                session.pc.pc_number !== null &&
                String(session.pc.pc_number).length > 0) {
            return qsTr("ПК №%1").arg(session.pc.pc_number)
        }
        if (session.pc.ip) {
            return String(session.pc.ip)
        }
        return qsTr("Компьютер готовится")
    }

    // Strip the GameStream port from a model address. IPv6 literals are kept
    // as-is; the club hosts are IPv4 on the NetBird overlay.
    function hostOnly(address) {
        var text = address ? String(address) : ""
        if (text.length === 0 || text.charAt(0) === "[") {
            return text
        }
        if (text.indexOf(":") === text.lastIndexOf(":") && text.indexOf(":") > 0) {
            return text.substring(0, text.indexOf(":"))
        }
        return text
    }

    // A discovered host is streamable only when it is the machine of the
    // active lease. The stable uuid is the primary key; the overlay address is
    // the fallback when the persisted binding is missing (for example a fresh
    // profile after an app update).
    function isLeaseHost(model, uuid) {
        if (!sessionLive || !leaseActive) {
            return false
        }
        var hostUuid = uuid ? String(uuid) : ""
        if (leaseHostUuid.length > 0 && hostUuid.length > 0) {
            return hostUuid === leaseHostUuid
        }
        if (leaseAddress.length > 0) {
            var address = model && model.address ? String(model.address) : ""
            return hostOnly(address) === leaseAddress
        }
        return false
    }

    function createModel() {
        var model = Qt.createQmlObject(
            'import ComputerModel 1.0; ComputerModel {}', parent, '')
        model.initialize(ComputerManager)
        return model
    }

    function activate() {
        clearError()
        pendingForgetUuid = ""
        pendingForgetName = ""
    }

    function focusFirst() {
        if (sessionLive && openSessionButton.visible) {
            openSessionButton.forceActiveFocus()
        }
        else {
            computerScroll.forceActiveFocus()
        }
    }

    function requestForget(uuid, name) {
        pendingForgetUuid = uuid ? String(uuid) : ""
        pendingForgetName = name && String(name).length > 0
                            ? String(name) : qsTr("Компьютер")
        forgetDialog.open()
    }

    function openApps(index, name, uuid) {
        go("qrc:/gui/legion/ComputerAppsView.qml", {
               computerIndex: index,
               computerName: name,
               computerUuid: uuid,
               leaseId: computers.leaseId
           })
    }

    function startPairing(index, name) {
        pairingIndex = index
        pairingName = name
        openAppsAfterPair = true
        pairDialog.pin = computerModel.generatePinString()
        pairDialog.pcName = name
        pairDialog.busy = true
        pairDialog.errorText = ""
        pairDialog.open()
        computerModel.pairComputer(index, pairDialog.pin)
    }

    function activateComputer(index, card) {
        if (!card) {
            return
        }
        if (!sessionLive) {
            // No lease means no streaming. Discovered hosts stay inventory-only
            // so a reachable club PC can never bypass the session/billing flow.
            showError("lease_required",
                      qsTr("Запуск доступен только в активной сессии клуба. Начните сессию."),
                      "computer")
            return
        }
        if (!leaseActive) {
            showError("lease_pending",
                      qsTr("Компьютер ещё готовится. Дождитесь подключения."),
                      "computer")
            go("qrc:/gui/legion/SessionView.qml", {})
            return
        }
        if (!card.currentSession) {
            showError("lease_required",
                      qsTr("Этот ПК не входит в вашу сессию. Запуск доступен только на ПК сессии."),
                      "computer")
            return
        }
        if (card.statusUnknown) {
            showError("computer_searching", qsTr("Компьютер ещё ищется."),
                      "computer")
            return
        }
        if (!card.online) {
            if (card.wakeable) {
                computerModel.wakeComputer(index)
                showError("computer_waking", qsTr("Пробуждаем компьютер…"),
                          "computer")
            }
            else {
                showError("computer_offline", qsTr("Компьютер недоступен."),
                          "computer")
            }
            return
        }
        if (!card.paired) {
            // Pairing is the fallback for the leased PC only. It is never a
            // route to a stream on some other discovered host.
            startPairing(index, card.computerName)
            return
        }
        openApps(index, card.computerName, card.uuid)
    }

    function finishPairing(error) {
        if (!pairDialog.visible) {
            return
        }
        pairDialog.busy = false
        if (!error || String(error).length === 0) {
            var index = pairingIndex
            var name = pairingName
            var openAfter = openAppsAfterPair
            pairDialog.close()
            if (openAfter && index >= 0 && sessionLive && leaseActive) {
                openAppsAfterPair = false
                openApps(index, name, "")
            }
        }
        else {
            pairDialog.errorText = String(error)
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Компьютеры")
        subtitle: qsTr("Подключённые и найденные ПК")
    }

    LegionScrollView {
        id: computerScroll
        anchors.top: header.bottom
        anchors.topMargin: Theme.spaceL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        anchors.bottomMargin: Theme.spaceL

        Column {
            width: parent.width
            spacing: Theme.spaceL

            ErrorBanner {
                width: parent.width
                visible: computers.hasInlineError
                tone: computers.errorTone
                text: computers.errorText
                canRetry: false
            }

            // ---- active session -----------------------------------------
            NeonFrame {
                width: parent.width
                height: activeColumn.implicitHeight + Theme.spaceXl * 2
                visible: computers.sessionLive
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: activeColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Текущая сессия")
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.bold: true
                        }

                        StatusPill {
                            text: computers.session
                                  ? Api.sessionStatusText(computers.session.status)
                                  : qsTr("Нет данных")
                            tone: (computers.session &&
                                   computers.session.status === "pending")
                                  ? "warn" : "good"
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: computers.session
                              ? qsTr("%1 · %2")
                                    .arg(computers.shell
                                         ? computers.shell.clubName(computers.session.club_id)
                                         : qsTr("Клуб"))
                                    .arg(computers.activePcLabel)
                              : ""
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            id: openSessionButton
                            kind: "primary"
                            text: qsTr("Открыть сессию")
                            onClicked: computers.go(
                                           "qrc:/gui/legion/SessionView.qml", {})
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            NeonFrame {
                width: parent.width
                height: idleColumn.implicitHeight + Theme.spaceXl * 2
                visible: !computers.sessionLive

                ColumnLayout {
                    id: idleColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Нет активного подключения")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Начните сессию в клубе — подключённый ПК появится здесь.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    LegionButton {
                        kind: "secondary"
                        text: qsTr("К клубам")
                        onClicked: computers.goSection("clubs")
                    }
                }
            }

            // ---- discovered computers ----------------------------------
            NeonFrame {
                width: parent.width
                height: discoveredColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: discoveredColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Найденные компьютеры")
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.bold: true
                        }

                        StatusPill {
                            text: qsTr("ПК: %1").arg(computerRepeater.count)
                            tone: "neutral"
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Запуск игры доступен только на ПК активной сессии клуба. " +
                                   "Остальные найденные ПК — только для справки.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS

                        Repeater {
                            id: computerRepeater
                            model: computers.computerModel

                            delegate: ComputerCard {
                                id: pcCard
                                width: parent.width
                                computerName: model.name ? String(model.name) : ""
                                uuid: model.uuid ? String(model.uuid) : ""
                                address: model.address ? String(model.address) : ""
                                online: model.online === true
                                paired: model.paired === true
                                busy: model.busy === true
                                wakeable: model.wakeable === true
                                statusUnknown: model.statusUnknown === true
                                legion: model.legion === true
                                currentSession: {
                                    // Re-evaluate on every lease-context change.
                                    var revision = computers.leaseContextRevision
                                    return computers.isLeaseHost(
                                                model,
                                                model.uuid ? String(model.uuid) : "")
                                }
                                onActivated: computers.activateComputer(index, pcCard)
                                onOpenSession: computers.go(
                                                   "qrc:/gui/legion/SessionView.qml", {})
                                onWake: computers.computerModel.wakeComputer(index)
                                onForget: computers.requestForget(
                                              String(model.uuid || ""),
                                              String(model.name || ""))
                            }
                        }
                    }

                    EmptyState {
                        Layout.fillWidth: true
                        visible: computerRepeater.count === 0
                        title: qsTr("Компьютеры не найдены")
                        text: qsTr("Они появятся здесь, когда NetBird или локальная сеть " +
                                   "найдёт клубный ПК.")
                        actionText: qsTr("К клубам")
                        onAction: computers.goSection("clubs")
                    }
                }
            }
        }
    }

    NavigableMessageDialog {
        id: forgetDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Забыть компьютер «%1»?").arg(computers.pendingForgetName)
        onAccepted: {
            if (computers.pendingForgetUuid.length > 0) {
                computers.computerModel.deleteComputerByUuid(
                    computers.pendingForgetUuid)
            }
            computers.pendingForgetUuid = ""
            computers.pendingForgetName = ""
        }
    }

    PairPcDialog {
        id: pairDialog
        onClosed: {
            computers.pairingIndex = -1
            computers.pairingName = ""
            computers.openAppsAfterPair = false
        }
    }

    Connections {
        target: computers.computerModel
        ignoreUnknownSignals: true

        function onPairingCompleted(error) {
            computers.finishPairing(error)
        }
    }

    Connections {
        target: ComputerManager
        ignoreUnknownSignals: true

        function onLegionLeaseContextChanged() {
            computers.leaseContextRevision += 1
        }
    }
}
