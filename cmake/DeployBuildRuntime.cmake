# SPDX-License-Identifier: MIT
#
# Copy the non-system runtime DLLs next to freshly linked Windows executables.
# This makes build-tree binaries runnable by double-clicking them, without
# requiring the MSYS2 MinGW bin directory on PATH. The install/CPack rules use
# the same dependency discovery for the portable package.

cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

if(NOT DEFINED TPC2_EXECUTABLES OR NOT DEFINED TPC2_DEPLOY_DIR)
    message(FATAL_ERROR "TPC2_EXECUTABLES and TPC2_DEPLOY_DIR are required")
endif()

string(REPLACE "|" ";" _executables "${TPC2_EXECUTABLES}")
if(NOT _executables)
    message(FATAL_ERROR "No executables were provided")
endif()

set(_plugin_dirs)
set(_plugin_dlls)
if(DEFINED TPC2_QT_PLUGIN_DIR AND IS_DIRECTORY "${TPC2_QT_PLUGIN_DIR}")
    # Qt looks for plugin directories next to the executable when there is no
    # qt.conf. Copy the plugin families this application needs; optional SQL
    # drivers would otherwise add unresolved client-library dependencies.
    foreach(_plugin_name IN ITEMS
            platforms styles imageformats iconengines platformthemes generic)
        set(_plugin_dir "${TPC2_QT_PLUGIN_DIR}/${_plugin_name}")
        if(IS_DIRECTORY "${_plugin_dir}")
            list(APPEND _plugin_dirs "${_plugin_dir}")
            file(GLOB_RECURSE _plugin_name_dlls "${_plugin_dir}/*.dll")
            list(APPEND _plugin_dlls ${_plugin_name_dlls})
        endif()
    endforeach()
endif()

set(_search_dirs)
if(DEFINED ENV{PATH})
    list(APPEND _search_dirs "$ENV{PATH}")
endif()
if(DEFINED TPC2_LIBRARY_DIR)
    list(APPEND _search_dirs "${TPC2_LIBRARY_DIR}")
endif()

# Resolve dependencies from temporary copies outside the deploy directory.
# Otherwise a previous deployment leaves Qt DLLs next to the executable, and
# CMake reports the same DLL as both a local and a toolchain dependency.
set(_staging_dir "${TPC2_DEPLOY_DIR}/.tpc2-deploy-staging")
file(REMOVE_RECURSE "${_staging_dir}")
file(MAKE_DIRECTORY "${_staging_dir}")

set(_staged_executables)
foreach(_executable IN LISTS _executables)
    if(NOT EXISTS "${_executable}")
        message(FATAL_ERROR "Executable not found: ${_executable}")
    endif()
    get_filename_component(_name "${_executable}" NAME)
    set(_staged "${_staging_dir}/${_name}")
    file(COPY_FILE "${_executable}" "${_staged}")
    list(APPEND _staged_executables "${_staged}")
endforeach()

set(_dependency_args EXECUTABLES ${_staged_executables})
if(_plugin_dlls)
    list(APPEND _dependency_args MODULES ${_plugin_dlls})
endif()

file(GET_RUNTIME_DEPENDENCIES
    ${_dependency_args}
    RESOLVED_DEPENDENCIES_VAR _runtime_deps
    UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
    DIRECTORIES ${_search_dirs}
    PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss].*"
)

foreach(_dep IN LISTS _runtime_deps)
    if(_dep MATCHES "[Mm]ingw64[/\\\\]bin"
       OR _dep MATCHES "[Uu]crt64[/\\\\]bin")
        get_filename_component(_name "${_dep}" NAME)
        file(COPY_FILE "${_dep}" "${TPC2_DEPLOY_DIR}/${_name}"
            ONLY_IF_DIFFERENT)
    endif()
endforeach()

foreach(_plugin_dir IN LISTS _plugin_dirs)
    if(IS_DIRECTORY "${_plugin_dir}")
        file(COPY "${_plugin_dir}" DESTINATION "${TPC2_DEPLOY_DIR}")
    endif()
endforeach()

file(REMOVE_RECURSE "${_staging_dir}")

if(_unresolved_deps)
    list(REMOVE_DUPLICATES _unresolved_deps)
    message(STATUS "Unresolved runtime dependencies: ${_unresolved_deps}")
endif()
