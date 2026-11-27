import QtQuick 2.9
import QtQuick.Controls 2.2

import "Notice.js" as Notice
import "Theme.js" as Theme

// Global notification host. Page-local errors stay in ErrorBanner; this host
// handles app-wide conditions (Django/network/rate limit), modal decisions
// and positive notices such as queue ready or a successful top-up.
Item {
    id: center

    property var banner: null
    property var popup: null
    property var bannerQueue: []
    property var popupQueue: []
    property var retryAction: null
    property var shell: null
    property var backgroundSource: null
    property int nextId: 0

    visible: shell !== null && (shell.mode === "app" || shell.mode === "onboarding")

    Connections {
        target: center.shell
        ignoreUnknownSignals: true
        function onModeChanged() {
            if (!center.shell || center.shell.mode === "startup") {
                center.clear()
            }
        }
    }

    onPopupChanged: {
        if (center.popup) {
            popupView.open()
        }
        else {
            popupView.close()
        }
    }

    function notify(item) {
        if (!item) {
            return
        }
        if (!item.presentation) {
            item.presentation = "banner"
        }
        if (item.presentation === "fullPage") {
            // Fatal states are owned by LegionShell/StartupGateView.
            return
        }
        if (!item.id) {
            center.nextId = center.nextId + 1
            item.id = center.nextId
        }
        item.dedupeKey = item.dedupeKey || (String(item.code) + "|" + String(item.presentation))

        if (item.presentation === "popup") {
            if (center.popup !== null) {
                center.popupQueue.push(item)
                return
            }
            center.popup = item
            return
        }

        if (center.banner !== null) {
            if (center.banner.dedupeKey === item.dedupeKey) {
                return
            }
            for (var i = 0; i < center.bannerQueue.length; ++i) {
                if (center.bannerQueue[i].dedupeKey === item.dedupeKey) {
                    return
                }
            }
            center.bannerQueue.push(item)
            return
        }
        center.showBanner(item)
    }

    function notifyError(code, message, op, action) {
        var item = Notice.forError(code, message)
        var policyPresentation = Notice.presentationFor(code)
        if (item.presentation === "inline" && policyPresentation !== "inline") {
            item.presentation = policyPresentation
        }
        if (item.presentation === "popup" && !action && code === "queue_deadline_expired") {
            action = function() {
                if (center.shell) {
                    center.shell.showSection("clubs")
                }
            }
        }
        if (action) {
            item.action = action
        }
        else if (item.retryable && center.retryAction) {
            item.action = center.retryAction
        }
        item.operation = op ? String(op) : ""
        center.notify(item)
    }

    function notifySuccess(title, text, detail, action, presentation) {
        var item = Notice.forSuccess(title, text, detail)
        item.presentation = presentation ? String(presentation) : "banner"
        item.actionText = action ? qsTr("Открыть") : ""
        item.action = action || null
        item.dedupeKey = "success|" + String(item.title) + "|" + item.presentation
        center.notify(item)
    }

    function showBanner(item) {
        center.banner = item
        bannerTimer.stop()
        if (item.autoHide && Number(item.autoHide) > 0) {
            bannerTimer.interval = Number(item.autoHide)
            bannerTimer.start()
        }
    }

    function dismissBanner() {
        bannerTimer.stop()
        center.banner = null
        if (center.bannerQueue.length > 0) {
            center.showBanner(center.bannerQueue.shift())
        }
    }

    function dismissPopup() {
        center.popup = null
        if (center.popupQueue.length > 0) {
            center.popup = center.popupQueue.shift()
        }
    }

    function clear() {
        bannerTimer.stop()
        center.bannerQueue = []
        center.popupQueue = []
        center.banner = null
        center.popup = null
        popupView.close()
    }

    Timer {
        id: bannerTimer
        repeat: false
        onTriggered: center.dismissBanner()
    }

    ModalBackdrop {
        anchors.fill: parent
        active: center.popup !== null
        backgroundSource: center.backgroundSource
    }

    NoticeBanner {
        id: bannerView
        visible: center.banner !== null
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        width: Math.min(540, parent.width - Theme.spaceXl * 2)
        notice: center.banner

        onActionClicked: {
            var action = center.banner ? center.banner.action : null
            center.dismissBanner()
            if (typeof action === "function") {
                action()
            }
        }

        onDismissed: center.dismissBanner()
    }

    NoticePopup {
        id: popupView
        notice: center.popup
        onDismissed: center.dismissPopup()
    }
}
