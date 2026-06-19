// src/main.c — external-microphone validation firmware (FreeWili 2 / RP2350B).
#include "pico/stdlib.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "platform/ioexp_pcal6524.h"
#include "display/st7796.h"
#include "audio/codec_nau88c10.h"
#include "audio/audio_i2s_rx.h"
#include "audio/vu_capture.h"
#include "audio/vu_meter.h"

// Big-endian RGB565 (the panel sends the high byte first).
static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

// VU bar geometry on the 480x320 panel.
#define VU_X      20
#define VU_Y      160
#define VU_W      440
#define VU_H      60

static void vu_draw(uint16_t peak) {
    int px = vu_bar_px(peak, VU_W);
    uint16_t fg = vu_color_be(peak);
    uint16_t bg = rgb565_be(0, 0, 40);
    if (px > 0) st7796_fill_rect(VU_X, VU_Y, px, VU_H, fg);          // filled
    if (px < VU_W) st7796_fill_rect(VU_X + px, VU_Y, VU_W - px, VU_H, bg); // tail
}

int main(void) {
    board_init();
    DIAG("\n=== externalmicvalid: boot ===\n");

    // Release the panel's hardware reset (SCREEN_nRST) and route the GPIO25
    // backlight to the RP2350 via the PCAL6524 I/O expander. Without this the
    // ST7796 ignores all SPI and the screen stays dark.
    if (!ioexp_init()) DIAG("ioexp: init NAK at 0x23\n");

    st7796_init();
    st7796_fill_screen(rgb565_be(0, 0, 40));      // dark blue test fill
    st7796_draw_text(8, 8, 2, rgb565_be(255,255,255), rgb565_be(0,0,40),
                     "EXT MIC VALIDATION");
    board_backlight_set(1);
    DIAG("scaffold: display up, backlight on\n");

    codec_nau88c10_init();
    bool codec_ok = codec_nau88c10_input_ok();
    if (codec_ok) DIAG("codec: input path ready\n");
    st7796_draw_text(8, 40, 2,
                     codec_ok ? rgb565_be(0,255,0) : rgb565_be(255,0,0),
                     rgb565_be(0,0,40),
                     codec_ok ? "CODEC OK" : "CODEC FAIL");

    audio_i2s_rx_init(16000);
    vu_capture_start();
    DIAG("vu_capture: streaming; tap the mic...\n");

    st7796_draw_text(VU_X, VU_Y - 24, 2, rgb565_be(255,255,255),
                     rgb565_be(0,0,40), "MIC LEVEL");
    vu_draw(0);   // empty bar baseline

    uint32_t blk = 0;
    absolute_time_t last_block = get_absolute_time();
    bool warned = false;
    while (true) {
        if (vu_block_ready()) {
            uint16_t pk = vu_block_peak();
            vu_draw(pk);
            if ((blk++ & 0x1F) == 0)              // ~twice a second at 16ms blocks
                DIAG("vu: blk=%u peak=%u\n", (unsigned)blk, (unsigned)pk);
            last_block = get_absolute_time();
            warned = false;
        } else if (!warned && absolute_time_diff_us(last_block, get_absolute_time()) > 1000000) {
            DIAG("vu: no audio blocks for >1s (RX/clock dead?)\n");
            warned = true;
        }
        tight_loop_contents();
    }
}
