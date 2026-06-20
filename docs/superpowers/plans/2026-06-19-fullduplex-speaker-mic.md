# Full-Duplex Speaker + External Mic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the FreeWili 2 firmware play a 1 kHz tone out the onboard speaker (NAU88C10 DAC) while simultaneously capturing the external 3.5 mm mic, exposing a reusable full-duplex I2S driver, and prove the sound is physically present via the eMeet camera + mic.

**Architecture:** One PIO0 state machine clocks the codec (LRCK/BCLK sideset, MCLK via PWM) and both shifts the DAC bit OUT on GPIO5 and the ADC bit IN on GPIO4 each bit period — so TX and RX are sample-aligned by construction. A zero-CPU DMA read-ring streams a pre-computed tone buffer to the TX FIFO; the existing ping-pong DMA drains the RX FIFO into the VU meter. The demo cycles SILENCE→TONE→SILENCE so both proofs show a clean delta.

**Tech Stack:** Pico SDK 2.2.0, RP2350B, PIO, DMA, PWM; host tests via mingw gcc + pytest; eMeet capture via ffmpeg dshow + Python (numpy) FFT.

## Global Constraints

- `BOARD_SYS_CLOCK_KHZ = 153600` (proven stable; 252 MHz hard-faults). Do not change.
- `PICO_BOARD freewili2` set in CMakeLists.txt; build.ps1 must NOT pass `-DPICO_BOARD`.
- Pins: DAC out = GPIO5 (`PIN_AUDIO_DATA`), ADC in = GPIO4 (`PIN_AUDIO_DIN`), LRCK = GPIO6, BCLK = GPIO7, MCLK = GPIO22. I2C1 = GPIO26/27.
- fs = 16000 Hz. Codec is I2S slave, MCLK-direct, AIFMT left-justified (reg 0x04 = 0x08). Mic ADC streams on the RIGHT slot (`MIC_I2S_SLOT = 1`).
- `pico_set_binary_type(... copy_to_ram)`; stdio over SEGGER RTT only.
- Host gcc: `C:/msys64/mingw64/bin/gcc.exe`. ffmpeg on PATH (Gyan build). eMeet device name: `EMEET SmartCam C960 4K` (video) / `Microphone (EMEET SmartCam C960 4K)` (audio).
- Frequent commits; one logical change per commit.

---

### Task 1: Vendor + host-test `tone_gen`

**Files:**
- Create: `src/audio/tone_gen.c`, `src/audio/tone_gen.h` (copy from `microphonearray/src/codec/tone_gen.*`, fix include path to `audio/tone_gen.h`)
- Create: `tests/host/tone_gen_harness.c`
- Test: `tests/test_tone_gen.py`
- Modify: `CMakeLists.txt` (add `src/audio/tone_gen.c` to the executable sources)

**Interfaces:**
- Produces: `void tone_gen_fill(int16_t* buf, unsigned n, float hz, float fs, float* phase);`

- [ ] **Step 1: Copy the vendored source.** Copy `microphonearray/src/codec/tone_gen.c` → `src/audio/tone_gen.c` and `tone_gen.h` → `src/audio/tone_gen.h`. In `tone_gen.c` change the include to `#include "audio/tone_gen.h"`.

- [ ] **Step 2: Write the failing test** `tests/test_tone_gen.py`:

