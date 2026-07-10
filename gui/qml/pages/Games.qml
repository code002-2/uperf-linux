import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: page
    padding: 20

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            text: "Detected Games"
            font.pixelSize: 32
            font.bold: true
            color: "#e94560"
        }

        Label {
            text: "Running game and game-like processes"
            font.pixelSize: 16
            color: "#a0a0b0"
        }

        // Game list
        ListView {
            id: gameList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            model: ListModel {
                id: gameModel
                // Populated from DBus via C++ proxy
            }

            delegate: Rectangle {
                width: gameList.width
                height: 100
                radius: 12
                color: "#16213e"
                border.color: "#0f3460"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    // Icon
                    Rectangle {
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        radius: 12
                        color: "#0f3460"

                        Label {
                            anchors.centerIn: parent
                            text: "\u{1F3AE}"
                            font.pixelSize: 28
                        }
                    }

                    // Info
                    Column {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: model.comm || "Unknown"
                            font.pixelSize: 20
                            font.bold: true
                            color: "#ffffff"
                            truncationMode: TruncateMode.Elide
                        }

                        Label {
                            text: "PID: " + model.pid + "  |  " + (model.cmdline || "")
                            font.pixelSize: 14
                            color: "#a0a0b0"
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }

                    // Mode selector
                    ComboBox {
                        Layout.preferredWidth: 140
                        model: ["balance", "powersave", "performance"]
                        currentIndex: {
                            if (model.mode === "performance") return 2
                            if (model.mode === "powersave") return 1
                            return 0
                        }
                        onActivated: {
                            // TODO: update per-app mode via DBus
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        // Empty state
        Label {
            visible: gameModel.count === 0
            text: "No game processes detected.\nGames are scanned periodically."
            font.pixelSize: 18
            color: "#666680"
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
