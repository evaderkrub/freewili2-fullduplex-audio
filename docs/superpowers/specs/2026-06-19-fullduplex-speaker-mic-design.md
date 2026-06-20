# Full-Duplex Speaker + External Mic — FreeWili 2 (RP2350B)

**Date:** 2026-06-19
**Status:** Approved design (user-confirmed decisions), ready for implementation planning
**Target:** FreeWili 2 display processor, RP2350B, Pico SDK (C/C++)
**Builds on:** `2026-06-19-external-mic-validation-design.md` (the proven RX path).
This is the deferred **Approach C** named there: full-duplex I2S TX+RX so the board
plays a tone out its own speaker while simultaneously capturing the mic.

## Goal

Ship a **reusable, GitHub-publishable full-duplex I2S audio driver** for the
FreeWili 2: it plays audio out the **NAU88C10 DAC → onboard speaker** and captures
the **external 3.5 mm mic → NAU88C10 ADC** at the same time, sharing one I2S bus.

The acceptance demo: generate a **1 kHz sine** out the speaker while the existing
mic→VU path runs, and prove the sound is physically present. The onboard speaker is
**blown / distorted**, so the bar is "sound present," not "sound clean."

## User-confirmed decisions (2026-06-19)

1. **Deliverable:** a clean reusable `audio_i2s_duplex` module (play + capture API),
   with this firmware as the E2E demo on top — not a throwaway tone bolt-on.
2. **Proof of sound (dual, independent):**
   - **On-device:** the existing mic→ADC→VU bar (rises during playback *if* the
     3.5 mm mic is acoustically coupled to the speaker — not guaranteed).
   - **Off-device (authoritative):** the eMeet SmartCam films the LCD **and** its
     own microphone records the room; an FFT/RMS of that recording shows energy at
     ~1 kHz → proves the speaker emits sound regardless of on-device coupling.
3. **Test signal:** steady **1 kHz** sine. Capture a **silence baseline** and a
   **tone window** so the delta (VU bar + room-audio spectrum) is unmistakable.

## Why this is low-risk on the codec side

`codec_nau88c10_init()` already powers and routes the **DAC + speaker** path
(R10/0x0A DAC soft-mute, R11/0x0B DAC vol full, `set_output(CODEC_OUT_SPEAKER)` →
R3 0xED, R54/0x36 spk vol full, R69/0x45 5 V boost). The mic ADC path is already
proven. The only genuinely new work is **clocking I2S data OUT on `SPK_DIN`
(GPIO 5)** in lockstep with the existing RX, and **unmuting the DAC**
(`codec_nau88c10_dac_mute(false)`).

## Architecture & data flow

```
                       1 kHz sine (tone_gen, host-tested)
                              | TX DMA (read-ring, whole periods, zero CPU)
                              v
RP2350B  MCLK(22,PWM) ----> codec MCLK   SPK_DIN(GPIO5) --I2S--> NAU88C10 DAC -> SPEAKER (blown)
         PIO0 SM0 (DUPLEX): sideset LRCK(6)+BCLK(7), out GPIO5, in GPIO4
                              ^
3.5mm mic -> PGA+boost -> ADC -+ SPK_DOUT(GPIO4) --I2S--> RX FIFO
                              | RX DMA (existing ping-pong, DMA_IRQ_0)
                              v
                         VU peak per block -> ST7796 LCD (bar + TONE/PLAY indicator)
```

One PIO0 state machine does **both** directions, so TX and RX are sample-aligned by
construction. Single core. The proven RX timing is preserved bit-for-bit.

## Full-duplex I2S approach (Approach C)

Extend the proven `i2s_rx.pio` into one `i2s_duplex.pio` program. The sideset clock
sequence (LRCK=bit0/GPIO6, BCLK=bit1/GPIO7) and the RX sample points stay **exactly**
as the validated RX program. In the BCLK-low half-slots that the RX program spends on
`jmp x--`, the duplex program also drives the DAC bit with `out pins, 1` on GPIO5.
Net: same instruction budget per bit, RX behavior unchanged, TX added.

- `out` pin base = `PIN_AUDIO_DATA` (GPIO5), `out_shift` MSB-first, autopull 32.
- `in`  pin base = `PIN_AUDIO_DIN` (GPIO4), `in_shift` MSB-first, autopush 32.
- Pin dirs: GPIO5/6/7 outputs, GPIO4 input.
- `clkdiv = 4*ticks` and MCLK = 256·fs PWM — identical to the RX driver.
- Frame = `[L16 | R16]`; the tone is written into **both** halves so DAC routing is
  slot-agnostic. (RX still reads the mic from `MIC_I2S_SLOT`, unchanged.)

**Alignment fallback:** if RX peaks regress after the merge, the duplex `in`
placement is the same knob the RX spec documents (which BCLK half samples DIN); TX
alignment is forgiving because the DAC only needs a periodic waveform.

## TX playback path (zero-CPU, glitch-free)

