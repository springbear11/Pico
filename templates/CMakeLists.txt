cmake_minimum_required(VERSION 3.24)

project(PicoATE.Plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(PICOATE_PLUGIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
    "Directory containing generated PicoATE plugin DLLs")

add_library(PicoATEPluginSdk INTERFACE)
target_include_directories(PicoATEPluginSdk INTERFACE
    "${CMAKE_SOURCE_DIR}/common/include"
    "${CMAKE_SOURCE_DIR}/third_party"
)
target_compile_features(PicoATEPluginSdk INTERFACE cxx_std_20)

function(picoate_add_plugin_category category entry_source)
    set(category_dir "${CMAKE_SOURCE_DIR}/${category}")
    if(NOT IS_DIRECTORY "${category_dir}")
        return()
    endif()

    file(GLOB category_headers CONFIGURE_DEPENDS
        "${category_dir}/*.h"
        "${category_dir}/*.hpp"
    )
    file(GLOB category_children RELATIVE "${category_dir}" "${category_dir}/*")

    foreach(vendor IN LISTS category_children)
        set(vendor_dir "${category_dir}/${vendor}")
        if(NOT IS_DIRECTORY "${vendor_dir}" OR vendor MATCHES "^[_\.]")
            continue()
        endif()

        file(GLOB_RECURSE vendor_sources CONFIGURE_DEPENDS
            "${vendor_dir}/*.c"
            "${vendor_dir}/*.cc"
            "${vendor_dir}/*.cpp"
            "${vendor_dir}/*.h"
            "${vendor_dir}/*.hpp"
        )
        set(compilation_units ${vendor_sources})
        list(FILTER compilation_units INCLUDE REGEX "\\.(c|cc|cpp)$")
        if(NOT compilation_units)
            continue()
        endif()

        string(MAKE_C_IDENTIFIER "PicoATE_${category}_${vendor}" target_name)
        add_library(${target_name} SHARED
            ${category_headers}
            "${entry_source}"
            ${vendor_sources}
        )
        target_link_libraries(${target_name} PRIVATE PicoATEPluginSdk)
        target_include_directories(${target_name} PRIVATE
            "${category_dir}"
            "${vendor_dir}"
        )
        target_compile_definitions(${target_name} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
        set_target_properties(${target_name} PROPERTIES
            OUTPUT_NAME "PicoATE.${category}.${vendor}"
            FOLDER "${category}"
            RUNTIME_OUTPUT_DIRECTORY "${PICOATE_PLUGIN_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${PICOATE_PLUGIN_OUTPUT_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY "${PICOATE_PLUGIN_OUTPUT_DIR}/lib"
        )

        set(runtime_dir "${CMAKE_SOURCE_DIR}/dependencies/${category}/${vendor}/x64")
        if(IS_DIRECTORY "${runtime_dir}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${runtime_dir}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Copying ${category}/${vendor} runtime DLLs"
            )
        endif()

        source_group(TREE "${category_dir}" PREFIX "${category}" FILES
            ${category_headers} "${entry_source}" ${vendor_sources})
        message(STATUS "Discovered plugin: ${category}/${vendor} -> PicoATE.${category}.${vendor}.dll")
    endforeach()
endfunction()

picoate_add_plugin_category(CAN "${CMAKE_SOURCE_DIR}/CAN/CanPluginBridge.cpp")
picoate_add_plugin_category(VISA "${CMAKE_SOURCE_DIR}/VISA/VisaPluginBridge.cpp")
picoate_add_plugin_category(Modbus "${CMAKE_SOURCE_DIR}/Modbus/ModbusPluginBridge.cpp")
