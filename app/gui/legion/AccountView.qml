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
    property bool kitSaved: false
    property string kitSavedPath: ""
    property string copiedText: ""
    readonly property var gamification: shell ? shell.gamification : ({})
    readonly property var summary: shell ? shell.accountSummary : ({})
    readonly property var streak: Api.streakOf(gamification)
    readonly property var achievements: Api.achievementsOf(gamification)
    readonly property var achievementItems: Api.achievementItems(gamification)
    readonly property var rewards: Api.rewardsOf(gamification)
    readonly property var referral: Api.referralOf(gamification)
    readonly property bool hasReferralCode: referral.code !== undefined &&
                                            String(referral.code).length > 0
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
            shell.refreshGamification(false)
            shell.refreshAccountSummary(false)
        }
        kitSaved = false
        copiedText = ""
        clearError()
    }

    function copyValue(value) {
        var text = value ? String(value) : ""
        if (text.length === 0) {
            return
        }
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        copiedText = text
        copiedTimer.restart()
    }

    function shareReferral() {
        if (!account.hasReferralCode) {
            return
        }
        var link = referral.link ? String(referral.link) : ""
        var text = qsTr("Играй со мной в LegionGames. Мой код: %1")
            .arg(String(referral.code))
        var url = "https://t.me/share/url?url=" + encodeURIComponent(link) +
                  "&text=" + encodeURIComponent(text)
        account.openUrl(url)
    }

    function useReward(code) {
        if (!code) {
            return
        }
        account.go("qrc:/gui/legion/PaymentView.qml",
                   { promoCode: String(code) })
    }

    function focusFirst() {
        kitButton.forceActiveFocus()
    }

    function reissueKit() {
        kitSaved = false
        clearError()
        Api.reissueWords(api)
    }

    function reprintKit() {
        kitSaved = false
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
            if (op === "kit" || op === "reissue_words") {
                account.kitSaved = false
            }
            account.showError(code, message, op)
        }

        function onRecoveryKitWritten(path, syncedFolder) {
            account.kitSaved = true
            account.kitSavedPath = path ? String(path) : ""
        }
    }

    TextEdit {
        id: clipboardHelper
        visible: false
        width: 0
        height: 0
    }

    Timer {
        id: copiedTimer
        interval: 1600
        onTriggered: account.copiedText = ""
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

                        SelectableText {
                            Layout.fillWidth: true
                            text: account.handle.length > 0 ? account.handle : qsTr("Аккаунт")
                            color: Theme.text
                            font.pointSize: Theme.fontHero
                            font.bold: true
                            wrapMode: TextEdit.NoWrap
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
                visible: account.hasInlineError
                tone: account.errorTone
                text: account.errorText
                detail: account.serverError
                canRetry: true
                onRetry: account.activate()
            }

            // ---- progress summary --------------------------------------
            NeonFrame {
                width: parent.width
                height: progressColumn.implicitHeight + Theme.spaceXl * 2
                highlighted: true
                flicker: true

                ColumnLayout {
                    id: progressColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Прогресс")
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.bold: true
                        }

                        StatusPill {
                            text: qsTr("Серия: %1").arg(Number(account.streak.current || 0))
                            tone: Number(account.streak.current || 0) > 0 ? "good" : "neutral"
                        }

                        StatusPill {
                            text: qsTr("Достижения: %1 / %2")
                                .arg(Number(account.achievements.total_earned || 0))
                                .arg(Number(account.achievements.total_count || 0))
                            tone: "accent"
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceXl

                        ColumnLayout {
                            spacing: 0

                            Label {
                                text: qsTr("На балансе")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }

                            Label {
                                text: account.summary.wallet_total !== undefined
                                      ? Theme.money(account.summary.wallet_total)
                                      : (account.shell ? Theme.money(Api.totalBalance(account.shell.wallet)) : Theme.money(0))
                                color: Theme.text
                                font.pointSize: Theme.fontTitle
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            spacing: 0

                            Label {
                                text: qsTr("Всего пополнено")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }

                            Label {
                                text: Theme.money(account.summary.total_deposited || 0)
                                color: Theme.text
                                font.pointSize: Theme.fontTitle
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            spacing: 0

                            Label {
                                text: qsTr("Активные сессии")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }

                            Label {
                                text: String(Number(account.summary.active_session_count || 0))
                                color: Theme.text
                                font.pointSize: Theme.fontTitle
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            spacing: 0

                            Label {
                                text: qsTr("Награды")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }

                            Label {
                                text: String(Number(account.summary.unused_reward_count || 0))
                                color: Theme.text
                                font.pointSize: Theme.fontTitle
                                font.bold: true
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: account.streak.today_connected === true
                        text: qsTr("Сегодняшний день уже засчитан в серию.")
                        color: Theme.success
                        font.pointSize: Theme.fontSmall
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: account.streak.freeze_available === true
                        text: qsTr("Доступна заморозка серии на пропущенный день.")
                        color: Theme.warning
                        font.pointSize: Theme.fontSmall
                    }
                }
            }

            // ---- achievements -----------------------------------------
            NeonFrame {
                width: parent.width
                height: achievementColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: achievementColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Достижения")
                            color: Theme.text
                            font.pointSize: Theme.fontTitle
                            font.bold: true
                        }

                        Label {
                            text: qsTr("Получено %1 из %2")
                                .arg(Number(account.achievements.total_earned || 0))
                                .arg(Number(account.achievements.total_count || 0))
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: shell !== null && shell.gamificationError !== undefined &&
                                 String(shell.gamificationError).length > 0
                        text: qsTr("Достижения и награды временно недоступны. Обновите страницу позже.")
                        color: Theme.warning
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: account.achievementItems.length === 0
                        text: qsTr("Достижений пока нет. Играйте, пополняйте баланс и " +
                                   "заходите каждый день.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: account.achievementItems

                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceXs

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceM

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.title ? String(modelData.title)
                                                          : qsTr("Достижение")
                                    color: Theme.text
                                    font.pointSize: Theme.fontSubtitle
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                StatusPill {
                                    visible: modelData.category !== undefined &&
                                             String(modelData.category).length > 0
                                    text: modelData.category ? String(modelData.category) : ""
                                    tone: "neutral"
                                }

                                Label {
                                    visible: Api.isAchievementEarned(modelData)
                                    text: Theme.dateTimeText(modelData.earned_at)
                                    color: Theme.success
                                    font.pointSize: Theme.fontSmall
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                radius: 3
                                color: Theme.panelAlt
                                visible: !Api.isAchievementEarned(modelData) &&
                                         Number(modelData.target || 0) > 0

                                Rectangle {
                                    width: parent.width * Api.achievementProgress(modelData)
                                    height: parent.height
                                    radius: 3
                                    color: Theme.accent
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceM

                                Label {
                                    Layout.fillWidth: true
                                    text: Api.achievementProgressText(modelData)
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }

                                SelectableText {
                                    visible: Api.isAchievementEarned(modelData) &&
                                             modelData.reward_code !== undefined &&
                                             modelData.reward_code !== null &&
                                             String(modelData.reward_code).length > 0
                                    text: modelData.reward_code ? String(modelData.reward_code) : ""
                                    color: Theme.success
                                    font.pointSize: Theme.fontSmall
                                    font.family: "monospace"
                                    wrapMode: TextEdit.NoWrap
                                }

                                LegionButton {
                                    kind: "ghost"
                                    compact: true
                                    visible: modelData.reward_code !== undefined &&
                                             modelData.reward_code !== null &&
                                             String(modelData.reward_code).length > 0
                                    text: account.copiedText === String(modelData.reward_code)
                                          ? qsTr("Скопировано")
                                          : qsTr("Копировать")
                                    onClicked: account.copyValue(modelData.reward_code)
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Theme.line
                                visible: index < account.achievementItems.length - 1
                            }
                        }
                    }
                }
            }

            // ---- referral ----------------------------------------------
            NeonFrame {
                width: parent.width
                height: referralColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: referralColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Пригласи друга")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Друг вводит ваш код при создании аккаунта. Награда придёт " +
                                   "после его первого пополнения.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !account.hasReferralCode
                        text: qsTr("Код появится после создания аккаунта.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontBody
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM
                        visible: account.hasReferralCode

                        SelectableText {
                            Layout.fillWidth: true
                            text: String(account.referral.code)
                            color: Theme.accent
                            font.pointSize: Theme.fontHeader
                            font.bold: true
                            font.family: "monospace"
                            wrapMode: TextEdit.NoWrap
                        }

                        StatusPill {
                            text: qsTr("Ожидают: %1").arg(Number(account.referral.pending || 0))
                            tone: "warn"
                        }

                        StatusPill {
                            text: qsTr("Награждены: %1").arg(Number(account.referral.rewarded || 0))
                            tone: "good"
                        }
                    }

                    SelectableText {
                        Layout.fillWidth: true
                        visible: account.hasReferralCode &&
                                 account.referral.link !== undefined &&
                                 String(account.referral.link).length > 0
                        text: account.referral.link ? String(account.referral.link) : ""
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        wrapMode: TextEdit.WrapAnywhere
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM
                        visible: account.hasReferralCode

                        LegionButton {
                            kind: "secondary"
                            compact: true
                            text: account.copiedText === String(account.referral.code)
                                  ? qsTr("Код скопирован")
                                  : qsTr("Копировать код")
                            onClicked: account.copyValue(account.referral.code)
                        }

                        LegionButton {
                            kind: "secondary"
                            compact: true
                            visible: account.referral.link !== undefined &&
                                     String(account.referral.link).length > 0
                            text: qsTr("Копировать ссылку")
                            onClicked: account.copyValue(account.referral.link)
                        }

                        LegionButton {
                            kind: "secondary"
                            compact: true
                            text: qsTr("Поделиться")
                            onClicked: account.shareReferral()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- rewards -----------------------------------------------
            NeonFrame {
                width: parent.width
                height: rewardsColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: rewardsColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Награды")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Код можно скопировать или сразу подставить при пополнении.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: account.rewards.length === 0
                        text: qsTr("Наград пока нет.")
                        color: Theme.textDim
                        font.pointSize: Theme.fontBody
                    }

                    Repeater {
                        model: account.rewards

                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceXs

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceM

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.title ? String(modelData.title)
                                                              : qsTr("Награда")
                                        color: modelData.used === true ? Theme.textMuted : Theme.text
                                        font.pointSize: Theme.fontSubtitle
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: {
                                            var parts = []
                                            if (modelData.source) {
                                                parts.push(String(modelData.source))
                                            }
                                            if (modelData.expires_at) {
                                                parts.push(qsTr("до %1")
                                                    .arg(Theme.dateTimeText(modelData.expires_at)))
                                            }
                                            return parts.join(" · ")
                                        }
                                        color: Theme.textMuted
                                        font.pointSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                }

                                Label {
                                    text: Api.rewardValueText(modelData)
                                    color: modelData.used === true ? Theme.textDim : Theme.success
                                    font.pointSize: Theme.fontSubtitle
                                    font.bold: true
                                }

                                StatusPill {
                                    text: modelData.used === true ? qsTr("Использована")
                                                                  : qsTr("Активна")
                                    tone: modelData.used === true ? "neutral" : "good"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceM

                                SelectableText {
                                    Layout.fillWidth: true
                                    text: modelData.code ? String(modelData.code) : ""
                                    color: modelData.used === true ? Theme.textDim : Theme.text
                                    font.pointSize: Theme.fontSmall
                                    font.family: "monospace"
                                    wrapMode: TextEdit.NoWrap
                                }

                                LegionButton {
                                    kind: "ghost"
                                    compact: true
                                    text: account.copiedText === String(modelData.code)
                                          ? qsTr("Скопировано")
                                          : qsTr("Копировать")
                                    onClicked: account.copyValue(modelData.code)
                                }

                                LegionButton {
                                    kind: "secondary"
                                    compact: true
                                    visible: modelData.used !== true
                                    text: qsTr("Использовать при пополнении")
                                    onClicked: account.useReward(modelData.code)
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Theme.line
                                visible: index < account.rewards.length - 1
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                visible: account.kitSaved
                height: visible ? savedLayout.implicitHeight + Theme.spaceM * 2 : 0
                radius: Theme.radiusS
                color: "#142214"
                border.width: 1
                border.color: Theme.success

                RowLayout {
                    id: savedLayout
                    anchors.fill: parent
                    anchors.margins: Theme.spaceM
                    spacing: Theme.spaceM

                    Label {
                        text: "✓"
                        color: Theme.success
                        font.pointSize: Theme.fontSubtitle
                        font.bold: true
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Файл набора сохранён")
                            color: Theme.success
                            font.pointSize: Theme.fontBody
                            font.bold: true
                        }

                        SelectableText {
                            Layout.fillWidth: true
                            visible: account.kitSavedPath.length > 0
                            text: account.kitSavedPath
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                            font.family: "monospace"
                            wrapMode: TextEdit.WrapAnywhere
                        }
                    }

                    LegionButton {
                        kind: "ghost"
                        compact: true
                        text: qsTr("Закрыть")
                        onClicked: account.kitSaved = false
                    }
                }
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
