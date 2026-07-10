import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: page
    padding: 20

    ColumnLayout {
        anchors.fill: parent
        spacing: 24

        Label {
            text: "Scheduler Settings"
            font.pixelSize: 32
            font.bold: true
            color: "#e94560"
        }

        Label {
            text: "Tune performance thresholds and scheduling behavior"
            font.pixelSize: 16
            color: "#a0a0b0"
        }

        // Section: Load Detection
        GroupBox {
            title: "Load Detection"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 16
                anchors.margins: 16

                SliderWithLabel {
                    label: "HeavyLoad Threshold"
                    value: 60
                    min: 20; max: 90
                    tooltip: "System load % above which boost mode activates"
                }

                SliderWithLabel {
                    label: "Idle Load Threshold"
                    value: 20
                    min: 5; max: 50
                    tooltip: "Load % below which boost mode deactivates"
                }

                SliderWithLabel {
                    label: "Sample Time"
                    value: 10
                    min: 5; max: 100
                    suffix: "ms"
                    tooltip: "Interval between /proc/stat samples"
                }

                SliderWithLabel {
                    label: "Burst Slack"
                    value: 3000
                    min: 500; max: 10000
                    suffix: "ms"
                    tooltip: "Cooldown before re-entering boost after exit"
                }
            }
        }

        // Section: Response
        GroupBox {
            title: "Response Timing"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 16
                anchors.margins: 16

                SliderWithLabel {
                    label: "Latency Time"
                    value: 200
                    min: 0; max: 800
                    suffix: "ms"
                    tooltip: "Response delay before boosting frequency"
                }

                SliderWithLabel {
                    label: "Margin"
                    value: 25
                    min: 0; max: 80
                    suffix: "%"
                    tooltip: "Headroom multiplier for frequency selection"
                }

                SliderWithLabel {
                    label: "Burst"
                    value: 0
                    min: 0; max: 100
                    suffix: "%"
                    tooltip: "Additional burst intensity modifier"
                }
            }
        }

        // Section: Power Budgets
        GroupBox {
            title: "Power Budgets"
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 16
                anchors.margins: 16

                SliderWithLabel {
                    label: "Slow Limit"
                    value: 3.0
                    min: 0.5; max: 20.0
                    decimals: 1
                    suffix: "W"
                    tooltip: "Power budget for slow response mode (Watts)"
                }

                SliderWithLabel {
                    label: "Fast Limit"
                    value: 6.0
                    min: 1.0; max: 50.0
                    decimals: 1
                    suffix: "W"
                    tooltip: "Power budget for fast response mode (Watts)"
                }

                SliderWithLabel {
                    label: "Fast Limit Capacity"
                    value: 10.0
                    min: 1.0; max: 100.0
                    decimals: 1
                    tooltip: "Maximum capacity cap during burst"
                }
            }
        }

        // Save button
        Button {
            text: "Apply Settings"
            font.pixelSize: 20
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 20
            Layout.bottomMargin: 40
            highlighted: true
            contentItem: Label {
                text: parent.text
                font: parent.font
                color: "#1a1a2e"
                horizontalAlignment: Qt.AlignHCenter
                verticalAlignment: Qt.AlignVCenter
            }
            background: Rectangle {
                radius: 12
                color: parent.hovered ? "#e94560" : "#4ecca3"
            }
            onClicked: {
                // TODO: send settings via DBus
                statusMsg.text = "Settings applied!"
                statusMsg.color = "#4ecca3"
            }
        }

        Label {
            id: statusMsg
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 16
            color: "#a0a0b0"
        }
    }
}
