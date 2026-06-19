// src/platform/board.h — pin map + board bring-up.
//
// Target (2026-06): FreeWili 2 display processor (RP2350B) + 480x320 panel.
// Pin map source of truth: freewili-firmware/freewilimain/FW2Display_pin_definitions.h.
// There is NO LCD reset GPIO on this board (GPIO12 is DVI_CLK_N) — the panel's RESX
// is handled in hardware, so the driver relies on SWRESET only.
#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>

// --- LCD (ST7789-class panel, 480x320, over SPI1) ---
#define PIN_LCD_DC     8    // LCD_DC_D: data/command
#define PIN_LCD_CS     9    // LCD_CS_D: chip select, active low
#define PIN_LCD_SCLK   10   // LCD_SCLK_D: SPI1 SCK
#define PIN_LCD_MOSI   11   // LCD_MOSI_D: SPI1 TX
#define PIN_LCD_TE     33   // LCD_TE: tearing-effect output from panel (unused for now)
#define PIN_LCD_BL     25   // BLACKLIGHT_EN: plain on/off, like the working reference

// --- Bus hygiene ---
#define PIN_LORA_CS    23   // LORA_SPI_CS: park HIGH so the radio never drives shared lines

// --- Audio (NAU88C10 codec; pins from FW2Display_pin_definitions.h) ---
#define PIN_AUDIO_DATA 5    // SPK_DIN: I2S data into the codec (PIO out)
#define PIN_AUDIO_DIN  4    // SPK_DOUT: codec ADC data into the MCU (PIO in)
#define PIN_AUDIO_LRCK 6    // SPK_LRCK: I2S word clock (PIO sideset bit 0)
#define PIN_AUDIO_BCLK 7    // SPK_BCLK: I2S bit clock (PIO sideset bit 1)
#define PIN_AUDIO_MCLK 22   // SPK_MCLK: 256*fs square wave from PWM
#define PIN_I2C1_SDA   26   // I2C1_SDA: codec control bus
#define PIN_I2C1_SCL   27   // I2C1_SCL: codec control bus

// --- Clocks ---
// 153.6 MHz (vreg 1.15V) — the PROVEN-STABLE rate for this FW2 board, matching the
// working microphonearray display firmware. The movieplayer value of 252 MHz is
// above this chip's stable ceiling even at 1.15 V: it causes intermittent core
// hard-faults during the heavy st7796 SPI/GPIO bring-up that reproduce only at full
// speed (clean when single-stepped) — the exact symptom we hit. 153.6 MHz =
// 50 * 3.072 MHz also yields clean audio dividers. SPI is pinned independently
// (st7796.c); the I2S MCLK/BCLK dividers self-adjust from clk_sys at runtime.
#define BOARD_SYS_CLOCK_KHZ 153600

// Bring up clocks (252 MHz + vreg 1.15V + clk_peri re-source), park LoRa CS, backlight off.
void board_init(void);

// Backlight: 0 = off, nonzero = on (plain GPIO; PWM dimming returns in a later phase).
void board_backlight_set(uint8_t level);

#endif
