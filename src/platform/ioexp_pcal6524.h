// src/platform/ioexp_pcal6524.h — FW2 display-side PCAL6524 (this hardware rev).
// Pure pin table + bit-packing (host-testable) plus the hardware driver API.
//
// REV NOTE: pin index 9 (P2_1) now gates the 1.5K USB D+ pull-up. In the older
// WiliDexed rev this same pin (IOEXP_V2_1) was an antenna/voltage select. Bit
// position is unchanged (output port 1, bit 1).
#ifndef IOEXP_PCAL6524_H
#define IOEXP_PCAL6524_H
#include <stdint.h>
#include <stdbool.h>

// 24 expander pins; enum index = bit position (byte = index/8, bit = index%8).
// "Px_y" maps to absolute bit index x*8 + y (P0=byte0, P1=byte1, P2=byte2).
typedef enum {
    IOEXP_HP1_EN = 0, IOEXP_UART1_RTS_DIR, IOEXP_UART1_RX_DIR, IOEXP_UART1_TX_DIR,
    IOEXP_SPI1_TX_DIR, IOEXP_SPI1_RX_DIR, IOEXP_SPI1_CS_DIR, IOEXP_SPI1_SCLK_DIR,
    IOEXP_UART1_CTS_DIR, IOEXP_V2_1, IOEXP_SCREEN_NRST, IOEXP_V1_1,
    IOEXP_HP2_EN, IOEXP_GPIO25_RP_DIR, IOEXP_I2C_PULL, IOEXP_MIC_PWR,
    IOEXP_IR_PWR,
    IOEXP_USB_DPLUS_EN,   // =17, P2_1: enables the 1.5K USB D+ pull-up (this rev;
                          // was HOTPLUG_DET/input in the old WiliDexed rev)
    IOEXP_MCLR, IOEXP_EXT_VREF,
    IOEXP_INT_VREF, IOEXP_P5V_VREF, IOEXP_P3V3_VREF, IOEXP_LED3,
    IOEXP_PIN_COUNT
} ioexp_pin_t;

// Pack 24 booleans LSB-first into 3 bytes (bit = pin%8, byte = pin/8). Pure.
static inline void ioexp_pack(const uint8_t flags[IOEXP_PIN_COUNT], uint8_t out[3]) {
    out[0] = out[1] = out[2] = 0;
    for (int i = 0; i < IOEXP_PIN_COUNT; i++)
        if (flags[i]) out[i / 8] |= (uint8_t)(1u << (i % 8));
}

// Safe defaults for the display firmware: enable the I2C bus pulls, RELEASE the
// LCD reset, and route the backlight enable (GPIO25) to the RP2350 so
// board_backlight_set() works. Everything else low. USB D+ starts DISCONNECTED
// (0). Directions: all outputs except MCLR (a hardware input). Pure.
//
// HISTORY: this was carried from the USB-device firmware, which left
// SCREEN_NRST=0 driven as an output — that holds the panel in hardware reset, so
// st7796_init() and every draw are silently ignored and the screen stays dark.
// The movieplayer reference (same FW2 panel) releases SCREEN_NRST and sets
// GPIO25_RP_DIR; we mirror just those two display-relevant bits here.
static inline void ioexp_defaults(uint8_t values[IOEXP_PIN_COUNT],
                                  uint8_t dir_input[IOEXP_PIN_COUNT]) {
    for (int i = 0; i < IOEXP_PIN_COUNT; i++) { values[i] = 0; dir_input[i] = 0; }
    values[IOEXP_I2C_PULL] = 1;        // enable bus pulls
    values[IOEXP_SCREEN_NRST] = 1;     // release LCD reset (else panel ignores SPI)
    values[IOEXP_GPIO25_RP_DIR] = 1;   // route backlight (GPIO25) to RP2350 control
    values[IOEXP_USB_DPLUS_EN] = 0;    // output, starts disconnected
    dir_input[IOEXP_MCLR] = 1;         // input
}

// ---- Hardware driver (ioexp_pcal6524.c) ----
bool ioexp_init(void);                  // I2C1 up + write defaults (outputs then dirs)
void ioexp_usb_dplus_drive(bool level); // drive the P2_1 D+-enable pin to a raw level + commit
void ioexp_mic_pwr_drive(bool on);      // drive IOEXP_MIC_PWR (P1_7, active-high) + commit
bool ioexp_commit(void);                // rewrite the 3 output ports from the shadow

// Diagnostic: write arbitrary output values + directions (1 = input). Updates
// the shadow. Used by the D+-enable bit sweep to find which pin gates the pull-up.
bool ioexp_write_raw(const uint8_t values[IOEXP_PIN_COUNT],
                     const uint8_t dir_input[IOEXP_PIN_COUNT]);

#endif // IOEXP_PCAL6524_H