`tone_gen_fill()` (vendored from `microphonearray`, pure sine, host-tested) fills a
fixed tone buffer once at startup. A single DMA channel streams it to the TX FIFO
with a **read-ring** so it loops forever with no IRQ and no core involvement.

- fs = 16 kHz, tone = 1 kHz → **16 frames/period**.
- Buffer = **64 frames** (`uint32_t`) = 256 bytes = `2^8` = exactly **4 periods** →
  the ring wraps on a period boundary, so there is no phase discontinuity.
- DMA: `read_increment=true`, ring on read addr (8-bit / 256-byte), write = TX FIFO
  (no increment), dreq = TX dreq, `chain_to` self (or rely on ring + endless count).

`audio_i2s_duplex_play_loop(const uint32_t* buf, size_t frames_pow2)` arms this;
`audio_i2s_duplex_play_stop()` parks the DAC at mid-scale (silence) for the baseline.

## Public driver API (the publishable surface)

`src/audio/audio_i2s_duplex.h`:
- `void audio_i2s_duplex_init(uint32_t sample_rate);` — MCLK + PIO + pindirs.
- `volatile const void *audio_i2s_duplex_rxf(void);` / `uint audio_i2s_duplex_rx_dreq(void);`
  — same shape as the RX driver so `vu_capture` binds to it unchanged.
- `volatile void *audio_i2s_duplex_txf(void);` / `uint audio_i2s_duplex_tx_dreq(void);`
- `void audio_i2s_duplex_play_loop(const uint32_t *buf, uint frames_pow2);`
- `void audio_i2s_duplex_play_stop(void);`

`audio_i2s_rx.*` is **superseded** by the duplex driver. To keep the RX-only build
working and the diff reviewable, the duplex driver lives alongside it; `main.c`
switches to the duplex driver. (RX-only files may be removed in the publish-cleanup
task once duplex is validated.)

## Demo app (`main.c`) changes

1. `codec_nau88c10_init()` then `codec_nau88c10_dac_mute(false)` (unmute DAC).
2. `audio_i2s_duplex_init(16000)`; fill tone buffer; `vu_capture_start()` (now on the
   duplex RX FIFO).
3. **Sequence on a timer** so both proofs capture a clean delta:
   `SILENCE 3 s → TONE 6 s → SILENCE 3 s` (looping), driving `play_loop`/`play_stop`.
4. LCD: keep the VU bar; add a large high-contrast **"TONE ON" / "TONE OFF"** banner
   and a frame counter so the camera can read state and confirm the firmware is live.
5. RTT: log `state=TONE peak=… ` each block so the on/off VU delta is visible in logs.

## Verification (E2E)

**Host (TDD, before hardware):**
- `tone_gen` host test (pytest harness, mirrors `microphonearray`): 1 kHz @ 16 kHz
  buffer has the right period, amplitude ~28000, and tiles seamlessly across the
  64-frame ring (last→first sample continuity).

**On-device (RTT):**
- `codec: nau88c10 init done (16 kHz, speaker)` + `CODEC OK`.
- During `TONE`, VU `peak` clearly exceeds the `SILENCE` baseline (if mic-coupled),
  and never starves (`no audio blocks` must not appear) → duplex RX still streams.

**eMeet (authoritative, hands-free):**
- `tools/screencap/` script: capture an LCD still in each state (tune focus/exposure
  /zoom; add the high-contrast banner so the screen is camera-readable — the raw
  1080p grab is currently washed out).
- Record ~12 s of eMeet-mic audio across SILENCE→TONE→SILENCE; compute a short-time
  spectrum. **Pass = a clear ~1 kHz peak present only during the TONE window**, with
  the room/silence windows as the noise floor. Save WAV + spectrum PNG as evidence.

## Components / isolation

- `tone_gen.c/.h` — pure sample math, host-testable, no hardware. (vendored)
- `audio_i2s_duplex.c/.h` + `i2s_duplex.pio` — the bus driver; owns PIO/DMA/MCLK.
- `vu_capture.c` — unchanged logic; re-points to the duplex RX accessors.
- `main.c` — demo orchestration only (state machine + LCD + RTT).
- `tools/screencap/` — eMeet capture + spectrum analysis (Python, host-side).

## Out of scope (YAGNI)

- Arbitrary WAV playback / streaming from flash (tone only).
- Fixing the blown speaker or any audio quality work.
- Volume/mixer controls beyond the existing `set_output`.
- Stereo content (mono tone duplicated to both slots).

## Risks

- **PIO merge regresses RX:** mitigated by preserving the exact RX sample sequence and
  validating the VU path first with TONE off.
- **Camera can't read the LCD:** mitigated by the high-contrast banner + a screencap
  tuning step (zoom/focus/exposure) before declaring E2E done.
- **3.5 mm mic doesn't hear the speaker:** expected possible; the eMeet-mic spectrum is
  the authoritative proof and does not depend on it.