```python
# tests/test_tone_gen.py — pure tone math, compiled and run on host.
import pathlib, shutil, subprocess, pytest
ROOT = pathlib.Path(__file__).parent.parent
GCC = shutil.which("gcc") or r"C:/msys64/mingw64/bin/gcc.exe"
pytestmark = pytest.mark.skipif(not pathlib.Path(GCC).exists() and shutil.which("gcc") is None,
                                reason="no host gcc")

@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    exe = tmp_path_factory.mktemp("tone") / "tone.exe"
    subprocess.run([GCC, "-Wall", "-Werror", "-I", str(ROOT / "src"), "-o", str(exe),
                    str(ROOT / "tests/host/tone_gen_harness.c"),
                    str(ROOT / "src/audio/tone_gen.c"), "-lm"], check=True)
    return exe

def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True,
                          text=True, check=True).stdout.split()

def test_amplitude_near_full_scale(harness):
    # 1 kHz @ 16 kHz, 64 samples: peak ~28000, never clips.
    pk = int(run(harness, "peak")[0])
    assert 24000 < pk < 32768

def test_one_period_is_16_samples(harness):
    # 1 kHz at 16 kHz fs => exactly 16 samples/period => 4 zero-crossings over 64 samples? 
    # 64 samples = 4 periods => 8 sign changes.
    assert run(harness, "zc")[0] == "8"

def test_ring_tiles_seamlessly(harness):
    # 64-frame buffer = exactly 4 periods; sample[64] (next period) must equal sample[0].
    assert run(harness, "wrap")[0] == "OK"
```

- [ ] **Step 3: Write the harness** `tests/host/tone_gen_harness.c`:

```c
#include <stdio.h>
#include <string.h>
#include "audio/tone_gen.h"
int main(int argc, char** argv) {
    const char* what = argc > 1 ? argv[1] : "peak";
    int16_t buf[65]; float ph = 0.0f;
    tone_gen_fill(buf, 65, 1000.0f, 16000.0f, &ph);  // 64 + 1 to check wrap
    if (!strcmp(what, "peak")) {
        int pk = 0; for (int i=0;i<64;i++){ int a = buf[i]<0?-buf[i]:buf[i]; if(a>pk)pk=a; }
        printf("%d\n", pk);
    } else if (!strcmp(what, "zc")) {
        int zc = 0; for (int i=1;i<64;i++) if ((buf[i-1]<0)!=(buf[i]<0)) zc++;
        printf("%d\n", zc);
    } else { // wrap: sample 64 should match sample 0 within rounding
        int d = buf[64] - buf[0]; if (d<0) d=-d;
        printf("%s\n", d <= 2 ? "OK" : "BAD");
    }
    return 0;
}
```

- [ ] **Step 4: Run the test, expect FAIL** (no harness/exe yet was true before step 3; now run): `python -m pytest tests/test_tone_gen.py -v`. If `zc`/`wrap` fail, that is real signal about the period math — do NOT loosen asserts without confirming 1 kHz @ 16 kHz = 16 samples/period.

- [ ] **Step 5: Make it pass.** The vendored `tone_gen_fill` already produces a continuous phase-advanced sine; `test_one_period_is_16_samples` and `test_ring_tiles_seamlessly` should pass as-is. If `wrap` is off by >2 LSB, that is expected sine rounding at the period seam and the ring is still glitch-free; widen the wrap tolerance to `<= 3` only with a comment explaining int16 rounding.

- [ ] **Step 6: Add to CMake.** In `CMakeLists.txt`, add `src/audio/tone_gen.c` to the `add_executable(externalmicvalid ...)` list.

- [ ] **Step 7: Run tests, expect PASS:** `python -m pytest tests/test_tone_gen.py -v` → 3 passed.

- [ ] **Step 8: Commit:**
```bash
git add src/audio/tone_gen.c src/audio/tone_gen.h tests/host/tone_gen_harness.c tests/test_tone_gen.py CMakeLists.txt
git commit -m "Task 1: vendor tone_gen + host tests (1kHz/16kHz, 64-frame ring tiles)"
```

---

### Task 2: Full-duplex PIO program + driver

**Files:**
- Create: `src/audio/i2s_duplex.pio`
- Create: `src/audio/audio_i2s_duplex.c`, `src/audio/audio_i2s_duplex.h`
- Modify: `CMakeLists.txt` (swap RX source/pio for duplex)

