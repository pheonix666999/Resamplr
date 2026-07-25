juce_add_console_app(padflow_tests PRODUCT_NAME "PadFlow Tests")
target_sources(
    padflow_tests
    PRIVATE TestMain.cpp
            Unit/FoundationTests.cpp
            Unit/Milestone1AssetTests.cpp
            Unit/Milestone1AudioTests.cpp
            Unit/Milestone1DeviceTests.cpp
            Unit/Milestone1InputTests.cpp
            Unit/Milestone1ModelTests.cpp
            Unit/Milestone1PreviewTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone1UiTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone2WaveformTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone2EditTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone2UiTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone2DerivedTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone2CaptureTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone3ChopTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone3AnalysisTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone3AssignmentTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone3PersistenceTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone3AuditionTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone3UiTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone4TimeTests.cpp)
target_sources(padflow_tests PRIVATE Unit/Milestone4TransportTests.cpp)
target_link_libraries(padflow_tests PRIVATE padflow_audio padflow_ui)
padflow_enable_project_warnings(padflow_tests)
padflow_set_output_directory(padflow_tests)

add_test(NAME padflow-unit-tests COMMAND padflow_tests)
