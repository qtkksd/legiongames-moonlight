import QtQuick 2.9
import QtQuick.Controls 2.2

import "Api.js" as Api
import "Theme.js" as Theme

// Base item for every LegionGames page.
//
// It resolves the `legionApi` context property defensively and exposes the
// shared server state as read-only properties, so views never guess a name
// that is not in notes/build-interfaces.md section 4.
Item {
    id: view

    // Set by LegionShell after the page is loaded.
    property var shell: null
    property var params: ({})

    // Snapshot of the account backend. `typeof` on an undeclared identifier is
    // safe in QML; the try/catch covers engines that resolve it eagerly.
    readonly property var api: {
        try {
            return (typeof legionApi !== "undefined") ? legionApi : null
        }
        catch (error) {
            return null
        }
    }

    readonly property bool apiAvailable: api !== null

    // legionApi properties (exact names from the build interface).
    readonly property string appState: api ? (api.state || "") : ""
    readonly property string handle: api ? (api.handle || "") : ""
    readonly property string onboardingStep: api ? (api.onboardingStep || "") : ""
    readonly property bool serverBusy: api ? (api.busy === true) : false
    readonly property string serverError: api ? (api.lastError || "") : ""
    readonly property string serverErrorCode: api ? (api.lastErrorCode || "") : ""
    readonly property var words: api ? api.words : []
    readonly property string recoveryKitPath: api ? (api.recoveryKitPath || "") : ""
    readonly property string catalogUpdatedAt: api ? (api.catalogUpdatedAt || "") : ""
    readonly property bool catalogStale: Api.available(api) && Theme.isStale(catalogUpdatedAt)

    // Local error surfaced by this page (a failed(op) routed here).
    property string errorCode: ""
    property string errorMessage: ""
    property string errorOp: ""

    readonly property bool hasError: errorCode.length > 0 || errorMessage.length > 0
    readonly property string errorText: hasError
                                        ? Api.errorTextFor(errorOp, errorCode, errorMessage)
                                        : ""

    // Overridden by concrete pages.
    function activate() {
    }

    function deactivate() {
    }

    function focusFirst() {
        forceActiveFocus()
    }

    function showError(code, message, op) {
        errorCode = code ? String(code) : ""
        errorMessage = message ? String(message) : ""
        errorOp = op ? String(op) : ""
    }

    function clearError() {
        errorCode = ""
        errorMessage = ""
        errorOp = ""
    }

    function openUrl(url) {
        if (!url) {
            return
        }
        if (!Api.openExternal(api, url)) {
            Qt.openUrlExternally(url)
        }
    }

    function go(url, pageParams) {
        if (shell) {
            shell.navigate(url, pageParams)
        }
    }

    function goBack() {
        if (shell) {
            shell.back()
        }
    }

    function goSection(section) {
        if (shell) {
            shell.showSection(section)
        }
    }
}
