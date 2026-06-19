// src/audio/vu_capture.h — free-running ping-pong DMA from the I2S RX FIFO into
// PCM block buffers; per-block peak for the VU meter.
#ifndef VU_CAPTURE_H
#define VU_CAPTURE_H
#include <stdint.h>
#include <stdbool.h>

#define VU_BLOCK_FRAMES 256   // ~16 ms at 16 kHz
#define MIC_I2S_SLOT    0     // 0 = left, 1 = right (flip if VU stays flat)

// Start MCLK/RX must already be running (audio_i2s_rx_init). Claims 2 DMA
// channels chained ping-pong into two block buffers and installs DMA_IRQ_0.
void vu_capture_start(void);

// True once a block has completed since the last vu_block_peak().
bool vu_block_ready(void);

// Peak (0..32767) of MIC_I2S_SLOT over the most recently completed block;
// clears the ready flag. Returns 0 if no block is ready.
uint16_t vu_block_peak(void);

#endif
