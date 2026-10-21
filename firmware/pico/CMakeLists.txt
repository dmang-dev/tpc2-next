cmake_minimum_required(VERSION 3.13)

# Stand-alone firmware build:
#   cmake -S firmware/pico -B build/pico -G Ninja
# The Arm toolchain must be on PATH; PICO_SDK_PATH selects the SDK.

if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR
        "PICO_SDK_PATH is not set. Clone https://github.com/raspberrypi/pico-sdk "
        "and point PICO_SDK_PATH at it.")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(tpc2_pico_bridge C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(tpc2_bridge
    main.c
    parallel.c
)

target_include_directories(tpc2_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tpc2_bridge PRIVATE pico_stdlib hardware_gpio)

# Binary framed protocol on the USB CDC port. UART stdio is disabled so the
# default UART pins stay free.
pico_enable_stdio_usb(tpc2_bridge 1)
pico_enable_stdio_uart(tpc2_bridge 0)
pico_add_extra_outputs(tpc2_bridge)

# picotool is optional in some SDK/toolchain combinations. Provide a small
# Python fallback so the build always produces a flashable UF2.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_command(TARGET tpc2_bridge POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bin2uf2.py
            $<TARGET_FILE_DIR:tpc2_bridge>/tpc2_bridge.bin
            $<TARGET_FILE_DIR:tpc2_bridge>/tpc2_bridge.uf2
        COMMENT "Creating tpc2_bridge.uf2"
        VERBATIM)
endif()
