cmake_minimum_required(VERSION 3.24)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(PicoATE.UI
        VERSION 0.1.0
        DESCRIPTION "PicoATE Qt Runner UI"
        LANGUAGES CXX
    )

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)

    find_package(Qt6 REQUIRED COMPONENTS Core Test Widgets)
    enable_testing()

    get_filename_component(PICOATE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    if(NOT TARGET PicoATECore)
        add_subdirectory(
            "${PICOATE_ROOT_DIR}/src/core"
            "${CMAKE_CURRENT_BINARY_DIR}/engine/core"
        )
    endif()
endif()

add_subdirectory(src)

option(PICOATE_BUILD_UI_TESTS "Build PicoATE UI tests" ON)
if(PICOATE_BUILD_UI_TESTS)
    add_subdirectory(tests)
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT PicoATEUi)
endif()