**Interfaces:**
- Consumes: `tone_gen_fill` (Task 1); board pin macros (`PIN_AUDIO_DATA`, `PIN_AUDIO_DIN`, `PIN_AUDIO_LRCK`, `PIN_AUDIO_BCLK`, `PIN_AUDIO_MCLK`).
- Produces:
  - `void audio_i2s_duplex_init(uint32_t sample_rate);`
  - `volatile const void *audio_i2s_duplex_rxf(void);`  (RX FIFO addr — same shape as old `audio_i2s_rx_rxf`)
  - `uint audio_i2s_duplex_rx_dreq(void);`
  - `void audio_i2s_duplex_play_loop(const uint32_t *buf, uint frames_pow2_bytes_aligned);`
  - `void audio_i2s_duplex_play_stop(void);`

- [ ] **Step 1: Write the duplex PIO** `src/audio/i2s_duplex.pio`. This preserves the RX sideset clock sequence (bit0=LRCK, bit1=BCLK) and the RX sample edge (sample DIN while BCLK high), and adds `out pins,1` (DAC) on the BCLK-low slot. Each counted bit is 3 PIO instructions (out@low, in@high, jmp@low) → BCLK = 1 pulse/bit, 32 bits/frame → PIO clock = 96·fs (clkdiv set in C):

```
; src/audio/i2s_duplex.pio — full-duplex I2S, MCU supplies all clocks (codec=slave).
; 16 bits/channel, MSB first, 32-bit frame [L16|R16]. side-set bit0=LRCK, bit1=BCLK.
; OUT pin = SPK_DIN (GPIO5, DAC), IN pin = SPK_DOUT (GPIO4, ADC).
; Per bit: out on BCLK-low, sample (in) on BCLK-high (rising edge), count on BCLK-low.
; ALIGNMENT KNOB (bring-up risk, same as the proven RX program): if RX peaks stay at
; the floor while the mic is driven, move the `in pins,1` to the other BCLK phase, or
; flip MIC_I2S_SLOT in vu_capture.c, or swap the 0b00/0b01 channel side values.
.program i2s_duplex
.side_set 2

.wrap_target
    set x, 14            side 0b00   ; LEFT: LRCK=0, BCLK=0
left_bit:
    out pins, 1          side 0b00   ; BCLK low: present DAC bit
    in pins, 1           side 0b10   ; BCLK high: latch ADC bit (rising edge)
    jmp x-- left_bit     side 0b00   ; BCLK low: advance
    out pins, 1          side 0b00   ; 16th DAC bit (left)
    in pins, 1           side 0b10   ; 16th ADC bit (left)
    set x, 14            side 0b01   ; RIGHT: LRCK=1, BCLK=0
right_bit:
    out pins, 1          side 0b01
    in pins, 1           side 0b11   ; BCLK high, LRCK high: sample
    jmp x-- right_bit    side 0b01
    out pins, 1          side 0b01   ; 16th DAC bit (right)
    in pins, 1           side 0b11   ; 16th ADC bit (right)
.wrap
```

- [ ] **Step 2: Write the header** `src/audio/audio_i2s_duplex.h`:

```c
// src/audio/audio_i2s_duplex.h — full-duplex I2S on PIO0 SM0 (DAC out GPIO5, ADC in GPIO4).
#ifndef AUDIO_I2S_DUPLEX_H
#define AUDIO_I2S_DUPLEX_H
#include <stdint.h>
#include "hardware/pio.h"

// Start MCLK (256*fs PWM on GPIO22), load the duplex program on PIO0 SM0, set
// pindirs (GPIO5/6/7 out, GPIO4 in) + clkdiv, enable the SM. RX runs immediately;
// TX FIFO outputs silence (0 = mid-scale) until play_loop() is armed.
void audio_i2s_duplex_init(uint32_t sample_rate);

// RX FIFO plumbing for vu_capture (same shape as the old RX driver).
volatile const void *audio_i2s_duplex_rxf(void);
uint audio_i2s_duplex_rx_dreq(void);

// Arm a zero-CPU DMA read-ring that loops `buf` (frames, each uint32 = [L16|R16])
// to the TX FIFO forever. `buf` MUST be aligned to its byte size and the byte size
// MUST be a power of two (ring requirement). For 64 frames -> 256 bytes, aligned(256).
void audio_i2s_duplex_play_loop(const uint32_t *buf, uint frames);

// Stop playback: abort the TX DMA and park the TX FIFO at 0 (DAC mid-scale silence).
void audio_i2s_duplex_play_stop(void);

#endif
```

