import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Blocking startup states: no vault, no network, revoked device, pending
// deletion. Exactly one primary action per state.
LegionView {
    id: gate

    readonly property string gateMode: shell ? (shell.gateMode || "checking") : "checking"

    function activate() {
        primaryButton.forceActiveFocus()
    }

    function focusFirst() {
        primaryButton.forceActiveFocus()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    NeonFrame {
        id: panel
        width: Math.min(parent.width - Theme.spaceXxl * 2, 720)
        height: Math.min(contentColumn.implicitHeight + Theme.spaceXxl * 2,
                         parent.height - Theme.spaceXxl * 2)
        anchors.centerIn: parent
        highlighted: true
        flicker: true

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: Theme.spaceXxl
            spacing: Theme.spaceL

            Label {
                Layout.fillWidth: true
                text: qsTr("LegionGames")
                color: Theme.accent
                font.pointSize: Theme.fontSubtitle
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                text: {
                    if (gate.gateMode === "vault") {
                        return qsTr("Хранилище ключей недоступно")
                    }
                    if (gate.gateMode === "offline") {
                        return qsTr("Нет соединения")
                    }
                    if (gate.gateMode === "revoked") {
                        return qsTr("Устройство отозвано")
                    }
                    if (gate.gateMode === "deletion") {
                        return qsTr("Аккаунт удаляется")
                    }
                    return qsTr("Проверяем состояние аккаунта")
                }
                color: Theme.text
                font.pointSize: Theme.fontHero
                font.bold: true
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: {
                    if (gate.gateMode === "vault") {
                        return qsTr("Приложение не может открыть защищённое хранилище ключа устройства. " +
                                    "Аккаунт, клубы и оплата недоступны, пока хранилище не восстановлено.")
                    }
                    if (gate.gateMode === "offline") {
                        return qsTr("Проверьте интернет и повторите попытку. " +
                                    "Без соединения клубы, сессии и оплата недоступны.")
                    }
                    if (gate.gateMode === "revoked") {
                        return qsTr("Это устройство больше не привязано к аккаунту. " +
                                    "Восстановите доступ по слову восстановления: старое устройство будет отозвано.")
                    }
                    if (gate.gateMode === "deletion") {
                        return qsTr("Аккаунт ожидает удаления. Доступ только на чтение: " +
                                    "клубы и оплата отключены.")
                    }
                    return qsTr("Это займёт пару секунд.")
                }
                color: Theme.textMuted
                font.pointSize: Theme.fontBody
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceM

                BusyIndicator {
                    visible: gate.gateMode === "checking"
                    running: visible
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                }

                Item { Layout.fillWidth: true }

                LegionButton {
                    kind: "ghost"
                    visible: gate.gateMode === "vault" || gate.gateMode === "deletion"
                    text: qsTr("Выйти")
                    onClicked: Api.logout(gate.api)
                }

                LegionButton {
                    kind: "secondary"
                    visible: gate.gateMode === "revoked"
                    text: qsTr("Выйти")
                    onClicked: Api.logout(gate.api)
                }

                LegionButton {
                    id: primaryButton
                    kind: "primary"
                    focus: true
                    text: {
                        if (gate.gateMode === "revoked") {
                            return qsTr("Восстановить доступ")
                        }
                        if (gate.gateMode === "deletion") {
                            return qsTr("Выйти")
                        }
                        return qsTr("Повторить")
                    }
                    onClicked: {
                        if (gate.gateMode === "revoked") {
                            if (gate.shell) {
                                gate.shell.recoveryVisible = true
                            }
                            return
                        }
                        if (gate.gateMode === "deletion") {
                            Api.logout(gate.api)
                            return
                        }
                        Api.retryFailedStep(gate.api)
                        Api.refreshMe(gate.api)
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: gate.serverError.length > 0
                text: Api.errorText(gate.serverErrorCode, gate.serverError)
                color: Theme.warning
                font.pointSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }

    ErrorBanner {
        visible: gate.serverErrorCode === "client_update_required"
        tone: "warning"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceXl
        text: qsTr("Обновите приложение, чтобы продолжить.")
        canRetry: false
    }
}
