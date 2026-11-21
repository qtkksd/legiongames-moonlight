import QtQuick 2.9
import QtQuick.Controls 2.2

import "Theme.js" as Theme

// Small status chip: "Свободно", "Занято", "Платёж обрабатывается".
// tone: neutral | good | warn | bad | accent
Rectangle {
    id: pill

    property string text: ""
    property string tone: "neutral"

    implicitWidth: label.implicitWidth + 20
    implicitHeight: 24
    radius: 12
    color: {
        if (tone === "good") {
            return "#1b3324"
        }
        if (tone === "warn") {
            return "#33290f"
        }
        if (tone === "bad") {
            return "#331a1a"
        }
        if (tone === "accent") {
            return "#3a1414"
        }
        return "#1f1f1f"
    }
    border.width: 1
    border.color: {
        if (tone === "good") {
            return Theme.success
        }
        if (tone === "warn") {
            return Theme.warning
        }
        if (tone === "bad") {
            return Theme.error
        }
        if (tone === "accent") {
            return Theme.accent
        }
        return Theme.line
    }

    Label {
        id: label
        anchors.centerIn: parent
        text: pill.text
        font.pointSize: Theme.fontCaption
        color: {
            if (pill.tone === "good") {
                return Theme.success
            }
            if (pill.tone === "warn") {
                return Theme.warning
            }
            if (pill.tone === "bad") {
                return Theme.error
            }
            if (pill.tone === "accent") {
                return Theme.text
            }
            return Theme.textMuted
        }
    }
}
