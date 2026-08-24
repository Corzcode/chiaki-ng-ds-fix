import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

CheckBox {
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    property bool sendOutput: false
    // When true, the label text wraps inside the available width instead of
    // overflowing / eliding. Used by Settings rows where the control column is
    // a fixed 650px and the description is longer than a single line.
    property bool wrapText: false

    contentItem: Text {
        text: parent.text
        font: parent.font
        // NOTE: must use Material.foreground here. palette.windowText resolves to
        // black (#000000) under the Material style even in Dark theme (Qt 6.11),
        // which made every checkbox label render black.
        color: parent.enabled ? parent.Material.foreground : parent.Material.hintTextColor
        leftPadding: parent.indicator ? parent.indicator.width + parent.spacing : 0
        rightPadding: parent.wrapText ? 10 : 0
        elide: parent.wrapText ? Text.ElideNone : Text.ElideRight
        wrapMode: parent.wrapText ? Text.WordWrap : Text.NoWrap
        verticalAlignment: Text.AlignVCenter
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Up:
            if (!firstInFocusChain) {
                let item = nextItemInFocusChain(false);
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
                if(!sendOutput)
                    event.accepted = true;
            }
            break;
        case Qt.Key_Down:
            if (!lastInFocusChain) {
                let item = nextItemInFocusChain();
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
                if(!sendOutput)
                    event.accepted = true;
            }
            break;
        case Qt.Key_Return:
            if (visualFocus) {
                toggle();
                toggled();
            }
            event.accepted = true;
            break;
        }
    }
}
