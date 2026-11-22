import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// One-time recovery kit: handle + eight words.
//
// The words are shown once. Nothing here stores them; the backend writes the
// kit file and only keeps the hash. The screen cannot be dismissed while the
// account is in the onboarding `words` step.
LegionView {
    id: words

    readonly property var wordList: {
        if (shell && shell.wordsList && shell.wordsList.length > 0) {
            return shell.wordsList
        }
        if (words.api && words.api.words && words.api.words.length > 0) {
            return words.api.words
        }
        return []
    }

    readonly property string shownHandle: {
        if (shell && shell.wordsHandle) {
            return shell.wordsHandle
        }
        return words.handle
    }

    readonly property bool closeAllowed: appState === "signed_in"

    function activate() {
        if (wordList.length > 0) {
            confirmButton.forceActiveFocus()
        }
    }

    function focusFirst() {
        confirmButton.forceActiveFocus()
    }

    function confirm() {
        Api.confirmWordsSaved(api)
        if (shell) {
            shell.wordsVisible = false
        }
        Api.refreshMe(api)
    }

    function close() {
        if (!closeAllowed) {
            return
        }
        if (shell) {
            shell.wordsVisible = false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    Item {
        anchors.fill: parent
        anchors.margins: Theme.spaceXxl

        NeonFrame {
            anchors.centerIn: parent
            width: Math.min(parent.width, 860)
            height: Math.min(parent.height, column.implicitHeight + Theme.spaceXxl * 2)
            highlighted: true
            flicker: true

            ColumnLayout {
                id: column
                anchors.fill: parent
                anchors.margins: Theme.spaceXxl
                spacing: Theme.spaceL

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Набор восстановления")
                        color: Theme.accent
                        font.pointSize: Theme.fontTitle + 2
                        font.bold: true
                    }

                    Label {
                        visible: words.closeAllowed
                        text: qsTr("Закрыть")
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall + 1
                    }

                    LegionButton {
                        visible: words.closeAllowed
                        kind: "ghost"
                        compact: true
                        text: qsTr("Закрыть")
                        onClicked: words.close()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Сохраните восемь слов")
                    color: Theme.text
                    font.pointSize: Theme.fontHero + 4
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Аккаунт: %1").arg(words.shownHandle.length > 0 ? words.shownHandle : "—")
                    color: Theme.textMuted
                    font.pointSize: Theme.fontBody + 3
                }

                Label {
                    Layout.fillWidth: true
                    visible: words.wordList.length === 0
                    text: qsTr("Слова не удалось получить. Выпустите новый набор.")
                    color: Theme.warning
                    font.pointSize: Theme.fontBody + 3
                    wrapMode: Text.Wrap
                }

                GridLayout {
                    Layout.fillWidth: true
                    visible: words.wordList.length > 0
                    columns: 2
                    columnSpacing: Theme.spaceM
                    rowSpacing: Theme.spaceS

                    Repeater {
                        model: words.wordList

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 52
                            radius: Theme.radiusS
                            color: Theme.panelAlt
                            border.width: 1
                            border.color: Theme.lineStrong

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spaceL
                                anchors.rightMargin: Theme.spaceL
                                spacing: Theme.spaceM

                                Label {
                                    text: (index + 1) + "."
                                    color: Theme.textDim
                                    font.pointSize: Theme.fontBody + 2
                                    font.bold: true
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: Theme.text
                                    font.pointSize: Theme.fontTitle + 2
                                    font.family: "monospace"
                                }
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Эти слова показываются один раз. Запишите их на бумагу и храните " +
                               "отдельно от компьютера. Любой, у кого есть слова, может восстановить аккаунт.")
                    color: Theme.warning
                    font.pointSize: Theme.fontBody + 3
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: {
                        if (shell && shell.kitPath.length > 0) {
                            return qsTr("Файл с набором сохранён: %1").arg(shell.kitPath)
                        }
                        return qsTr("Файл с набором сохранён в папке «Документы/LegionGames».")
                    }
                    color: Theme.textMuted
                    font.pointSize: Theme.fontSmall + 2
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: shell ? shell.kitInSyncedFolder : false
                    text: qsTr("Эта папка синхронизируется с облаком. Храните набор отдельно от компьютера.")
                    color: Theme.warning
                    font.pointSize: Theme.fontSmall + 2
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceM

                    LegionButton {
                        visible: words.wordList.length === 0
                        kind: "secondary"
                        text: qsTr("Выпустить новый набор")
                        onClicked: Api.reissueWords(words.api)
                    }

                    Item { Layout.fillWidth: true }

                    LegionButton {
                        kind: "secondary"
                        visible: words.closeAllowed
                        text: qsTr("Позже")
                        onClicked: words.close()
                    }

                    LegionButton {
                        id: confirmButton
                        kind: "primary"
                        focus: true
                        enabled: words.wordList.length > 0
                        text: qsTr("Я сохранил слова")
                        onClicked: words.confirm()
                    }
                }

                ErrorBanner {
                    Layout.fillWidth: true
                    visible: words.hasError
                    text: words.errorText
                    canRetry: false
                }
            }
        }
    }
}