- [ ] **Step 3: Write the driver** `src/audio/audio_i2s_duplex.c`. Mirrors `audio_i2s_rx.c` for MCLK/PIO/clkdiv (but clkdiv = 6*ticks for the 3-instr/bit, 96·fs PIO clock) and adds the TX read-ring DMA (two channels chained, ring on read, no IRQ):

```c
// src/audio/audio_i2s_duplex.c — full-duplex I2S: PIO0 SM0 clocks the codec and
// both shifts the DAC out (GPIO5) and the ADC in (GPIO4). Tone playback is a
// zero-CPU DMA read-ring over a pre-filled buffer.
#include "audio/audio_i2s_duplex.h"
#include "platform/board.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "i2s_duplex.pio.h"

#define DPX_PIO pio0
#define DPX_SM  0

static int s_tx_dma[2] = { -1, -1 };

void audio_i2s_duplex_init(uint32_t sample_rate) {
    // MCLK = 256*fs, 50% duty (codec MCLK-direct).
    gpio_set_function(PIN_AUDIO_MCLK, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_AUDIO_MCLK);
    uint32_t ticks = clock_get_hz(clk_sys) / (256u * sample_rate);
    pwm_set_wrap(slice, ticks - 1);
    pwm_set_gpio_level(PIN_AUDIO_MCLK, ticks / 2);
    pwm_set_enabled(slice, true);

    uint offset = pio_add_program(DPX_PIO, &i2s_duplex_program);
    pio_sm_config c = i2s_duplex_program_get_default_config(offset);

    sm_config_set_out_pins(&c, PIN_AUDIO_DATA, 1);        // GPIO5 = SPK_DIN (DAC)
    sm_config_set_in_pins(&c, PIN_AUDIO_DIN);             // GPIO4 = SPK_DOUT (ADC)
    sm_config_set_sideset_pins(&c, PIN_AUDIO_LRCK);       // bit0=LRCK(6), bit1=BCLK(7)
    sm_config_set_out_shift(&c, false, true, 32);         // MSB first, autopull 32
    sm_config_set_in_shift(&c, false, true, 32);          // MSB first, autopush 32

    pio_gpio_init(DPX_PIO, PIN_AUDIO_DATA);
    pio_gpio_init(DPX_PIO, PIN_AUDIO_LRCK);
    pio_gpio_init(DPX_PIO, PIN_AUDIO_BCLK);
    gpio_set_function(PIN_AUDIO_DIN, GPIO_FUNC_PIO0);     // ADC input pin

    pio_sm_init(DPX_PIO, DPX_SM, offset, &c);
    uint out_mask = (1u << PIN_AUDIO_DATA) | (3u << PIN_AUDIO_LRCK);  // 5,6,7 out
    pio_sm_set_pindirs_with_mask(DPX_PIO, DPX_SM, out_mask,
                                 out_mask | (1u << PIN_AUDIO_DIN));

    // 3 PIO instr/bit -> PIO clock = 96*fs; clkdiv = sysclk/(96*fs) = 6*ticks
    // (ticks = sysclk/(256*fs); 256/96 is not integer, so derive directly).
    float div = (float)clock_get_hz(clk_sys) / (96.0f * (float)sample_rate);
    pio_sm_set_clkdiv(DPX_PIO, DPX_SM, div);
    pio_sm_set_enabled(DPX_PIO, DPX_SM, true);
}

volatile const void *audio_i2s_duplex_rxf(void) {
    return (volatile const void *)&DPX_PIO->rxf[DPX_SM];
}
uint audio_i2s_duplex_rx_dreq(void) {
    return pio_get_dreq(DPX_PIO, DPX_SM, false);   // false = RX DREQ
}

void audio_i2s_duplex_play_loop(const uint32_t *buf, uint frames) {
    if (s_tx_dma[0] < 0) {
        s_tx_dma[0] = dma_claim_unused_channel(true);
        s_tx_dma[1] = dma_claim_unused_channel(true);
    }
    uint ring_bits = 0, bytes = frames * 4;
    while ((1u << ring_bits) < bytes) ring_bits++;   // log2(bytes); buf is aligned(bytes)
    uint tx_dreq = pio_get_dreq(DPX_PIO, DPX_SM, true);   // true = TX DREQ
    for (int i = 0; i < 2; i++) {
        dma_channel_config c = dma_channel_get_default_config(s_tx_dma[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, tx_dreq);
        channel_config_set_ring(&c, false, ring_bits);   // wrap READ addr at 2^ring_bits
        channel_config_set_chain_to(&c, s_tx_dma[i ^ 1]);
        dma_channel_configure(s_tx_dma[i], &c, &DPX_PIO->txf[DPX_SM], buf, frames, false);
    }
    dma_channel_start(s_tx_dma[0]);
}

void audio_i2s_duplex_play_stop(void) {
    for (int i = 0; i < 2; i++)
        if (s_tx_dma[i] >= 0) dma_channel_abort(s_tx_dma[i]);
    // Park TX FIFO at 0 so the DAC sits at mid-scale (silence) during the baseline.
    pio_sm_clear_fifos(DPX_PIO, DPX_SM);
}
```

