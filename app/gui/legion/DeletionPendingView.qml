import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Blocking gate while the account is inside the 24-hour deletion grace period.
// The countdown is rendered from the server timestamp, never from a local
// request timer. Restore is a signed ceremony: handle + eight words + the
// existing device key. The page refuses to disappear on a local action; only a
// new signed_in state from the server closes it.
LegionView {
    id: deletion

    property bool submitting: false
    property double nowMs: Date.now()

    readonly property string requestedAt: shell ? (shell.deletionRequestedAt || "") : ""
    readonly property string purgeTimestamp: shell ? (shell.purgeAfter || "") : ""
    readonly property double purgeMs: {
        var exact = Theme.parseDate(purgeTimestamp)
        if (exact !== null) {
            return exact.getTime()
        }
        var requested = Theme.parseDate(requestedAt)
        if (requested !== null) {
            return requested.getTime() + 24 * 60 * 60 * 1000
        }
        return 0
    }
    readonly property int remainingSeconds: purgeMs > 0
        ? Math.max(0, Math.floor((purgeMs - nowMs) / 1000))
        : 0
    readonly property string purgeText: Theme.parseDate(purgeTimestamp) !== null
        ? Theme.dateTimeText(purgeTimestamp)
        : qsTr("через 24 часа после запроса")
    readonly property bool restoreAvailable: Api.hasRestoreAccount(api)
    readonly property bool canRestore: restoreAvailable && !submitting && !serverBusy &&
        handleField.text.trim().length > 0 &&
        deleteWords.text.trim().split(/\s+/).length === 8

    function activate() {
        if (handleField.text.length === 0) {
            handleField.text = handle
        }
        submitting = false
        clearError()
    }

    function focusFirst() {
        deleteWords.forceActiveFocus()
    }

    function restore() {
        if (!canRestore) {
            return
        }
        clearError()
        submitting = true
        var parts = deleteWords.text.trim().toLowerCase().split(/\s+/)
        if (!Api.restoreAccount(api, handleField.text.trim(), parts)) {
            submitting = false
            showError("unsupported_capability", "", "account_restore")
        }
    }

    Connections {
        target: deletion.api
        ignoreUnknownSignals: true

        function onFailed(op, code, message) {
            if (op !== "account_restore") {
                return
            }
            deletion.submitting = false
            deletion.showError(code, message, op)
        }

        function onResponse(op, data) {
            if (op !== "account_restore") {
                return
            }
            deletion.submitting = false
            deletion.clearError()
            deleteWords.text = ""
            if (deletion.shell) {
                deletion.shell.refreshAll(true)
                deletion.shell.refreshGamification(true)
                deletion.shell.refreshAccountSummary(true)
            }
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: deletion.purgeMs > 0
        onTriggered: deletion.nowMs = Date.now()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    NeonFrame {
        width: Math.min(parent.width - Theme.spaceXxl * 2, 760)
        height: Math.min(content.implicitHeight + Theme.spaceXxl * 2,
                         parent.height - Theme.spaceXxl * 2)
        anchors.centerIn: parent
        highlighted: true
        flicker: true

        ColumnLayout {
            id: content
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
                text: qsTr("Аккаунт удаляется")
                color: Theme.text
                font.pointSize: Theme.fontHero
                font.bold: true
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Доступ к клубам, сессиям и оплате закрыт. " +
                           "Аккаунт и все данные будут удалены безвозвратно.")
                color: Theme.textMuted
                font.pointSize: Theme.fontBody
                wrapMode: Text.Wrap
            }

            NeonFrame {
                Layout.fillWidth: true
                Layout.preferredHeight: countdownColumn.implicitHeight + Theme.spaceL * 2

                ColumnLayout {
                    id: countdownColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    spacing: Theme.spaceXs

                    Label {
                        text: qsTr("Удаление запланировано на")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }

                    Label {
                        Layout.fillWidth: true
                        text: deletion.purgeText
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: deletion.remainingSeconds > 0
                        text: qsTr("Осталось: %1").arg(Theme.duration(deletion.remainingSeconds))
                        color: Theme.warning
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Аккаунт, устройство, баланс, история сессий и награды удаляются. " +
                                   "Платёжные документы и чеки сохраняются по требованию 54-ФЗ; " +
                                   "финансовая история обезличивается.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Чтобы отменить удаление, введите имя аккаунта и восемь слов " +
                           "восстановления. Устройство будет привязано заново.")
                color: Theme.textMuted
                font.pointSize: Theme.fontBody
                wrapMode: Text.Wrap
            }

            TextField {
                id: handleField
                Layout.fillWidth: true
                placeholderText: qsTr("Имя аккаунта")
                font.pointSize: Theme.fontSubtitle
            }

            TextField {
                id: deleteWords
                Layout.fillWidth: true
                placeholderText: qsTr("Восемь слов через пробел")
                font.pointSize: Theme.fontSubtitle
                Keys.onReturnPressed: deletion.restore()
                Keys.onEnterPressed: deletion.restore()
            }

            ErrorBanner {
                Layout.fillWidth: true
                visible: deletion.hasInlineError
                tone: deletion.errorTone
                text: deletion.errorText
                detail: deletion.serverError
                canRetry: deletion.canRestore
                onRetry: deletion.restore()
            }

            Label {
                Layout.fillWidth: true
                visible: !deletion.restoreAvailable
                text: qsTr("Восстановление недоступно в этой версии приложения. " +
                           "Напишите в поддержку до истечения срока.")
                color: Theme.warning
                font.pointSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceM

                BusyIndicator {
                    visible: deletion.submitting || deletion.serverBusy
                    running: visible
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }

                Item { Layout.fillWidth: true }

                LegionButton {
                    kind: "ghost"
                    text: qsTr("Выйти")
                    onClicked: Api.logout(deletion.api)
                }

                LegionButton {
                    kind: "primary"
                    enabled: deletion.canRestore
                    text: qsTr("Отменить удаление")
                    onClicked: deletion.restore()
                }
            }
        }
    }
}
