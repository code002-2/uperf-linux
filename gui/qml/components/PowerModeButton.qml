import QtQuick 2.15
import QtQuick.Controls 2.15

// Reusable power mode button component
Button {
    id: root
    property string label: ""
    property string icon: ""
    property bool active: false

    implicitHeight: 120
    implicitWidth: 160

    contentItem: Column {
        anchors.centerIn: parent
        spacing: 8

        Label {
            text: root.icon
            font.pixelSize: 36
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Label {
            text: root.label
            font.pixelSize: 18
            font.bold: true
            color: root.active ? "#1a1a2e" : "#ffffff"
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    background: Rectangle {
        radius: 16
        color: root.active ? "#4ecca3" : "#16213e"
        border.color: root.active ? "#4ecca3" : "#0f3460"
        border.width: root.active ? 3 : 1
    }

    onClicked: {
        if (!root.active) {
            parent.children.forEach(child => {
                if (child !== root && child.hasOwnProperty("active"))
                    child.active = false
            })
        }
        root.active = !root.active
    }
}
