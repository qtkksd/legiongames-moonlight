import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Gate 1: legal consent before an account exists.
//
// purpose = "register"  — full first run: consent, device label, registration.
// purpose = "reconsent" — documents changed while the account exists.
LegionView {
    id: onboarding

    property string purpose: "register"

    readonly property bool reconsent: purpose === "reconsent"
    readonly property bool wordsPending: !reconsent && onboardingStep === "words"

    property bool agreementChecked: false
    property bool pdnChecked: false
    property bool submitting: false

    // The hostname is not exposed to QML by the current backend; the field is
    // prefilled with a readable default and sent to register(label).
    property string deviceLabel: qsTr("Мой компьютер")

    readonly property bool canRegister: agreementChecked && pdnChecked &&
                                        deviceLabel.trim().length > 0 &&
                                        !submitting && !serverBusy

    // Legal documents come from the backend property; the URLs are only used
    // to open the exact served text for each checkbox.
    readonly property var legalDocuments: (api && api.legalDocuments) ? api.legalDocuments : []
    readonly property string agreementUrl: Api.documentUrlIn(legalDocuments, "user_agreement")
    readonly property string pdnUrl: Api.documentUrlIn(legalDocuments, "pdn_consent")
    readonly property bool hasDocumentLinks: agreementUrl.length > 0 || pdnUrl.length > 0
    readonly property string ageNotice: api ? (api.ageNotice || "") : ""
    readonly property string ageNoticeText: ageNotice.length > 0
                                            ? ageNotice
                                            : qsTr("Сервис предназначен для лиц старше 18 лет.")

    function activate() {
        if (legalDocuments.length === 0) {
            Api.getLegalDocuments(api)
        }
        if (!wordsPending) {
            agreementBox.forceActiveFocus()
        }
    }

    function focusFirst() {
        if (!wordsPending) {
            agreementBox.forceActiveFocus()
        }
    }

    function submit() {
        if (reconsent) {
            if (!agreementChecked || !pdnChecked) {
                return
            }
            Api.setConsent(api, true, true)
            // submitConsent is the dedicated invokable; retryFailedStep keeps
            // older builds working.
            if (!Api.submitConsent(api)) {
                Api.retryFailedStep(api)
            }
            Api.refreshMe(api)
            if (shell) {
                shell.reconsentVisible = false
            }
            return
        }
        if (!canRegister) {
            return
        }
        clearError()
        submitting = true
        Api.setConsent(api, true, true)
        if (!Api.register(api, deviceLabel.trim())) {
            submitting = false
            showError("unsupported_capability", "", "register")
        }
    }

    Connections {
        target: onboarding.api
        ignoreUnknownSignals: true

        function onFailed(op, code, message) {
            if (op === "legal_documents") {
                // Document URLs are informational here; never block consent.
                return
            }
            onboarding.submitting = false
            onboarding.showError(code, message, op)
        }

        function onWordsReady(handle, words) {
            onboarding.submitting = false
        }

        function onStateChanged() {
            if (onboarding.appState === "signed_in") {
                onboarding.submitting = false
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    Item {
        anchors.fill: parent
        anchors.margins: Theme.spaceXxl

        NeonFrame {
            anchors.centerIn: parent
            width: Math.min(parent.width, 860)
            height: Math.min(parent.height, column.implicitHeight + Theme.spaceXxl * 2)
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
                        text: qsTr("LegionGames")
                        color: Theme.accent
                        font.pointSize: Theme.fontSubtitle
                        font.bold: true
                    }

                    Label {
                        text: onboarding.reconsent ? qsTr("Документы обновились")
                                                   : qsTr("Шаг 1 из 2")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: onboarding.reconsent ? qsTr("Примите обновлённые документы")
                                               : qsTr("Создайте аккаунт")
                    color: Theme.text
                    font.pointSize: Theme.fontHero
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: !onboarding.reconsent && !onboarding.wordsPending
                    text: qsTr("Аккаунт создаётся на этом компьютере. Ключ устройства хранится " +
                               "в защищённом хранилище системы, пароль не нужен.")
                    color: Theme.textMuted
                    font.pointSize: Theme.fontBody
                    wrapMode: Text.Wrap
                }

                // ---- words pending: the kit was shown but not confirmed ----
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: onboarding.wordsPending
                    spacing: Theme.spaceM

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Набор восстановления не подтверждён")
                        color: Theme.warning
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Пока вы не подтвердили, что сохранили восемь слов, аккаунт " +
                                   "не считается готовым. Если слова потеряны, выпустите новый набор.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    LegionButton {
                        kind: "primary"
                        text: qsTr("Показать набор ещё раз")
                        onClicked: Api.reissueWords(onboarding.api)
                    }
                }

                // ---- consent form ----------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !onboarding.wordsPending
                    spacing: Theme.spaceS

                    CheckBox {
                        id: agreementBox
                        Layout.fillWidth: true
                        text: qsTr("Я принимаю Пользовательское соглашение")
                        font.pointSize: Theme.fontBody
                        checked: onboarding.agreementChecked
                        onToggled: onboarding.agreementChecked = checked
                    }

                    LegionButton {
                        Layout.leftMargin: Theme.spaceXl
                        kind: "ghost"
                        compact: true
                        visible: onboarding.agreementUrl.length > 0
                        text: qsTr("Открыть текст")
                        onClicked: onboarding.openUrl(onboarding.agreementUrl)
                    }

                    CheckBox {
                        id: pdnBox
                        Layout.fillWidth: true
                        text: qsTr("Я даю согласие на обработку персональных данных")
                        font.pointSize: Theme.fontBody
                        checked: onboarding.pdnChecked
                        onToggled: onboarding.pdnChecked = checked
                    }

                    LegionButton {
                        Layout.leftMargin: Theme.spaceXl
                        kind: "ghost"
                        compact: true
                        visible: onboarding.pdnUrl.length > 0
                        text: qsTr("Открыть текст")
                        onClicked: onboarding.openUrl(onboarding.pdnUrl)
                    }

                    Label {
                        Layout.fillWidth: true
                        text: onboarding.ageNoticeText
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !onboarding.hasDocumentLinks
                        text: qsTr("Полные тексты документов — на сайте legiongames.ru")
                        color: Theme.textDim
                        font.pointSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }

                    LegionButton {
                        Layout.leftMargin: Theme.spaceXl
                        kind: "ghost"
                        compact: true
                        visible: !onboarding.hasDocumentLinks
                        text: qsTr("Открыть сайт")
                        onClicked: onboarding.openUrl("https://legiongames.ru/")
                    }
                }

                // ---- device label ----------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !onboarding.reconsent && !onboarding.wordsPending
                    spacing: Theme.spaceXs

                    Label {
                        text: qsTr("Имя этого компьютера")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }

                    TextField {
                        id: labelField
                        Layout.fillWidth: true
                        text: onboarding.deviceLabel
                        maximumLength: 32
                        placeholderText: qsTr("Например, DESKTOP-01")
                        onTextChanged: onboarding.deviceLabel = text
                        Keys.onReturnPressed: onboarding.submit()
                        Keys.onEnterPressed: onboarding.submit()
                    }

                    Label {
                        text: qsTr("Так этот ПК будет называться в аккаунте.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontCaption
                    }
                }

                ErrorBanner {
                    Layout.fillWidth: true
                    visible: onboarding.hasError
                    text: onboarding.errorText
                    detail: onboarding.serverError
                    canRetry: true
                    onRetry: onboarding.submit()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceM

                    BusyIndicator {
                        visible: onboarding.submitting || onboarding.serverBusy
                        running: visible
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: onboarding.submitting || onboarding.serverBusy
                        text: qsTr("Создаём ключ и аккаунт…")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }

                    Item { Layout.fillWidth: true }

                    LegionButton {
                        kind: "ghost"
                        visible: !onboarding.reconsent && !onboarding.wordsPending
                        text: qsTr("Восстановить доступ")
                        onClicked: {
                            if (onboarding.shell) {
                                onboarding.shell.recoveryVisible = true
                            }
                        }
                    }

                    LegionButton {
                        kind: "ghost"
                        visible: !onboarding.reconsent && !onboarding.wordsPending
                        text: qsTr("У меня уже есть аккаунт")
                        onClicked: Api.login(onboarding.api)
                    }

                    LegionButton {
                        id: primaryButton
                        kind: "primary"
                        focus: !onboarding.wordsPending
                        enabled: onboarding.reconsent
                                 ? (onboarding.agreementChecked && onboarding.pdnChecked)
                                 : onboarding.canRegister
                        text: onboarding.reconsent ? qsTr("Продолжить")
                                                   : qsTr("Создать аккаунт")
                        onClicked: onboarding.submit()
                    }
                }
            }
        }
    }
}
