import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Bluetooth 1.0

Page {

    id: page

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent

        PullDownMenu {

            MenuItem {
                text: "Find Funwheel"
                onClicked: devicePicker.open()
            }

            MenuItem {
                text: "Refresh"
                onClicked: vescBackend.refresh()
            }
        }

        contentHeight: column.height

        Column {
            id: column

            width: page.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Floatwatch")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: "Status: " + vescBackend.status
                color: Theme.primaryColor
                wrapMode: Text.Wrap
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: "Device: " + vescBackend.deviceName
                color: Theme.primaryColor
                wrapMode: Text.Wrap
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin

                text: vescBackend.voltage > 0
                      ? "Voltage: "
                        + (Math.abs(vescBackend.voltageDelta) < 0.05
                           ? ""
                           : (vescBackend.voltageDelta > 0 ? "+" : ""))
                        + vescBackend.voltageDelta.toFixed(1) + " V  "
                        + vescBackend.voltage.toFixed(1) + " V"
                      : "Voltage: --.- V"

                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeLarge
                wrapMode: Text.Wrap
            }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                text: "Speed: " + vescBackend.speedKph.toFixed(1) + " km/h"
                color: Theme.primaryColor
                wrapMode: Text.Wrap
            }
        }

        BluetoothDevicePickerDialog {
            id: devicePicker

            onAccepted: {
                console.log("Selected device:", selectedDevice)
                vescBackend.connectToDevice(selectedDevice)
            }
        }
    }
}
