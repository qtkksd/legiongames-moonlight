import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Theme.js" as Theme

// Staging-only QA surface. It exercises the presentation layer, not the
// backend: no balance, payment, session or account mutation is performed.
LegionView {
    id: qa

    property bool fullPageVisible: false
    property bool inlineSuccess: false

    function activate() {
        fullPageVisible = false
    }

    function focusFirst() {
        forceActiveFocus()
    }

    function bannerError(code, message) {
        if (shell && shell.notifications) {
            shell.notifications.notifyError(code, message, "qa")
        }
    }

    function infoBanner(title, text) {
        if (shell && shell.notifications) {
            shell.notifications.notify({
                tone: "info",
                title: title,
                text: text,
                detail: "QA preview",
                presentation: "banner",
                autoHide: 4500
            })
        }
    }

    function clearAll() {
        clearError()
        inlineSuccess = false
        if (shell && shell.notifications) {
            shell.notifications.clear()
        }
    }

    PageHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
        title: qsTr("QA")
        subtitle: qsTr("Тестовый режим staging")
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

            ErrorBanner {
                width: parent.width
                visible: qa.hasInlineError
                tone: qa.errorTone
                text: qa.errorText
                detail: qa.serverError
                canRetry: false
            }

            ErrorBanner {
                width: parent.width
                visible: qa.inlineSuccess
                tone: "success"
                text: qsTr("Тестовое inline-уведомление: успех.")
                detail: qsTr("Зелёный border, без модального окна.")
                canRetry: false
            }

            NeonFrame {
                width: parent.width
                height: notificationsColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: notificationsColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Уведомления")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Каждый tone и каждый способ показа. Ничего не уходит на сервер.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: Theme.spaceS
                        columnSpacing: Theme.spaceS

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Inline: ошибка")
                            onClicked: qa.showError("club_unavailable",
                                                    qsTr("Клуб сейчас недоступен."), "qa")
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Inline: предупреждение")
                            onClicked: qa.showError("computer_searching",
                                                    qsTr("Компьютер ещё ищется."), "qa")
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Inline: успех")
                            onClicked: {
                                qa.clearError()
                                qa.inlineSuccess = true
                            }
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Banner: offline (red)")
                            onClicked: qa.bannerError("offline",
                                                      qsTr("Тест отсутствия соединения."))
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Banner: Django 5xx (yellow)")
                            onClicked: qa.bannerError("server_error",
                                                      qsTr("Тестовый 500 от Django."))
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Banner: 429 (yellow)")
                            onClicked: qa.bannerError("rate_limited",
                                                      qsTr("Тестовый 429."))
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Banner: update (yellow)")
                            onClicked: qa.bannerError("client_update_required",
                                                      qsTr("Тестовое требование обновления."))
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Banner: Django + legal docs")
                            onClicked: qa.bannerError("legal_documents_unavailable",
                                                      qsTr("Документы недоступны, Django недоступен."))
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Banner: успех (green)")
                            onClicked: {
                                if (shell && shell.notifications) {
                                    shell.notifications.notifySuccess(
                                        qsTr("Баланс пополнен"),
                                        qsTr("Тестовое зачисление."),
                                        qsTr("QA preview"))
                                }
                            }
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Banner: информация")
                            onClicked: qa.infoBanner(qsTr("Информация"),
                                                     qsTr("Тестовое info-уведомление."))
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Popup: очередь истекла")
                            onClicked: qa.bannerError("queue_deadline_expired",
                                                      qsTr("Тестовый истёкший слот."))
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Popup: слот готов")
                            onClicked: {
                                if (shell && shell.notifications) {
                                    shell.notifications.notifySuccess(
                                        qsTr("Слот готов"),
                                        qsTr("Тестовый слот ждёт подтверждения."),
                                        qsTr("QA preview"),
                                        function() { qa.goSection("clubs") },
                                        "popup")
                                }
                            }
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Popup: ошибка")
                            onClicked: {
                                if (shell && shell.notifications) {
                                    shell.notifications.notify({
                                        tone: "danger",
                                        title: qsTr("Тестовая ошибка"),
                                        text: qsTr("Модальное уведомление с действием."),
                                        detail: qsTr("QA preview"),
                                        actionText: qsTr("К клубам"),
                                        action: function() { qa.goSection("clubs") },
                                        presentation: "popup"
                                    })
                                }
                            }
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "secondary"
                            text: qsTr("Full page: ошибка ключа")
                            onClicked: {
                                qa.clearError()
                                qa.fullPageVisible = true
                            }
                        }

                        LegionButton {
                            Layout.fillWidth: true
                            kind: "danger"
                            text: qsTr("Очистить всё")
                            onClicked: qa.clearAll()
                        }
                    }
                }
            }

            NeonFrame {
                width: parent.width
                height: pagesColumn.implicitHeight + Theme.spaceXl * 2

                ColumnLayout {
                    id: pagesColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXl
                    spacing: Theme.spaceS

                    Label {
                        text: qsTr("Страницы и переходы")
                        color: Theme.text
                        font.pointSize: Theme.fontTitle
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Проверка навигации, фокуса и компоновки без серверных изменений.")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: Theme.spaceS
                        columnSpacing: Theme.spaceS

                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Главная"); onClicked: qa.goSection("home") }
                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Сессии"); onClicked: qa.goSection("sessions") }
                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Компьютеры"); onClicked: qa.goSection("computers") }
                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Клубы"); onClicked: qa.goSection("clubs") }
                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Кошелёк"); onClicked: qa.goSection("wallet") }
                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Аккаунт"); onClicked: qa.goSection("account") }
                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Настройки"); onClicked: qa.goSection("settings") }
                        LegionButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Стриминг"); onClicked: qa.go("qrc:/gui/legion/StreamSettingsView.qml", {}) }
                    }
                }
            }

            Label {
                width: parent.width
                text: qsTr("QA включён только в staging-сборке. В production страницы и ресурса нет.")
                color: Theme.textDim
                font.pointSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        visible: qa.fullPageVisible
        z: 50

        NeonFrame {
            width: Math.min(parent.width - Theme.spaceXxl * 2, 680)
            height: fullPageColumn.implicitHeight + Theme.spaceXxl * 2
            anchors.centerIn: parent
            highlighted: true
            flicker: true

            ColumnLayout {
                id: fullPageColumn
                anchors.fill: parent
                anchors.margins: Theme.spaceXxl
                spacing: Theme.spaceL

                Label {
                    text: qsTr("LegionGames")
                    color: Theme.accent
                    font.pointSize: Theme.fontSubtitle
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Хранилище ключей недоступно")
                    color: Theme.text
                    font.pointSize: Theme.fontHero
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Это тестовый full-page экран. В реальном приложении здесь будут recovery и выход.")
                    color: Theme.textMuted
                    font.pointSize: Theme.fontBody
                    wrapMode: Text.Wrap
                }

                LegionButton {
                    kind: "primary"
                    text: qsTr("Закрыть")
                    onClicked: qa.fullPageVisible = false
                }
            }
        }
    }
}
