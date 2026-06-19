// src/audio/vu_capture.c — two DMA channels ping-pong frames from the RX FIFO
// into two block buffers. The completion IRQ flags the just-filled buffer; the
// core-0 loop drains it via vu_block_peak().
#include "audio/vu_capture.h"
#include "audio/audio_i2s_rx.h"
#include "audio/vu_meter.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

static uint32_t s_buf[2][VU_BLOCK_FRAMES];
static int s_dma[2];
static volatile int s_done = -1;    // index of a freshly filled buffer, or -1
static volatile bool s_ready = false;

static void start_channel(int ch, int other_ch, uint32_t *dst) {
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, audio_i2s_rx_dreq());
    channel_config_set_chain_to(&c, other_ch);   // ping-pong
    dma_channel_configure(ch, &c, dst, audio_i2s_rx_rxf(), VU_BLOCK_FRAMES, false);
}

static void dma_irq(void) {
    for (int i = 0; i < 2; i++) {
        if (dma_channel_get_irq0_status(s_dma[i])) {
            dma_channel_acknowledge_irq0(s_dma[i]);
            // Rearm this channel for its next turn (the other is now running).
            dma_channel_set_write_addr(s_dma[i], s_buf[i], false);
            s_done = i;
            s_ready = true;
        }
    }
}

void vu_capture_start(void) {
    s_dma[0] = dma_claim_unused_channel(true);
    s_dma[1] = dma_claim_unused_channel(true);
    start_channel(s_dma[0], s_dma[1], s_buf[0]);
    start_channel(s_dma[1], s_dma[0], s_buf[1]);

    dma_channel_set_irq0_enabled(s_dma[0], true);
    dma_channel_set_irq0_enabled(s_dma[1], true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(s_dma[0]);   // channel 1 is chained from channel 0
}

bool vu_block_ready(void) { return s_ready; }

uint16_t vu_block_peak(void) {
    if (!s_ready) return 0;
    int idx = s_done;
    s_ready = false;
    if (idx < 0) return 0;
    return vu_peak(s_buf[idx], VU_BLOCK_FRAMES, MIC_I2S_SLOT);
}
