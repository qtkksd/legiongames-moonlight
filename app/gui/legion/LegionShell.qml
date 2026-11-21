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

    function loadCatalog(availableOnly) {
        catalogLoading = true
        catalogErrorCode = ""
        catalogErrorMessage = ""
        if (!Api.getClubs(api, availableOnly === true)) {
            catalogLoading = false
            catalogErrorCode = "offline"
            catalogErrorMessage = ""
        }
    }

    function refreshWallet() {
        Api.getWallet(api)
    }

    function refreshSession() {
        Api.getCurrentSession(api)
    }

    function refreshQueue() {
        Api.getQueue(api)
    }

    function refreshAll() {
        loadCatalog(false)
        refreshWallet()
        refreshSession()
        refreshQueue()
    }

    function clubById(id) {
        return Api.clubById(catalog, id)
    }

    function clubName(id) {
        return Api.clubName(catalog, id)
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
        refreshAll()
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

    // ---- lifecycle ------------------------------------------------------

    Component.onCompleted: {
        if (mode === "app") {
            primeApp()
        }
        syncWords()
    }

    onModeChanged: {
        if (mode === "app") {
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

    Connections {
        target: shell.api
        ignoreUnknownSignals: true

        function onResponse(op, data) {
            if (op === "clubs") {
                shell.catalogLoading = false
                shell.catalog = Api.clubsOf(data)
                shell.catalogAt = Api.updatedAtOf(data)
                shell.catalogErrorCode = ""
                shell.catalogErrorMessage = ""
            }
            else if (op === "wallet") {
                shell.wallet = Api.walletClubsOf(data)
            }
            else if (op === "session_current" || op === "session_start" ||
                     op === "session_stop" || op === "session_reconnect") {
                shell.session = Api.sessionOf(data)
            }
            else if (op === "queue" || op === "queue_join" || op === "queue_leave") {
                shell.queueEntry = Api.entryOf(data)
            }
            else if (op === "queue_claim") {
                shell.queueEntry = null
                shell.session = Api.sessionOf(data)
            }
        }

        function onFailed(op, code, message) {
            if (op === "clubs") {
                shell.catalogLoading = false
                shell.catalogErrorCode = code ? String(code) : "server_error"
                shell.catalogErrorMessage = message ? String(message) : ""
            }
            else if (op === "session_current" && code === "not_found") {
                // No active session is a normal answer, not an error.
                shell.session = null
            }
            else if (op === "queue" && code === "not_found") {
                shell.queueEntry = null
            }
        }

        function onWordsReady(handle, words) {
            shell.wordsHandle = handle ? String(handle) : shell.handle
            shell.wordsList = words ? words : []
            shell.wordsVisible = shell.wordsList.length > 0
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
                shell.focusContentArea()
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
