# External Microphone Validation — FreeWili 2 (RP2350B)

**Date:** 2026-06-19
**Status:** Approved design, ready for implementation planning
**Target:** FreeWili 2 display processor, RP2350B, Pico SDK (C/C++)
**Reference:** `C:\~prj\Dropbox\vibeProjects\movieplayer` (proven I2S **output** driver)

## Goal

Validate the **external microphone** on the FreeWili 2: the mic on the 3.5 mm
jack, captured through the **NAU88C10 codec ADC** and an I2S **input** stream on
`SPK_DOUT` (GPIO 4). Success is shown as a **live VU meter on the ST7796 LCD**:
tap/speak into the mic and the bar tracks the level; silence sits at the floor.

This is explicitly **not** the four onboard PDM/I2S mics (GPIO 28/29/30), which
the reference `mic.c` already handles a different way.

## Key finding (de-risks the codec side)

The reference `codec_nau88c10_init()` already programs the **input** path even
though that firmware only ever plays audio out:

- `R1 (0x01)=0x015D` — MICBIAS + analog bias enabled
- `R2 (0x02)=0x0015` — **ADCEN (bit0) + PGAEN (bit2) + BSTEN (bit4)** enabled
- `R44 (0x2C)=0x0003` — MICP/MICN routed into the PGA
- `R45 (0x2D)=0x0010` — PGA gain
- `R47 (0x2F)=0x0100` — +20 dB mic boost enabled
- `R14 (0x0E)=0x0108` — ADC control (HPF / oversample)
- `R4 (0x04)=0x0010`, `R7 (0x07)=0x0006` — I2S 16-bit slave, 16 kHz, MCLK-direct

So the codec ADC is essentially live after the existing init. The genuinely new
work is **capturing the codec's I2S ADC output on GPIO 4**, which the reference
never reads.

## Architecture & data flow

```
3.5mm mic jack -> NAU88C10 PGA+boost -> ADC -+
                  (I2C1 config @ 0x1A)        | I2S (codec = slave)
                                              v
RP2350B drives:  MCLK(GPIO22,PWM) BCLK(GPIO7) LRCK(GPIO6)  -- clocks --> codec
                 SPK_DOUT(GPIO4) <-- ADC data -- codec
                       |
                  PIO0 SM0 (RX) -- autopush 32 --> RX FIFO
                       | DMA (ping-pong, DMA_IRQ_0)
                       v
                 PCM block buffers --> peak per block
                       v
                 VU bar render --> ST7796 LCD (SPI1: fill_rect + draw_text)
```

Single core (core 0). Loop: DMA fills a block -> compute peak -> draw bar ->
repeat. Secondary diagnostics over SEGGER RTT.

## I2S RX approach (chosen: Approach A — dedicated RX-only PIO SM)

One PIO0 state machine (SM0) mirrors the reference TX program exactly — it
generates LRCK + BCLK via sideset and reuses the proven `clkdiv = 4*ticks`
MCLK-lock recipe — but shifts the `SPK_DOUT` pin **in** (`in pins,1` + autopush
32) instead of out. MCLK is a 256·fs PWM square wave, identical to the reference.
The codec, an I2S **slave**, clocks its ADC data out on our BCLK/LRCK.

Rejected alternatives:
- **B (clock-gen SM + sampler SM):** two SMs, per-SM clock-lock duplicated, no
  benefit for a record-only test.
- **C (full-duplex TX+RX program):** only needed for live loopback-to-speaker;
  overkill here. This is the upgrade path if loopback is wanted later.

### RX clocking constraints (carried over from the reference)

- MCLK = 256·fs via PWM; `ticks = clk_sys / (256*fs)`.
- `pio_sm_set_clkdiv_int_frac(pio, sm, 4*ticks, 0)` so `fs = MCLK/256` exactly.
  Independent dividers leave LRCK vs MCLK/256 ~0.4% apart and the MCLK-direct
  codec drops/repeats samples (audible buzz / corrupt capture) — same failure
  class proven on the reference TX path.
- `clk_sys = 252 MHz` (board default). Actual `fs ~= 252e6/(256*4*ticks)`; small
  offset is fine for a VU test.

### Format / channel

- 16-bit, 16 kHz, codec = I2S slave (matches existing codec R4/R7).
- The 3.5 mm mic is mono. Read whichever I2S slot carries it; **assume LEFT
  first**, fall back to RIGHT via a single compile-time constant
  (`MIC_I2S_SLOT`). Peak is taken from that slot's 16-bit samples.
- Block size: **256 samples/block** (~16 ms @ 16 kHz), ping-pong of 2 buffers.

