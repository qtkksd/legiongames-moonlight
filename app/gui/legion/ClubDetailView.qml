import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Club detail: the decision point before a session, a queue slot or a top-up.
LegionView {
    id: detail

    property var club: null
    property string detailUpdatedAt: ""
    property bool starting: false
    property bool joining: false

    readonly property int clubId: {
        if (club && club.id !== undefined) {
            return club.id
        }
        if (params && params.clubId !== undefined) {
            return params.clubId
        }
        return -1
    }

    readonly property string clubName: {
        if (club && club.name) {
            return club.name
        }
        if (params && params.name) {
            return params.name
        }
        return qsTr("Клуб")
    }

    readonly property string updatedAt: {
        if (detailUpdatedAt.length > 0) {
            return detailUpdatedAt
        }
        if (shell && shell.catalogAt) {
            return shell.catalogAt
        }
        if (club && club.updated_at) {
            return club.updated_at
        }
        return catalogUpdatedAt
    }

    readonly property bool stale: Theme.isStale(updatedAt)
    readonly property bool open: Api.isClubOpen(club)
    readonly property int freeUnits: club ? Number(club.free_units || 0) : 0
    readonly property var balance: shell ? shell.balanceFor(clubId) : 0
    readonly property real money: Number(balance || 0)
    readonly property real minutePrice: club ? Number(club.price_per_minute || 0) : 0
    readonly property bool enoughForMinute: money > 0 &&
                                            (minutePrice <= 0 || money >= minutePrice)
    readonly property bool hasFunds: money > 0
    readonly property bool canPlay: open && freeUnits > 0 && !stale && enoughForMinute &&
                                    !starting && !serverBusy
    readonly property bool canQueue: open && freeUnits <= 0 && !stale && hasFunds &&
                                     !joining && !serverBusy
    readonly property string blockedReason: club ? Api.traceReason(club, stale, balance) : ""

    // The primary button is either "play", "queue" or "top up". Only the
    // first two actions have a policy gate; a top-up must remain available
    // even when the club is closed or the data is stale. Disabling the button
    // when its own gate rejects the click removes the silent no-op: the
    // reason is already shown in `blockedReason` above it.
    readonly property bool primaryIsStart: freeUnits > 0 && enoughForMinute
    readonly property bool primaryIsQueue: freeUnits <= 0 && hasFunds
    readonly property bool primaryBlocked: (primaryIsStart && !canPlay) ||
                                           (primaryIsQueue && !canQueue)

    function activate() {
        starting = false
        joining = false
        detailUpdatedAt = ""
        if (shell && params && params.clubId !== undefined) {
            var cached = shell.clubById(params.clubId)
            if (cached) {
                club = cached
            }
        }

        var slug = params && params.slug ? String(params.slug) : ""
        if (slug.length === 0 && club && club.slug) {
            slug = String(club.slug)
        }
        if (slug.length > 0) {
            if (shell) {
                var cachedAt = shell.clubFetchedAtMs(slug)
                if (cachedAt > 0) {
                    detailUpdatedAt = new Date(cachedAt).toISOString()
                }
                // Render the cached club immediately. This only sends a
                // request when the club is missing or the TTL has expired.
                shell.refreshClub(slug, false)
            }
            else {
                Api.getClub(api, slug)
            }
        }
        if (shell) {
            shell.refreshWalletIfStale()
            if (shell.catalog.length === 0) {
                shell.loadCatalog(false)
            }
        }
    }

    function focusFirst() {
        playButton.forceActiveFocus()
    }

    function startSession() {
        if (starting || serverBusy) {
            return
        }
        if (!canPlay) {
            showError("client_blocked",
                      blockedReason.length > 0 ? blockedReason
                                               : qsTr("Игра сейчас недоступна."),
                      "session_start")
            return
        }
        clearError()
        starting = true
        if (!Api.startSession(api, clubId)) {
            starting = false
            showError("offline", "")
        }
    }

    function joinQueue() {
        if (joining || serverBusy) {
            return
        }
        if (!canQueue) {
            showError("client_blocked",
                      blockedReason.length > 0 ? blockedReason
                                               : qsTr("Очередь сейчас недоступна."),
                      "queue_join")
            return
        }
        clearError()
        joining = true
        if (!Api.joinQueue(api, clubId)) {
            joining = false
            showError("offline", "")
        }
    }

    Connections {
        target: detail.api
        ignoreUnknownSignals: true

        function onResponse(op, data) {
            if (op === "club") {
                var updatedClub = Api.clubOf(data)
                if (updatedClub) {
                    // Ignore a response for a club the user has already left.
                    if (detail.club && detail.club.id !== undefined &&
                            String(updatedClub.id) !== String(detail.club.id)) {
                        return
                    }
                    detail.club = updatedClub
                    detail.detailUpdatedAt = new Date().toISOString()
                }
            }
            else if (op === "session_start") {
                detail.starting = false
                detail.go("qrc:/gui/legion/SessionView.qml", {})
            }
            else if (op === "queue_join") {
                detail.joining = false
                detail.go("qrc:/gui/legion/QueueView.qml", {})
            }
        }

        function onFailed(op, code, message) {
            if (op === "club" || op === "session_start" || op === "queue_join") {
                detail.starting = false
                detail.joining = false
                detail.showError(code, message, op)
            }
        }
    }

    Connections {
        target: detail.shell
        ignoreUnknownSignals: true

        function onCatalogChanged() {
            var synced = detail.shell ? detail.shell.clubById(detail.clubId) : null
            if (synced) {
                detail.club = synced
                if (detail.shell && detail.shell.catalogAt &&
                        detail.shell.catalogAt > detail.detailUpdatedAt) {
                    detail.detailUpdatedAt = detail.shell.catalogAt
                }
            }
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        showBack: true
        backText: qsTr("Клубы")
        title: detail.clubName
        subtitle: detail.club && detail.club.address ? detail.club.address : ""
        onBack: detail.goBack()
    }

    ScrollView {
        contentWidth: availableWidth
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

            NeonFrame {
                width: parent.width
                height: 200
                highlighted: true
                flicker: true

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Theme.radiusM - 1
                    color: Theme.panelAlt

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: detail.club && detail.club.image_url ? detail.club.image_url : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: source.toString().length > 0
                    }

                    Image {
                        anchors.centerIn: parent
                        width: Math.min(parent.width, parent.height) * 0.6
                        height: width
                        source: "qrc:/res/logo_alien.svg"
                        fillMode: Image.PreserveAspectFit
                        opacity: 0.08
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceM

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        spacing: Theme.spaceXs

                        Label {
                            Layout.fillWidth: true
                            text: detail.clubName
                            color: Theme.text
                            font.pointSize: Theme.fontHero
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: detail.club && detail.club.specs ? detail.club.specs : ""
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                            elide: Text.ElideRight
                        }
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignBottom
                        spacing: Theme.spaceXs

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: Theme.pricePerHour(detail.club ? detail.club.price_per_hour : 0)
                            color: Theme.text
                            font.pointSize: Theme.fontHeader
                            font.bold: true
                        }

                        StatusPill {
                            Layout.alignment: Qt.AlignRight
                            visible: detail.club !== null
                            text: Api.clubAvailabilityText(detail.club)
                            tone: Api.clubTone(detail.club)
                        }
                    }
                }
            }

            ErrorBanner {
                width: parent.width
                visible: detail.hasError
                text: detail.errorText
                detail: detail.serverError
                canRetry: detail.canPlay || detail.canQueue
                onRetry: {
                    if (detail.canPlay) {
                        detail.startSession()
                    }
                    else {
                        detail.joinQueue()
                    }
                }
            }

            ErrorBanner {
                width: parent.width
                visible: detail.stale
                text: qsTr("Данные устарели. Обновите список.")
                detail: qsTr("Цены и доступность показаны по состоянию на %1.").arg(Theme.timeText(detail.updatedAt))
                canRetry: true
                onRetry: {
                    if (detail.club && detail.club.slug) {
                        if (detail.shell) {
                            detail.shell.refreshClub(detail.club.slug, true)
                        }
                        else {
                            Api.getClub(detail.api, detail.club.slug)
                        }
                    }
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spaceL

                NeonFrame {
                    width: parent.width * 0.58
                    height: infoColumn.implicitHeight + Theme.spaceXl * 2

                    ColumnLayout {
                        id: infoColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spaceXl
                        spacing: Theme.spaceS

                        Label {
                            Layout.fillWidth: true
                            visible: detail.club && detail.club.description
                            text: detail.club && detail.club.description ? detail.club.description : ""
                            color: Theme.text
                            font.pointSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: detail.club !== null
                            text: qsTr("Свободно %1 из %2 ПК").arg(detail.freeUnits)
                                  .arg(detail.club ? (detail.club.total_units || 0) : 0)
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: detail.club === null
                            text: qsTr("Загружаем данные клуба…")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontBody
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: detail.club && detail.club.queue_length > 0
                            text: qsTr("В очереди: %1").arg(detail.club ? detail.club.queue_length : 0)
                            color: Theme.warning
                            font.pointSize: Theme.fontBody
                        }

                        Label {
                            Layout.fillWidth: true
                            text: {
                                if (!detail.club || !detail.club.schedule) {
                                    return qsTr("Работает по расписанию клуба")
                                }
                                if (detail.club.schedule.closed) {
                                    return qsTr("Закрыто. Откроется позже.")
                                }
                                if (detail.club.schedule.next_price_per_hour) {
                                    return qsTr("После смены тарифа: %1")
                                        .arg(Theme.pricePerHour(detail.club.schedule.next_price_per_hour))
                                }
                                return qsTr("Работает по расписанию клуба")
                            }
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Данные обновлены %1").arg(Theme.ageText(detail.updatedAt))
                            color: detail.stale ? Theme.warning : Theme.textDim
                            font.pointSize: Theme.fontSmall
                        }
                    }
                }

                BalancePanel {
                    width: parent.width * 0.42 - Theme.spaceL
                    height: infoColumn.implicitHeight + Theme.spaceXl * 2
                    clubName: qsTr("Баланс для этого клуба")
                    balance: detail.balance
                    bonus: detail.walletRow ? detail.walletRow.bonus : "0"
                    minutes: detail.walletRow ? Number(detail.walletRow.minutes || 0) : -1
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spaceS

                Label {
                    width: parent.width
                    visible: detail.blockedReason.length > 0
                    text: detail.blockedReason
                    color: Theme.warning
                    font.pointSize: Theme.fontBody
                    wrapMode: Text.Wrap
                }

                Row {
                    spacing: Theme.spaceM

                    LegionButton {
                        id: playButton
                        kind: "primary"
                        enabled: !detail.serverBusy && !detail.starting &&
                                 !detail.joining && !detail.primaryBlocked
                        focus: detail.canPlay || detail.canQueue
                        text: {
                            if (detail.freeUnits > 0) {
                                if (detail.starting) {
                                    return qsTr("Запускаем…")
                                }
                                return detail.enoughForMinute ? qsTr("Играть")
                                                              : qsTr("Пополнить")
                            }
                            if (detail.joining) {
                                return qsTr("Встаём в очередь…")
                            }
                            return detail.hasFunds ? qsTr("В очередь") : qsTr("Пополнить")
                        }
                        onClicked: {
                            if (detail.freeUnits > 0) {
                                if (detail.enoughForMinute) {
                                    detail.startSession()
                                }
                                else {
                                    detail.go("qrc:/gui/legion/PaymentView.qml",
                                              { clubId: detail.clubId,
                                                clubName: detail.clubName })
                                }
                            }
                            else if (detail.hasFunds) {
                                detail.joinQueue()
                            }
                            else {
                                detail.go("qrc:/gui/legion/PaymentView.qml",
                                          { clubId: detail.clubId,
                                            clubName: detail.clubName })
                            }
                        }
                    }

                    LegionButton {
                        kind: "secondary"
                        visible: detail.freeUnits > 0 ? detail.enoughForMinute
                                                      : detail.hasFunds
                        text: qsTr("Пополнить")
                        onClicked: detail.go("qrc:/gui/legion/PaymentView.qml",
                                             { clubId: detail.clubId, clubName: detail.clubName })
                    }

                    LegionButton {
                        kind: "ghost"
                        text: qsTr("Обновить")
                        onClicked: {
                            if (detail.club && detail.club.slug) {
                                if (detail.shell) {
                                    detail.shell.refreshClub(detail.club.slug, true)
                                }
                                else {
                                    Api.getClub(detail.api, detail.club.slug)
                                }
                            }
                            if (detail.shell) {
                                detail.shell.refreshWallet(true)
                            }
                        }
                    }
                }
            }
        }
    }

    readonly property var walletRow: shell ? shell.walletFor(clubId) : null
}
