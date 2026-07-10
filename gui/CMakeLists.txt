# Qt6 GUI CMakeLists.txt for uperf-linux
cmake_minimum_required(VERSION 3.20)
project(uperf-gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick DBus Charts)
qt_standard_project_setup()

set(QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml")

# Qt6 resource file
qt_add_resources(QML_RESOURCES gui/resources/icons.qrc)

# Executable
add_executable(uperf-gui
    main.cpp
    ${QML_RESOURCES}
)

target_link_libraries(uperf-gui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    Qt6::Charts
)

target_include_directories(uperf-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Install QML files alongside the binary
install(TARGETS uperf-gui RUNTIME DESTINATION /usr/local/bin)

# Install QML directory
install(DIRECTORY ${QML_DIR}/
    DESTINATION share/uperf-gui/qml
    FILES_MATCHING PATTERN "*.qml"
)

# Desktop entry
install(FILES uperf-gui.desktop
    DESTINATION share/applications
)

# Icons
install(FILES resources/icons/uperf.png
    DESTINATION share/icons/hicolor/128x128/apps
)
