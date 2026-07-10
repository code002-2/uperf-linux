import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.uperflinux 1.0

ApplicationWindow {
    id: root
    width: 1080
    height: 2400
    minimumWidth: 600
    minimumHeight: 1200
    visible: true
    title: "uperf-linux"
    color: "#1a1a2e"

    // Bottom tab bar
    footer: TabBar {
        id: tabBar
        width: root.width
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft

        TabButton {
            text: "\u{1F3E0}\nDashboard"
            font.pixelSize: 16
            width: tabBar.width / 4
        }
        TabButton {
            text: "\u{1F3AE}\nGames"
            font.pixelSize: 16
            width: tabBar.width / 4
        }
        TabButton {
            text: "\u{1F6E0}\nSettings"
            font.pixelSize: 16
            width: tabBar.width / 4
        }
        TabButton {
            text: "\u{1F4CB}\nLogs"
            font.pixelSize: 16
            width: tabBar.width / 4
        }
    }

    // Page stack
    StackLayout {
        anchors.fill: parent
        currentIndex: tabBar.currentIndex

        DashboardPage { }
        GamesPage { }
        SettingsPage { }
        LogsPage { }
    }
}