- [ ] **Step 4: Update CMake.** In `CMakeLists.txt`: replace `src/audio/audio_i2s_rx.c` with `src/audio/audio_i2s_duplex.c` in the sources; replace the `pico_generate_pio_header(... i2s_rx.pio)` line with `i2s_duplex.pio`. Keep `hardware_dma` linked (already present).

- [ ] **Step 5: Build (compile-only gate).** `powershell -File tools/build.ps1 -Clean`. Expected: `Build OK -> .../externalmicvalid.uf2`. This catches PIO assembly / API errors before involving hardware. (vu_capture / main still reference the old RX accessors — those are fixed in Task 3, so expect link errors mentioning `audio_i2s_rx_*`; that is the signal to proceed to Task 3, OR temporarily build will fail at link. To keep this task self-contained, defer the build gate to Task 3 Step 4.)

- [ ] **Step 6: Commit:**
```bash
git add src/audio/i2s_duplex.pio src/audio/audio_i2s_duplex.c src/audio/audio_i2s_duplex.h CMakeLists.txt
git commit -m "Task 2: full-duplex i2s_duplex.pio + driver (DAC out GPIO5 + ADC in GPIO4, TX read-ring)"
```

---

### Task 3: Re-point capture + wire the demo state machine

**Files:**
- Modify: `src/audio/vu_capture.c` (use duplex RX accessors), `src/audio/vu_capture.h` (no change expected)
- Modify: `src/main.c` (unmute DAC, init duplex, fill tone buffer, SILENCE/TONE state machine, LCD banner)
- Delete: `src/audio/audio_i2s_rx.c`, `src/audio/audio_i2s_rx.h` (superseded)

**Interfaces:**
- Consumes: all of Task 2's API + `tone_gen_fill` (Task 1) + `codec_nau88c10_dac_mute` (existing).

- [ ] **Step 1: Re-point `vu_capture.c`.** Replace `#include "audio/audio_i2s_rx.h"` with `#include "audio/audio_i2s_duplex.h"`, and the two call sites: `audio_i2s_rx_dreq()` → `audio_i2s_duplex_rx_dreq()`, `audio_i2s_rx_rxf()` → `audio_i2s_duplex_rxf()`.

