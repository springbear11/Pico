qt_add_executable(PicoATEUiTests
    ExecutionViewModelTests.cpp
)

target_link_libraries(PicoATEUiTests
    PRIVATE
        PicoATEUiRuntime
        Qt6::Core
        Qt6::Test
)

target_compile_definitions(PicoATEUiTests
    PRIVATE
        PICOATE_UI_TEST_PROJECT_DIR="${PICOATE_ROOT_DIR}"
)

set_target_properties(PicoATEUiTests
    PROPERTIES
        FOLDER "Tests/UI"
)

add_test(NAME PicoATEUiTests COMMAND PicoATEUiTests)
