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

    readonly property var session: shell ? shell.session : null
    readonly property bool sessionLive: session !== null &&
                                        (session.status === "pending" ||
                                         session.status === "active")
    readonly property string activeAddress: {
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
                        text: qsTr("Список обновляется автоматически. Запуск игры по-прежнему " +
                                   "идёт через сессию клуба.")
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
                                width: parent.width
                                computerName: model.name ? String(model.name) : ""
                                address: model.address ? String(model.address) : ""
                                online: model.online === true
                                paired: model.paired === true
                                busy: model.busy === true
                                wakeable: model.wakeable === true
                                statusUnknown: model.statusUnknown === true
                                legion: model.legion === true
                                currentSession: computers.activeAddress.length > 0 &&
                                                String(model.address) === computers.activeAddress
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
}
