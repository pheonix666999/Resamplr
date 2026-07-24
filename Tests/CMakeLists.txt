juce_add_console_app(padflow_tests PRODUCT_NAME "PadFlow Tests")
target_sources(
    padflow_tests
    PRIVATE TestMain.cpp
            Unit/FoundationTests.cpp
            Unit/Milestone1AssetTests.cpp
            Unit/Milestone1ModelTests.cpp)
target_link_libraries(padflow_tests PRIVATE padflow_audio)
padflow_enable_project_warnings(padflow_tests)
padflow_set_output_directory(padflow_tests)

add_test(NAME padflow-unit-tests COMMAND padflow_tests)