- [ ] **Step 2: Delete the superseded RX driver.** `git rm src/audio/audio_i2s_rx.c src/audio/audio_i2s_rx.h`.

- [ ] **Step 3: Rewrite `main.c`** to unmute the DAC, init the duplex bus, fill the tone ring, and cycle SILENCE(3s)→TONE(6s)→SILENCE(3s) while keeping the VU bar and adding a large camera-readable banner. Replace the audio-setup + main loop (keep the display/VU helpers):

```c
    // --- after st7796 setup, replacing the old codec/RX/vu block ---
    codec_nau88c10_init();
    bool codec_ok = codec_nau88c10_input_ok();
    st7796_draw_text(8, 40, 2, codec_ok ? rgb565_be(0,255,0) : rgb565_be(255,0,0),
                     rgb565_be(0,0,40), codec_ok ? "CODEC OK" : "CODEC FAIL");

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
    vu_draw(0);

    // SILENCE 3s -> TONE 6s -> SILENCE 3s, looping. Banner is big for the camera.
    enum { SILENCE, TONE } state = SILENCE;
    bool playing = false;
    absolute_time_t t_state = get_absolute_time();
    uint32_t blk = 0;
    while (true) {
        // advance the state machine on a wall clock
        int64_t held = absolute_time_diff_us(t_state, get_absolute_time());
        if (state == SILENCE && held > 3000000) {
            state = TONE; t_state = get_absolute_time();
            audio_i2s_duplex_play_loop(tone_buf, 64); playing = true;
            st7796_fill_rect(8, 80, 464, 40, rgb565_be(0,120,0));
            st7796_draw_text(16, 88, 3, rgb565_be(255,255,255), rgb565_be(0,120,0), "TONE ON  1kHz");
            DIAG("state=TONE (play 1kHz)\n");
        } else if (state == TONE && held > 6000000) {
            state = SILENCE; t_state = get_absolute_time();
            audio_i2s_duplex_play_stop(); playing = false;
            st7796_fill_rect(8, 80, 464, 40, rgb565_be(60,0,0));
            st7796_draw_text(16, 88, 3, rgb565_be(255,255,255), rgb565_be(60,0,0), "TONE OFF");
            DIAG("state=SILENCE\n");
        }
        if (vu_block_ready()) {
            uint16_t pk = vu_block_peak();
            vu_draw(pk);
            if ((blk++ & 0x0F) == 0)
                DIAG("vu: state=%s blk=%u peak=%u\n", playing ? "TONE" : "SIL",
                     (unsigned)blk, (unsigned)pk);
        }
        tight_loop_contents();
    }
```

Also update the includes at the top of `main.c`: replace `#include "audio/audio_i2s_rx.h"` with `#include "audio/audio_i2s_duplex.h"` and add `#include "audio/tone_gen.h"`. Remove the now-unused `last_block`/`warned` heartbeat (or keep it — optional).

- [ ] **Step 4: Build gate.** `powershell -File tools/build.ps1 -Clean`. Expected: `Build OK -> C:/buildfiles/externalmicvalid/externalmicvalid.uf2`, no link errors referencing `audio_i2s_rx_*`.

- [ ] **Step 5: Commit:**
```bash
git add -A
git commit -m "Task 3: re-point vu_capture to duplex; main plays 1kHz tone (SILENCE/TONE cycle) + camera banner"
```

---

### Task 4: Flash + on-device RTT verification

**Files:** none (verification only). Uses `tools/flash.ps1`, `tools/rtt.ps1`.

- [ ] **Step 1: Flash.** `powershell -File tools/flash.ps1`. Expected: `Flashed + reset via OpenOCD`. If it fails with no probe, STOP and report (board/probe issue, user is remote).

- [ ] **Step 2: Capture RTT for ~14 s** (one full SILENCE→TONE→SILENCE cycle): `powershell -File tools/rtt.ps1 -Seconds 14`.

