import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import SystemProperties 1.0

import "Api.js" as Api
import "Theme.js" as Theme
import ".."

// Account: handle, this PC, recovery kit, recovery on a new PC, deletion.
// There is no device list and no invite in phase 1: one account, one device.
LegionView {
    id: account

    readonly property bool deletionAvailable: Api.hasDeleteAccount(api)
    readonly property bool reprintAvailable: Api.hasReprintRecoveryKit(api)
    // `device` is a notifyable backend property: read it in the binding so the
    // card updates when the label or version changes.
    readonly property var device: (api && api.device) ? api.device : null
    readonly property string deviceLabel: device && device.label ? String(device.label) : ""
    readonly property string deviceOs: device && device.os ? String(device.os) : ""
    readonly property string deviceVersion: device && device.app_version
                                          ? String(device.app_version) : ""

    function activate() {
        Api.refreshMe(api)
        if (shell) {
            shell.refreshWallet()
        }
        clearError()
    }

    function focusFirst() {
        kitButton.forceActiveFocus()
    }

    function reissueKit() {
        clearError()
        Api.reissueWords(api)
    }

    function reprintKit() {
        clearError()
        Api.reprintRecoveryKit(api)
    }

    function startRecovery() {
        if (shell) {
            shell.recoveryVisible = true
        }
    }

    function deleteAccount() {
        if (deleteHandle.text.trim().length === 0 || deleteWords.text.trim().length === 0) {
            return
        }
        // deleteAccount(handle, words) is only called when the backend exposes
        // it; otherwise the panel points at support instead.
        clearError()
        var parts = deleteWords.text.trim().toLowerCase().split(/\s+/)
        Api.deleteAccount(api, deleteHandle.text.trim(), parts)
    }

    Connections {
        target: account.api
        ignoreUnknownSignals: true

        function onFailed(op, code, message) {
            account.showError(code, message, op)
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Аккаунт")
        subtitle: account.handle.length > 0 ? account.handle : qsTr("Нет данных")
    }

    ScrollView {
        contentWidth: availableWidth
        anchors.top: header.bottom
        anchors.topMargin: Theme.spaceL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        anchors.bottomMargin: Theme.spaceL
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: parent.width
            spacing: Theme.spaceL

            NeonFrame {
                width: parent.width
                height: identityColumn.implicitHeight + Theme.spaceXl * 2
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: identityColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: account.handle.length > 0 ? account.handle : qsTr("Аккаунт")
                            color: Theme.text
                            font.pointSize: Theme.fontHero
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        StatusPill {
                            text: qsTr("Активен")
                            tone: "good"
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Этот компьютер — единственное устройство аккаунта.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: account.device !== null
                        text: qsTr("Устройство: %1").arg(account.deviceLabel.length > 0
                                                          ? account.deviceLabel
                                                          : qsTr("без имени"))
                        color: Theme.text
                        font.pointSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: account.device !== null &&
                                 (account.deviceOs.length > 0 || account.deviceVersion.length > 0)
                        text: {
                            var parts = []
                            if (account.deviceOs.length > 0) {
                                parts.push(account.deviceOs)
                            }
                            if (account.deviceVersion.length > 0) {
                                parts.push(qsTr("версия %1").arg(account.deviceVersion))
                            }
                            return parts.join(" · ")
                        }
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: account.device === null
                        text: qsTr("Имя устройства задаётся при создании аккаунта.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Версия приложения: %1").arg(SystemProperties.versionString)
                        color: Theme.textDim
                        font.pointSize: Theme.fontSmall
                    }
                }
            }

            ErrorBanner {
                width: parent.width
                visible: account.hasError
                text: account.errorText
                detail: account.serverError
                canRetry: true
                onRetry: account.activate()
            }

            NeonFrame {
                width: parent.width
                height: kitColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: kitColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Набор восстановления")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Восемь слов — единственный способ восстановить доступ, если этот ПК " +
                                   "потерян. Храните набор отдельно от компьютера.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            id: kitButton
                            kind: "secondary"
                            text: qsTr("Перевыпустить набор")
                            onClicked: reissueDialog.open()
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: account.reprintAvailable
                            text: qsTr("Сохранить файл набора заново")
                            onClicked: account.reprintKit()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            NeonFrame {
                width: parent.width
                height: moveColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: moveColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Восстановление на новом ПК")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Перенесите аккаунт на другой компьютер по имени аккаунта и восьми словам. " +
                                   "Текущее устройство будет отозвано.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Восстановить на новом ПК")
                            onClicked: transferDialog.open()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            NeonFrame {
                width: parent.width
                height: dangerColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: dangerColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Выход и удаление")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Удаление аккаунта отзывает устройство и удаляет доступ. " +
                                   "Финансовые документы сохраняются по требованию закона.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Выйти")
                            onClicked: logoutDialog.open()
                        }

                        LegionButton {
                            kind: "danger"
                            visible: account.deletionAvailable
                            text: qsTr("Удалить аккаунт")
                            onClicked: deleteDialog.open()
                        }

                        LegionButton {
                            kind: "danger"
                            visible: !account.deletionAvailable
                            text: qsTr("Удаление через поддержку")
                            onClicked: account.openUrl("https://legiongames.ru/help/")
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    NavigableMessageDialog {
        id: reissueDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Перевыпустить набор? Старые восемь слов перестанут работать.")
        onAccepted: account.reissueKit()
    }

    NavigableMessageDialog {
        id: transferDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Восстановить аккаунт на этом ПК? Текущее устройство будет отозвано, " +
                   "а для входа понадобятся имя аккаунта и восемь слов.")
        onAccepted: account.startRecovery()
    }

    NavigableMessageDialog {
        id: logoutDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Выйти из аккаунта на этом ПК? Сессия игры, если она активна, " +
                   "продолжит списывать время.")
        onAccepted: Api.logout(account.api)
    }

    NavigableDialog {
        id: deleteDialog
        title: qsTr("Удаление аккаунта")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            deleteHandle.text = account.handle
            deleteWords.text = ""
            deleteWords.forceActiveFocus()
        }
        onAccepted: account.deleteAccount()

        ColumnLayout {
            Label {
                Layout.fillWidth: true
                text: qsTr("Введите имя аккаунта и восемь слов восстановления.")
                color: Theme.text
                font.pointSize: Theme.fontBody
                wrapMode: Text.Wrap
            }

            TextField {
                id: deleteHandle
                Layout.fillWidth: true
                placeholderText: qsTr("Имя аккаунта")
            }

            TextField {
                id: deleteWords
                Layout.fillWidth: true
                placeholderText: qsTr("Восемь слов через пробел")
            }
        }
    }
}
