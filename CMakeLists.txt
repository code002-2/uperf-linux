cmake_minimum_required(VERSION 3.20)
project(uperf-linux VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Warnings as errors for production build
set(CMAKE_C_FLAGS_RELEASE "-O2 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g -Wall -Wextra")

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)

# Optional: libsystemd for journald integration
find_path(LIBSYSTEMD_INCLUDE_DIR NAMES systemd/sd-journal.h
          PATHS /usr/include /usr/local/include /usr/include/systemd)
find_library(LIBSYSTEMD_LIBRARY NAMES systemd
             PATHS /usr/lib /usr/local/lib)

# ---- Library sources ----
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
)

# ---- Main daemon binary ----
add_executable(uperf-linux ${UPERF_SOURCES} ${UPERF_HEADERS})

target_include_directories(uperf-linux PRIVATE
    ${JSONC_INCLUDE_DIRS}
    ${LIBSYSTEMD_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

target_link_libraries(uperf-linux PRIVATE
    ${JSONC_LIBRARIES}
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

# ---- Install targets ----
install(TARGETS uperf-linux uperfctl
    RUNTIME DESTINATION /usr/local/bin
)
install(DIRECTORY config/ DESTINATION /etc/uperf-linux)
install(FILES systemd/uperf-linux.service DESTINATION /etc/systemd/system)
