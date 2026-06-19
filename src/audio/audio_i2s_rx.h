// src/audio/audio_i2s_rx.h — I2S receive on PIO0 SM0 (codec ADC -> MCU).
#ifndef AUDIO_I2S_RX_H
#define AUDIO_I2S_RX_H
#include <stdint.h>
#include "hardware/pio.h"

// Start MCLK (256*fs PWM on GPIO22), load the RX program on PIO0 SM0, set pins/
// clkdiv (clkdiv = 4*ticks locks fs = MCLK/256), and enable the SM.
void audio_i2s_rx_init(uint32_t sample_rate);

// DMA plumbing for the RX FIFO.
volatile const void *audio_i2s_rx_rxf(void);
uint audio_i2s_rx_dreq(void);

// Blocking single-frame pop (bring-up/test only; not for the steady-state path).
uint32_t audio_i2s_rx_get_blocking(void);

#endif
