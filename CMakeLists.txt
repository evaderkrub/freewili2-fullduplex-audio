cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(externalmicvalid C CXX ASM)
pico_sdk_init()

add_executable(externalmicvalid
    src/main.c
    src/platform/board.c
    src/display/st7796.c
    src/display/font5x7.c
    third_party/segger_rtt/SEGGER_RTT.c
    third_party/segger_rtt/SEGGER_RTT_printf.c
)

target_include_directories(externalmicvalid PRIVATE
    src third_party/segger_rtt)

# 4KB RTT up-buffer so boot DIAG survives until tools/rtt.ps1 attaches.
target_compile_definitions(externalmicvalid PRIVATE BUFFER_SIZE_UP=4096 PICO_BUILD=1)

target_link_libraries(externalmicvalid PRIVATE
    pico_stdlib
    hardware_clocks
    hardware_gpio
    hardware_spi
    hardware_dma
    hardware_pwm
    hardware_i2c
    hardware_pio
)

# USB is host-mode; no UART debug. All diagnostics over SEGGER RTT.
pico_enable_stdio_usb(externalmicvalid 0)
pico_enable_stdio_uart(externalmicvalid 0)

pico_add_extra_outputs(externalmicvalid)
