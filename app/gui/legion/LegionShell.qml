import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
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

    // Shared server snapshots. Views read these instead of fetching twice.
    property var session: null
    property var queueEntry: null
    property var wallet: []
    property var catalog: []
    property string catalogAt: ""
    property bool catalogLoading: false
    property string catalogErrorCode: ""
    property string catalogErrorMessage: ""

    // Cache-first data. Views render the last snapshot immediately and only
    // revalidate after the short TTL. Mutations use force=true to invalidate.
    // Availability changes quickly at a club. Keep the display window short
    // (the server still re-checks every mutation); a long club TTL made the
    // list and the detail page disagree for up to 45 seconds.
    readonly property int catalogCacheTtlMs: 5000
    readonly property int clubCacheTtlMs: 10000
    readonly property int walletCacheTtlMs: 10000
    readonly property int sessionCacheTtlMs: 5000
    readonly property int queueCacheTtlMs: 3000

    property double catalogFetchedAt: 0
    property var clubFetchedAt: ({})
    property var clubInFlight: ({})
    property var clubDirty: ({})
    property double walletFetchedAt: 0
    property double sessionFetchedAt: 0
    property double queueFetchedAt: 0
    property bool catalogInFlight: false
    property bool walletInFlight: false
    property bool sessionInFlight: false
    property bool queueInFlight: false
    property bool catalogDirty: false
    property bool walletDirty: false
    property bool sessionDirty: false
    property bool queueDirty: false

    // A current-session/queue read is a snapshot. If a mutation completes
    // while the read is in flight, the mutation is newer and the snapshot
    // must be discarded even if a follow-up refresh fails. The read captures
    // the mutation sequence at start; each mutation completion bumps it.
    property int sessionMutationSeq: 0
    property int sessionReadMutationSeq: 0
    property int queueMutationSeq: 0
    property int queueReadMutationSeq: 0

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
        // The app keeps one full catalog and filters it locally. The
        // availableOnly argument is kept for call-site compatibility.
        if (catalogInFlight) {
            if (force) {
                catalogDirty = true
            }
            return false
        }
        if (!force && catalogFetchedAt > 0 &&
                (Date.now() - catalogFetchedAt) < catalogCacheTtlMs) {
            return false
        }
        catalogInFlight = true
        catalogLoading = true
        catalogErrorCode = ""
        catalogErrorMessage = ""
        if (!Api.getClubs(api, false)) {
            catalogInFlight = false
            catalogLoading = false
            catalogErrorCode = "offline"
            catalogErrorMessage = ""
            return false
        }
        return true
    }

    function clubFetchedAtMs(slug) {
        if (!slug) {
            return 0
        }
        var value = clubFetchedAt[String(slug)]
        return value ? Number(value) : 0
    }

    function refreshClub(slug, force) {
        var key = slug ? String(slug).trim() : ""
        if (key.length === 0) {
            return false
        }
        if (clubInFlight[key] === true) {
            if (force) {
                clubDirty[key] = true
            }
            return false
        }
        var fetchedAt = clubFetchedAtMs(key)
        if (!force && fetchedAt > 0 &&
                (Date.now() - fetchedAt) < clubCacheTtlMs) {
            return false
        }
        clubInFlight[key] = true
        if (!Api.getClub(api, key)) {
            clubInFlight[key] = false
            return false
        }
        return true
    }

    function refreshWallet(force) {
        if (walletInFlight) {
            if (force) {
                walletDirty = true
            }
            return false
        }
        if (!force && walletFetchedAt > 0 &&
                (Date.now() - walletFetchedAt) < walletCacheTtlMs) {
            return false
        }
        walletInFlight = true
        if (!Api.getWallet(api)) {
            walletInFlight = false
            return false
        }
        return true
    }

    function refreshWalletIfStale() {
        return refreshWallet(false)
    }

    function refreshSession(force) {
        if (sessionInFlight) {
            if (force) {
                sessionDirty = true
            }
            return false
        }
        if (!force && sessionFetchedAt > 0 &&
                (Date.now() - sessionFetchedAt) < sessionCacheTtlMs) {
            return false
        }
        sessionReadMutationSeq = sessionMutationSeq
        sessionInFlight = true
        if (!Api.getCurrentSession(api)) {
            sessionInFlight = false
            return false
        }
        return true
    }

    function refreshQueue(force) {
        if (queueInFlight) {
            if (force) {
                queueDirty = true
            }
            return false
        }
        if (!force && queueFetchedAt > 0 &&
                (Date.now() - queueFetchedAt) < queueCacheTtlMs) {
            return false
        }
        queueReadMutationSeq = queueMutationSeq
        queueInFlight = true
        if (!Api.getQueue(api)) {
            queueInFlight = false
            return false
        }
        return true
    }

    function refreshAll(force) {
        loadCatalog(false, force === true)
        refreshWallet(force === true)
        refreshSession(force === true)
        refreshQueue(force === true)
    }

    function clubById(id) {
        return Api.clubById(catalog, id)
    }

    function clubName(id) {
        return Api.clubName(catalog, id)
    }

    // Replace one catalog row with a fresher server payload. The home and
    // clubs views read `catalog`, so a detail fetch immediately updates the
    // free-PC count everywhere instead of leaving a stale list behind.
    function updateClub(updated) {
        if (!updated || updated.id === undefined) {
            return
        }
        var next = []
        var replaced = false
        for (var i = 0; i < catalog.length; i++) {
            if (String(catalog[i].id) === String(updated.id)) {
                next.push(updated)
                replaced = true
            }
            else {
                next.push(catalog[i])
            }
        }
        if (!replaced) {
            next.push(updated)
        }
        catalog = next
    }

    function walletFor(clubId) {
        return Api.walletFor(wallet, clubId)
    }

    function balanceFor(clubId) {
        return Api.balanceFor(wallet, clubId)
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
        interval: 5000
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

                Column {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spaceXl
                    Layout.rightMargin: Theme.spaceXl
                    Layout.topMargin: Theme.spaceL
                    spacing: Theme.spaceS

                    ErrorBanner {
                        width: parent.width
                        visible: shell.appState === "offline" || shell.lastErrorCode === "offline"
                        text: qsTr("Нет соединения. Проверьте интернет.")
                        detail: qsTr("Данные на экране могли устареть.")
                        canRetry: true
                        onRetry: Api.retryFailedStep(shell.api)
                    }

                    ErrorBanner {
                        width: parent.width
                        visible: shell.lastErrorCode === "client_update_required"
                        text: qsTr("Обновите приложение, чтобы продолжить.")
                        detail: qsTr("Текущая версия больше не поддерживается сервером.")
                        canRetry: false
                    }

                    ErrorBanner {
                        width: parent.width
                        visible: shell.lastErrorCode === "unauthenticated" ||
                                 shell.lastErrorCode === "device_revoked" ||
                                 shell.lastErrorCode === "consent_version_mismatch"
                        text: Api.errorText(shell.lastErrorCode, shell.lastError)
                        detail: ""
                        canRetry: shell.lastErrorCode !== "device_revoked"
                        onRetry: Api.retryFailedStep(shell.api)
                    }
                }

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
