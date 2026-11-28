import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Notice.js" as Notice
import "Theme.js" as Theme

import ComputerManager 1.0

// Root of the app-first product: startup gate, onboarding, rail shell.
//
// It is pushed as the initial StackView page by main.qml when the account
// backend (the `legionApi` context property) is present. Everything the screen
// shows about the account, catalog, session, queue and money comes from that
// backend; the shell only routes and caches for display.
Item {
    id: shell

    objectName: "legion-shell"

    readonly property var api: {
        try {
            return (typeof legionApi !== "undefined") ? legionApi : null
        }
        catch (error) {
            return null
        }
    }

    readonly property string appState: api ? (api.state || "") : ""
    readonly property string handle: api ? (api.handle || "") : ""
    readonly property string onboardingStep: api ? (api.onboardingStep || "") : ""
    readonly property string lastError: api ? (api.lastError || "") : ""
    readonly property string lastErrorCode: api ? (api.lastErrorCode || "") : ""
    readonly property string catalogUpdatedAt: api ? (api.catalogUpdatedAt || "") : ""
    readonly property var notifications: notificationCenter
    readonly property bool qaEnabled: {
        try {
            return lgStagingBuild === true
        }
        catch (error) {
            return false
        }
    }
    property string lastQueueStatus: ""

    readonly property bool windowVisible: {
        try {
            return window.visible === true
        }
        catch (error) {
            return true
        }
    }

    readonly property bool windowActive: {
        try {
            return window.active === true
        }
        catch (error) {
            return true
        }
    }

    // True once the account bootstrap has reached a terminal or onboarding
    // state. The splash waits for this instead of flashing the checking gate.
    readonly property bool startupSettled: appState.length > 0 &&
                                           appState !== "checking"

    // Terminal authentication loss: the stored session cannot be used again.
    // The shell shows a dedicated re-login/recovery gate instead of the app.
    readonly property bool authExpired: appState === "session_expired"

    // app | onboarding | deletion | startup
    readonly property string mode: {
        if (!Api.available(api)) {
            return "startup"
        }
        switch (appState) {
        case "signed_in":
            return "app"
        case "no_account":
        case "consent":
        case "onboarding":
            return "onboarding"
        case "deletion_pending":
            return "deletion"
        case "session_expired":
            return "startup"
        default:
            return "startup"
        }
    }

    // vault | offline | revoked | deletion | checking
    readonly property string gateMode: {
        switch (appState) {
        case "vault_unavailable":
            return "vault"
        case "offline":
            return "offline"
        case "revoked":
            return "revoked"
        case "deletion_pending":
            return "deletion"
        default:
            return "checking"
        }
    }

    // Shared server snapshots. The store is the only owner of cache keys,
    // TTLs, in-flight guards and mutation ordering; the shell exposes aliases
    // so existing views keep their stable read API.
    QueryStore {
        id: queryStore
        api: shell.api
    }

    property alias session: queryStore.session
    property alias queueEntry: queryStore.queueEntry
    property alias wallet: queryStore.wallet
    property alias catalog: queryStore.catalog
    property alias catalogAt: queryStore.catalogAt
    property alias catalogLoading: queryStore.catalogLoading
    property alias catalogErrorCode: queryStore.catalogErrorCode
    property alias catalogErrorMessage: queryStore.catalogErrorMessage
    property alias catalogFetchedAt: queryStore.catalogFetchedAt
    property alias clubFetchedAt: queryStore.clubFetchedAt
    property alias clubInFlight: queryStore.clubInFlight
    property alias clubDirty: queryStore.clubDirty
    property alias walletFetchedAt: queryStore.walletFetchedAt
    property alias sessionFetchedAt: queryStore.sessionFetchedAt
    property alias queueFetchedAt: queryStore.queueFetchedAt
    property alias sessionHistory: queryStore.sessionHistory
    property alias sessionHistoryFetchedAt: queryStore.sessionHistoryFetchedAt
    property alias sessionHistoryLoading: queryStore.sessionHistoryLoading
    property alias sessionHistoryInFlight: queryStore.sessionHistoryInFlight
    property alias sessionHistoryError: queryStore.sessionHistoryError
    property alias catalogInFlight: queryStore.catalogInFlight
    property alias walletInFlight: queryStore.walletInFlight
    property alias sessionInFlight: queryStore.sessionInFlight
    property alias queueInFlight: queryStore.queueInFlight
    property alias catalogDirty: queryStore.catalogDirty
    property alias walletDirty: queryStore.walletDirty
    property alias sessionDirty: queryStore.sessionDirty
    property alias queueDirty: queryStore.queueDirty
    property alias sessionMutationSeq: queryStore.sessionMutationSeq
    property alias sessionReadMutationSeq: queryStore.sessionReadMutationSeq
    property alias queueMutationSeq: queryStore.queueMutationSeq
    property alias queueReadMutationSeq: queryStore.queueReadMutationSeq
    property alias queuePollInterval: queryStore.queuePollInterval
    readonly property int catalogCacheTtlMs: queryStore.catalogCacheTtlMs
    readonly property int clubCacheTtlMs: queryStore.clubCacheTtlMs
    readonly property int walletCacheTtlMs: queryStore.walletCacheTtlMs
    readonly property int sessionCacheTtlMs: queryStore.sessionCacheTtlMs
    readonly property int queueCacheTtlMs: queryStore.queueCacheTtlMs
    readonly property int sessionHistoryCacheTtlMs: queryStore.sessionHistoryCacheTtlMs

    // Optional account read models (gamification, aggregate summary). They
    // never block the main flows; a missing endpoint leaves an empty model and
    // the views render their safe stub state. Both are re-read through
    // refreshAccountModels() on session, queue and deposit transitions and on
    // window resume; no view duplicates award logic.
    property var gamification: ({})
    property var accountSummary: ({})
    property double gamificationFetchedAt: 0
    property double summaryFetchedAt: 0
    property bool gamificationInFlight: false
    property bool summaryInFlight: false
    property string gamificationError: ""
    property string summaryError: ""
    readonly property int gamificationCacheTtlMs: 30000
    readonly property int summaryCacheTtlMs: 15000
    readonly property string deletionRequestedAt: api ? (api.deletionRequestedAt || "") : ""
    readonly property string purgeAfter: api ? (api.purgeAfter || "") : ""

    // Navigation
    property string section: "home"
    property string pageSource: ""
    property var pageParams: ({})
    property var pageHistory: []

    // Overlays
    property bool wordsVisible: false
    property string wordsHandle: ""
    property var wordsList: []
    property bool recoveryVisible: false
    property bool reconsentVisible: false

    // Lease health banner (outside the stream). The health agent delivers
    // notices asynchronously; StreamSegue displays the same notices in-stream
    // through the SDL overlay. This is server copy, never a local timer.
    property var healthNotice: null
    property string healthNoticeAction: ""
    property string healthNoticeCode: ""

    // Written recovery kit file (recoveryKitWritten signal)
    property string kitPath: ""
    property bool kitInSyncedFolder: false

    // ---- navigation -----------------------------------------------------

    function setPage(url, pageParamsValue, focusContent) {
        var nextParams = pageParamsValue ? pageParamsValue : ({})
        if (pageSource === url && pageLoader.item) {
            pageParams = nextParams
            wirePage(pageLoader.item, focusContent === true)
            return
        }
        pageParams = nextParams
        pageSource = url
        if (focusContent === true) {
            focusContentArea()
        }
    }

    function showSection(key) {
        section = key
        pageHistory = []
        if (key === "clubs") {
            setPage("qrc:/gui/legion/ClubsView.qml", {})
        }
        else if (key === "sessions") {
            setPage("qrc:/gui/legion/SessionsView.qml", {})
        }
        else if (key === "computers") {
            setPage("qrc:/gui/legion/ComputersView.qml", {})
        }
        else if (key === "wallet") {
            setPage("qrc:/gui/legion/WalletView.qml", {})
        }
        else if (key === "account") {
            setPage("qrc:/gui/legion/AccountView.qml", {})
        }
        else if (key === "settings") {
            setPage("qrc:/gui/legion/LegionSettingsView.qml", {})
        }
        else if (key === "qa" && qaEnabled) {
            setPage("qrc:/gui/legion/QaView.qml", {})
        }
        else {
            section = "home"
            setPage("qrc:/gui/legion/HomeView.qml", {})
        }
    }

    function navigate(url, params) {
        var history = pageHistory.slice(0)
        history.push({ url: pageSource, params: pageParams, section: section })
        pageHistory = history
        setPage(url, params ? params : ({}), false)
    }

    function back() {
        if (pageHistory.length === 0) {
            return
        }
        var history = pageHistory.slice(0)
        var previous = history.pop()
        pageHistory = history
        section = previous.section ? previous.section : section
        setPage(previous.url, previous.params ? previous.params : ({}), true)
    }

    function focusContentArea() {
        if (pageLoader.item && typeof pageLoader.item.focusFirst === "function") {
            pageLoader.item.focusFirst()
        }
    }

    function focusIsInRail() {
        try {
            var item = window.activeFocusItem
            while (item) {
                if (item === rail) {
                    return true
                }
                item = item.parent
            }
        }
        catch (error) {
            return false
        }
        return false
    }

    function wirePage(item, refocus) {
        if (!item) {
            return
        }
        if (item.shell !== undefined) {
            item.shell = shell
        }
        if (item.params !== undefined) {
            item.params = pageParams
        }
        if (typeof item.activate === "function") {
            item.activate()
        }
        if (refocus === true && typeof item.focusFirst === "function") {
            item.focusFirst()
        }
    }

    // ---- server data ----------------------------------------------------

    function loadCatalog(availableOnly, force) {
        return queryStore.loadCatalog(availableOnly, force)
    }

    function clubFetchedAtMs(slug) {
        return queryStore.clubFetchedAtMs(slug)
    }

    function refreshClub(slug, force) {
        return queryStore.refreshClub(slug, force)
    }

    function refreshWallet(force) {
        return queryStore.refreshWallet(force)
    }

    function refreshWalletIfStale() {
        return queryStore.refreshWalletIfStale()
    }

    function refreshSession(force) {
        return queryStore.refreshSession(force)
    }

    function refreshQueue(force) {
        return queryStore.refreshQueue(force)
    }

    function refreshSessionHistory(force) {
        return queryStore.refreshSessionHistory(force)
    }

    function refreshAll(force) {
        return queryStore.refreshAll(force)
    }

    function refreshGamification(force) {
        if (gamificationInFlight) {
            return false
        }
        if (!force && gamificationFetchedAt > 0 &&
                (Date.now() - gamificationFetchedAt) < gamificationCacheTtlMs) {
            return false
        }
        gamificationInFlight = true
        gamificationError = ""
        if (!Api.getGamification(api)) {
            gamificationInFlight = false
            gamificationError = "unsupported_capability"
            return false
        }
        return true
    }

    function refreshAccountSummary(force) {
        if (summaryInFlight) {
            return false
        }
        if (!force && summaryFetchedAt > 0 &&
                (Date.now() - summaryFetchedAt) < summaryCacheTtlMs) {
            return false
        }
        summaryInFlight = true
        summaryError = ""
        if (!Api.getAccountSummary(api)) {
            summaryInFlight = false
            summaryError = "unsupported_capability"
            return false
        }
        return true
    }

    // The gamification read model and the aggregate account summary change
    // together on every lifecycle transition (session, queue, deposit). Both
    // reads are optional and guarded, so a burst of server signals collapses
    // into at most one request per endpoint.
    function refreshAccountModels(force) {
        refreshGamification(force === true)
        refreshAccountSummary(force === true)
    }

    // Foreground reconciliation: a minimized, background or unfocused window
    // keeps the last snapshots. Re-read with the normal TTL guards instead of
    // adding another global polling timer.
    function refreshOnResume() {
        if (mode !== "app") {
            return
        }
        refreshAll(false)
        refreshAccountModels(false)
    }

    function clubById(id) {
        return queryStore.clubById(id)
    }

    function clubName(id) {
        return queryStore.clubName(id)
    }

    function updateClub(updated) {
        queryStore.updateClub(updated)
    }

    function walletFor(clubId) {
        return queryStore.walletFor(clubId)
    }

    function balanceFor(clubId) {
        return queryStore.balanceFor(clubId)
    }

    function primeApp() {
        if (!Api.available(api)) {
            return
        }
        if (pageSource.length === 0) {
            showSection("home")
        }
        refreshAll(true)
        refreshAccountModels(true)
    }

    function syncWords() {
        if (!Api.available(api)) {
            return
        }
        if (api.words && api.words.length > 0) {
            wordsHandle = handle
            wordsList = api.words
            wordsVisible = true
        }
    }

    // ---- lease health banner -------------------------------------------

    function healthNoticeTitle(code) {
        if (code === "low_balance") {
            return qsTr("Баланс заканчивается")
        }
        if (code === "club_closing") {
            return qsTr("Клуб закрывается")
        }
        if (code === "session_ending") {
            return qsTr("Сессия завершается")
        }
        return qsTr("Сообщение сервера")
    }

    function healthNoticeText(code, notice) {
        var text = notice && notice.message ? String(notice.message) : ""
        if (text.length > 0) {
            return text
        }
        var minutes = notice ? Number(notice.minutes_left || 0) : 0
        if (code === "low_balance") {
            return minutes > 0
                ? qsTr("Пополните счёт, иначе сессия завершится через %1 мин.").arg(minutes)
                : qsTr("Пополните баланс, иначе сессия завершится.")
        }
        if (code === "club_closing") {
            return qsTr("Клуб скоро закроется. Завершите текущий матч.")
        }
        if (code === "session_ending") {
            return qsTr("Сессия скоро завершится.")
        }
        return qsTr("Проверьте состояние сессии.")
    }

    function showHealthNotice(code, notice) {
        if (!notice) {
            return
        }
        var noticeCode = code ? String(code) : String(notice.code || "")
        var action = ""
        var actionText = ""
        if (noticeCode === "low_balance") {
            action = "wallet"
            actionText = qsTr("Пополнить")
        }
        else if (noticeCode === "club_closing" ||
                 noticeCode === "session_ending") {
            action = "session"
            actionText = qsTr("К сессии")
        }
        healthNotice = {
            tone: noticeCode === "session_ending" ? "danger" : "warning",
            title: healthNoticeTitle(noticeCode),
            text: healthNoticeText(noticeCode, notice),
            actionText: actionText
        }
        healthNoticeAction = action
        healthNoticeCode = noticeCode
        if (healthNoticeTimer) {
            healthNoticeTimer.restart()
        }
    }

    // Health-state copy from plan 19, 5.5.1. The server state is authoritative;
    // the banner never invents a billing result.
    function showHealthAction(action, health) {
        if (!health) {
            return
        }
        var state = String(health.state || "")
        if (healthNotice !== null && healthNoticeCode === state) {
            // The server repeats the same state every sample; do not restart
            // the banner on every report.
            return
        }
        var criticalState = state === "broken_user" ||
            state === "broken_club" || state === "both_break" ||
            state === "control_unknown" || state === "stream_stall"
        if (healthNotice !== null && healthNoticeCode !== state && !criticalState) {
            // A low-balance/closing notice is more actionable than a degraded
            // or suspect hint; do not replace it.
            return
        }
        var tone = "danger"
        var title = ""
        var text = ""
        var nextAction = ""
        var actionText = ""
        if (state === "broken_user" || state === "both_break") {
            title = state === "both_break" ? qsTr("Нет связи") : qsTr("Связь потеряна")
            text = state === "both_break"
                ? qsTr("Проверьте интернет. Оплата приостановлена.")
                : qsTr("Пытаемся переподключиться. Оплата приостановлена.")
            nextAction = "reconnect"
            actionText = qsTr("Переподключиться")
        }
        else if (state === "broken_club") {
            title = qsTr("Компьютер не отвечает")
            text = qsTr("Сессия остановлена. Оплата приостановлена.")
            nextAction = "clubs"
            actionText = qsTr("Выбрать клуб")
        }
        else if (state === "control_unknown") {
            tone = "warning"
            title = qsTr("Сервер недоступен")
            text = qsTr("Проверяем состояние. Оплата приостановлена.")
        }
        else if (state === "stream_stall") {
            tone = "warning"
            title = qsTr("Изображение остановилось")
            text = qsTr("Пытаемся восстановить соединение.")
            nextAction = "session"
            actionText = qsTr("К сессии")
        }
        else if (state === "suspect_user") {
            tone = "warning"
            title = qsTr("Проверяем связь")
            text = qsTr("Нет данных от приложения.")
        }
        else if (state === "degraded") {
            tone = "warning"
            title = qsTr("Связь нестабильна")
            text = qsTr("Изображение может тормозить.")
        }
        else if (action === "stop") {
            title = qsTr("Сессия остановлена")
            text = qsTr("Итоговое списание считает сервер.")
            nextAction = "session"
            actionText = qsTr("К сессии")
        }
        if (title.length === 0) {
            return
        }
        healthNotice = {
            tone: tone,
            title: title,
            text: text,
            actionText: actionText
        }
        healthNoticeAction = nextAction
        healthNoticeCode = state
        if (healthNoticeTimer) {
            healthNoticeTimer.restart()
        }
    }

    function dismissHealthNotice() {
        if (healthNoticeTimer) {
            healthNoticeTimer.stop()
        }
        healthNotice = null
        healthNoticeAction = ""
        healthNoticeCode = ""
    }

    function runHealthNoticeAction() {
        var action = healthNoticeAction
        dismissHealthNotice()
        if (action === "wallet") {
            showSection("wallet")
        }
        else if (action === "clubs") {
            showSection("clubs")
        }
        else if (action === "session") {
            showSection("sessions")
        }
        else if (action === "reconnect") {
            if (session && session.id !== undefined) {
                Api.reconnectSession(api, String(session.id))
            }
            showSection("sessions")
        }
    }

    // The window forces focus on the pushed page (main.qml), which would land
    // on this shell root. Restore focus to the topmost screen once the push
    // has settled; only navigation and startup call this.
    function restoreFocus() {
        if (shell.authExpired) {
            authExpiredPrimary.forceActiveFocus()
        }
        else if (shell.wordsVisible && wordsOverlay.item &&
                typeof wordsOverlay.item.focusFirst === "function") {
            wordsOverlay.item.focusFirst()
        }
        else if (shell.reconsentVisible && consentOverlay.item &&
                 typeof consentOverlay.item.focusFirst === "function") {
            consentOverlay.item.focusFirst()
        }
        else if (shell.recoveryVisible && recoveryOverlay.item &&
                 typeof recoveryOverlay.item.focusFirst === "function") {
            recoveryOverlay.item.focusFirst()
        }
        else if (shell.mode === "deletion" && deletionLoader.item &&
                 typeof deletionLoader.item.focusFirst === "function") {
            deletionLoader.item.focusFirst()
        }
        else if (shell.mode === "onboarding" && onboardingLoader.item &&
                 typeof onboardingLoader.item.focusFirst === "function") {
            onboardingLoader.item.focusFirst()
        }
        else if (shell.mode === "startup" && startupLoader.item &&
                 typeof startupLoader.item.focusFirst === "function") {
            startupLoader.item.focusFirst()
        }
        else if (shell.mode === "app") {
            focusContentArea()
        }
    }

    // ---- lifecycle ------------------------------------------------------

    Component.onCompleted: {
        if (mode === "app") {
            primeApp()
        }
        syncWords()
        Qt.callLater(shell.restoreFocus)
    }

    onModeChanged: {
        if (mode === "app") {
            wordsVisible = false
            recoveryVisible = false
            primeApp()
        }
        else {
            pageHistory = []
            pageSource = ""
            wordsVisible = false
            recoveryVisible = false
            dismissHealthNotice()
            if (mode === "onboarding") {
                syncWords()
            }
        }
    }

    onAppStateChanged: {
        syncWords()
        if (authExpired) {
            Qt.callLater(shell.restoreFocus)
        }
    }
    onOnboardingStepChanged: syncWords()

    onQueueEntryChanged: {
        var status = queueEntry ? String(queueEntry.status || "") : ""
        if (status === "notified" && lastQueueStatus !== "notified" &&
                shell.notifications) {
            shell.notifications.notifySuccess(
                qsTr("Слот готов"),
                qsTr("Можно подключаться к компьютеру. Слот ждёт подтверждения."),
                "",
                function() {
                    shell.navigate("qrc:/gui/legion/QueueView.qml", {})
                },
                "popup")
        }
        lastQueueStatus = status
    }

    Connections {
        target: shell.api
        ignoreUnknownSignals: true

        function onResponse(op, data) {
            if (op === "clubs") {
                shell.catalogLoading = false
                shell.catalogInFlight = false
                shell.catalog = Api.clubsOf(data)
                // Age is measured from the local receipt time, not the server
                // clock: a small clock skew must not mark fresh data stale.
                shell.catalogAt = new Date().toISOString()
                shell.catalogFetchedAt = Date.now()
                shell.catalogErrorCode = ""
                shell.catalogErrorMessage = ""
                if (shell.catalogDirty) {
                    shell.catalogDirty = false
                    shell.loadCatalog(false, true)
                }
            }
            else if (op === "wallet") {
                shell.wallet = Api.walletClubsOf(data)
                shell.walletInFlight = false
                shell.walletFetchedAt = Date.now()
                if (shell.walletDirty) {
                    shell.walletDirty = false
                    shell.refreshWallet(true)
                }
            }
            else if (op === "club") {
                var updatedClub = Api.clubOf(data)
                if (updatedClub) {
                    var clubSlug = updatedClub.slug ? String(updatedClub.slug) : ""
                    if (clubSlug.length > 0) {
                        shell.clubInFlight[clubSlug] = false
                        shell.clubFetchedAt[clubSlug] = Date.now()
                    }
                    else {
                        shell.clubInFlight = ({})
                        shell.clubDirty = ({})
                    }
                    shell.updateClub(updatedClub)
                    if (clubSlug.length > 0 && shell.clubDirty[clubSlug]) {
                        shell.clubDirty[clubSlug] = false
                        shell.refreshClub(clubSlug, true)
                    }
                }
            }
            else if (op === "session_current") {
                if (shell.sessionReadMutationSeq !== shell.sessionMutationSeq) {
                    // A start/stop/reconnect/claim completed after this read
                    // started. Its payload is authoritative; do not let the
                    // older snapshot overwrite it. The read is now settled,
                    // so clear the in-flight guard without scheduling a
                    // refresh that could fail and preserve stale data.
                    shell.sessionInFlight = false
                    shell.sessionDirty = false
                    return
                }
                shell.sessionInFlight = false
                shell.sessionFetchedAt = Date.now()
                var previousSessionStatus = shell.session ? String(shell.session.status || "") : ""
                shell.session = Api.sessionOf(data)
                if (shell.session === null) {
                    shell.dismissHealthNotice()
                }
                var nextSessionStatus = shell.session ? String(shell.session.status || "") : ""
                if (previousSessionStatus !== nextSessionStatus &&
                        (nextSessionStatus === "" || nextSessionStatus === "expired" ||
                         nextSessionStatus === "stopped")) {
                    // The lease ended server-side (expiry, zero balance or
                    // revocation). Disconnect rewards and the summary are now
                    // stale even though no page mutation reported it.
                    shell.refreshAccountModels(false)
                }
                if (shell.sessionDirty) {
                    shell.sessionDirty = false
                    shell.refreshSession(true)
                }
            }
            else if (op === "session_start" || op === "session_stop" ||
                     op === "session_reconnect") {
                shell.sessionMutationSeq += 1
                shell.sessionDirty = false
                shell.sessionFetchedAt = Date.now()
                shell.session = op === "session_stop" ? null : Api.sessionOf(data)
                if (op === "session_stop") {
                    // The lease is gone: tear down its NetBird profile. The
                    // connect material is cleared by LegionApi for the stopped
                    // lease only.
                    shell.dismissHealthNotice()
                    ComputerManager.stopLegionConnect()
                }
                // If a read is in flight, keep its guard until the reply
                // arrives; that reply will be discarded by the sequence
                // check above.
                shell.loadCatalog(false, true)
                shell.refreshWallet(true)
                shell.refreshAccountModels(true)
            }
            else if (op === "session_connect") {
                // The payload itself is handled by onConnectMaterialReady, not
                // by this broadcast router: a page cannot consume the one-shot
                // material even if it is the only listener left. The material
                // does not change rewards yet; activation does, and is picked
                // up by onLegionConnectSucceeded below. This cheap guarded read
                // keeps the cards fresh if activation already happened.
                shell.refreshAccountModels(false)
            }
            else if (op === "queue") {
                if (shell.queueReadMutationSeq !== shell.queueMutationSeq) {
                    // A join/leave/claim completed after this read started.
                    // Keep the mutation result and discard the old snapshot.
                    shell.queueInFlight = false
                    shell.queueDirty = false
                    return
                }
                shell.queueInFlight = false
                shell.queuePollInterval = 5000
                shell.queueFetchedAt = Date.now()
                var previousQueueStatus = shell.queueEntry ? String(shell.queueEntry.status || "") : ""
                shell.queueEntry = Api.entryOf(data)
                var nextQueueStatus = shell.queueEntry ? String(shell.queueEntry.status || "") : ""
                if (previousQueueStatus !== nextQueueStatus &&
                        (nextQueueStatus === "" || nextQueueStatus === "notified" ||
                         nextQueueStatus === "claimed" || nextQueueStatus === "expired" ||
                         nextQueueStatus === "left")) {
                    // Promotion, expiry or removal discovered by polling instead
                    // of SSE. Queue rewards and the summary may have changed.
                    shell.refreshAccountModels(false)
                }
                if (shell.queueDirty) {
                    shell.queueDirty = false
                    shell.refreshQueue(true)
                }
            }
            else if (op === "queue_join" || op === "queue_leave") {
                shell.queueMutationSeq += 1
                shell.queueDirty = false
                shell.queueFetchedAt = Date.now()
                shell.queueEntry = Api.entryOf(data)
                shell.loadCatalog(false, true)
                shell.refreshAccountModels(true)
            }
            else if (op === "queue_claim") {
                // Claim creates a session and clears the queue entry in one
                // server transaction. Treat both projections as mutations.
                shell.queueMutationSeq += 1
                shell.sessionMutationSeq += 1
                shell.queueDirty = false
                shell.sessionDirty = false
                shell.queueFetchedAt = Date.now()
                shell.queueEntry = null
                shell.sessionFetchedAt = Date.now()
                shell.session = Api.sessionOf(data)
                shell.loadCatalog(false, true)
                shell.refreshWallet(true)
                shell.refreshAccountModels(true)
            }
            else if (op === "intent") {
                var intentStatus = Api.intentStatusOf(data)
                if (intentStatus === "succeeded" || intentStatus === "refunded") {
                    // A confirmed deposit credits the wallet and emits the
                    // add_funds/referral rewards server-side. PaymentView owns
                    // the result screen; the shell owns the shared caches.
                    shell.refreshWallet(true)
                    shell.refreshAccountModels(true)
                }
            }
            else if (op === "session_history") {
                shell.sessionHistory = (data && data.sessions) ? data.sessions : []
                shell.sessionHistoryInFlight = false
                shell.sessionHistoryLoading = false
                shell.sessionHistoryFetchedAt = Date.now()
                shell.sessionHistoryError = ""
            }
            else if (op === "gamification") {
                shell.gamification = data ? data : ({})
                shell.gamificationInFlight = false
                shell.gamificationFetchedAt = Date.now()
                shell.gamificationError = ""
            }
            else if (op === "account_summary") {
                shell.accountSummary = data ? data : ({})
                shell.summaryInFlight = false
                shell.summaryFetchedAt = Date.now()
                shell.summaryError = ""
            }
            else if (op === "account_restore") {
                // The restore response already carries fresh tokens. Refresh
                // every projection instead of trusting a local timer.
                shell.refreshAll(true)
                shell.refreshAccountModels(true)
            }
        }

        function onFailed(op, code, message) {
            var sessionReadStale = false
            if (op === "clubs") {
                shell.catalogLoading = false
                shell.catalogInFlight = false
                shell.catalogDirty = false
                shell.catalogErrorCode = code ? String(code) : "server_error"
                shell.catalogErrorMessage = message ? String(message) : ""
            }
            else if (op === "club") {
                shell.clubInFlight = ({})
                shell.clubDirty = ({})
            }
            else if (op === "wallet") {
                shell.walletInFlight = false
                shell.walletDirty = false
            }
            else if (op === "session_current") {
                // A 404 from a read that started before a start/claim/stop
                // mutation is stale; the mutation payload is authoritative.
                sessionReadStale = shell.sessionReadMutationSeq !== shell.sessionMutationSeq
                shell.sessionInFlight = false
                shell.sessionDirty = false
            }
            else if (op === "queue") {
                shell.queueInFlight = false
                shell.queueDirty = false
                shell.queuePollInterval = Math.min(shell.queuePollInterval * 2, 30000)
            }
            else if (op === "session_history") {
                shell.sessionHistoryInFlight = false
                shell.sessionHistoryLoading = false
                shell.sessionHistoryError = code ? String(code) : "server_error"
            }
            else if (op === "gamification") {
                shell.gamificationInFlight = false
                shell.gamificationError = code ? String(code) : "server_error"
            }
            else if (op === "account_summary") {
                shell.summaryInFlight = false
                shell.summaryError = code ? String(code) : "server_error"
            }
            if (op === "session_connect" && code === "connect_material_consumed") {
                // The server will not reissue the material for this lease.
                // Clear local state and end the paid lease instead of leaving
                // the user with a session they cannot enter.
                Api.clearConnectMaterial(shell.api)
                var lostSession = shell.session
                if (lostSession && lostSession.id !== undefined) {
                    Api.stopSession(shell.api, String(lostSession.id))
                }
                if (shell.notifications) {
                    shell.notifications.notifyError(
                        "connect_material_consumed",
                        qsTr("Данные подключения уже использованы. Сессия завершена."),
                        "global")
                }
            }
            if (op === "session_current" && code === "not_found" && !sessionReadStale) {
                // No active session is a normal answer, not an error. If one
                // just disappeared, its disconnect rewards are now pending.
                var hadSession = shell.session !== null
                shell.session = null
                shell.sessionFetchedAt = Date.now()
                if (hadSession) {
                    shell.refreshAccountModels(false)
                }
            }
            else if (op === "queue" && code === "not_found") {
                var hadQueueEntry = shell.queueEntry !== null
                shell.queueEntry = null
                shell.queueFetchedAt = Date.now()
                if (hadQueueEntry) {
                    shell.refreshAccountModels(false)
                }
            }
        }

        function onLastErrorChanged() {
            if (shell.mode !== "app") {
                return
            }
            if (shell.notifications &&
                    (Notice.isDjangoDown(shell.lastErrorCode) ||
                     shell.lastErrorCode === "client_update_required" ||
                     shell.lastErrorCode === "rate_limited")) {
                shell.notifications.notifyError(
                    shell.lastErrorCode, shell.lastError, "global")
            }
        }

        function onSessionHealthNotice(code, notice) {
            if (shell.mode !== "app" || !shell.api || !shell.api.leaseId) {
                // A notice queued while the stream was running may be
                // delivered after stop cleared the lease; do not show it.
                return
            }
            shell.showHealthNotice(code, notice)
        }

        function onSessionHealthAction(action, health) {
            if (shell.mode !== "app" || !shell.api || !shell.api.leaseId) {
                return
            }
            shell.showHealthAction(action, health)
        }

        function onQueueEvent(name, data) {
            if (shell.mode !== "app") {
                return
            }
            if (name === "connected" || name === "heartbeat") {
                return
            }
            if (name === "resync") {
                // A reconnect may have missed hints. Refresh everything from
                // the authoritative endpoints instead of replaying events.
                shell.refreshAll(true)
                shell.refreshAccountModels(true)
                return
            }
            // SSE is a hint to refresh the authoritative projections. Never
            // mutate local session/queue state from the event payload.
            shell.refreshQueue(true)
            if (name === "promoted" || name === "claimed" ||
                    name === "expired" || name === "left") {
                shell.refreshSession(true)
                shell.refreshWallet(true)
                shell.refreshAccountModels(true)
            }
        }

        function onWordsReady(handle, words) {
            shell.wordsHandle = handle ? String(handle) : shell.handle
            shell.wordsList = words ? words : []
            shell.wordsVisible = shell.wordsList.length > 0
        }

        function onConnectMaterialReady(sessionId, data) {
            // Persistent owner of the one-shot connect command: the issuing
            // page may already be gone, but the shell lives for the whole app.
            if (!data) {
                return
            }
            if (ComputerManager.legionConnectBusy()) {
                // The live flow already holds this material.
                return
            }
            ComputerManager.startLegionConnectWithData(data)
        }

        function onRecoveryKitWritten(path, syncedFolder) {
            shell.kitPath = path ? String(path) : ""
            shell.kitInSyncedFolder = syncedFolder === true
        }

        function onConsentRequired() {
            shell.reconsentVisible = true
        }

        function onPaymentReturned(publicId) {
            if (!publicId || shell.mode !== "app") {
                return
            }
            // The deep link is a hint: open the wait screen and let it poll.
            shell.navigate("qrc:/gui/legion/PaymentView.qml", { publicId: String(publicId) })
        }

        function onSignOutStarted() {
            // LegionApi stops the lease it tracks. If the app has a newer
            // session snapshot than the backend (for example immediately after
            // a restart), stop that lease too; the server stop is idempotent.
            if (shell.api && shell.api.leaseId) {
                return
            }
            if (shell.session && shell.session.id !== undefined) {
                Api.stopSession(shell.api, String(shell.session.id))
            }
        }

        function onStateChanged() {
            if (shell.appState === "signed_in") {
                shell.reconsentVisible = false
                shell.recoveryVisible = false
            }
            else if (shell.appState === "session_expired") {
                // Terminal auth loss: the lease cannot be stopped with an
                // invalid token, but the local NetBird profile must go.
                ComputerManager.stopLegionConnect()
            }
        }
    }

    Connections {
        target: ComputerManager
        ignoreUnknownSignals: true

        function onLegionConnectSucceeded() {
            // The material has been consumed by the server and applied by the
            // client; nothing to resume. Activation starts billing and emits
            // the connection_activated rewards, so the cards must re-read.
            Api.clearConnectMaterial(shell.api)
            shell.refreshSession(true)
            shell.refreshWallet(true)
            shell.refreshAccountModels(true)
        }
    }

    // Foreground reconciliation. `windowVisible` catches minimize/restore and
    // `windowActive` catches focus loss/return (for example, the payment
    // browser). Both go through the TTL guards, so a quick alt-tab is free.
    onWindowVisibleChanged: {
        if (windowVisible) {
            shell.refreshOnResume()
        }
    }

    onWindowActiveChanged: {
        if (windowActive) {
            shell.refreshOnResume()
        }
    }

    Timer {
        id: sessionTimer
        interval: 20000
        repeat: true
        running: shell.mode === "app" && shell.windowVisible
        triggeredOnStart: false
        onTriggered: shell.refreshSession()
    }

    Timer {
        id: healthNoticeTimer
        interval: 12000
        repeat: false
        onTriggered: shell.dismissHealthNotice()
    }

    Timer {
        id: queueTimer
        interval: shell.queuePollInterval
        repeat: true
        running: shell.mode === "app" && shell.windowVisible &&
                 shell.queueEntry !== null &&
                 (shell.queueEntry.status === "waiting" || shell.queueEntry.status === "notified")
        triggeredOnStart: false
        onTriggered: shell.refreshQueue()
    }

    // ---- product shell --------------------------------------------------

    Row {
        id: appRow
        anchors.fill: parent
        visible: shell.mode === "app"

        LegionNavRail {
            id: rail
            height: parent.height
            current: shell.section
            handle: shell.handle
            sessionActive: shell.session !== null && shell.session.status === "active"
            qaEnabled: shell.qaEnabled
            queueReady: shell.queueEntry !== null && shell.queueEntry.status === "notified"
            queuePosition: (shell.queueEntry !== null && shell.queueEntry.status === "waiting")
                           ? Number(shell.queueEntry.position || 0) : 0

            onNavigate: {
                shell.showSection(key)
            }

            onContentRequested: shell.focusContentArea()
        }

        Item {
            id: contentHost
            width: parent.width - rail.width
            height: parent.height

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Loader {
                        id: pageLoader
                        anchors.fill: parent
                        source: shell.pageSource
                        onLoaded: shell.wirePage(item, false)
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: shell.pageSource.length === 0
                        visible: running
                    }
                }
            }
        }
    }

    // ---- notifications --------------------------------------------------

    NoticeBanner {
        id: healthBanner
        anchors.horizontalCenter: contentHost.horizontalCenter
        anchors.bottom: contentHost.bottom
        anchors.bottomMargin: Theme.spaceL
        width: Math.min(560, contentHost.width - Theme.spaceXl * 2)
        z: 40
        notice: shell.healthNotice
        onDismissed: shell.dismissHealthNotice()
        onActionClicked: shell.runHealthNoticeAction()
    }

    NotificationCenter {
        id: notificationCenter
        anchors.fill: parent
        shell: shell
        z: 45
        backgroundSource: shell.mode === "app" ? appRow
                         : (shell.mode === "onboarding" ? onboardingLoader
                         : (shell.mode === "deletion" ? deletionLoader : null))
        retryAction: function() {
            Api.retryFailedStep(shell.api)
        }
    }

    // ---- startup gate ---------------------------------------------------

    Loader {
        id: startupLoader
        anchors.fill: parent
        active: shell.mode === "startup" && !shell.authExpired
        visible: active
        source: "qrc:/gui/legion/StartupGateView.qml"
        onLoaded: {
            item.shell = shell
            item.activate()
        }
    }

    // ---- terminal auth loss ---------------------------------------------
    // The stored session expired or was revoked. The device key is still in
    // the vault, so login is a ceremony without a password; recovery is the
    // fallback when the device or key is gone. The app UI stays closed.
    Item {
        id: authExpiredGate
        anchors.fill: parent
        visible: shell.authExpired
        z: 44

        Rectangle {
            anchors.fill: parent
            color: Theme.bg
        }

        NeonFrame {
            width: Math.min(parent.width - Theme.spaceXxl * 2, 720)
            height: Math.min(authExpiredColumn.implicitHeight + Theme.spaceXxl * 2,
                             parent.height - Theme.spaceXxl * 2)
            anchors.centerIn: parent
            highlighted: true
            flicker: true

            ColumnLayout {
                id: authExpiredColumn
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
                    text: qsTr("Сессия истекла")
                    color: Theme.text
                    font.pointSize: Theme.fontHero
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Войдите снова на этом ПК. Ключ устройства сохранён, пароль не нужен. " +
                               "Если устройство потеряно, восстановите доступ по восьми словам.")
                    color: Theme.textMuted
                    font.pointSize: Theme.fontBody
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: shell.lastError.length > 0
                    text: Api.errorText(shell.lastErrorCode, shell.lastError)
                    color: Theme.warning
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceM

                    Item { Layout.fillWidth: true }

                    LegionButton {
                        kind: "secondary"
                        text: qsTr("Восстановить доступ")
                        onClicked: shell.recoveryVisible = true
                    }

                    LegionButton {
                        kind: "ghost"
                        text: qsTr("Выйти")
                        onClicked: Api.logout(shell.api)
                    }

                    LegionButton {
                        id: authExpiredPrimary
                        kind: "primary"
                        text: qsTr("Войти снова")
                        onClicked: Api.login(shell.api)
                    }
                }
            }
        }
    }

    // ---- deletion grace period -----------------------------------------

    Loader {
        id: deletionLoader
        anchors.fill: parent
        active: shell.mode === "deletion"
        visible: active
        source: "qrc:/gui/legion/DeletionPendingView.qml"
        onLoaded: {
            item.shell = shell
            item.activate()
        }
    }

    // ---- onboarding -----------------------------------------------------

    Loader {
        id: onboardingLoader
        anchors.fill: parent
        active: shell.mode === "onboarding"
        visible: active
        source: "qrc:/gui/legion/OnboardingView.qml"
        onLoaded: {
            item.shell = shell
            item.purpose = "register"
            item.activate()
        }
    }

    // ---- overlays -------------------------------------------------------

    Loader {
        id: wordsOverlay
        anchors.fill: parent
        active: shell.wordsVisible
        visible: active
        z: 60
        source: "qrc:/gui/legion/RecoveryWordsView.qml"
        onLoaded: {
            item.shell = shell
            item.activate()
        }
    }

    Loader {
        id: recoveryOverlay
        anchors.fill: parent
        active: shell.recoveryVisible
        visible: active
        z: 55
        source: "qrc:/gui/legion/RecoveryView.qml"
        onLoaded: {
            item.shell = shell
            item.activate()
        }
    }

    Loader {
        id: consentOverlay
        anchors.fill: parent
        active: shell.reconsentVisible
        visible: active
        z: 58
        source: "qrc:/gui/legion/OnboardingView.qml"
        onLoaded: {
            item.shell = shell
            item.purpose = "reconsent"
            item.activate()
        }
    }

    // Escape closes the app-level detail page before falling through to the
    // window (which opens the quit confirmation at the root).
    Keys.onEscapePressed: {
        if (shell.wordsVisible) {
            event.accepted = true
            return
        }
        if (shell.recoveryVisible) {
            shell.recoveryVisible = false
            event.accepted = true
            return
        }
        if (shell.reconsentVisible) {
            event.accepted = true
            return
        }
        if (shell.pageHistory.length > 0) {
            shell.back()
            event.accepted = true
        }
    }

    Keys.onMenuPressed: {
        if (shell.mode !== "app") {
            return
        }
        if (shell.focusIsInRail()) {
            shell.focusContentArea()
        }
        else {
            rail.forceActiveFocus()
            rail.moveFocus(0)
        }
        event.accepted = true
    }
}
