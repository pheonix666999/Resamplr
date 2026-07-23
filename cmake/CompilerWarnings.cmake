if(MSVC)
    set(PADFLOW_PROJECT_WARNING_OPTIONS /W4 /WX /permissive- /Zc:__cplusplus)
else()
    set(PADFLOW_PROJECT_WARNING_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Werror)
endif()

function(padflow_enable_project_warnings target)
    get_target_property(target_sources ${target} SOURCES)
    foreach(source IN LISTS target_sources)
        if(NOT source MATCHES "external/JUCE" AND NOT source MATCHES "juce_")
            set_property(
                SOURCE "${source}"
                APPEND
                PROPERTY COMPILE_OPTIONS ${PADFLOW_PROJECT_WARNING_OPTIONS})
        endif()
    endforeach()
endfunction()

function(padflow_set_output_directory target)
    set_target_properties(
        ${target}
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
                   LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
                   ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

    if(CMAKE_CONFIGURATION_TYPES)
        foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(
                ${target}
                PROPERTIES
                    "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
                    "LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/lib"
                    "ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/lib")
        endforeach()
    endif()
endfunction()
