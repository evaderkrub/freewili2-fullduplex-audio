// src/display/st7796.h — 480x320 LCD driver (ST7789-class controller) over the RP2350
// hardware SPI peripheral (SPI1: SCLK=GPIO10, MOSI=GPIO11). Blocking transfers for
// bring-up paths plus a DMA frame-stream API for playback.
#ifndef ST7796_DRIVER_H
#define ST7796_DRIVER_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ST7796_W 480
#define ST7796_H 320

// Initialize SPI1 + the panel. Backlight is controlled separately (board.c).
void st7796_init(void);

// Set the GRAM address window for subsequent pixel writes.
void st7796_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// Blit a full-screen RGB565 framebuffer. Pixels must be stored BIG-ENDIAN (high byte
// first in memory) so the 8-bit SPI sends the high byte first, as the panel expects.
// Currently always blocking; `wait` is ignored (kept for the future DMA version).
void st7796_blit_full(const uint16_t *fb_rgb565, bool wait);

// Blit a w*h big-endian RGB565 image centered on the panel. On the first call (or
// when w/h change) the surrounding letterbox bars are painted in `bar_be`
// (a big-endian RGB565 color); subsequent calls rewrite only the image window.
// Currently always blocking; `wait` is ignored (kept for the future DMA version).
void st7796_blit_centered(const uint16_t *img, int w, int h, uint16_t bar_be, bool wait);

// Fence for blit completion. A no-op (the blocking blit functions have no DMA path;
// the frame-stream API handles its own completion in st7796_stream_end).
void st7796_wait_blit(void);

// Paint the letterbox bars around a centered w*h image in `bar_be` (big-endian
// RGB565). Cached: repaints only when w/h/bar_be change. Blocking block writes.
void st7796_paint_bars(int w, int h, uint16_t bar_be);

// ---- Frame streaming (DMA): one RAMWR/CS session per frame, fed in strips. ----
// stream_begin sets the window and opens the session; each stream_strip waits for
// the PREVIOUS strip's DMA (returning that buffer to the caller) and starts DMA on
// `bytes` (which must stay valid until the next call or stream_end); stream_end
// drains DMA + SPI and closes the session.
void st7796_stream_begin(int x0, int y0, int w, int h);
void st7796_stream_strip(const uint8_t *bytes, size_t n);
void st7796_stream_end(void);

// Fill the whole panel with one big-endian RGB565 color (block write; also
// invalidates the paint_bars cache).
void st7796_fill_screen(uint16_t color_be);

// Invalidate the paint_bars cache (e.g. after drawing status text onto the
// bars) so the next st7796_paint_bars call repaints them.
void st7796_bars_invalidate(void);

// Fill a rectangle with one big-endian RGB565 color (single block window, the
// proven write shape). Clipped to the panel. For small overlays (e.g. an OSD
// progress bar) drawn onto the letterbox bars; clear them by repainting bars.
void st7796_fill_rect(int x, int y, int w, int h, uint16_t color_be);

// Draw ASCII text with the built-in 5x7 font at integer scale (1..4): each
// glyph cell is 6*scale x 8*scale, drawn as one block window per character
// (the proven write shape). Lowercase maps to uppercase; unknown chars are
// blanks. Text that would overrun the panel edge is clipped at whole chars.
void st7796_draw_text(int x, int y, int scale, uint16_t fg_be, uint16_t bg_be,
                      const char *s);

#endif
