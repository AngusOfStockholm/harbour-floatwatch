import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {

    Column {
        anchors {
            top: parent.top
            topMargin: Theme.paddingLarge
            horizontalCenter: parent.horizontalCenter
        }

        width: parent.width
        spacing: Theme.paddingSmall

        Label {
            text: vescBackend.voltage > 0
                  ? (Math.abs(vescBackend.voltageDelta) < 0.05
                     ? ""
                     : (vescBackend.voltageDelta > 0 ? "+" : ""))
                    + vescBackend.voltageDelta.toFixed(1) + " V"
                  : "--.- V"

            font.pixelSize: Theme.fontSizeLarge
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        Label {
            text: vescBackend.voltage > 0
                  ? vescBackend.voltage.toFixed(1) + " V"
                  : "--.- V"

            font.pixelSize: Theme.fontSizeMedium
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }
    }

    CoverActionList {
        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: vescBackend.refresh()
        }
    }
}
