cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

project(TPC2Next VERSION 2.2.0 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TPC2_BUILD_GUI "Build the Qt desktop application" ON)
option(TPC2_BUILD_CLI "Build the command line tool" ON)
option(TPC2_BUILD_TESTS "Build the unit tests" ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(TPC2_WARNINGS "")
if(MSVC)
    set(TPC2_WARNINGS /W4 /permissive- /utf-8)
else()
    set(TPC2_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core SerialPort)
find_package(Qt6 6.4 QUIET COMPONENTS Widgets)
if(TPC2_BUILD_GUI AND NOT Qt6Widgets_FOUND)
    message(WARNING "Qt6 Widgets not found; building without the GUI")
endif()

# Locate the Qt plugin tree so the Windows build-tree deployment can copy the
# platform/style/image plugins next to the freshly linked executable.
set(TPC2_QT_PLUGIN_DIR "")
if(TARGET Qt6::Core)
    set(_qt_cmake_dir "${Qt6_DIR}")
    if(NOT _qt_cmake_dir)
        set(_qt_cmake_dir "${Qt6Core_DIR}")
    endif()
    if(_qt_cmake_dir)
        get_filename_component(_qt_prefix "${_qt_cmake_dir}/../../.." ABSOLUTE)
        foreach(_plugin_candidate
                "${_qt_prefix}/share/qt6/plugins"
                "${_qt_prefix}/plugins")
            if(IS_DIRECTORY "${_plugin_candidate}")
                set(TPC2_QT_PLUGIN_DIR "${_plugin_candidate}")
                break()
            endif()
        endforeach()
    endif()
endif()

add_library(tpc2core STATIC
    src/core/Error.h
    src/core/Protocol.h
    src/core/Protocol.cpp
    src/core/Link.h
    src/core/Session.h
    src/core/Session.cpp
    src/io/ByteTransport.h
    src/io/BridgeFrame.h
    src/io/BridgeFrame.cpp
    src/link/BridgeLink.h
    src/link/BridgeLink.cpp
    src/link/DemoLink.h
    src/link/DemoLink.cpp
    src/link/ParallelLink.h
    src/link/ParallelLink.cpp
    src/platform/ParallelPort.h
)

target_include_directories(tpc2core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(tpc2core PUBLIC TPC2_VERSION="${PROJECT_VERSION}")
target_compile_options(tpc2core PRIVATE ${TPC2_WARNINGS})

if(WIN32)
    target_sources(tpc2core PRIVATE src/platform/ParallelPortWindows.cpp)
elseif(APPLE)
    target_sources(tpc2core PRIVATE src/platform/ParallelPortMac.cpp)
elseif(UNIX)
    target_sources(tpc2core PRIVATE src/platform/ParallelPortLinux.cpp)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

if(TPC2_BUILD_CLI)
    add_executable(tpc2cli
        src/cli/main.cpp
        src/io/QtSerialTransport.h
        src/io/QtSerialTransport.cpp
    )
    target_link_libraries(tpc2cli PRIVATE tpc2core Qt6::Core Qt6::SerialPort)
    target_compile_options(tpc2cli PRIVATE ${TPC2_WARNINGS})
    if(WIN32)
        set_target_properties(tpc2cli PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
endif()

if(TPC2_BUILD_GUI AND Qt6Widgets_FOUND)
    add_executable(tpc2 WIN32
        src/gui/main.cpp
        src/gui/MainWindow.h
        src/gui/MainWindow.cpp
        src/gui/DeviceWorker.h
        src/gui/DeviceWorker.cpp
        src/gui/TransferPage.h
        src/gui/TransferPage.cpp
        src/gui/RemotePage.h
        src/gui/RemotePage.cpp
        src/gui/Theme.h
        src/gui/Theme.cpp
        src/gui/BackendConfig.h
        src/io/QtSerialTransport.h
        src/io/QtSerialTransport.cpp
    )
    target_link_libraries(tpc2 PRIVATE tpc2core Qt6::Core Qt6::Widgets Qt6::SerialPort)
    target_compile_options(tpc2 PRIVATE ${TPC2_WARNINGS})
    if(APPLE)
        set_target_properties(tpc2 PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "TPC2 Next"
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.dmangdev.tpc2next"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        )

        # iconutil is part of the macOS developer tools. Generate the .icns
        # from the committed iconset so the repository does not need to carry
        # a generated binary.
        find_program(ICONUTIL_EXECUTABLE iconutil)
        file(GLOB _tpc2_iconset_files
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/tpc2-next.iconset/*.png")
        if(ICONUTIL_EXECUTABLE AND _tpc2_iconset_files)
            set(_tpc2_icns "${CMAKE_CURRENT_BINARY_DIR}/tpc2-next.icns")
            execute_process(
                COMMAND "${ICONUTIL_EXECUTABLE}" -c icns
                    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/tpc2-next.iconset"
                    -o "${_tpc2_icns}"
                RESULT_VARIABLE _tpc2_icon_result)
            if(NOT _tpc2_icon_result EQUAL 0)
                message(FATAL_ERROR "iconutil failed to create ${_tpc2_icns}")
            endif()
            set_source_files_properties("${_tpc2_icns}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources")
            target_sources(tpc2 PRIVATE "${_tpc2_icns}")
            set_target_properties(tpc2 PROPERTIES
                MACOSX_BUNDLE_ICON_FILE "tpc2-next.icns")
        else()
            message(WARNING
                "iconutil or packaging/icons/tpc2-next.iconset is missing; "
                "the macOS app will use the default icon")
        endif()
    endif()

    if(WIN32)
        set(TPC2_WINDOWS_ICON
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/tpc2-next.ico")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/tpc2.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/tpc2.rc"
            @ONLY)
        target_sources(tpc2 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/tpc2.rc")
    endif()
endif()

# The portable Linux package puts Qt libraries in ../lib and the macOS CLI
# lives inside the app bundle, next to the bundled frameworks.
if(APPLE)
    if(TARGET tpc2cli)
        set_target_properties(tpc2cli PROPERTIES
            INSTALL_RPATH "@executable_path/../Frameworks")
    endif()
elseif(UNIX)
    foreach(_target tpc2 tpc2cli)
        if(TARGET ${_target})
            set_target_properties(${_target} PROPERTIES
                INSTALL_RPATH "$ORIGIN/../lib")
        endif()
    endforeach()
endif()

if(TPC2_BUILD_TESTS)
    enable_testing()
    add_executable(tpc2_tests tests/main.cpp)
    target_link_libraries(tpc2_tests PRIVATE tpc2core)
    target_compile_options(tpc2_tests PRIVATE ${TPC2_WARNINGS})
    add_test(NAME tpc2-tests COMMAND tpc2_tests)
endif()

# Make the build-tree executables runnable by double-clicking on Windows. A
# MinGW build links against libwinpthread/libstdc++/Qt DLLs that live in the
# MSYS2 bin directory; Windows only searches PATH by default, which is not set
# for Explorer launches. Deploy once, after all executables have linked, so
# parallel build steps cannot race on the same DLLs.
if(WIN32 AND MINGW)
    set(_tpc2_runtime_targets)
    foreach(_target tpc2 tpc2cli tpc2_tests)
        if(TARGET ${_target})
            list(APPEND _tpc2_runtime_targets ${_target})
        endif()
    endforeach()

    if(_tpc2_runtime_targets)
        set(_tpc2_runtime_executables)
        foreach(_target IN LISTS _tpc2_runtime_targets)
            if(_tpc2_runtime_executables)
                string(APPEND _tpc2_runtime_executables "|")
            endif()
            string(APPEND _tpc2_runtime_executables "$<TARGET_FILE:${_target}>")
        endforeach()

        get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(_plugin_arg)
        if(TPC2_QT_PLUGIN_DIR)
            set(_plugin_arg "-DTPC2_QT_PLUGIN_DIR=${TPC2_QT_PLUGIN_DIR}")
        endif()

        add_custom_target(tpc2_deploy_build_runtime ALL
            COMMAND ${CMAKE_COMMAND}
                "-DTPC2_EXECUTABLES=${_tpc2_runtime_executables}"
                "-DTPC2_DEPLOY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                "-DTPC2_LIBRARY_DIR=${_compiler_bin}"
                ${_plugin_arg}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployBuildRuntime.cmake"
            COMMENT "Deploying Windows runtime next to build-tree executables"
            VERBATIM)
        add_dependencies(tpc2_deploy_build_runtime ${_tpc2_runtime_targets})
    endif()
endif()

# ---------------------------------------------------------------------------
# Installation and packaging
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

if(TARGET tpc2)
    install(TARGETS tpc2
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(TARGET tpc2cli)
    if(APPLE AND TARGET tpc2)
        # Keep the CLI inside the app bundle so it can use the bundled Qt
        # frameworks without a separate runtime directory.
        install(TARGETS tpc2cli RUNTIME DESTINATION "tpc2.app/Contents/MacOS")
    else()
        install(TARGETS tpc2cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# Bundle the Qt runtime with the GUI where the platform supports it. The CLI
# links the same Qt Core/SerialPort libraries, so it benefits as well.
if(TARGET tpc2 AND Qt6Widgets_FOUND)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
        set(_deploy_tool_options)
        if(WIN32 AND Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
            # The MSYS2 Qt build has no D3D shader compiler to deploy.
            set(_deploy_tool_options
                DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler)
        endif()
        qt_generate_deploy_app_script(
            TARGET tpc2
            OUTPUT_SCRIPT tpc2_deploy_script
            NO_TRANSLATIONS
            ${_deploy_tool_options}
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
        install(SCRIPT ${tpc2_deploy_script})

        if(APPLE)
            # macdeployqt does not rewrite an extra executable placed inside
            # the app bundle, so point the CLI at the bundled frameworks and
            # then sign the complete bundle.
            install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FixMacCli.cmake")
        elseif(UNIX)
            # The generic Qt deploy tool copies Qt libraries but not the
            # platform plugins on Linux. Include the platform plugins (and
            # the Qt libraries they need) so the package is self-contained.
            install(DIRECTORY "${_qt_prefix}/plugins/platforms"
                DESTINATION plugins)
            install(DIRECTORY "${_qt_prefix}/plugins/styles"
                DESTINATION plugins OPTIONAL)
            install(DIRECTORY "${_qt_prefix}/lib/"
                DESTINATION lib
                FILES_MATCHING PATTERN "libQt6*.so.6*")
        endif()
    else()
        # OUTPUT_SCRIPT was added in Qt 6.5. Older Linux distributions ship
        # Qt 6.4, where the application uses the system Qt installation.
        message(STATUS
            "Qt ${Qt6_VERSION} does not support install-time app deployment; "
            "using the system Qt installation")
    endif()
endif()

# windeployqt copies Qt and its plugins, but not the MinGW runtime or the
# third-party libraries those DLLs link against (freetype, ICU, libjpeg, ...).
# Resolve the complete runtime dependency set and add the non-Windows DLLs.
if(WIN32 AND MINGW)
    install(CODE [[
        if(POLICY CMP0207)
            cmake_policy(SET CMP0207 NEW)
        endif()
        file(GLOB_RECURSE _tpc2_qt_plugins
            "${CMAKE_INSTALL_PREFIX}/share/qt6/plugins/*.dll")
        set(_tpc2_dep_args)
        if(_tpc2_qt_plugins)
            list(APPEND _tpc2_dep_args MODULES ${_tpc2_qt_plugins})
        endif()
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "$<TARGET_FILE:tpc2>" "$<TARGET_FILE:tpc2cli>"
            ${_tpc2_dep_args}
            RESOLVED_DEPENDENCIES_VAR _tpc2_runtime
            UNRESOLVED_DEPENDENCIES_VAR _tpc2_unresolved
            DIRECTORIES $ENV{PATH}
            PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
            POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss].*"
        )
        foreach(_dep IN LISTS _tpc2_runtime)
            if(_dep MATCHES "[Mm]ingw64[/\\\\]bin"
               OR _dep MATCHES "[Uu]crt64[/\\\\]bin")
                file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                    TYPE SHARED_LIBRARY FILES "${_dep}")
            endif()
        endforeach()
    ]])
endif()

install(FILES README.md LICENSE NOTICE CONTRIBUTING.md SECURITY.md
    CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES firmware/pico/README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/firmware/pico)

# Linux desktop integration. The portable tarball contains these files under
# share/, so a system install (or a package built from it) can register the
# application with the desktop environment.
if(UNIX AND NOT APPLE AND TARGET tpc2)
    install(FILES packaging/linux/tpc2-next.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/icons/tpc2-next-256.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME tpc2-next.png)
    install(FILES packaging/icons/tpc2-next-512.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
        RENAME tpc2-next.png)
endif()

set(CPACK_PACKAGE_NAME "TPC2 Next")
set(CPACK_PACKAGE_VENDOR "TPC2 Next contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Modern Doctor V64 upload/download and remote control utility")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "TPC2 Next")
set(CPACK_PACKAGE_EXECUTABLES
    "tpc2;TPC2 Next"
    "tpc2cli;TPC2 Next CLI"
)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "TPC2 Next")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "TPC2 Next contributors")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6, libqt6serialport6")
endif()
include(CPack)
