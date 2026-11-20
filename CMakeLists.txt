cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "RP2040 board")
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR "Set PICO_SDK_PATH to your Raspberry Pi Pico SDK checkout")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(rp2040_uart_gpio C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(rp2040_uart_gpio main.c)
target_link_libraries(rp2040_uart_gpio PRIVATE
    pico_stdlib hardware_clocks hardware_gpio hardware_pwm hardware_uart)
pico_enable_stdio_uart(rp2040_uart_gpio 0)
pico_enable_stdio_usb(rp2040_uart_gpio 0)
pico_add_extra_outputs(rp2040_uart_gpio)
