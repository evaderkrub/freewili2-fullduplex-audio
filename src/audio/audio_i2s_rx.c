// src/audio/audio_i2s_rx.c — I2S RX: PIO0 SM0 clocks the codec (slave) and
// shifts its ADC output in on PIN_AUDIO_DIN. MCLK is a 256*fs PWM square wave.
#include "audio/audio_i2s_rx.h"
#include "platform/board.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "i2s_rx.pio.h"

#define RX_PIO pio0
#define RX_SM  0

void audio_i2s_rx_init(uint32_t sample_rate) {
    // MCLK = 256*fs, 50% duty (codec is MCLK-direct), same recipe as the
    // reference TX path.
    gpio_set_function(PIN_AUDIO_MCLK, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_AUDIO_MCLK);
    uint32_t ticks = clock_get_hz(clk_sys) / (256u * sample_rate);
    pwm_set_wrap(slice, ticks - 1);
    pwm_set_gpio_level(PIN_AUDIO_MCLK, ticks / 2);
    pwm_set_enabled(slice, true);

    uint offset = pio_add_program(RX_PIO, &i2s_rx_program);
    pio_sm_config c = i2s_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&c, PIN_AUDIO_DIN);            // GPIO4 = SPK_DOUT
    sm_config_set_sideset_pins(&c, PIN_AUDIO_LRCK);      // bit0=LRCK(6),bit1=BCLK(7)
    sm_config_set_in_shift(&c, false, true, 32);         // MSB first, autopush 32

    pio_gpio_init(RX_PIO, PIN_AUDIO_LRCK);
    pio_gpio_init(RX_PIO, PIN_AUDIO_BCLK);
    gpio_set_function(PIN_AUDIO_DIN, GPIO_FUNC_PIO0);    // input pin for PIO0

    pio_sm_init(RX_PIO, RX_SM, offset, &c);
    // LRCK/BCLK are outputs (sideset); DIN is an input.
    uint out_mask = (3u << PIN_AUDIO_LRCK);              // GPIO6,7
    pio_sm_set_pindirs_with_mask(RX_PIO, RX_SM, out_mask,
                                 out_mask | (1u << PIN_AUDIO_DIN));

    // clkdiv = 4*ticks -> fs = MCLK/256 exactly (anti-slip, reference finding).
    pio_sm_set_clkdiv_int_frac(RX_PIO, RX_SM, 4 * ticks, 0);
    pio_sm_set_enabled(RX_PIO, RX_SM, true);
}

volatile const void *audio_i2s_rx_rxf(void) {
    return (volatile const void *)&RX_PIO->rxf[RX_SM];
}

uint audio_i2s_rx_dreq(void) {
    return pio_get_dreq(RX_PIO, RX_SM, false);   // false = RX DREQ
}

uint32_t audio_i2s_rx_get_blocking(void) {
    return pio_sm_get_blocking(RX_PIO, RX_SM);
}
