import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Page {
    id: page
    padding: 20

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        // Title
        Label {
            text: "uperf-linux"
            font.pixelSize: 48
            font.bold: true
            color: "#e94560"
            Layout.alignment: Qt.AlignHCenter
        }

        // Subtitle with SoC info
        Label {
            text: "SM8550 · Snapdragon 8 Gen 2"
            font.pixelSize: 20
            color: "#a0a0b0"
            Layout.alignment: Qt.AlignHCenter
        }

        // Power mode selector
        GroupBox {
            title: "Power Mode"
            Layout.fillWidth: true
            Layout.preferredHeight: 200

            RowLayout {
                anchors.fill: parent
                spacing: 16
                anchors.margins: 16

                PowerModeButton {
                    label: "Balance"
                    icon: "\u{1F7E2}"
                    active: root.modeProxy.currentMode === "balance"
                    onClicked: root.modeProxy.setMode("balance")
                    Layout.fillWidth: true
                }

                PowerModeButton {
                    label: "Powersave"
                    icon: "\u{1F50B}"
                    active: root.modeProxy.currentMode === "powersave"
                    onClicked: root.modeProxy.setMode("powersave")
                    Layout.fillWidth: true
                }

                PowerModeButton {
                    label: "Performance"
                    icon: "\u{1F525}"
                    active: root.modeProxy.currentMode === "performance"
                    onClicked: root.modeProxy.setMode("performance")
                    Layout.fillWidth: true
                }
            }
        }

        // Current scene badge
        Label {
            text: "Scene: " + (root.modeProxy.currentScene || "idle").toUpperCase()
            font.pixelSize: 24
            font.bold: true
            color: root.modeProxy.isHeavyLoad ? "#ff6b6b" : "#4ecca3"
            horizontalAlignment: Qt.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }

        // CPU Frequency Chart
        GroupBox {
            title: "CPU Frequency (MHz)"
            Layout.fillWidth: true
            Layout.preferredHeight: 350

            ChartView {
                id: freqChart
                anchors.fill: parent
                anchors.margins: 10
                theme: ChartView.ChartThemeDark
                antialiasing: true
                legend.visible: false
                valueAxisX {
                    titleText: "Time"
                    gridVisible: true
                }
                valueAxisY {
                    titleText: "MHz"
                    min: 0
                    max: 5000
                    gridVisible: true
                }

                LineSeries {
                    name: "Prime"
                    color: "#ff6b6b"
                    useOpenGL: true
                    id: seriesPrime
                }
                LineSeries {
                    name: "Performance"
                    color: "#4ecdc4"
                    useOpenGL: true
                    id: seriesPerf
                }
                LineSeries {
                    name: "Efficiency"
                    color: "#45b7d1"
                    useOpenGL: true
                    id: seriesEff
                }
            }

            // Timer to update chart from DBus
            Timer {
                interval: 500
                running: true
                repeat: true
                onTriggered: {
                    var freqs = root.modeProxy.frequencies
                    var loads = root.modeProxy.loads
                    if (freqs.length >= 3) {
                        seriesPrime.append(Date.now(), freqs[0])
                        seriesPerf.append(Date.now(), freqs[1])
                        seriesEff.append(Date.now(), freqs[2])
                        // Keep only last 60 points
                        if (seriesPrime.count > 60) {
                            seriesPrime.remove(0)
                            seriesPerf.remove(0)
                            seriesEff.remove(0)
                        }
                    }
                }
            }
        }

        // CPU Load Meters
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            spacing: 16

            Column {
                Label { text: "System Load"; color: "#a0a0b0"; font.pixelSize: 18 }
                ProgressBar {
                    id: loadBar
                    value: root.modeProxy.isHeavyLoad ? 0.9 : 0.3
                    from: 0; to: 1
                    Layout.fillWidth: true
                }
                Label {
                    text: Math.round(loadBar.value * 100) + "%"
                    font.pixelSize: 20; font.bold: true
                    color: loadBar.value > 0.7 ? "#ff6b6b" : "#4ecca3"
                }
            }

            Column {
                Label { text: "Heavy Load"; color: "#a0a0b0"; font.pixelSize: 18 }
                Switch {
                    checked: root.modeProxy.isHeavyLoad
                    enabled: false  // Read-only indicator
                }
                Label {
                    text: root.modeProxy.isHeavyLoad ? "ACTIVE" : "Normal"
                    font.pixelSize: 20; font.bold: true
                    color: root.modeProxy.isHeavyLoad ? "#ff6b6b" : "#4ecca3"
                }
            }
        }

        // Per-CPU frequency display
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true

            GridView {
                id: cpuGrid
                width: page.width - 40
                columns: 4
                cellWidth: (width - 16) / 4
                cellHeight: 100
                model: 8

                delegate: Rectangle {
                    width: cpuGrid.cellWidth
                    height: cpuGrid.cellHeight
                    radius: 12
                    color: "#16213e"
                    border.color: "#0f3460"

                    Column {
                        anchors.centerIn: parent
                        spacing: 4

                        Label {
                            text: "CPU" + (modelIndex)
                            color: "#a0a0b0"
                            font.pixelSize: 14
                            horizontalAlignment: Qt.AlignHCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Label {
                            text: freqs ? Math.round(freqs[modelIndex] / 1000 * 100) / 100 + " GHz" : "--"
                            color: "#4ecca3"
                            font.pixelSize: 16
                            font.bold: true
                            horizontalAlignment: Qt.AlignHCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }
            }
        }

        // Thermal zones
        GroupBox {
            title: "Thermal Zones"
            Layout.fillWidth: true
            Layout.preferredHeight: 160

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                // Show max temp prominently
                Label {
                    text: "Max: " + (root.modeProxy.maxTemp > 0 ?
                        (root.modeProxy.maxTemp / 1000) + "." +
                        String(root.modeProxy.maxTemp % 1000).padStart(3, '0') + "°C" :
                        "--°C")
                    font.pixelSize: 20
                    font.bold: true
                    color: {
                        var temp = root.modeProxy.maxTemp / 1000;
                        if (temp >= 80) return "#ff6b6b";
                        if (temp >= 70) return "#ffa500";
                        return "#4ecca3";
                    }
                    Layout.alignment: Qt.AlignHCenter
                }

                // Zone count indicator
                Label {
                    text: "Thermal state: " + (root.modeProxy.thermalState || "NORMAL")
                    font.pixelSize: 14
                    color: root.modeProxy.thermalState === "CRITICAL" ? "#ff6b6b" :
                          root.modeProxy.thermalState === "THROTTLED" ? "#ffa500" :
                          root.modeProxy.thermalState === "WARNING" ? "#ffd700" : "#4ecca3"
                    Layout.alignment: Qt.AlignHCenter
                }

                // Progress bar for visual severity
                ProgressBar {
                    value: root.modeProxy.maxTemp > 0 ?
                        Math.min(1.0, (root.modeProxy.maxTemp / 1000 - 40) / 60) : 0
                    from: 0; to: 1
                    Layout.fillWidth: true
                    contentItem: Item {
                        Rectangle {
                            width: parent.width
                            height: parent.height
                            radius: 4
                            color: "#16213e"
                        }
                        Rectangle {
                            width: parent.width * parent.visualPosition
                            height: parent.height
                            radius: 4
                            color: {
                                var t = parent.parent.value;
                                if (t >= 0.8) return "#ff6b6b";
                                if (t >= 0.5) return "#ffa500";
                                return "#4ecca3";
                            }
                        }
                    }
                }
            }
        }
    }
}
