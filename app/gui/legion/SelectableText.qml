import QtQuick 2.9
import QtQuick.Layouts 1.3

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
    // TextEdit.implicitHeight is read-only. Layouts get the wrapped content
    // height through the attached property; assigning implicitHeight made the
    // whole component fail to load and left the product shell blank.
    Layout.preferredHeight: contentHeight
}
