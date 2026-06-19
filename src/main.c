// src/main.c — external-microphone validation firmware (FreeWili 2 / RP2350B).
#include "pico/stdlib.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "display/st7796.h"

// Big-endian RGB565 (the panel sends the high byte first).
static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

int main(void) {
    board_init();
    DIAG("\n=== externalmicvalid: boot ===\n");

    st7796_init();
    st7796_fill_screen(rgb565_be(0, 0, 40));      // dark blue test fill
    st7796_draw_text(8, 8, 2, rgb565_be(255,255,255), rgb565_be(0,0,40),
                     "EXT MIC VALIDATION");
    board_backlight_set(1);
    DIAG("scaffold: display up, backlight on\n");

    while (true) {
        tight_loop_contents();
    }
}
