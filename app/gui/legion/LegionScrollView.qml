import QtQuick 2.9
import QtQuick.Controls 2.2

import "Theme.js" as Theme

// ScrollView with the product scrollbar. A bare `ScrollBar.vertical` object
// does not inherit the default style's x/y/height bindings, so it must be
// positioned explicitly against the ScrollView. This also reserves the
// right-side gutter so the bar never covers content.
ScrollView {
    id: control

    contentWidth: availableWidth
    rightPadding: Theme.scrollbarGutter
    background: null
    clip: true

    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: LegionScrollBar {
        parent: control
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight
    }
}
