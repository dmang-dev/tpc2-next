if(NOT APPLE)
    return()
endif()

set(_app "${CMAKE_INSTALL_PREFIX}/tpc2.app")
set(_cli "${_app}/Contents/MacOS/tpc2cli")

if(NOT EXISTS "${_cli}")
    message(FATAL_ERROR "Expected CLI executable not found: ${_cli}")
endif()

find_program(_otool NAMES otool REQUIRED)
find_program(_install_name_tool NAMES install_name_tool REQUIRED)

execute_process(
    COMMAND "${_otool}" -L "${_cli}"
    OUTPUT_VARIABLE _otool_output
    RESULT_VARIABLE _otool_result)
if(NOT _otool_result EQUAL 0)
    message(FATAL_ERROR "otool failed for ${_cli}")
endif()

string(REPLACE "\n" ";" _otool_lines "${_otool_output}")
foreach(_line IN LISTS _otool_lines)
    if(NOT _line MATCHES "/opt/homebrew/.*\\.framework")
        continue()
    endif()
    string(REGEX REPLACE "^[ \t]*([^ ]+).*$" "\\1" _old "${_line}")
    string(REGEX REPLACE "^.*/([^/]+)\\.framework/.*$" "\\1"
        _framework "${_old}")
    set(_new
        "@executable_path/../Frameworks/${_framework}.framework/Versions/A/${_framework}")
    execute_process(
        COMMAND "${_install_name_tool}" -change "${_old}" "${_new}" "${_cli}"
        RESULT_VARIABLE _change_result)
    if(NOT _change_result EQUAL 0)
        message(FATAL_ERROR
            "install_name_tool failed to rewrite ${_old}")
    endif()
endforeach()

find_program(_codesign NAMES codesign REQUIRED)
execute_process(
    COMMAND "${_codesign}" --force --deep --sign - "${_app}"
    RESULT_VARIABLE _codesign_result)
if(NOT _codesign_result EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${_app}")
endif()