## Components (standalone project in `externalmicvalid/`)

### Vendored unchanged from movieplayer
- `CMakeLists.txt` (trimmed to this project's sources), `pico_sdk_import.cmake`
- `src/boards/freewili2.h`
- `third_party/segger_rtt/` (`SEGGER_RTT.c`, `SEGGER_RTT_printf.c`, headers)
- `src/platform/diag.h` — `DIAG(...)` over RTT channel 0
- `src/platform/board.{c,h}` — clocks (252 MHz, vreg 1.15 V, clk_peri
  re-source), LoRa-CS park, backlight
- `src/display/st7796.{c,h}` + `src/display/font5x7.c` — used read-only via
  `st7796_fill_screen`, `st7796_fill_rect`, `st7796_draw_text`

### Vendored + small extension
- `src/audio/codec_nau88c10.{c,h}` — existing init already powers the ADC path.
  Extension: call `codec_nau88c10_dump()` at boot to confirm the part answers
  (reg `0x3F` should be nonzero = silicon revision) and to verify R2/R47 input
  bits. DAC/speaker routing is unused (we never play out) and may be left as-is
  or trimmed; do not regress codec init.

### New
- `src/audio/audio_i2s_rx.{c,h}` — Approach-A RX PIO program + API:
  - `void audio_i2s_rx_init(uint32_t sample_rate);` — load program on PIO0 SM0,
    start MCLK PWM, set pins/dirs (GPIO4 in; GPIO6/7 sideset out), set clkdiv,
    enable SM.
  - `volatile const void *audio_i2s_rx_rxf(void);` — RX FIFO source for DMA.
  - `uint audio_i2s_rx_dreq(void);` — RX DREQ for DMA pacing.
- `src/audio/vu_capture.{c,h}` — owns ping-pong DMA into two PCM block buffers,
  a block-ready flag (set in `DMA_IRQ_0` handler), and:
  - `void vu_capture_start(void);`
  - `bool vu_block_ready(void);`
  - `uint16_t vu_block_peak(void);` — peak (0..32767) of the active channel for
    the most recent completed block; clears the ready flag.
- `src/main.c` — bring-up order:
  1. `board_init()`
  2. `st7796_init()` + `board_backlight_set(1)`; clear screen, draw static frame
  3. `codec_nau88c10_init()` + `codec_nau88c10_dump()`
  4. `audio_i2s_rx_init(16000)`
  5. `vu_capture_start()`
  6. loop: on `vu_block_ready()`, read `vu_block_peak()`, draw VU bar, and DIAG
     the peak periodically (e.g. every N blocks) as a secondary signal.

## VU meter render

- Clear screen once to a dark background; draw a static label/border once.
- Each ready block: draw a horizontal bar whose length scales with peak. A
  log/dB-ish mapping gives a better feel than linear. Color green -> yellow ->
  red by level. Optional peak-hold tick decaying over ~1 s.
- Redraw only the bar region (fill the active bar rect + clear its tail) to avoid
  full-screen flicker. Numeric peak shown via `st7796_draw_text`.

## Error handling

- Codec I2C write failures already `DIAG` per-register and continue.
- `codec_nau88c10_dump()` at boot proves the bus; if reg `0x3F == 0`, DIAG a loud
  "codec not responding".
- If no DMA block completes within a timeout, the loop DIAGs a "no audio blocks"
  heartbeat so a dead RX/clock path is visible over RTT even though the LCD would
  just sit flat.
- VU flat while tapping the mic -> known suspects, surfaced in DIAG:
  1. wrong I2S channel slot -> flip `MIC_I2S_SLOT`;
  2. RX sample point off by one BCLK -> single-cycle shift in the RX PIO program;
  3. PGA/boost not actually enabled -> re-check R2 (`0x0015`) / R47 (`0x0100`).

## Testing / success criteria

- **Primary (on-device):** speak into / tap the 3.5 mm mic -> VU bar rises and
  tracks input; silence -> bar at floor.
- **Secondary (RTT):** per-block peak printed over RTT rises with input,
  confirming capture independent of the display.
- **Bring-up checklist:** codec dump OK -> DMA blocks flowing -> peak responds.
  If flat, apply the error-handling suspects in order.

## Out of scope (YAGNI)

- Live loopback-to-speaker (full-duplex I2S) — Approach C, future only.
- Recording to flash/PSRAM or WAV dump.
- The onboard PDM/I2S mics (GPIO 28/29/30).
- Headphone/speaker output routing for this test.
- Stereo capture / both mic channels.
```
