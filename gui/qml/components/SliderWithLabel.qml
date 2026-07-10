import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Reusable slider with label and tooltip
Item {
    id: container
    property string label: ""
    property real value: 0
    property real min: 0
    property real max: 100
    property real decimals: 0
    property string suffix: ""
    property string tooltip: ""

    implicitHeight: 80
    Layout.fillWidth: true

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        Row {
            spacing: 12
            Layout.fillWidth: true

            Label {
                text: container.label
                font.pixelSize: 16
                color: "#c0c0d0"
                Layout.alignment: Qt.AlignVCenter
            }

            Spacer { }

            Label {
                text: container.value.toFixed(container.decimals) + (container.suffix ? " " + container.suffix : "")
                font.pixelSize: 18
                font.bold: true
                color: "#4ecca3"
                Layout.alignment: Qt.AlignVCenter
            }
        }

        Slider {
            id: slider
            anchors.left: parent.left
            anchors.right: parent.right
            from: container.min
            to: container.max
            value: container.value
            stepSize: (container.max - container.min) / 100
            onValueChanged: container.value = value

            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.height / 2 - height / 2
                width: 36
                height: 36
                radius: 18
                color: slider.pressed ? "#4ecca3" : "#2d6a8e"
                border.color: "#4ecca3"
                border.width: 2

                Label {
                    anchors.centerIn: parent
                    text: "●"
                    font.pixelSize: 14
                    color: "#ffffff"
                }
            }
        }
    }
}
