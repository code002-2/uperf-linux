cmake_minimum_required(VERSION 3.20)
project(uperf-linux VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings as errors for production build
set(CMAKE_C_FLAGS_RELEASE "-O2 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g -Wall -Wextra")

option(BUILD_GUI "Build Qt6 GUI" ON)
option(BUILD_TESTS "Build tests" ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)

# GLib for DBus integration (daemon side)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0>=2.56)

# Optional: libsystemd for journald integration
find_path(LIBSYSTEMD_INCLUDE_DIR NAMES systemd/sd-journal.h
          PATHS /usr/include /usr/local/include /usr/include/systemd)
find_library(LIBSYSTEMD_LIBRARY NAMES systemd
             PATHS /usr/lib /usr/local/lib)

# ---- Daemon sources ----
set(UPERF_SOURCES
    src/log.c
    src/config_parser.c
    src/sysfs_writer.c
    src/power_model.c
    src/state_machine.c
    src/input_monitor.c
    src/cgroup_manager.c
    src/heavyload_detector.c
    src/game_scanner.c
    src/dbus_interface.c
    src/main.c
)

set(UPERF_HEADERS
    src/include/log.h
    src/include/config.h
    src/include/sysfs_writer.h
    src/include/power_model.h
    src/include/state_machine.h
    src/include/input_monitor.h
    src/include/cgroup_manager.h
    src/include/heavyload_detector.h
    src/include/game_scanner.h
    src/include/dbus_interface.h
)

# ---- Main daemon binary ----
add_executable(uperf-linux ${UPERF_SOURCES} ${UPERF_HEADERS})

target_include_directories(uperf-linux PRIVATE
    ${JSONC_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
    ${LIBSYSTEMD_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

target_link_libraries(uperf-linux PRIVATE
    ${JSONC_LIBRARIES}
    ${GLIB2_LIBRARIES}
    pthread
    rt
)

if(LIBSYSTEMD_LIBRARY AND LIBSYSTEMD_INCLUDE_DIR)
    target_link_libraries(uperf-linux PRIVATE ${LIBSYSTEMD_LIBRARY})
    target_compile_definitions(uperf-linux PRIVATE HAVE_LIBSYSTEMD=1)
endif()

# ---- CLI tool ----
add_executable(uperfctl src/cli.c)
target_include_directories(uperfctl PRIVATE
    ${JSONC_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)
target_link_libraries(uperfctl PRIVATE
    ${JSONC_LIBRARIES}
    pthread
)

# ---- Tests ----
if(BUILD_TESTS)
    enable_testing()

    add_executable(test_config_parser tests/test_config_parser.c src/log.c)
    target_include_directories(test_config_parser PRIVATE
        ${JSONC_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    )
    target_link_libraries(test_config_parser PRIVATE ${JSONC_LIBRARIES} pthread)

    add_executable(test_state_machine tests/test_state_machine.c src/log.c src/state_machine.c)
    target_include_directories(test_state_machine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    )
    target_link_libraries(test_state_machine PRIVATE pthread)

    add_executable(test_power_model tests/test_power_model.c src/log.c src/power_model.c)
    target_include_directories(test_power_model PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    )
    target_link_libraries(test_power_model PRIVATE pthread)

    add_executable(test_sysfs_writer tests/test_sysfs_writer.c src/log.c src/sysfs_writer.c)
    target_include_directories(test_sysfs_writer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    )
    target_link_libraries(test_sysfs_writer PRIVATE pthread)
endif()

# ---- Qt6 GUI ----
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick DBus Charts)
    qt_standard_project_setup()

    add_subdirectory(gui)
endif()

# ---- Install targets ----
install(TARGETS uperf-linux uperfctl
    RUNTIME DESTINATION /usr/local/bin
)
install(DIRECTORY config/ DESTINATION /etc/uperf-linux)
install(FILES systemd/uperf-linux.service DESTINATION /etc/systemd/system)

# ---- Deb packaging ----
if(BUILD_GUI AND Qt6_FOUND)
    # Create a package target for deb
    add_custom_target(package
        COMMAND cmake -E make_directory "${CMAKE_BINARY_DIR}/deb"
        COMMAND cpack --config CPackConfig.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building deb package"
    )
endif()
