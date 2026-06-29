cmake_minimum_required(VERSION 3.24)

project(PicoATE.CanHardwareTemplate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)

option(PICOATE_CAN_TEMPLATE_SIMULATION
       "Build the adapter with the software loopback implementation"
       ON)
set(VENDOR_CAN_INCLUDE_DIR "" CACHE PATH "Vendor CAN SDK include directory")
set(VENDOR_CAN_LIBRARY "" CACHE FILEPATH "Vendor CAN SDK import library (.lib)")

add_library(PicoATECanHardwareTemplate SHARED
    PicoATECanHardwareModule.cpp
    VendorCanAdapter.cpp
    VendorCanAdapter.h
)

target_link_libraries(PicoATECanHardwareTemplate PRIVATE Qt6::Core)
target_compile_definitions(PicoATECanHardwareTemplate PRIVATE
    PICOATE_CAN_TEMPLATE_SIMULATION=$<BOOL:${PICOATE_CAN_TEMPLATE_SIMULATION}>
)

if(VENDOR_CAN_INCLUDE_DIR)
    target_include_directories(PicoATECanHardwareTemplate PRIVATE ${VENDOR_CAN_INCLUDE_DIR})
endif()

if(VENDOR_CAN_LIBRARY)
    target_link_libraries(PicoATECanHardwareTemplate PRIVATE ${VENDOR_CAN_LIBRARY})
endif()

set_target_properties(PicoATECanHardwareTemplate PROPERTIES
    OUTPUT_NAME "PicoATE.CanHardwareTemplate"
)
