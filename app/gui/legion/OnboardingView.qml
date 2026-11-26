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
    // A local account record exists but the device key could not be loaded.
    // Do not offer account creation again: that would create a duplicate
    // server account. Recovery is the only safe path.
    readonly property bool keyLost: appState === "no_account" && handle.length > 0

    property bool agreementChecked: false
    property bool pdnChecked: false
    property bool submitting: false

    // Legal texts are served by the website; the app links out and does not
    // bundle them. These URLs are the production documents.
    readonly property string agreementDocUrl: "https://legiongames.ru/user-agreement.pdf"
    readonly property string policyDocUrl: "https://legiongames.ru/policy.pdf"
    readonly property string consentDocUrl: "https://legiongames.ru/privacy-consent.pdf"

    // Prefill from the OS hostname (QSysInfo::machineHostName via the backend);
    // the user can still edit it before creating the account.
    property string deviceLabel: (api && api.suggestedDeviceLabel)
                                  ? api.suggestedDeviceLabel
                                  : qsTr("Мой компьютер")

    readonly property bool canRegister: !keyLost && agreementChecked && pdnChecked &&
                                        deviceLabel.trim().length > 0 &&
                                        !submitting && !serverBusy

    readonly property string ageNotice: api ? (api.ageNotice || "") : ""
    readonly property string ageNoticeText: ageNotice.length > 0
                                            ? ageNotice
                                            : qsTr("Сервис предназначен для лиц старше 18 лет.")

    function activate() {
        if (!wordsPending && !keyLost) {
            agreementBox.forceActiveFocus()
        }
    }

    function focusFirst() {
        if (keyLost) {
            recoveryButton.forceActiveFocus()
        }
        else if (wordsPending) {
            if (onboarding.words.length > 0) {
                wordsConfirmButton.forceActiveFocus()
            }
            else {
                wordsReissueButton.forceActiveFocus()
            }
        }
        else {
            agreementBox.forceActiveFocus()
        }
    }

    function submit() {
        if (wordsPending) {
            return
        }
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
                        font.pointSize: Theme.fontTitle + 2
                        font.bold: true
                    }

                    Label {
                        text: onboarding.reconsent ? qsTr("Документы обновились")
                                                   : qsTr("Шаг 1 из 2")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 1
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: onboarding.keyLost
                          ? qsTr("Восстановление доступа")
                          : (onboarding.reconsent
                             ? qsTr("Примите обновлённые документы")
                             : qsTr("Создание аккаунта"))
                    color: Theme.text
                    font.pointSize: Theme.fontHero + 4
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: onboarding.keyLost
                    spacing: Theme.spaceS

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Аккаунт %1 уже создан на этом ПК.").arg(onboarding.handle)
                        color: Theme.text
                        font.pointSize: Theme.fontBody + 2
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Ключ устройства не загрузился. Восстановите доступ по восьми " +
                                   "словам — новый аккаунт создавать не нужно.")
                        color: Theme.warning
                        font.pointSize: Theme.fontBody + 2
                        wrapMode: Text.Wrap
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !onboarding.reconsent && !onboarding.wordsPending
                    text: qsTr("Аккаунт создаётся в этом браузере. Логина и пароля нет — " +
                               "только ключ устройства и 8 слов восстановления.")
                    color: Theme.textMuted
                    font.pointSize: Theme.fontBody + 3
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

                    Label {
                        Layout.fillWidth: true
                        visible: onboarding.words.length > 0
                        text: qsTr("Ваши слова восстановления:")
                        color: Theme.text
                        font.pointSize: Theme.fontBody + 2
                        font.bold: true
                    }

                    Repeater {
                        model: onboarding.words

                        delegate: Label {
                            Layout.fillWidth: true
                            text: (index + 1) + ". " + modelData
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.family: "monospace"
                        }
                    }

                    LegionButton {
                        id: wordsReissueButton
                        kind: onboarding.words.length > 0 ? "secondary" : "primary"
                        text: qsTr("Показать набор ещё раз")
                        onClicked: Api.reissueWords(onboarding.api)
                    }

                    LegionButton {
                        id: wordsConfirmButton
                        kind: "primary"
                        visible: onboarding.words.length > 0
                        text: qsTr("Я сохранил слова")
                        onClicked: Api.confirmWordsSaved(onboarding.api)
                    }
                }

                // ---- consent form ----------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !onboarding.wordsPending && !onboarding.keyLost
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS

                        CheckBox {
                            id: agreementBox
                            Layout.alignment: Qt.AlignTop
                            checked: onboarding.agreementChecked
                            onToggled: onboarding.agreementChecked = checked
                            Accessible.name: qsTr("Я принимаю пользовательское соглашение")
                        }

                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.RichText
                            text: qsTr("Я принимаю <a href=\"%1\">пользовательское соглашение</a>.")
                                      .arg(onboarding.agreementDocUrl)
                            color: Theme.text
                            font.pointSize: Theme.fontBody + 3
                            wrapMode: Text.Wrap
                            onLinkActivated: onboarding.openUrl(link)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS

                        CheckBox {
                            id: pdnBox
                            Layout.alignment: Qt.AlignTop
                            checked: onboarding.pdnChecked
                            onToggled: onboarding.pdnChecked = checked
                            Accessible.name: qsTr("Я принимаю политику обработки ПДн и даю согласие на обработку")
                        }

                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.RichText
                            text: qsTr("Я принимаю <a href=\"%1\">политику обработки ПДн</a> " +
                                       "и даю <a href=\"%2\">согласие на обработку</a>.")
                                      .arg(onboarding.policyDocUrl)
                                      .arg(onboarding.consentDocUrl)
                            color: Theme.text
                            font.pointSize: Theme.fontBody + 3
                            wrapMode: Text.Wrap
                            onLinkActivated: onboarding.openUrl(link)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: onboarding.ageNoticeText
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 2
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
                        onClicked: onboarding.openUrl("https://legiongames.ru/")
                    }
                }

                // ---- device label ----------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !onboarding.reconsent && !onboarding.wordsPending &&
                             !onboarding.keyLost
                    spacing: Theme.spaceXs

                    Label {
                        text: qsTr("Имя этого компьютера")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 2
                    }

                    TextField {
                        id: labelField
                        Layout.fillWidth: true
                        text: onboarding.deviceLabel
                        maximumLength: 32
                        font.pointSize: Theme.fontBody + 2
                        onTextChanged: onboarding.deviceLabel = text
                        Keys.onReturnPressed: onboarding.submit()
                        Keys.onEnterPressed: onboarding.submit()
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

                LegionButton {
                    Layout.fillWidth: true
                    visible: onboarding.errorCode === "device_key_corrupt" ||
                             onboarding.keyLost
                    kind: "secondary"
                    text: qsTr("Сбросить локальные данные")
                    onClicked: Api.resetLocalAccount(onboarding.api)
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
                        font.pointSize: Theme.fontSmall + 2
                    }

                    Item { Layout.fillWidth: true }

                    LegionButton {
                        id: recoveryButton
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
                        visible: !onboarding.reconsent && !onboarding.wordsPending &&
                                 !onboarding.keyLost
                        text: qsTr("У меня уже есть аккаунт")
                        onClicked: Api.login(onboarding.api)
                    }

                    LegionButton {
                        id: primaryButton
                        kind: "primary"
                        visible: !onboarding.keyLost && !onboarding.wordsPending
                        focus: !onboarding.wordsPending && !onboarding.keyLost
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
