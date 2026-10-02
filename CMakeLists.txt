cmake_minimum_required(VERSION 3.13)

# Eigenständiges Testprojekt, analog zum allerersten Sinuston-Test für
# den DAC - isoliert das INMP441/PIO-Problem vom restlichen Vocoder-
# Code, siehe DEVLOG.
#
# Vor dem Bauen:
#   export PICO_SDK_PATH=/pfad/zu/pico-sdk
#   (pico_sdk_import.cmake einmalig hierher kopieren, siehe Haupt-
#   projekt - identisch zu dem dort schon verwendeten)

set(PICO_BOARD pico CACHE STRING "Board type")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

include(pico_sdk_import.cmake)

project(hello_mic C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(hello_mic
    src/hello_mic.cpp
)

pico_generate_pio_header(hello_mic ${CMAKE_CURRENT_LIST_DIR}/src/i2s_mic.pio)

target_link_libraries(hello_mic PRIVATE
    pico_stdlib
    hardware_pio
)

pico_enable_stdio_usb(hello_mic 1)
pico_enable_stdio_uart(hello_mic 0)

pico_add_extra_outputs(hello_mic)
