cmake_minimum_required(VERSION 3.24)

option(PICOATE_BUILD_UI "Build the PicoATE Qt Runner UI" OFF)

if(PICOATE_BUILD_UI)
    set(PICOATE_PROJECT_NAME "PicoATE.All")
else()
    set(PICOATE_PROJECT_NAME "PicoATE")
endif()

project(${PICOATE_PROJECT_NAME}
    VERSION 0.1.0
    DESCRIPTION "Small ATE execution runtime core"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(PICOATE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "PicoATE repository root")

set(PICOATE_QT_COMPONENTS Core Test)
if(PICOATE_BUILD_UI)
    list(APPEND PICOATE_QT_COMPONENTS Widgets)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${PICOATE_QT_COMPONENTS})
find_package(Python3 COMPONENTS Interpreter QUIET)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

if(PICOATE_BUILD_UI)
    add_subdirectory(ui)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT PicoATEUi)
else()
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT PicoATECli)
endif()
