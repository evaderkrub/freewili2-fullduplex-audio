// src/platform/board.c
#include "platform/board.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"

void board_init(void) {
    // Raise the core voltage before overclocking (safe at stock speed too).
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(10);
    set_sys_clock_khz(BOARD_SYS_CLOCK_KHZ, true);

    // After overclocking sys, re-source the peripheral clock from clk_sys so the
    // hardware SPI peripheral has a valid clock. WITHOUT this the SPI clock is dead
    // and the LCD shows nothing — the working reference driver does exactly this.
    uint32_t f = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f);

    // Park the LoRa radio's SPI CS high before any LCD traffic so it never drives
    // lines shared with the LCD.
    gpio_init(PIN_LORA_CS);
    gpio_set_dir(PIN_LORA_CS, GPIO_OUT);
    gpio_put(PIN_LORA_CS, 1);

    // Backlight as a plain GPIO, matching the working reference (its PWM path is
    // disabled). PWM dimming returns in a later phase — one fewer bring-up variable.
    gpio_init(PIN_LCD_BL);
    gpio_set_dir(PIN_LCD_BL, GPIO_OUT);
    board_backlight_set(0);   // start dark; on after display init pushes a frame
}

void board_backlight_set(uint8_t level) {
    gpio_put(PIN_LCD_BL, level != 0);
}
