import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "Api.js" as Api
import "Theme.js" as Theme

// One club in the catalog list.
//
// The row is intentionally information-dense: the app is the storefront, so
// name, price, specs, address, free/total PCs, queue length and the closed
// schedule are visible without opening the club.
Rectangle {
    id: card

    property var club: ({})
    property bool stale: false
    property bool hasFunds: true
    property bool current: false
    property bool hovered: false

    signal activated()

    implicitHeight: 124
    radius: Theme.radiusM
    color: current ? "#1f1414" : Theme.panel
    property color borderColor: (current || hovered) ? Theme.accent : Theme.line
    border.width: current ? 2 : 1
    border.color: borderColor

    Behavior on borderColor {
        ColorAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceXl

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceXs

            RowLayout {
                spacing: Theme.spaceS
                Layout.fillWidth: true

                Label {
                    text: card.club.name ? card.club.name : qsTr("Клуб")
                    color: Theme.text
                    font.pointSize: Theme.fontTitle
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                StatusPill {
                    text: Api.clubAvailabilityText(card.club)
                    tone: Api.clubTone(card.club)
                }

                StatusPill {
                    visible: card.stale
                    text: qsTr("Данные устарели")
                    tone: "warn"
                }
            }

            Label {
                Layout.fillWidth: true
                text: card.club.specs ? card.club.specs : ""
                color: Theme.textMuted
                font.pointSize: Theme.fontBody
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: {
                    var parts = []
                    if (card.club.address) {
                        parts.push(card.club.address)
                    }
                    if (card.club.queue_length > 0) {
                        parts.push(qsTr("очередь: %1").arg(card.club.queue_length))
                    }
                    if (card.club.os) {
                        parts.push(card.club.os)
                    }
                    return parts.join(" · ")
                }
                color: Theme.textDim
                font.pointSize: Theme.fontSmall
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            spacing: Theme.spaceXs

            Label {
                text: Theme.pricePerHour(card.club.price_per_hour)
                color: Theme.text
                font.pointSize: Theme.fontSubtitle
                font.bold: true
                horizontalAlignment: Text.AlignRight
                Layout.alignment: Qt.AlignRight
            }

            Label {
                text: qsTr("ПК: %1 из %2").arg(card.club.free_units || 0).arg(card.club.total_units || 0)
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                horizontalAlignment: Text.AlignRight
                Layout.alignment: Qt.AlignRight
            }

            Label {
                visible: card.club.schedule && card.club.schedule.next_price_per_hour
                text: qsTr("после смены: %1").arg(Theme.pricePerHour(
                            card.club.schedule ? card.club.schedule.next_price_per_hour : 0))
                color: Theme.textDim
                font.pointSize: Theme.fontSmall
                horizontalAlignment: Text.AlignRight
                Layout.alignment: Qt.AlignRight
            }

            Label {
                visible: !card.hasFunds
                text: qsTr("Недостаточно средств")
                color: Theme.warning
                font.pointSize: Theme.fontSmall
                horizontalAlignment: Text.AlignRight
                Layout.alignment: Qt.AlignRight
            }
        }

        Label {
            text: ">"
            color: Theme.accent
            font.pointSize: Theme.fontTitle
            font.bold: true
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: card.hovered = true
        onExited: card.hovered = false
        onClicked: card.activated()
    }
}