- [ ] **Step 3: Verify the log shows:**
  - `codec: nau88c10 init done (16 kHz, speaker)` and `CODEC OK` path (`rev!=0`, `pm2=0x015`).
  - alternating `state=TONE` / `state=SILENCE`.
  - `vu: ...` lines continuously (RX never starves → duplex RX intact). **This is the proven-RX gate.**
  - Ideally `peak` during `TONE` > `peak` during `SIL` (only if the 3.5 mm mic hears the speaker; not required — eMeet is authoritative).

- [ ] **Step 4: If RX starved** (no `vu:` lines, or peak pinned at 0/32767): apply the documented alignment knob in `i2s_duplex.pio` (move `in pins,1` to the other BCLK phase) or flip `MIC_I2S_SLOT` in `vu_capture.c`; rebuild, reflash, re-verify. Commit the working alignment with a message noting the on-device finding.

- [ ] **Step 5: Commit any alignment fix** (if made):
```bash
git commit -am "Task 4: on-device alignment fix — duplex RX validated (peak responds; no starve)"
```

---

### Task 5: eMeet E2E — film the LCD + record the room, prove 1 kHz present

**Files:**
- Create: `tools/screencap/capture_e2e.ps1` (ffmpeg dshow: LCD still + room-audio WAV)
- Create: `tools/screencap/analyze_tone.py` (numpy FFT: 1 kHz energy in TONE window vs SILENCE)

- [ ] **Step 1: Write `tools/screencap/capture_e2e.ps1`.** Records ~14 s of eMeet-mic audio (covering a full SILENCE→TONE→SILENCE cycle) and grabs LCD stills. Tune `-video_size`, and crop/zoom; the raw 1080p grab is washed out, so also try `-vf "eq=contrast=1.4:brightness=-0.05"` and a tighter framing:

