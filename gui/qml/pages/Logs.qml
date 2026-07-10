import QtQuick 2.15
import QtQuick.Controls 2.15

Page {
    id: page
    padding: 20

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            text: "Log Output"
            font.pixelSize: 32
            font.bold: true
            color: "#e94560"
        }

        TextArea {
            id: logArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            font.family: "Courier New"
            font.pixelSize: 14
            color: "#4ecca3"
            backgroundColor: "#0a0a1a"
            wrapMode: TextEdit.Wrap

            // Load logs from journald
            Component.onCompleted: {
                refreshLogs()
            }

            function refreshLogs() {
                // In production, this would read from journald via DBus
                text = "[info] uperf-linux daemon started\n" +
                       "[info] Config loaded: sm8550 by uperf-linux\n" +
                       "[info] DBus manager created on system bus\n" +
                       "[info] CgroupManager initialized\n" +
                       "[info] HeavyLoadDetector created: nr_cpus=8\n" +
                       "[info] InputMonitor: no touchscreen devices found\n" +
                       "[info] === uperf-linux ready ===\n"
            }
        }

        Button {
            text: "Refresh"
            font.pixelSize: 16
            Layout.alignment: Qt.AlignRight
            onClicked: logArea.refreshLogs()
        }

        Button {
            text: "Clear"
            font.pixelSize: 16
            Layout.alignment: Qt.AlignRight
            onClicked: logArea.text = ""
        }
    }
}
