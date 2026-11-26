import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Wallet top-up and payment handoff.
//
// The app never touches card data: it creates an intent, opens the system
// browser with a one-time handoff URL and polls the intent. A deep link only
// starts the poll; it never credits the wallet.
LegionView {
    id: payment

    // form | waiting | succeeded | failed | canceled | refunded | timeout
    property string stage: "form"
    property int amount: 500
    property int selectedClubId: -1
    property string publicId: ""
    property string handoffUrl: ""
    property real handoffAt: 0
    property string intentStatus: ""
    property string intentAmount: ""
    property string intentBonus: ""
    property string promoCode: ""
    property int pollTries: 0
    property bool submitting: false

    readonly property int clubId: {
        if (selectedClubId > 0) {
            return selectedClubId
        }
        if (params && params.clubId !== undefined) {
            return params.clubId
        }
        return -1
    }

    readonly property string clubName: {
        if (shell && clubId > 0) {
            var name = shell.clubName(clubId)
            if (name.length > 0) {
                return name
            }
        }
        if (params && params.clubName) {
            return params.clubName
        }
        return qsTr("Клуб не выбран")
    }

    readonly property bool canPay: clubId > 0 && amount >= 1 && amount <= 10000 && !submitting
    readonly property bool waiting: stage === "waiting"
    property bool handoffFresh: false
    property bool reissuing: false

    // A fresh 120 s handoff can be re-issued for the same pending intent.
    readonly property bool canReissue: Api.hasReissueHandoff(api) && publicId.length > 0

    function activate() {
        pollTimer.stop()
        submitting = false
        reissuing = false
        pollTries = 0
        intentStatus = ""
        intentAmount = ""
        intentBonus = ""
        publicId = ""
        handoffUrl = ""
        handoffAt = 0
        handoffFresh = false
        promoCode = ""
        selectedClubId = -1
        stage = "form"
        if (shell) {
            shell.refreshWallet()
            if (shell.catalog.length === 0) {
                shell.loadCatalog(false)
            }
        }
        if (params && params.publicId) {
            publicId = String(params.publicId)
            stage = "waiting"
            if (params.handoffUrl) {
                handoffUrl = String(params.handoffUrl)
                handoffAt = new Date().getTime()
                refreshHandoffFreshness()
                payment.openUrl(handoffUrl)
            }
            pollTimer.start()
            return
        }
        if (params && params.clubId !== undefined) {
            selectedClubId = params.clubId
        }
        stage = "form"
    }

    function focusFirst() {
        payButton.forceActiveFocus()
    }

    function startPolling() {
        pollTries = 0
        pollTimer.start()
    }

    function poll() {
        if (publicId.length === 0) {
            pollTimer.stop()
            return
        }
        Api.pollIntent(api, publicId)
    }

    function pay() {
        if (!canPay) {
            return
        }
        clearError()
        submitting = true
        if (!Api.createTopup(api, clubId, (amount * 1.0).toFixed(2), promoCode)) {
            submitting = false
            showError("unsupported_capability", "", "topup")
        }
    }

    function checkStatus() {
        clearError()
        stage = "waiting"
        startPolling()
    }

    function reopenBrowser() {
        if (!handoffFresh) {
            return
        }
        payment.openUrl(handoffUrl)
    }

    // The handoff code lives 120 s. Once it is stale, ask the backend for a
    // new URL for the same intent instead of creating a second intent.
    function reopenPayment() {
        if (handoffFresh) {
            reopenBrowser()
            return
        }
        if (!canReissue || reissuing) {
            return
        }
        clearError()
        reissuing = true
        if (!Api.reissueHandoff(api, publicId)) {
            reissuing = false
        }
    }

    function refreshHandoffFreshness() {
        handoffFresh = handoffUrl.length > 0 &&
                       (new Date().getTime() - handoffAt) < 110000
    }

    function finishWith(status) {
        pollTimer.stop()
        intentStatus = status
        if (status === "succeeded") {
            stage = "succeeded"
            if (shell) {
                shell.refreshWallet(true)
            }
        }
        else if (status === "failed") {
            stage = "failed"
        }
        else if (status === "canceled") {
            stage = "canceled"
        }
        else if (status === "refunded") {
            stage = "refunded"
        }
        else {
            stage = "waiting"
        }
    }

    Connections {
        target: payment.api
        ignoreUnknownSignals: true

        function onResponse(op, data) {
            if (op === "topup") {
                payment.submitting = false
                payment.publicId = data && data.public_id ? String(data.public_id) : ""
                payment.handoffUrl = data && data.handoff_url ? String(data.handoff_url) : ""
                payment.handoffAt = new Date().getTime()
                payment.refreshHandoffFreshness()
                payment.stage = "waiting"
                if (payment.handoffUrl.length > 0) {
                    payment.openUrl(payment.handoffUrl)
                }
                payment.startPolling()
            }
            else if (op === "handoff") {
                payment.reissuing = false
                var url = ""
                if (data && data.url) {
                    url = String(data.url)
                }
                else if (data && data.handoff_url) {
                    url = String(data.handoff_url)
                }
                if (url.length > 0) {
                    payment.handoffUrl = url
                    payment.handoffAt = new Date().getTime()
                    payment.refreshHandoffFreshness()
                    payment.stage = "waiting"
                    payment.openUrl(url)
                    payment.startPolling()
                }
            }
            else if (op === "intent") {
                payment.pollTries = payment.pollTries + 1
                payment.intentAmount = data && data.amount ? String(data.amount) : ""
                payment.intentBonus = data && data.bonus ? String(data.bonus) : ""
                var status = Api.intentStatusOf(data)
                if (Api.isFinalIntentStatus(status)) {
                    payment.finishWith(status)
                }
                else {
                    payment.intentStatus = status.length > 0 ? status : "pending"
                    if (payment.pollTries >= 30) {
                        pollTimer.stop()
                        payment.stage = "timeout"
                    }
                }
            }
        }

        function onFailed(op, code, message) {
            if (op === "topup") {
                payment.submitting = false
                payment.showError(code, message, op)
            }
            else if (op === "handoff") {
                payment.reissuing = false
                payment.showError(code, message, op)
            }
            else if (op === "intent") {
                payment.pollTries = payment.pollTries + 1
                payment.showError(code, message, op)
                if (code === "not_found" || payment.pollTries >= 30) {
                    pollTimer.stop()
                    payment.stage = "timeout"
                }
            }
        }

        function onPaymentReturned(publicId) {
            // A deep link for this intent only refreshes the poll.
            if (publicId && String(publicId) === payment.publicId && payment.stage === "waiting") {
                payment.startPolling()
            }
        }
    }

    Timer {
        id: pollTimer
        interval: 3000
        repeat: true
        onTriggered: payment.poll()
    }

    Timer {
        interval: 1000
        repeat: true
        running: payment.stage === "waiting"
        onTriggered: payment.refreshHandoffFreshness()
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        showBack: true
        backText: qsTr("Назад")
        title: qsTr("Пополнение")
        subtitle: payment.clubName
        onBack: payment.goBack()
    }

    ScrollView {
        contentWidth: availableWidth
        rightPadding: Theme.scrollbarGutter
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
        ScrollBar.vertical: LegionScrollBar { }

        Column {
            width: parent.width
            spacing: Theme.spaceL

            // ---- amount form --------------------------------------------
            NeonFrame {
                width: parent.width
                height: formColumn.implicitHeight + Theme.spaceXl * 2
                visible: payment.stage === "form"
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: formColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Пополнение баланса")
                        color: Theme.text
                        font.pointSize: Theme.fontHeader + 2
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Баланс пополняется для одного клуба. Чек 54-ФЗ придёт на email, " +
                                   "который вы укажете в браузере.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody + 5
                        wrapMode: Text.Wrap
                    }

                    // Club picker (when opened from the wallet)
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: payment.clubId <= 0
                        spacing: Theme.spaceS

                        Label {
                            text: qsTr("Выберите клуб")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall + 4
                        }

                        Repeater {
                            model: payment.shell ? payment.shell.catalog : []

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 52
                                radius: Theme.radiusS
                                color: payment.selectedClubId === modelData.id ? "#221414" : Theme.panelAlt
                                border.width: 1
                                border.color: payment.selectedClubId === modelData.id ? Theme.accent : Theme.line

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spaceL
                                    anchors.rightMargin: Theme.spaceL

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: Theme.text
                                        font.pointSize: Theme.fontSubtitle + 2
                                        font.bold: true
                                    }

                                    Label {
                                        text: Theme.pricePerHour(modelData.price_per_hour)
                                        color: Theme.textMuted
                                        font.pointSize: Theme.fontSmall + 4
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: payment.selectedClubId = modelData.id
                                }
                            }
                        }
                    }

                    Label {
                        text: qsTr("Сумма")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 4
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS

                        Repeater {
                            model: [100, 200, 500, 1000]

                            delegate: LegionButton {
                                kind: payment.amount === modelData ? "primary" : "secondary"
                                compact: true
                                text: qsTr("%1 ₽").arg(modelData)
                                onClicked: {
                                    payment.amount = modelData
                                    amountField.text = String(modelData)
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        text: qsTr("Своя сумма")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 4
                    }

                    TextField {
                        id: amountField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Своя сумма")
                        font.pointSize: Theme.fontBody + 4
                        text: String(payment.amount)
                        validator: IntValidator { bottom: 1; top: 10000 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        onTextChanged: {
                            var trimmed = text.trim()
                            if (trimmed.length === 0) {
                                payment.amount = 0
                                return
                            }
                            var value = parseInt(trimmed, 10)
                            if (!isNaN(value)) {
                                payment.amount = value
                            }
                        }
                    }

                    Label {
                        text: qsTr("Промокод")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 4
                    }

                    TextField {
                        id: promoField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Если есть — введите код")
                        font.pointSize: Theme.fontBody + 4
                        onTextChanged: payment.promoCode = text.trim()
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Минимум 1 ₽, максимум 10 000 ₽.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontCaption + 3
                    }

                    BalancePanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 88
                        clubName: payment.clubName
                        balance: payment.shell ? payment.shell.balanceFor(payment.clubId) : "0"
                        bonus: payment.walletRow ? payment.walletRow.bonus : "0"
                        minutes: payment.walletRow ? Number(payment.walletRow.minutes || 0) : -1
                    }

                    ErrorBanner {
                        Layout.fillWidth: true
                        visible: payment.hasError && payment.stage === "form"
                        text: payment.errorText
                        detail: payment.serverError
                        canRetry: payment.canPay
                        onRetry: payment.pay()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        BusyIndicator {
                            visible: payment.submitting
                            running: visible
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                        }

                        Item { Layout.fillWidth: true }

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Кошелёк")
                            onClicked: payment.goSection("wallet")
                        }

                        LegionButton {
                            id: payButton
                            kind: "primary"
                            focus: true
                            enabled: payment.canPay
                            text: qsTr("Перейти к оплате")
                            onClicked: payment.pay()
                        }
                    }
                }
            }

            // ---- waiting ------------------------------------------------
            NeonFrame {
                width: parent.width
                height: waitColumn.implicitHeight + Theme.spaceXl * 2
                visible: payment.stage === "waiting"
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: waitColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        spacing: Theme.spaceL

                        BusyIndicator {
                            running: visible
                            visible: payment.stage === "waiting"
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 42
                        }

                        ColumnLayout {
                            spacing: Theme.spaceXs

                            Label {
                                text: qsTr("Платёж обрабатывается")
                                color: Theme.text
                                font.pointSize: Theme.fontHeader + 2
                                font.bold: true
                            }

                            Label {
                                text: qsTr("Можно закрыть это окно: оплата подтвердится на сервере.")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontBody + 5
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Оплата %1 для клуба «%2» открыта в браузере. " +
                                   "Баланс обновится только после подтверждения сервером.")
                            .arg(Theme.money(payment.amount)).arg(payment.clubName)
                        color: Theme.text
                        font.pointSize: Theme.fontBody + 5
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: payment.handoffFresh
                              ? qsTr("Если браузер закрылся, откройте страницу оплаты снова.")
                              : (payment.canReissue
                                 ? qsTr("Ссылка на оплату действует 2 минуты. Откройте платёж снова — клуб и сумма останутся прежними.")
                                 : qsTr("Ссылка на оплату истекла. Если оплата не прошла, создайте новое пополнение."))
                        color: payment.handoffFresh ? Theme.textMuted : Theme.warning
                        font.pointSize: Theme.fontSmall + 4
                        wrapMode: Text.Wrap
                    }

                    ErrorBanner {
                        Layout.fillWidth: true
                        visible: payment.hasError
                        text: payment.errorText
                        detail: payment.serverError
                        canRetry: true
                        onRetry: payment.checkStatus()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        Item { Layout.fillWidth: true }

                        LegionButton {
                            kind: "secondary"
                            visible: payment.handoffFresh || payment.canReissue
                            enabled: (payment.handoffFresh || payment.canReissue) && !payment.reissuing
                            text: payment.reissuing
                                  ? qsTr("Запрашиваем ссылку…")
                                  : (payment.handoffFresh ? qsTr("Открыть браузер снова")
                                                          : qsTr("Открыть оплату снова"))
                            onClicked: payment.reopenPayment()
                        }

                        LegionButton {
                            kind: "secondary"
                            text: qsTr("Проверить статус")
                            onClicked: payment.checkStatus()
                        }

                        LegionButton {
                            kind: "primary"
                            text: qsTr("Свернуть")
                            onClicked: payment.goSection("home")
                        }
                    }
                }
            }

            // ---- result -------------------------------------------------
            NeonFrame {
                width: parent.width
                height: resultColumn.implicitHeight + Theme.spaceXl * 2
                visible: payment.stage !== "form" && payment.stage !== "waiting"
                highlighted: payment.stage === "succeeded"
                flicker: payment.stage === "succeeded"

                ColumnLayout {
                    id: resultColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    StatusPill {
                        text: Api.intentStatusText(payment.intentStatus)
                        tone: Api.intentTone(payment.intentStatus)
                    }

                    Label {
                        Layout.fillWidth: true
                        text: Api.intentStatusText(payment.intentStatus)
                        color: payment.stage === "succeeded" ? Theme.success
                                                              : (payment.stage === "failed" ? Theme.error : Theme.text)
                        font.pointSize: Theme.fontHeader + 2
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: payment.stage === "succeeded"
                        text: qsTr("Баланс клуба «%1» пополнен на %2.")
                            .arg(payment.clubName)
                            .arg(Theme.money(payment.intentAmount.length > 0 ? payment.intentAmount
                                                                             : payment.amount))
                        color: Theme.text
                        font.pointSize: Theme.fontBody + 5
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: payment.stage === "failed"
                        text: qsTr("Деньги не списаны. Проверьте данные карты и попробуйте снова.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody + 5
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: payment.stage === "canceled"
                        text: qsTr("Платёж отменён. Можно создать новое пополнение.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody + 5
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: payment.stage === "timeout"
                        text: qsTr("Платёж пока не подтверждён. Проверьте статус ещё раз через минуту.")
                        color: Theme.warning
                        font.pointSize: Theme.fontBody + 5
                        wrapMode: Text.Wrap
                    }

                    BalancePanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 88
                        visible: payment.stage === "succeeded"
                        clubName: payment.clubName
                        balance: payment.shell ? payment.shell.balanceFor(payment.clubId) : "0"
                        bonus: payment.walletRow ? payment.walletRow.bonus : "0"
                        minutes: payment.walletRow ? Number(payment.walletRow.minutes || 0) : -1
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Возврат — через поддержку.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 4
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        Item { Layout.fillWidth: true }

                        LegionButton {
                            kind: "ghost"
                            text: qsTr("Поддержка")
                            onClicked: payment.openUrl("https://legiongames.ru/help/")
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: payment.stage === "failed" || payment.stage === "canceled" ||
                                     payment.stage === "timeout"
                            text: qsTr("Новое пополнение")
                            onClicked: {
                                payment.stage = "form"
                                payment.publicId = ""
                                payment.handoffUrl = ""
                            }
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: payment.stage === "timeout"
                            text: qsTr("Проверить статус")
                            onClicked: payment.checkStatus()
                        }

                        LegionButton {
                            kind: "secondary"
                            visible: payment.stage === "timeout" && payment.canReissue
                            enabled: !payment.reissuing
                            text: qsTr("Открыть оплату снова")
                            onClicked: payment.reopenPayment()
                        }

                        LegionButton {
                            kind: "primary"
                            visible: payment.stage === "succeeded"
                            text: qsTr("К клубу")
                            onClicked: {
                                if (payment.clubId > 0) {
                                    payment.go("qrc:/gui/legion/ClubDetailView.qml",
                                               { clubId: payment.clubId, name: payment.clubName })
                                }
                                else {
                                    payment.goSection("wallet")
                                }
                            }
                        }

                        LegionButton {
                            kind: "primary"
                            visible: payment.stage === "failed" || payment.stage === "canceled"
                            text: qsTr("Попробовать снова")
                            onClicked: {
                                payment.stage = "form"
                                payment.publicId = ""
                                payment.handoffUrl = ""
                            }
                        }
                    }
                }
            }
        }
    }

    readonly property var walletRow: shell ? shell.walletFor(clubId) : null
}
