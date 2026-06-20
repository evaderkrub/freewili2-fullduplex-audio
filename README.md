# externalmicvalid — Full-Duplex Speaker + External Mic (FreeWili 2 / RP2350B)

A small, dependency-light **full-duplex I2S audio driver** for the FreeWili 2 display
processor (RP2350B) and its on-board **NAU88C10** codec. It plays audio out the
on-board speaker (codec DAC) **and** captures the external 3.5 mm microphone (codec
ADC) **at the same time**, over a single I2S bus driven by one PIO state machine.

The included demo plays a 1 kHz tone out the speaker while running a live mic VU meter
on the ST7796 LCD, and ships scripts that prove — hands-free, via a USB webcam + mic —
that the speaker is actually emitting sound.

> Bench note: the speaker on the dev unit is physically blown, so its output is heavily
> distorted (a ~5 kHz resonance dominates). The validation goal here is "sound is
> present and tracks the commanded frequency," not audio quality.

## What's in the box

| Piece | File |
|-------|------|
| Full-duplex I2S driver (PIO + DMA) | `src/audio/audio_i2s_duplex.{c,h}`, `src/audio/i2s_duplex.pio` |
| NAU88C10 codec bring-up (I2C) | `src/audio/codec_nau88c10.{c,h}` |
| Pure sine tone generator (host-tested) | `src/audio/tone_gen.{c,h}` |
| Mic capture → VU meter | `src/audio/vu_capture.{c,h}`, `src/audio/vu_meter.{c,h}` |
| ST7796 LCD + I/O expander | `src/display/`, `src/platform/` |
| Demo app | `src/main.c` |

## The driver API

```c
#include "audio/audio_i2s_duplex.h"

audio_i2s_duplex_init(16000);          // MCLK + one PIO0 SM clocking the codec:
                                       //   DAC out on GPIO5, ADC in on GPIO4,
                                       //   LRCK GPIO6, BCLK GPIO7, MCLK GPIO22.

// Playback: a zero-CPU DMA read-ring loops a pre-filled buffer forever. The buffer
// must hold whole tone periods, be a power-of-two bytes, and be aligned to its size.
static uint32_t tone[64] __attribute__((aligned(256)));  // 64 frames @16k = 4x 1kHz
// ... fill tone[i] = (L16<<16)|R16 ...
audio_i2s_duplex_play_loop(tone, 64);
audio_i2s_duplex_play_stop();          // park DAC at silence

// Route the output: onboard speaker, or the 3.5 mm TRRS jack (headphone/line out).
// Mic capture is unaffected either way (independent ADC path).
codec_nau88c10_set_output(CODEC_OUT_SPEAKER);     // onboard speaker
codec_nau88c10_set_output(CODEC_OUT_HEADPHONE);   // 3.5 mm jack output

// Capture: RX FIFO accessors plug straight into a ping-pong DMA (see vu_capture.c).
volatile const void *rxf = audio_i2s_duplex_rxf();
uint dreq = audio_i2s_duplex_rx_dreq();
```

TX and RX are sample-aligned by construction: one PIO program drives the codec clocks
(sideset LRCK/BCLK), shifts the DAC bit **out** and the ADC bit **in** in the same
loop. Mono content is duplicated to both I2S slots; the NAU88C10 ADC streams its mono
sample on the **right** slot (`MIC_I2S_SLOT = 1`).

## Pin map (from `FW2Display_pin_definitions.h`)

| Signal | GPIO | Dir |
|--------|------|-----|
| `SPK_DIN`  (DAC data → codec) | 5  | out |
| `SPK_DOUT` (ADC data ← codec) | 4  | in  |
| `SPK_LRCK` (I2S word clock)   | 6  | out |
| `SPK_BCLK` (I2S bit clock)    | 7  | out |
| `SPK_MCLK` (256·fs, PWM)      | 22 | out |
| `I2C1 SDA / SCL` (codec ctrl) | 26 / 27 | — |

System clock is pinned at **153.6 MHz** (proven stable; 252 MHz hard-faults this part —
see `src/platform/board.h`).

## Build / flash / run

Uses the Raspberry Pi Pico VS Code extension's installed toolchain (Pico SDK 2.2.0,
ARM GCC 14.2, OpenOCD via a CMSIS-DAP probe). No global env changes.

```powershell
powershell -File tools/build.ps1 -Clean   # -> C:/buildfiles/externalmicvalid/*.uf2/.elf
powershell -File tools/flash.ps1          # program + verify + reset over SWD
powershell -File tools/rtt.ps1 -Seconds 16   # live SEGGER RTT diagnostics
```

The demo cycles **SILENCE → SPEAKER (4 s) → 3.5 mm JACK (4 s)**, drawing a per-state
banner (`SPEAKER 1kHz` / `3.5mm JACK 1kHz` / `TONE OFF`) and a live mic VU bar on the
LCD. The tone streams continuously across the SPEAKER and JACK windows — only the
codec's analog output routing switches — so the same 1 kHz tone moves from the onboard
speaker to the headphone jack while the mic keeps capturing.

## Tests

Host-side, pure logic (no hardware), via mingw gcc + pytest:

```bash
python -m pytest tests/ -v        # tone_gen + vu_meter
```

## Hands-free E2E validation (USB webcam + mic)

With the firmware running, capture the screen and the room audio, then check the
recording for a cycle-correlated tone:

```powershell
powershell -File tools/screencap/capture_e2e.ps1 -Seconds 16
python tools/screencap/analyze_tone.py tools/screencap/captures/room.wav
```

`analyze_tone.py` passes when the speaker radiates a sharp, narrowband tone in sync
with the TONE windows (total-energy bimodality + high peak crest factor). Latest bench
result and the fundamental-tracking proof: `docs/superpowers/findings/2026-06-19-fullduplex-e2e.md`.

## Design / plan

- Spec: `docs/superpowers/specs/2026-06-19-fullduplex-speaker-mic-design.md`
- Plan: `docs/superpowers/plans/2026-06-19-fullduplex-speaker-mic.md`
