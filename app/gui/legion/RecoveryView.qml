import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Recovery fallback inside the app: handle + eight words, then a new device
// key is created on this PC. Recovery revokes the previous device; the browser
// never does this (the site only issues a ticket).
LegionView {
    id: recovery

    property bool agreementChecked: false
    property bool pdnChecked: false
    property bool submitting: false

    property string handleInput: ""
    property var wordInputs: ["", "", "", "", "", "", "", ""]
    property string deviceLabel: (api && api.suggestedDeviceLabel)
                                  ? api.suggestedDeviceLabel
                                  : qsTr("Новый компьютер")

    readonly property string agreementDocUrl: "https://legiongames.ru/user-agreement.pdf"
    readonly property string policyDocUrl: "https://legiongames.ru/policy.pdf"
    readonly property string consentDocUrl: "https://legiongames.ru/privacy-consent.pdf"

    readonly property bool wordsComplete: {
        var filled = 0
        for (var i = 0; i < wordInputs.length; i++) {
            if (String(wordInputs[i]).trim().length > 0) {
                filled++
            }
        }
        return filled === 8
    }

    readonly property bool canSubmit: handleInput.trim().length > 0 && wordsComplete &&
                                      deviceLabel.trim().length > 0 &&
                                      agreementChecked && pdnChecked &&
                                      !submitting && !serverBusy

    readonly property string ageNotice: api ? (api.ageNotice || "") : ""
    readonly property string ageNoticeText: ageNotice.length > 0
                                            ? ageNotice
                                            : qsTr("Сервис предназначен для лиц старше 18 лет.")

    function activate() {
        handleField.forceActiveFocus()
        clearError()
    }

    function focusFirst() {
        handleField.forceActiveFocus()
    }

    function setWord(index, value) {
        var copy = wordInputs.slice(0)
        copy[index] = value
        wordInputs = copy
    }

    function submit() {
        if (!canSubmit) {
            return
        }
        clearError()
        submitting = true
        Api.setConsent(api, true, true)
        var parts = []
        for (var i = 0; i < wordInputs.length; i++) {
            parts.push(String(wordInputs[i]).trim().toLowerCase())
        }
        // recoverComplete takes QStringList words, matching the contract.
        Api.recoverComplete(api, handleInput.trim(), parts, deviceLabel.trim())
    }

    function cancel() {
        if (shell) {
            shell.recoveryVisible = false
        }
    }

    Connections {
        target: recovery.api
        ignoreUnknownSignals: true

        function onFailed(op, code, message) {
            if (op === "legal_documents") {
                return
            }
            recovery.submitting = false
            recovery.showError(code, message, op)
        }

        function onStateChanged() {
            if (recovery.appState === "signed_in") {
                recovery.submitting = false
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spaceXl
        contentWidth: width
        contentHeight: panel.height
        clip: true
        boundsBehavior: Flickable.OvershootBounds
        ScrollBar.vertical: LegionScrollBar { }

        NeonFrame {
            id: panel
            width: Math.min(parent.width - Theme.scrollbarGutter, 860)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: -Theme.scrollbarGutter / 2
            height: column.implicitHeight + Theme.spaceXxl * 2
            highlighted: true
            flicker: true

            ColumnLayout {
                id: column
                anchors.fill: parent
                anchors.margins: Theme.spaceXxl
                spacing: Theme.spaceM

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Восстановление доступа")
                        color: Theme.accent
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    LegionButton {
                        kind: "ghost"
                        compact: true
                        text: qsTr("Отмена")
                        onClicked: recovery.cancel()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Введите имя аккаунта и восемь слов из набора")
                    color: Theme.text
                    font.pointSize: Theme.fontHero
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("На этом компьютере будет создан новый ключ, а старое устройство " +
                               "отозвано. Слова не отправляются на сервер в открытом виде.")
                    color: Theme.textMuted
                    font.pointSize: Theme.fontSubtitle
                    wrapMode: Text.Wrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    Label {
                        text: qsTr("Имя аккаунта")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                    }

                    TextField {
                        id: handleField
                        Layout.fillWidth: true
                        placeholderText: "lg-..."
                        text: recovery.handleInput
                        font.pointSize: Theme.fontSubtitle
                        onTextChanged: recovery.handleInput = text
                        Keys.onReturnPressed: recovery.focusFirst()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Восемь слов")
                    color: Theme.textMuted
                    font.pointSize: Theme.fontBody
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spaceM
                    rowSpacing: Theme.spaceS

                    Repeater {
                        model: 8

                        delegate: TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Слово %1").arg(index + 1)
                            text: recovery.wordInputs[index]
                            font.pointSize: Theme.fontSubtitle
                            onTextChanged: recovery.setWord(index, text)
                            Keys.onReturnPressed: recovery.submit()
                            Keys.onEnterPressed: recovery.submit()
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs

                    Label {
                        text: qsTr("Имя этого компьютера")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                    }

                    TextField {
                        Layout.fillWidth: true
                        maximumLength: 32
                        text: recovery.deviceLabel
                        font.pointSize: Theme.fontSubtitle
                        onTextChanged: recovery.deviceLabel = text
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceS

                    CheckBox {
                        id: agreementBox
                        Layout.alignment: Qt.AlignTop
                        checked: recovery.agreementChecked
                        onToggled: recovery.agreementChecked = checked
                        Accessible.name: qsTr("Я принимаю пользовательское соглашение")
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.RichText
                        text: qsTr("Я принимаю <a href=\"%1\">пользовательское соглашение</a>.")
                                  .arg(recovery.agreementDocUrl)
                        color: Theme.text
                        font.pointSize: Theme.fontSubtitle
                        wrapMode: Text.Wrap
                        onLinkActivated: recovery.openUrl(link)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceS

                    CheckBox {
                        id: pdnBox
                        Layout.alignment: Qt.AlignTop
                        checked: recovery.pdnChecked
                        onToggled: recovery.pdnChecked = checked
                        Accessible.name: qsTr("Я принимаю политику обработки ПДн и даю согласие на обработку")
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.RichText
                        text: qsTr("Я принимаю <a href=\"%1\">политику обработки ПДн</a> " +
                                   "и даю <a href=\"%2\">согласие на обработку</a>.")
                                  .arg(recovery.policyDocUrl)
                                  .arg(recovery.consentDocUrl)
                        color: Theme.text
                        font.pointSize: Theme.fontSubtitle
                        wrapMode: Text.Wrap
                        onLinkActivated: recovery.openUrl(link)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: recovery.ageNoticeText
                    color: Theme.textMuted
                    font.pointSize: Theme.fontBody
                    wrapMode: Text.Wrap
                }

                LegionButton {
                    Layout.alignment: Qt.AlignLeft
                    kind: "ghost"
                    compact: true
                    text: qsTr("Открыть сайт legiongames.ru")
                    background: Rectangle {
                        color: Theme.bg
                        border.width: 0
                    }
                    onClicked: recovery.openUrl("https://legiongames.ru/")
                }

                ErrorBanner {
                    Layout.fillWidth: true
                    visible: recovery.hasError
                    text: recovery.errorText
                    detail: recovery.serverError
                    canRetry: true
                    onRetry: recovery.submit()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceM

                    BusyIndicator {
                        visible: recovery.submitting || recovery.serverBusy
                        running: visible
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: recovery.submitting || recovery.serverBusy
                        text: qsTr("Проверяем слова и создаём ключ…")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                    }

                    Item { Layout.fillWidth: true }

                    LegionButton {
                        kind: "secondary"
                        text: qsTr("Отмена")
                        onClicked: recovery.cancel()
                    }

                    LegionButton {
                        kind: "primary"
                        enabled: recovery.canSubmit
                        text: qsTr("Восстановить доступ")
                        onClicked: recovery.submit()
                    }
                }
            }
        }
    }
}
