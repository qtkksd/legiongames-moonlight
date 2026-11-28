import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Wallet: per-club balance, bonus and the estimated minutes left.
// The wallet is always per club; there is no shared balance.
LegionView {
    id: wallet

    readonly property var rows: shell ? shell.wallet : []
    readonly property real total: Api.totalBalance(rows)
    readonly property bool empty: rows.length === 0

    property var historyItems: []
    property string historyCursor: ""
    property bool historyLoading: false
    property bool historyLoaded: false
    property string historyError: ""
    property string pendingCursor: ""

    function activate() {
        if (shell) {
            shell.refreshWallet()
            if (shell.catalog.length === 0) {
                shell.loadCatalog(false)
            }
        }
        if (!historyLoaded && !historyLoading) {
            loadHistory(true)
        }
    }

    function loadHistory(reset) {
        if (historyLoading) {
            return
        }
        if (reset) {
            historyItems = []
            historyCursor = ""
            pendingCursor = ""
            historyLoaded = false
        }
        historyLoading = true
        historyError = ""
        if (!Api.getWalletHistory(api, pendingCursor)) {
            historyLoading = false
            historyError = "unsupported_capability"
        }
    }

    function loadMoreHistory() {
        if (historyCursor.length === 0) {
            return
        }
        pendingCursor = historyCursor
        loadHistory(false)
    }

    function historyClubName(item) {
        if (item && item.club_name && String(item.club_name).length > 0) {
            return String(item.club_name)
        }
        if (item && item.club_id !== undefined) {
            return wallet.clubName(item.club_id)
        }
        return qsTr("Клуб")
    }

    Connections {
        target: wallet.api
        ignoreUnknownSignals: true

        function onResponse(op, data) {
            if (op === "wallet") {
                wallet.clearError()
            }
            else if (op === "wallet_history") {
                var items = (data && data.items) ? data.items : []
                if (wallet.pendingCursor.length === 0) {
                    wallet.historyItems = items
                }
                else {
                    wallet.historyItems = wallet.historyItems.concat(items)
                }
                wallet.historyCursor = (data && data.next_cursor)
                    ? String(data.next_cursor) : ""
                wallet.pendingCursor = ""
                wallet.historyLoading = false
                wallet.historyLoaded = true
                wallet.historyError = ""
            }
        }

        function onFailed(op, code, message) {
            if (op === "wallet") {
                wallet.showError(code, message, op)
            }
            else if (op === "wallet_history") {
                wallet.historyLoading = false
                wallet.historyError = code ? String(code) : "server_error"
            }
        }
    }

    function focusFirst() {
        if (rows.length > 0 && firstButton !== null) {
            firstButton.forceActiveFocus()
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Кошелёк")
        subtitle: qsTr("Всего %1").arg(Theme.money(total))
    }

    LegionScrollView {
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

            NeonFrame {
                width: parent.width
                height: summaryColumn.implicitHeight + Theme.spaceXl * 2
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: summaryColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Баланс")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }

                    Label {
                        text: Theme.money(wallet.total)
                        color: wallet.total > 0 ? Theme.text : Theme.warning
                        font.pointSize: Theme.fontDisplay
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Пополнение проходит в браузере. Баланс зачисляется после " +
                                   "подтверждения платежа сервером.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }
                }
            }

            EmptyState {
                width: parent.width
                height: 220
                visible: wallet.empty
                title: qsTr("Баланс пока пуст")
                text: qsTr("Выберите клуб и пополните баланс — оплата откроется в браузере.")
                actionText: qsTr("Выбрать клуб")
                onAction: wallet.goSection("clubs")
            }

            Column {
                width: parent.width
                spacing: Theme.spaceM

                Repeater {
                    model: wallet.rows

                    delegate: Rectangle {
                        width: parent.width
                        height: 112
                        radius: Theme.radiusM
                        color: Theme.panel
                        border.width: 1
                        border.color: Theme.line

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spaceL
                            spacing: Theme.spaceXl

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: wallet.clubName(modelData.club_id)
                                    color: Theme.text
                                    font.pointSize: Theme.fontTitle
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Примерно %1 игры")
                                        .arg(Theme.duration(Number(modelData.minutes || 0) * 60))
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }
                            }

                            ColumnLayout {
                                spacing: 2

                                Label {
                                    Layout.alignment: Qt.AlignRight
                                    text: qsTr("Основной %1").arg(Theme.money(Api.clubMain(modelData)))
                                    color: Api.clubMain(modelData) > 0 ? Theme.text : Theme.warning
                                    font.pointSize: Theme.fontSubtitle
                                    font.bold: true
                                }

                                Label {
                                    Layout.alignment: Qt.AlignRight
                                    text: qsTr("Бонус %1").arg(Theme.money(Api.clubBonus(modelData)))
                                    color: Api.clubBonus(modelData) > 0 ? Theme.success : Theme.textDim
                                    font.pointSize: Theme.fontSmall
                                }

                                Label {
                                    Layout.alignment: Qt.AlignRight
                                    text: qsTr("Всего %1").arg(Theme.money(Api.clubTotal(modelData)))
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }
                            }

                            LegionButton {
                                id: rowButton
                                kind: "secondary"
                                text: qsTr("Пополнить")
                                onClicked: wallet.go("qrc:/gui/legion/PaymentView.qml",
                                                     { clubId: modelData.club_id,
                                                       clubName: wallet.clubName(modelData.club_id) })
                                Component.onCompleted: {
                                    if (index === 0) {
                                        firstButton = rowButton
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- wallet history -----------------------------------------
            NeonFrame {
                width: parent.width
                height: historyColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: historyColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("История пополнений")
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.bold: true
                        }

                        BusyIndicator {
                            visible: wallet.historyLoading
                            running: visible
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                        }

                        LegionButton {
                            kind: "ghost"
                            compact: true
                            text: qsTr("Обновить")
                            onClicked: {
                                wallet.pendingCursor = ""
                                wallet.loadHistory(true)
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: wallet.historyLoaded && wallet.historyItems.length === 0 &&
                                 wallet.historyError.length === 0
                        text: qsTr("Пополнений пока нет.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: wallet.historyError.length > 0
                        text: qsTr("История временно недоступна. Попробуйте обновить.")
                        color: Theme.warning
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: wallet.historyItems

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 62
                            radius: Theme.radiusS
                            color: Theme.panelAlt
                            border.width: 1
                            border.color: Theme.line

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spaceL
                                anchors.rightMargin: Theme.spaceL
                                spacing: Theme.spaceM

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.type === "refund"
                                              ? qsTr("Возврат платежа")
                                              : qsTr("Пополнение")
                                        color: Theme.text
                                        font.pointSize: Theme.fontSubtitle
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · %2")
                                            .arg(Theme.dateTimeText(modelData.created_at))
                                            .arg(wallet.historyClubName(modelData))
                                        color: Theme.textMuted
                                        font.pointSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                }

                                ColumnLayout {
                                    spacing: 2

                                    Label {
                                        Layout.alignment: Qt.AlignRight
                                        text: Theme.money(Api.walletHistoryTotal(modelData))
                                        color: modelData.type === "refund" ? Theme.warning : Theme.success
                                        font.pointSize: Theme.fontSubtitle
                                        font.bold: true
                                    }

                                    Label {
                                        Layout.alignment: Qt.AlignRight
                                        text: qsTr("основной %1 + бонус %2")
                                            .arg(Theme.money(Api.walletHistoryMain(modelData)))
                                            .arg(Theme.money(Api.walletHistoryBonus(modelData)))
                                        color: Theme.textMuted
                                        font.pointSize: Theme.fontSmall
                                    }
                                }
                            }
                        }
                    }

                    LegionButton {
                        Layout.alignment: Qt.AlignHCenter
                        kind: "secondary"
                        visible: wallet.historyCursor.length > 0
                        text: qsTr("Показать ещё")
                        onClicked: wallet.loadMoreHistory()
                    }
                }
            }

            ErrorBanner {
                width: parent.width
                visible: wallet.hasInlineError
                tone: wallet.errorTone
                text: wallet.errorText
                detail: wallet.serverError
                canRetry: true
                onRetry: wallet.activate()
            }

            Label {
                width: parent.width
                text: qsTr("Возврат оформляет поддержка. Напишите в Telegram-бот или на почту.")
                color: Theme.textMuted
                font.pointSize: Theme.fontSubtitle
                wrapMode: Text.Wrap
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spaceM

                LegionButton {
                    kind: "secondary"
                    text: qsTr("Telegram-бот")
                    onClicked: wallet.openUrl("https://t.me/legiongamebot")
                }

                LegionButton {
                    kind: "secondary"
                    text: qsTr("rem.legiongames@yandex.com")
                    onClicked: wallet.openUrl("mailto:rem.legiongames@yandex.com")
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    // The first "Пополнить" button receives focus for keyboard and controller.
    property var firstButton: null

    function clubName(clubId) {
        var name = shell ? shell.clubName(clubId) : ""
        return name.length > 0 ? name : qsTr("Клуб")
    }
}
