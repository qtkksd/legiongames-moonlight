import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// Public club catalog. Read-only data from the server: prices, availability
// and the queue length are never computed on the client.
LegionView {
    id: clubs

    property bool onlyAvailable: false

    readonly property var allRows: Api.sortClubs(shell ? shell.catalog : [])
    readonly property var rows: {
        if (!onlyAvailable) {
            return allRows
        }
        var filtered = []
        for (var i = 0; i < allRows.length; i++) {
            var club = allRows[i]
            if (Api.isClubOpen(club) && Number(club.free_units || 0) > 0) {
                filtered.push(club)
            }
        }
        return filtered
    }
    readonly property string updatedAt: shell ? shell.catalogAt : ""
    readonly property bool stale: Theme.isStale(updatedAt.length > 0 ? updatedAt : catalogUpdatedAt)
    readonly property string loadErrorCode: shell ? shell.catalogErrorCode : ""
    readonly property string loadErrorMessage: shell ? shell.catalogErrorMessage : ""
    readonly property bool loading: shell ? shell.catalogLoading : false

    function activate() {
        if (shell) {
            shell.loadCatalog(false)
        }
    }

    function focusFirst() {
        clubList.forceActiveFocus()
    }

    function refresh() {
        if (shell) {
            shell.loadCatalog(false, true)
        }
    }

    function openClub(club) {
        if (!club) {
            return
        }
        clubs.go("qrc:/gui/legion/ClubDetailView.qml",
                 { slug: club.slug, clubId: club.id, name: club.name })
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("Клубы")
        subtitle: qsTr("Цены и доступность обновлены %1").arg(Theme.ageText(updatedAt))
    }

    RowLayout {
        id: filterRow
        anchors.top: header.bottom
        anchors.topMargin: Theme.spaceM
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        spacing: Theme.spaceL

        CheckBox {
            text: qsTr("Только свободные")
            font.pointSize: Theme.fontBody
            checked: clubs.onlyAvailable
            onToggled: {
                clubs.onlyAvailable = checked
            }
        }

        Item { Layout.fillWidth: true }

        BusyIndicator {
            visible: clubs.loading
            running: visible
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
        }

        Label {
            visible: clubs.stale
            text: qsTr("Данные устарели")
            color: Theme.warning
            font.pointSize: Theme.fontSmall
        }

        LegionButton {
            kind: "secondary"
            compact: true
            text: qsTr("Обновить")
            onClicked: clubs.refresh()
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors.top: filterRow.bottom
        anchors.topMargin: Theme.spaceL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        anchors.bottomMargin: Theme.spaceL
        spacing: Theme.spaceM

        ErrorBanner {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: clubs.loadErrorCode.length > 0
            text: Api.errorTextFor("clubs", clubs.loadErrorCode, clubs.loadErrorMessage)
            canRetry: true
            onRetry: clubs.refresh()
        }

        ErrorBanner {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: clubs.stale && clubs.rows.length > 0
            text: qsTr("Данные устарели. Обновите список.")
            detail: qsTr("Цены и доступность показаны по состоянию на %1. Запуск и оплата отключены.").arg(Theme.timeText(updatedAt))
            canRetry: true
            onRetry: clubs.refresh()
        }

        ListView {
            id: clubList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spaceM
            model: clubs.rows
            currentIndex: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: ClubCard {
                width: clubList.width
                height: 124
                club: modelData
                stale: clubs.stale
                hasFunds: clubs.shell ? clubs.shell.balanceFor(modelData.id) > 0 : false
                current: ListView.isCurrentItem && clubList.activeFocus
                onActivated: clubs.openClub(modelData)
            }

            Keys.onUpPressed: decrementCurrentIndex()
            Keys.onDownPressed: incrementCurrentIndex()
            Keys.onReturnPressed: {
                if (currentItem) {
                    currentItem.activated()
                }
            }
            Keys.onEnterPressed: {
                if (currentItem) {
                    currentItem.activated()
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spaceXxl * 4, 560)
        visible: !clubs.loading && clubs.rows.length === 0
        title: clubs.onlyAvailable ? qsTr("Нет свободных клубов")
                                   : qsTr("Пока нет клубов")
        text: clubs.onlyAvailable
              ? qsTr("Снимите фильтр или загляните позже: очередь подскажет, когда освободится место.")
              : qsTr("Список клубов появится, как только сервер его вернёт.")
        actionText: clubs.onlyAvailable ? qsTr("Показать все") : qsTr("Обновить")
        onAction: {
            if (clubs.onlyAvailable) {
                clubs.onlyAvailable = false
            }
            clubs.refresh()
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        visible: clubs.loading && clubs.rows.length === 0
        running: visible
    }
}
