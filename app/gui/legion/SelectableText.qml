import QtQuick 2.9

import "Theme.js" as Theme

// Read-only, mouse-selectable text for values users may want to copy:
// handles, recovery-kit paths and diagnostic strings. Ordinary prose remains
// a Label so it keeps the normal text layout.
TextEdit {
    id: control

    readOnly: true
    selectByMouse: true
    selectByKeyboard: true
    persistentSelection: true
    wrapMode: TextEdit.Wrap
    color: Theme.text
    font.pointSize: Theme.fontBody
    activeFocusOnPress: true
    implicitHeight: contentHeight
}
