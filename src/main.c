// src/main.c — full-duplex speaker + external-mic firmware (FreeWili 2 / RP2350B).
// Plays a 1 kHz tone out the onboard speaker (NAU88C10 DAC) while capturing the
// 3.5 mm external mic (NAU88C10 ADC) on the same I2S bus. Cycles SILENCE/TONE so
// the on-device VU bar and an off-device (eMeet) recording both show a clean delta.
#include "pico/stdlib.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "platform/ioexp_pcal6524.h"
#include "display/st7796.h"
#include "audio/codec_nau88c10.h"
#include "audio/audio_i2s_duplex.h"
#include "audio/tone_gen.h"
#include "audio/vu_capture.h"
#include "audio/vu_meter.h"

// Big-endian RGB565 (the panel sends the high byte first).
static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

// VU bar geometry on the 480x320 panel.
#define VU_X      20
#define VU_Y      200
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
    DIAG("\n=== externalmicvalid: full-duplex boot ===\n");

    // Release the panel's hardware reset (SCREEN_nRST) and route the GPIO25
    // backlight to the RP2350 via the PCAL6524 I/O expander. Without this the
    // ST7796 ignores all SPI and the screen stays dark.
    if (!ioexp_init()) DIAG("ioexp: init NAK at 0x23\n");

    st7796_init();
    st7796_fill_screen(rgb565_be(0, 0, 40));      // dark blue test fill
    st7796_draw_text(8, 8, 2, rgb565_be(255,255,255), rgb565_be(0,0,40),
                     "AUDIO OUT + MIC");
    board_backlight_set(1);
    DIAG("scaffold: display up, backlight on\n");

    codec_nau88c10_init();
    bool codec_ok = codec_nau88c10_input_ok();
    if (codec_ok) DIAG("codec: input path ready\n");
    st7796_draw_text(8, 40, 2,
                     codec_ok ? rgb565_be(0,255,0) : rgb565_be(255,0,0),
                     rgb565_be(0,0,40),
                     codec_ok ? "CODEC OK" : "CODEC FAIL");

    // 1 kHz tone ring: 64 frames @ 16 kHz = 4 whole periods = 256 bytes (ring-aligned).
    static uint32_t tone_buf[64] __attribute__((aligned(256)));
    int16_t mono[64]; float ph = 0.0f;
    tone_gen_fill(mono, 64, 1000.0f, 16000.0f, &ph);
    for (int i = 0; i < 64; i++) {
        uint16_t s = (uint16_t)mono[i];
        tone_buf[i] = ((uint32_t)s << 16) | s;   // same sample on L and R slots
    }

    codec_nau88c10_dac_mute(false);           // DAC live (init left it soft-muted)
    audio_i2s_duplex_init(16000);
    vu_capture_start();
    DIAG("duplex: streaming; cycling SILENCE/TONE...\n");

    st7796_draw_text(VU_X, VU_Y - 24, 2, rgb565_be(255,255,255),
                     rgb565_be(0,0,40), "MIC LEVEL");
    vu_draw(0);   // empty bar baseline

    // A/B output demo: SILENCE 3s -> SPEAKER 4s -> 3.5mm JACK 4s, looping. The 1 kHz
    // DAC stream runs continuously across SPEAKER+JACK; only the codec's analog output
    // routing switches (set_output), so the same tone moves speaker -> headphone jack.
    // Banners are large for the camera; output regs are logged on each switch.
    enum { ST_SILENCE, ST_SPEAKER, ST_JACK } state = ST_SILENCE;
    const char *st_name[] = { "SIL", "SPK", "JACK" };
    st7796_fill_rect(8, 80, 464, 44, rgb565_be(60,0,0));
    st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(60,0,0), "TONE OFF");
    absolute_time_t t_state = get_absolute_time();
    uint32_t blk = 0;
    while (true) {
        int64_t held = absolute_time_diff_us(t_state, get_absolute_time());
        if (state == ST_SILENCE && held > 3000000) {
            state = ST_SPEAKER; t_state = get_absolute_time();
            codec_nau88c10_set_output(CODEC_OUT_SPEAKER);
            audio_i2s_duplex_play_loop(tone_buf, 64);
            st7796_fill_rect(8, 80, 464, 44, rgb565_be(0,120,0));
            st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(0,120,0),
                             "SPEAKER 1kHz");
            DIAG("state=SPEAKER (tone -> onboard speaker)\n");
            codec_nau88c10_log_output();
        } else if (state == ST_SPEAKER && held > 4000000) {
            state = ST_JACK; t_state = get_absolute_time();
            codec_nau88c10_set_output(CODEC_OUT_HEADPHONE);   // route to 3.5mm jack
            st7796_fill_rect(8, 80, 464, 44, rgb565_be(0,90,160));
            st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(0,90,160),
                             "3.5mm JACK 1kHz");
            DIAG("state=JACK (tone -> 3.5mm headphone jack; speaker muted)\n");
            codec_nau88c10_log_output();
        } else if (state == ST_JACK && held > 4000000) {
            state = ST_SILENCE; t_state = get_absolute_time();
            audio_i2s_duplex_play_stop();
            st7796_fill_rect(8, 80, 464, 44, rgb565_be(60,0,0));
            st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(60,0,0),
                             "TONE OFF");
            DIAG("state=SILENCE\n");
        }
        if (vu_block_ready()) {
            uint16_t pk = vu_block_peak();
            vu_draw(pk);
            if ((blk++ & 0x0F) == 0)
                DIAG("vu: state=%s blk=%u peak=%u\n", st_name[state],
                     (unsigned)blk, (unsigned)pk);
        }
        tight_loop_contents();
    }
}