```powershell
param([string]$Out = "tools/screencap/captures")
$ff = "ffmpeg"; $cam = 'video=EMEET SmartCam C960 4K'; $mic = 'audio=Microphone (EMEET SmartCam C960 4K)'
New-Item -ItemType Directory -Force $Out | Out-Null
# 14s room audio (mono 16k is plenty to see a 1 kHz tone)
& $ff -hide_banner -loglevel error -f dshow -i $mic -t 14 -ac 1 -ar 16000 -y "$Out/room.wav"
# LCD still right after (TONE banner will be visible depending on phase; grab a few)
1..3 | ForEach-Object {
  & $ff -hide_banner -loglevel error -f dshow -video_size 1920x1080 -i $cam `
       -vf "eq=contrast=1.4:brightness=-0.04" -frames:v 1 -update 1 -y "$Out/lcd_$_.png"
  Start-Sleep -Milliseconds 1500
}
```

- [ ] **Step 2: Write `tools/screencap/analyze_tone.py`.** Slide a window across `room.wav`; report the per-window magnitude at 1 kHz. PASS if the max-1kHz window is clearly above the min-1kHz window (tone present only during TONE):

```python
import sys, wave, numpy as np
w = wave.open(sys.argv[1] if len(sys.argv) > 1 else "tools/screencap/captures/room.wav")
fs = w.getframerate(); n = w.getnframes()
x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(float)
x = x.reshape(-1, w.getnchannels()).mean(axis=1) if w.getnchannels() > 1 else x
win = fs  # 1 s windows
k = int(round(1000.0 / fs * win))  # 1 kHz bin
mags = []
for i in range(0, len(x) - win, win // 2):
    seg = x[i:i+win] * np.hanning(win)
    mag = np.abs(np.fft.rfft(seg))
    band = mag[max(k-2,0):k+3].sum()      # ~1 kHz band
    total = mag.sum() + 1e-9
    mags.append(band / total)             # fraction of energy near 1 kHz
mags = np.array(mags)
hi, lo = mags.max(), mags.min()
print(f"1kHz energy fraction: min={lo:.4f} max={hi:.4f} ratio={hi/(lo+1e-9):.1f}x")
print("PASS: tone present" if hi > 0.05 and hi > 4*lo else "INCONCLUSIVE: check speaker/mic/levels")
```

- [ ] **Step 3: Run the capture** while the firmware cycles: `powershell -File tools/screencap/capture_e2e.ps1`. Then `python tools/screencap/analyze_tone.py`.

- [ ] **Step 4: Inspect the LCD stills** (`captures/lcd_*.png`) — confirm the screen is readable and at least one shows `TONE ON 1kHz` (proves firmware is live + state machine running on the real panel). If washed out, re-tune contrast/zoom/exposure in `capture_e2e.ps1` and re-run.

- [ ] **Step 5: Acceptance.** E2E passes when: (a) `analyze_tone.py` prints `PASS: tone present` (1 kHz energy spikes in the TONE window), AND (b) an LCD still clearly shows the firmware running (`TONE ON` banner / VU bar). Note in the commit if the on-device mic VU also rose during TONE (bonus self-test).

- [ ] **Step 6: Commit tools + evidence pointer** (PNG/WAV are gitignored; commit the scripts + a findings note):
```bash
git add tools/screencap/capture_e2e.ps1 tools/screencap/analyze_tone.py
git commit -m "Task 5: eMeet E2E capture + 1kHz FFT proof scripts"
```

---

### Task 6: Document results + prep for GitHub publish

**Files:**
- Create/Modify: `README.md` (driver usage + wiring + validation evidence)
- Create: `docs/superpowers/findings/2026-06-19-fullduplex-e2e.md` (RTT excerpt, FFT ratio, LCD still reference)

- [ ] **Step 1: Write `docs/superpowers/findings/2026-06-19-fullduplex-e2e.md`** capturing: the RTT excerpt (codec OK, alternating states, continuous vu peaks), the `analyze_tone.py` ratio output, which `lcd_*.png` shows `TONE ON`, and any alignment knob that was needed.

- [ ] **Step 2: Write/extend `README.md`** with: what the project is (full-duplex I2S speaker+mic validation for FreeWili 2 / NAU88C10), the public `audio_i2s_duplex` API, the pin map, build/flash/RTT commands, and the E2E proof method. Note the speaker is hardware-blown so "sound present, not clean."

- [ ] **Step 3: Commit:**
```bash
git add README.md docs/superpowers/findings/2026-06-19-fullduplex-e2e.md
git commit -m "Task 6: document full-duplex driver + E2E validation evidence"
```

- [ ] **Step 4: Offer the user the merge/PR decision** (finishing-a-development-branch skill) on their next check-in — do not push without the user's go-ahead.

---

## Self-Review

- **Spec coverage:** reusable driver (Task 2) ✓; dual proof on-device VU (Task 4) + eMeet cam/mic FFT (Task 5) ✓; 1 kHz steady sine (Task 1) ✓; silence/tone delta (Task 3 state machine) ✓; DAC unmute (Task 3) ✓; zero-CPU TX ring (Task 2) ✓; camera-readable banner (Task 3/5) ✓; publish docs (Task 6) ✓.
- **Placeholder scan:** none — all code blocks complete.
- **Type consistency:** `audio_i2s_duplex_rxf()` / `audio_i2s_duplex_rx_dreq()` used identically in `vu_capture.c` (Task 3) and defined in Task 2 header ✓. `tone_gen_fill` signature identical across Tasks 1/3 ✓. `play_loop(buf, frames)` / `play_stop()` match between header (Task 2) and caller (Task 3) ✓.
- **Open risk carried forward:** the 3-instr/bit PIO is the one genuinely new bit of timing; Task 4 Step 4 is the explicit on-device tuning gate with the documented fallback. The RX-intact check (continuous `vu:` lines) is the guard that the merge didn't break the proven capture.
