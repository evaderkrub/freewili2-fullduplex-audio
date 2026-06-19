cmake_minimum_required(VERSION 3.13)

# Use the FreeWili 2 board header (PICO_RP2350A=0 -> RP2350B / 48 GPIO, 16MB flash),
# matching the proven microphonearray display firmware. build.ps1 must NOT pass
# -DPICO_BOARD (that would override this with pico2 = wrong RP2350A config).
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/src/boards")

include(pico_sdk_import.cmake)

project(externalmicvalid C CXX ASM)
pico_sdk_init()

add_executable(externalmicvalid
    src/main.c
    src/platform/board.c
    src/platform/ioexp_pcal6524.c
    src/display/st7796.c
    src/display/font5x7.c
    src/audio/vu_meter.c
    src/audio/tone_gen.c
    src/audio/codec_nau88c10.c
    src/audio/audio_i2s_duplex.c
    src/audio/vu_capture.c
    third_party/segger_rtt/SEGGER_RTT.c
    third_party/segger_rtt/SEGGER_RTT_printf.c
)

pico_generate_pio_header(externalmicvalid
    ${CMAKE_CURRENT_LIST_DIR}/src/audio/i2s_duplex.pio)

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
    hardware_irq
    hardware_pwm
    hardware_i2c
    hardware_pio
)

# USB is host-mode; no UART debug. All diagnostics over SEGGER RTT.
pico_enable_stdio_usb(externalmicvalid 0)
pico_enable_stdio_uart(externalmicvalid 0)

# Run from SRAM (stored in QSPI, copied to RAM at boot) — the proven, OpenOCD-
# friendly binary type for this FW2 board (matches microphonearray).
pico_set_binary_type(externalmicvalid copy_to_ram)

pico_add_extra_outputs(externalmicvalid)
