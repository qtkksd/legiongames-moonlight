import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Notice.js" as Notice
import "Theme.js" as Theme

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

    // True once the account bootstrap has reached a terminal or onboarding
    // state. The splash waits for this instead of flashing the checking gate.
    readonly property bool startupSettled: appState.length > 0 &&
                                           appState !== "checking"

    // app | onboarding | startup
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

    // The window forces focus on the pushed page (main.qml), which would land
    // on this shell root. Restore focus to the topmost screen once the push
    // has settled; only navigation and startup call this.
    function restoreFocus() {
        if (shell.wordsVisible && wordsOverlay.item &&
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
            if (mode === "onboarding") {
                syncWords()
            }
        }
    }

    onAppStateChanged: syncWords()
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
                shell.session = Api.sessionOf(data)
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
                // If a read is in flight, keep its guard until the reply
                // arrives; that reply will be discarded by the sequence
                // check above.
                shell.loadCatalog(false, true)
                shell.refreshWallet(true)
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
                shell.queueEntry = Api.entryOf(data)
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
            }
            else if (op === "session_history") {
                shell.sessionHistory = (data && data.sessions) ? data.sessions : []
                shell.sessionHistoryInFlight = false
                shell.sessionHistoryLoading = false
                shell.sessionHistoryFetchedAt = Date.now()
                shell.sessionHistoryError = ""
            }
        }

        function onFailed(op, code, message) {
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
            if (op === "session_current" && code === "not_found") {
                // No active session is a normal answer, not an error.
                shell.session = null
                shell.sessionFetchedAt = Date.now()
            }
            else if (op === "queue" && code === "not_found") {
                shell.queueEntry = null
                shell.queueFetchedAt = Date.now()
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
                return
            }
            // SSE is a hint to refresh the authoritative projections. Never
            // mutate local session/queue state from the event payload.
            shell.refreshQueue(true)
            if (name === "promoted" || name === "claimed" ||
                    name === "expired" || name === "left") {
                shell.refreshSession(true)
                shell.refreshWallet(true)
            }
        }

        function onWordsReady(handle, words) {
            shell.wordsHandle = handle ? String(handle) : shell.handle
            shell.wordsList = words ? words : []
            shell.wordsVisible = shell.wordsList.length > 0
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

        function onStateChanged() {
            if (shell.appState === "signed_in") {
                shell.reconsentVisible = false
                shell.recoveryVisible = false
            }
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

    NotificationCenter {
        id: notificationCenter
        anchors.fill: parent
        shell: shell
        z: 45
        backgroundSource: shell.mode === "app" ? appRow
                         : (shell.mode === "onboarding" ? onboardingLoader : null)
        retryAction: function() {
            Api.retryFailedStep(shell.api)
        }
    }

    // ---- startup gate ---------------------------------------------------

    Loader {
        id: startupLoader
        anchors.fill: parent
        active: shell.mode === "startup"
        visible: active
        source: "qrc:/gui/legion/StartupGateView.qml"
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
