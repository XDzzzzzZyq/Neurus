import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: mainWindow

    visible: true
    width: 800
    height: 600
    title: "Neurus"

    color: "#1a1a26"

    // --- Menu Bar ---
    menuBar: MenuBar {
        Menu {
            title: "File"

            MenuItem {
                text: "Exit"
                onTriggered: Qt.quit()
            }
        }
    }

    // --- Central Content ---
    Rectangle {
        anchors.centerIn: parent
        color: "transparent"

        Column {
            anchors.centerIn: parent
            spacing: 16

            // Title
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Neurus"
                font.pixelSize: 32
                font.bold: true
                color: "#ffffff"
            }

            // Subtitle
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Vulkan 1.4 Renderer"
                font.pixelSize: 14
                color: "#888899"
            }
        }
    }

    // --- Status Bar ---
    footer: Rectangle {
        height: 24
        color: "#0d0d14"

        Row {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 16

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Vulkan 1.4"
                font.pixelSize: 11
                color: "#666677"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "GPU: " + (EventBus ? EventBus.gpuName : "Unknown")
                font.pixelSize: 11
                color: "#666677"
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "800x600"
                font.pixelSize: 11
                color: "#666677"
            }
        }
    }

    // --- Render Loop Timer ---
    Timer {
        id: renderTimer
        interval: 16  // ~60 FPS
        running: true
        repeat: true
        onTriggered: {
            if (EventBus) {
                EventBus.renderRequested();
            }
        }
    }
}
