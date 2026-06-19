# External Microphone Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build standalone FreeWili 2 (RP2350B) firmware that captures the 3.5mm-jack microphone through the NAU88C10 ADC over a new I2S **receive** path on `SPK_DOUT` (GPIO 4) and proves it works with a live VU meter on the ST7796 LCD.

**Architecture:** A single PIO0 state machine generates MCLK/BCLK/LRCK (codec is I2S slave) and shifts the codec's ADC data **in**, reusing the proven `clkdiv = 4*ticks` MCLK-lock recipe from the reference TX driver. A ping-pong DMA fills PCM block buffers; per block we compute a peak and draw a VU bar. Pure DSP/geometry logic is isolated into a host-testable unit; hardware bring-up is verified on-device over SEGGER RTT.

**Tech Stack:** Pico SDK 2.2.0, RP2350 (arm-none-eabi-gcc 14.2), CMake + Ninja, PIO (pioasm), hardware DMA/PWM/I2C/SPI, SEGGER RTT, OpenOCD (CMSIS-DAP probe). Host tests: `gcc` + Python `pytest`.

## Global Constraints

- **Board/MCU:** RP2350B, `clk_sys = 252 MHz`, vreg 1.15 V (set by vendored `board.c`). Build with `PICO_BOARD=pico2`, `PICO_PLATFORM=rp2350` (matches the proven reference build; this test touches only GPIOs < 30 so the RP2350A/B GPIO-count difference is irrelevant).
- **Codec:** NAU88C10 on I2C1 (GPIO 26 SDA / 27 SCL, 400 kHz), 7-bit address `0x1A` (decimal 26). Register writes are 7-bit reg + 9-bit value (`byte0 = reg<<1 | val[8]`, `byte1 = val[7:0]`).
- **Audio format:** I2S, 16-bit/channel, **16 kHz**, codec = slave, MCLK-direct. MCLK = 256·fs via PWM on GPIO 22. `ticks = clk_sys / (256*sample_rate)`; PIO `clkdiv = 4*ticks` so `fs = MCLK/256` exactly (independent dividers cause the MCLK-direct codec to slip samples — proven reference finding).
- **Audio pins:** `SPK_DOUT`=GPIO 4 (ADC data **in**, the new RX path), `SPK_DIN`=GPIO 5 (DAC data, unused here), `SPK_LRCK`=GPIO 6, `SPK_BCLK`=GPIO 7, `MCLK`=GPIO 22.
- **No UART/USB stdio:** all text goes over SEGGER RTT channel 0 via `DIAG(...)`. View with `tools/rtt.ps1`.
- **Diagnostics have NO float:** `DIAG`/`SEGGER_RTT_printf` supports `%d %u %x %s %c` only. Float math (log10) is allowed in firmware C and host tests, never in a format string.
- **Reference to vendor from:** `C:\~prj\Dropbox\vibeProjects\movieplayer` (call it `$REF`).
- **Toolchain paths** (from `$REF/tools/build.ps1`): `~/.pico-sdk/sdk/2.2.0`, `toolchain/14_2_Rel1`, `ninja/v1.12.1`, `cmake/v3.31.5`, `picotool/2.2.0-a4`, `openocd/0.12.0+dev`.
- **Commit style:** small, frequent commits; one per task minimum. Repo already initialized at project root.

---

## File Structure

**Vendored unchanged from `$REF`:**
- `pico_sdk_import.cmake`
- `src/boards/freewili2.h`
- `src/platform/diag.h`
- `src/platform/board.{c,h}`
- `src/display/st7796.{c,h}`, `src/display/font5x7.c`
- `third_party/segger_rtt/{SEGGER_RTT.c,SEGGER_RTT_printf.c,SEGGER_RTT.h,SEGGER_RTT_Conf.h}`
- `tools/build.ps1`, `tools/flash.ps1`, `tools/rtt.ps1` (adapted: project name + no `-SourceFlash/-Spike` options)

**Vendored + extended:**
- `src/audio/codec_nau88c10.{c,h}` (add a boot-time input-path verify helper)

**New:**
- `CMakeLists.txt` — trimmed to this project's sources, assembles the RX PIO.
- `src/audio/i2s_rx.pio` — I2S master-clock RX program (pioasm).
- `src/audio/audio_i2s_rx.{c,h}` — RX PIO init + FIFO/DREQ accessors.
- `src/audio/vu_meter.{c,h}` — **pure, host-testable**: sample extraction, peak, dB→bar geometry, color.
- `src/audio/vu_capture.{c,h}` — ping-pong DMA capture + block-ready + per-block peak.
- `src/main.c` — bring-up + render loop.
- `tests/host/vu_meter_harness.c`, `tests/test_vu_meter.py` — host unit tests for `vu_meter`.

---

## Task 1: Project scaffold — build, flash, LCD lights, RTT banner

**Files:**
- Create: `CMakeLists.txt`, `pico_sdk_import.cmake`, `src/boards/freewili2.h`, `src/platform/diag.h`, `src/platform/board.{c,h}`, `src/display/st7796.{c,h}`, `src/display/font5x7.c`, `third_party/segger_rtt/*`, `tools/build.ps1`, `tools/flash.ps1`, `tools/rtt.ps1`, `src/main.c`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `board_init()`, `board_backlight_set(uint8_t)`, `st7796_init()`, `st7796_fill_screen(uint16_t color_be)`, `st7796_fill_rect(int,int,int,int,uint16_t)`, `st7796_draw_text(int,int,int,uint16_t,uint16_t,const char*)`, `DIAG(...)` — all consumed by later tasks.

- [ ] **Step 1: Vendor the unchanged support files**

Run (Git Bash), copying from the reference (`$REF`):
```bash
REF="/c/~prj/Dropbox/vibeProjects/movieplayer"
DST="/c/~prj/Dropbox/vibeProjects/externalmicvalid"
mkdir -p "$DST"/{src/boards,src/platform,src/display,src/audio,third_party/segger_rtt,tools,tests/host}
cp "$REF/pico_sdk_import.cmake" "$DST/"
cp "$REF/src/boards/freewili2.h" "$DST/src/boards/"
cp "$REF/src/platform/diag.h" "$DST/src/platform/"
cp "$REF/src/platform/board.c" "$REF/src/platform/board.h" "$DST/src/platform/"
cp "$REF/src/display/st7796.c" "$REF/src/display/st7796.h" "$REF/src/display/font5x7.c" "$DST/src/display/"
cp "$REF/third_party/segger_rtt/SEGGER_RTT.c" "$REF/third_party/segger_rtt/SEGGER_RTT_printf.c" "$DST/third_party/segger_rtt/"
cp "$REF/third_party/segger_rtt/SEGGER_RTT.h" "$REF/third_party/segger_rtt/SEGGER_RTT_Conf.h" "$DST/third_party/segger_rtt/"
cp "$REF/tools/build.ps1" "$REF/tools/flash.ps1" "$REF/tools/rtt.ps1" "$DST/tools/"
```
Verify `st7796.c` includes only `board.h`, `pico/stdlib.h`, `hardware/spi.h`, `hardware/gpio.h`, `hardware/dma.h` (no other src deps). If it includes more, vendor those too.

- [ ] **Step 2: Adapt the build script for this project**

Edit `tools/build.ps1`: change the default `-BuildDir` to `C:/buildfiles/externalmicvalid`, remove the `-SourceFlash` and `-Spike` switches and their `-D...` cmake lines, and remove the two `-DMOVIE_*` flags. Keep the toolchain-path block, `PICO_BOARD=pico2`, `PICO_PLATFORM=rp2350`, and the final `Write-Host "Build OK -> $BuildDir/externalmicvalid.uf2"`. Edit `tools/flash.ps1`: change default `-Elf` to `C:/buildfiles/externalmicvalid/externalmicvalid.elf`.

- [ ] **Step 3: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(externalmicvalid C CXX ASM)
pico_sdk_init()

add_executable(externalmicvalid
    src/main.c
    src/platform/board.c
    src/display/st7796.c
    src/display/font5x7.c
    third_party/segger_rtt/SEGGER_RTT.c
    third_party/segger_rtt/SEGGER_RTT_printf.c
)

target_include_directories(externalmicvalid PRIVATE
    src third_party/segger_rtt)

# 4KB RTT up-buffer so boot DIAG survives until tools/rtt.ps1 attaches.
target_compile_definitions(externalmicvalid PRIVATE BUFFER_SIZE_UP=4096 PICO_BUILD=1)

target_link_libraries(externalmicvalid PRIVATE
    pico_stdlib
    hardware_clocks
    hardware_gpio
    hardware_spi
    hardware_dma
    hardware_pwm
    hardware_i2c
    hardware_pio
)

# USB is host-mode; no UART debug. All diagnostics over SEGGER RTT.
pico_enable_stdio_usb(externalmicvalid 0)
pico_enable_stdio_uart(externalmicvalid 0)

pico_add_extra_outputs(externalmicvalid)
```

- [ ] **Step 4: Write the scaffold `src/main.c`**

```c
// src/main.c — external-microphone validation firmware (FreeWili 2 / RP2350B).
#include "pico/stdlib.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "display/st7796.h"

// Big-endian RGB565 (the panel sends the high byte first).
static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

int main(void) {
    board_init();
    DIAG("\n=== externalmicvalid: boot ===\n");

    st7796_init();
    st7796_fill_screen(rgb565_be(0, 0, 40));      // dark blue test fill
    st7796_draw_text(8, 8, 2, rgb565_be(255,255,255), rgb565_be(0,0,40),
                     "EXT MIC VALIDATION");
    board_backlight_set(1);
    DIAG("scaffold: display up, backlight on\n");

    while (true) {
        tight_loop_contents();
    }
}
```

- [ ] **Step 5: Build**

Run: `powershell -File tools/build.ps1 -Clean`
Expected: `Build OK -> C:/buildfiles/externalmicvalid/externalmicvalid.uf2` (no errors).

- [ ] **Step 6: Flash and observe**

Run: `powershell -File tools/flash.ps1`
Expected: `Flashed + reset via OpenOCD`. The LCD shows a dark-blue screen with white "EXT MIC VALIDATION" text and the backlight on.

- [ ] **Step 7: Confirm RTT banner**

Run: `powershell -File tools/rtt.ps1 -Seconds 5`
Expected output contains:
```
=== externalmicvalid: boot ===
scaffold: display up, backlight on
```

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "Task 1: project scaffold — board + ST7796 LCD + RTT boot banner"
```

---

## Task 2: VU meter pure logic (host-tested)

Pure DSP/geometry with no hardware deps, so it gets a real host TDD cycle.

**Files:**
- Create: `src/audio/vu_meter.h`, `src/audio/vu_meter.c`, `tests/host/vu_meter_harness.c`, `tests/test_vu_meter.py`

**Interfaces:**
- Consumes: nothing.
- Produces (used by Tasks 5 & 6):
  - `int16_t vu_sample(uint32_t frame, int slot);` — extract signed 16-bit sample. `slot 0` = left = high 16 bits; `slot 1` = right = low 16 bits.
  - `uint16_t vu_peak(const uint32_t *frames, uint32_t n, int slot);` — max `abs(sample)` over `n` frames, clamped to `0..32767`.
  - `int vu_bar_px(uint16_t peak, int max_px);` — log (dB) mapping of peak to a bar length `0..max_px`. Floor at `VU_DB_FLOOR` dB.
  - `uint16_t vu_color_be(uint16_t peak);` — big-endian RGB565: green below `VU_YELLOW_PEAK`, yellow below `VU_RED_PEAK`, red at/above.
  - Constants: `VU_DB_FLOOR` (-48), `VU_YELLOW_PEAK` (8192), `VU_RED_PEAK` (24576).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_vu_meter.py`:
```python
# tests/test_vu_meter.py — pure VU logic, compiled and run on host.
import pathlib, shutil, subprocess, pytest
ROOT = pathlib.Path(__file__).parent.parent
GCC = shutil.which("gcc")
pytestmark = pytest.mark.skipif(GCC is None, reason="no host gcc on PATH")

@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    exe = tmp_path_factory.mktemp("vu_meter") / "vu_meter.exe"
    subprocess.run([GCC, "-Wall", "-Werror", "-I", str(ROOT / "src"),
                    "-o", str(exe),
                    str(ROOT / "tests" / "host" / "vu_meter_harness.c"),
                    str(ROOT / "src" / "audio" / "vu_meter.c"),
                    "-lm"], check=True)
    return exe

def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True,
                          text=True, check=True).stdout.split()

def test_sample_left_is_high_word(harness):
    # frame 0x1234FFFF -> left = 0x1234 (4660), right = 0xFFFF (-1)
    assert run(harness, "sample", "0x1234FFFF", "0") == ["4660"]
    assert run(harness, "sample", "0x1234FFFF", "1") == ["-1"]

def test_sample_sign_extends(harness):
    # left = 0x8000 = -32768
    assert run(harness, "sample", "0x80000000", "0") == ["-32768"]

def test_peak_finds_max_abs_in_slot(harness):
    # two frames, left words 0x0064 (100) and 0xFF9C (-100); peak left = 100
    assert run(harness, "peak", "0", "0x00640000", "0xFF9C0000") == ["100"]

def test_peak_clamps_32768_to_32767(harness):
    # left = 0x8000 = -32768 -> abs clamps to 32767
    assert run(harness, "peak", "0", "0x80000000") == ["32767"]

def test_bar_floor_is_zero(harness):
    assert run(harness, "bar", "0", "100") == ["0"]

def test_bar_full_scale_is_max(harness):
    assert run(harness, "bar", "32767", "100") == ["100"]

def test_bar_monotonic_midscale(harness):
    lo = int(run(harness, "bar", "1000", "100")[0])
    hi = int(run(harness, "bar", "10000", "100")[0])
    assert 0 < lo < hi < 100

def test_color_thresholds(harness):
    # green / yellow / red as big-endian RGB565 hex
    assert run(harness, "color", "1000") == ["e007"]   # green 0x07E0 -> BE 0xE007
    assert run(harness, "color", "12000") == ["e0ff"]  # yellow 0xFFE0 -> BE 0xE0FF
    assert run(harness, "color", "30000") == ["00f8"]  # red 0xF800 -> BE 0x00F8
```

- [ ] **Step 2: Write the harness (no implementation yet)**

Create `tests/host/vu_meter_harness.c`:
```c
// tests/host/vu_meter_harness.c — drive vu_meter.c from argv; print tokens.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio/vu_meter.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *cmd = argv[1];

    if (!strcmp(cmd, "sample")) {
        uint32_t frame = (uint32_t)strtoul(argv[2], NULL, 0);
        int slot = atoi(argv[3]);
        printf("%d\n", (int)vu_sample(frame, slot));
        return 0;
    }
    if (!strcmp(cmd, "peak")) {
        int slot = atoi(argv[2]);
        uint32_t frames[64]; uint32_t n = 0;
        for (int i = 3; i < argc && n < 64; i++)
            frames[n++] = (uint32_t)strtoul(argv[i], NULL, 0);
        printf("%u\n", (unsigned)vu_peak(frames, n, slot));
        return 0;
    }
    if (!strcmp(cmd, "bar")) {
        uint16_t peak = (uint16_t)atoi(argv[2]);
        int max_px = atoi(argv[3]);
        printf("%d\n", vu_bar_px(peak, max_px));
        return 0;
    }
    if (!strcmp(cmd, "color")) {
        uint16_t peak = (uint16_t)atoi(argv[2]);
        printf("%04x\n", (unsigned)vu_color_be(peak));
        return 0;
    }
    return 1;
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `python -m pytest tests/test_vu_meter.py -v`
Expected: FAIL — compilation error, `audio/vu_meter.h` not found.

- [ ] **Step 4: Write the header**

Create `src/audio/vu_meter.h`:
```c
// src/audio/vu_meter.h — pure VU-meter math (no hardware deps; host-testable).
#ifndef VU_METER_H
#define VU_METER_H
#include <stdint.h>

// dB floor of the bar scale; peaks at/below this map to length 0.
#define VU_DB_FLOOR   (-48)
// Peak thresholds for the bar color (linear 0..32767).
#define VU_YELLOW_PEAK 8192
#define VU_RED_PEAK    24576

// Extract a signed 16-bit sample from one 32-bit I2S frame.
// slot 0 = left = high 16 bits; slot 1 = right = low 16 bits.
int16_t vu_sample(uint32_t frame, int slot);

// Max abs(sample) over n frames for `slot`, clamped to 0..32767.
uint16_t vu_peak(const uint32_t *frames, uint32_t n, int slot);

// Map peak (0..32767) to a bar length 0..max_px on a dB scale (floor VU_DB_FLOOR).
int vu_bar_px(uint16_t peak, int max_px);

// Big-endian RGB565 bar color for a peak: green/yellow/red by threshold.
uint16_t vu_color_be(uint16_t peak);

#endif // VU_METER_H
```

- [ ] **Step 5: Write the implementation**

Create `src/audio/vu_meter.c`:
```c
// src/audio/vu_meter.c — pure VU-meter math. No hardware, no I/O.
#include "audio/vu_meter.h"
#include <math.h>

int16_t vu_sample(uint32_t frame, int slot) {
    uint16_t u = (slot == 0) ? (uint16_t)(frame >> 16) : (uint16_t)(frame & 0xFFFF);
    return (int16_t)u;   // two's-complement reinterpret
}

uint16_t vu_peak(const uint32_t *frames, uint32_t n, int slot) {
    int32_t peak = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t s = vu_sample(frames[i], slot);
        if (s < 0) s = -s;
        if (s > peak) peak = s;
    }
    if (peak > 32767) peak = 32767;   // -32768 -> 32767
    return (uint16_t)peak;
}

int vu_bar_px(uint16_t peak, int max_px) {
    if (peak == 0) return 0;
    float db = 20.0f * log10f((float)peak / 32767.0f);
    if (db <= (float)VU_DB_FLOOR) return 0;
    if (db >= 0.0f) return max_px;
    float frac = (db - (float)VU_DB_FLOOR) / (0.0f - (float)VU_DB_FLOOR);
    int px = (int)(frac * (float)max_px + 0.5f);
    if (px < 0) px = 0;
    if (px > max_px) px = max_px;
    return px;
}

static uint16_t be(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

uint16_t vu_color_be(uint16_t peak) {
    if (peak < VU_YELLOW_PEAK) return be(0x07E0);   // green
    if (peak < VU_RED_PEAK)    return be(0xFFE0);   // yellow
    return be(0xF800);                              // red
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `python -m pytest tests/test_vu_meter.py -v`
Expected: PASS (all 9 tests). If `gcc` is not on PATH the file is skipped — install MinGW `gcc` or run in an environment that has it; do not mark the task done on a skip.

- [ ] **Step 7: Confirm `vu_meter.c` compiles under the firmware toolchain too**

Add `src/audio/vu_meter.c` to the `add_executable(externalmicvalid ...)` list in `CMakeLists.txt`, then run `powershell -File tools/build.ps1`.
Expected: `Build OK` (catches any arm-gcc-only issues early; the file is wired in now for Tasks 5/6).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "Task 2: VU meter pure logic (sample/peak/bar/color) with host tests"
```

---

## Task 3: Codec ADC input-path verify at boot

The vendored init already powers the ADC/PGA/boost; this task vendors the driver and adds a boot check that proves the part answers and the input bits are set.

**Files:**
- Create: `src/audio/codec_nau88c10.c`, `src/audio/codec_nau88c10.h` (vendored from `$REF`)
- Modify: `src/audio/codec_nau88c10.h` (add one prototype), `src/audio/codec_nau88c10.c` (add one function), `CMakeLists.txt`, `src/main.c`

**Interfaces:**
- Consumes: `DIAG(...)`, I2C1 pins from `board.h`.
- Produces (used by later tasks): `void codec_nau88c10_init(void);`, `bool codec_nau88c10_input_ok(void);` — returns true iff reg `0x3F` (silicon revision) is nonzero AND reg `0x02` reads `0x0015` (ADC+PGA+boost enabled).

- [ ] **Step 1: Vendor the codec driver**

```bash
REF="/c/~prj/Dropbox/vibeProjects/movieplayer"
DST="/c/~prj/Dropbox/vibeProjects/externalmicvalid"
cp "$REF/src/audio/codec_nau88c10.c" "$REF/src/audio/codec_nau88c10.h" "$DST/src/audio/"
```

- [ ] **Step 2: Add the input-verify prototype to the header**

In `src/audio/codec_nau88c10.h`, add before `#endif`:
```c
// Boot-time bring-up check for the MIC/ADC path: returns true iff the part
// answers (reg 0x3F silicon revision != 0) AND reg 0x02 == 0x0015 (ADCEN+PGAEN+
// BSTEN). DIAGs each value. Call after codec_nau88c10_init().
bool codec_nau88c10_input_ok(void);
```

- [ ] **Step 3: Implement the verify helper**

In `src/audio/codec_nau88c10.c`, add a file-static reader and the function (the existing `codec_nau88c10_dump()` shows the read protocol: address byte `reg<<1`, then read 2 bytes, value = `((val[0]&1)<<8)|val[1]`):
```c
static uint16_t codec_read(uint8_t reg) {
    uint8_t addr_byte = (uint8_t)(reg << 1);
    uint8_t val[2] = { 0, 0 };
    if (i2c_write_blocking(i2c1, CODEC_ADDR, &addr_byte, 1, true) != 1) return 0xFFFF;
    if (i2c_read_blocking(i2c1, CODEC_ADDR, val, 2, false) != 2) return 0xFFFF;
    return (uint16_t)(((val[0] & 0x01) << 8) | val[1]);
}

bool codec_nau88c10_input_ok(void) {
    uint16_t rev = codec_read(0x3F);
    uint16_t pm2 = codec_read(0x02);
    DIAG("codec: rev(0x3F)=0x%03x pm2(0x02)=0x%03x\n", (unsigned)rev, (unsigned)pm2);
    bool ok = (rev != 0 && rev != 0xFFFF) && (pm2 == 0x0015);
    if (!ok) DIAG("codec: INPUT PATH NOT READY (expect rev!=0, pm2=0x015)\n");
    return ok;
}
```

- [ ] **Step 4: Wire the codec into the build and boot**

Add `src/audio/codec_nau88c10.c` to `add_executable(...)` in `CMakeLists.txt`. In `src/main.c`, add `#include "audio/codec_nau88c10.h"` and, after the display block, before the `while`:
```c
    codec_nau88c10_init();
    if (codec_nau88c10_input_ok())
        DIAG("codec: input path ready\n");
    st7796_draw_text(8, 40, 2, rgb565_be(0,255,0), rgb565_be(0,0,40),
                     "CODEC OK");
```

- [ ] **Step 5: Build, flash, observe**

Run: `powershell -File tools/build.ps1` then `powershell -File tools/flash.ps1`
Then: `powershell -File tools/rtt.ps1 -Seconds 5`
Expected RTT contains:
```
codec: nau88c10 init done (16 kHz, speaker)
codec: rev(0x3F)=0x0... pm2(0x02)=0x015
codec: input path ready
```
`rev` must be nonzero; `pm2` must be `0x015`. The LCD shows "CODEC OK" in green. If `pm2 != 0x015`, the vendored init regressed — re-check `codec_write(0x02, 0x0015)`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Task 3: vendor NAU88C10 driver + boot-time ADC input-path verify"
```

---

## Task 4: I2S RX driver — prove raw ADC data flows on GPIO 4

Bring up the receive PIO and prove the codec clocks live ADC data in, by polling the RX FIFO directly (no DMA yet).

**Files:**
- Create: `src/audio/i2s_rx.pio`, `src/audio/audio_i2s_rx.c`, `src/audio/audio_i2s_rx.h`
- Modify: `CMakeLists.txt`, `src/platform/board.h` (add the RX data pin), `src/main.c`

**Interfaces:**
- Consumes: audio pins from `board.h`, `DIAG(...)`.
- Produces (used by Task 5):
  - `void audio_i2s_rx_init(uint32_t sample_rate);` — start MCLK PWM, load+start the RX SM on PIO0 SM0.
  - `volatile const void *audio_i2s_rx_rxf(void);` — RX FIFO read address (DMA source).
  - `uint audio_i2s_rx_dreq(void);` — RX DREQ for DMA pacing.
  - `uint32_t audio_i2s_rx_get_blocking(void);` — pop one 32-bit frame (test/bring-up use).

- [ ] **Step 1: Add the RX data pin to `board.h`**

In `src/platform/board.h`, in the Audio block, add:
```c
#define PIN_AUDIO_DIN  4    // SPK_DOUT: codec ADC data into the MCU (PIO in)
```

- [ ] **Step 2: Write the RX PIO program**

Create `src/audio/i2s_rx.pio`:
```
; src/audio/i2s_rx.pio — I2S receiver, MCU supplies all clocks (codec = slave).
; 16 bits/channel, MSB first, 32 bits/frame. side-set: bit0 = LRCK, bit1 = BCLK.
; 2 PIO cycles per bit -> PIO clock = 64*fs (BCLK = 32*fs). Sample DIN on the
; BCLK-high half (rising edge). Autopush at 32 -> one 32-bit frame = [L16 | R16].
;
; ALIGNMENT KNOB (the known bring-up risk): if captured peaks stay at the floor
; while the mic is driven, the sample point is off by one BCLK / the LRCK phase
; is inverted. Fixes, tried in order: (1) flip MIC_I2S_SLOT in vu_capture.c;
; (2) move the `in pins,1` from the BCLK-high to the BCLK-low instruction in
; each pair; (3) swap which side value (0b00 vs 0b01) starts the left channel.

.program i2s_rx
.side_set 2

.wrap_target
    set x, 14            side 0b00   ; LEFT: LRCK=0, BCLK=0, 16 bits total
left_bit:
    in pins, 1           side 0b10   ; BCLK high: sample DIN (rising edge)
    jmp x-- left_bit     side 0b00   ; BCLK low: advance
    in pins, 1           side 0b10   ; 16th left bit
    set x, 14            side 0b01   ; RIGHT: LRCK=1, BCLK=0
right_bit:
    in pins, 1           side 0b11   ; BCLK high, LRCK high: sample
    jmp x-- right_bit    side 0b01   ; BCLK low
    in pins, 1           side 0b11   ; 16th right bit
.wrap
```

- [ ] **Step 3: Assemble the PIO via CMake**

In `CMakeLists.txt`, after `add_executable(...)`, add:
```cmake
pico_generate_pio_header(externalmicvalid
    ${CMAKE_CURRENT_LIST_DIR}/src/audio/i2s_rx.pio)
```
and add `src/audio/audio_i2s_rx.c` to the `add_executable(...)` source list.

- [ ] **Step 4: Write the RX driver header**

Create `src/audio/audio_i2s_rx.h`:
```c
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
```

- [ ] **Step 5: Write the RX driver implementation**

Create `src/audio/audio_i2s_rx.c`:
```c
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
```

- [ ] **Step 6: Add a bring-up probe to `main.c`**

In `src/main.c`, add `#include "audio/audio_i2s_rx.h"` and, after the codec block, before the `while`:
```c
    audio_i2s_rx_init(16000);
    DIAG("i2s_rx: started, sampling raw frames...\n");
    for (int i = 0; i < 8; i++) {
        uint32_t f = audio_i2s_rx_get_blocking();
        DIAG("i2s_rx: frame[%d] = 0x%08x (L=0x%04x R=0x%04x)\n",
             i, (unsigned)f, (unsigned)(f >> 16), (unsigned)(f & 0xFFFF));
    }
```

- [ ] **Step 7: Build, flash, observe (tap the mic during capture)**

Run: `powershell -File tools/build.ps1` then `powershell -File tools/flash.ps1`
Then run `powershell -File tools/rtt.ps1 -Seconds 8` and, while it captures, **tap or speak into the 3.5mm mic**.
Expected: 8 `i2s_rx: frame[..]` lines print. The L (or R) word must be **non-static** — values change when you tap the mic, near `0x0000`/`0xFFFF` (small +/-) at rest. If every frame is identical (all `0x00000000` or all `0xFFFFFFFF`), apply the ALIGNMENT KNOB notes in `i2s_rx.pio` (Step 2): the clocks may be running but the sample point/slot is wrong, or DIN isn't routed.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "Task 4: I2S RX PIO driver — raw codec ADC frames captured on GPIO4"
```

---

## Task 5: Continuous capture via ping-pong DMA + per-block peak over RTT

Replace blocking polls with a free-running DMA and expose a per-block peak — the **secondary success signal** (independent of the LCD).

**Files:**
- Create: `src/audio/vu_capture.c`, `src/audio/vu_capture.h`
- Modify: `CMakeLists.txt`, `src/main.c`

**Interfaces:**
- Consumes: `audio_i2s_rx_rxf()`, `audio_i2s_rx_dreq()` (Task 4); `vu_peak()` (Task 2).
- Produces (used by Task 6):
  - `void vu_capture_start(void);` — claim 2 DMA channels, start the ping-pong, install `DMA_IRQ_0`.
  - `bool vu_block_ready(void);` — true when a fresh block has completed.
  - `uint16_t vu_block_peak(void);` — peak (0..32767) of `MIC_I2S_SLOT` over the most recent completed block; clears the ready flag.
  - Block size constant `VU_BLOCK_FRAMES` (256) and `MIC_I2S_SLOT` (default 0 = left).

- [ ] **Step 1: Write the capture header**

Create `src/audio/vu_capture.h`:
```c
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
```

- [ ] **Step 2: Write the capture implementation**

Create `src/audio/vu_capture.c`:
```c
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
```

- [ ] **Step 3: Wire into the build and replace the Task-4 probe in `main.c`**

Add `src/audio/vu_capture.c` to `add_executable(...)`. In `src/main.c`: add `#include "audio/vu_capture.h"`, **delete** the Task-4 `for (int i = 0; i < 8; ...)` blocking-probe loop, and replace it with:
```c
    vu_capture_start();
    DIAG("vu_capture: streaming; tap the mic...\n");
    uint32_t blk = 0;
    while (true) {
        if (vu_block_ready()) {
            uint16_t pk = vu_block_peak();
            if ((blk++ & 0x1F) == 0)              // ~twice a second at 16ms blocks
                DIAG("vu: blk=%u peak=%u\n", (unsigned)blk, (unsigned)pk);
        }
        tight_loop_contents();
    }
```
Keep the existing `while (true) { tight_loop_contents(); }` removed (this replaces it).

- [ ] **Step 4: Build, flash, observe (tap the mic)**

Run: `powershell -File tools/build.ps1` then `powershell -File tools/flash.ps1`
Then `powershell -File tools/rtt.ps1 -Seconds 10`, tapping/speaking into the mic.
Expected: periodic `vu: blk=.. peak=..` lines. `peak` is small (< ~200) at rest and **rises clearly** (thousands) when you tap/speak. If it never rises, flip `MIC_I2S_SLOT` to 1 (rebuild/flash); if still flat, apply the `i2s_rx.pio` alignment knob.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Task 5: ping-pong DMA capture + per-block peak over RTT"
```

---

## Task 6: VU bar on the LCD — primary success signal

Render the live VU bar so the mic can be validated on-device with no host tools.

**Files:**
- Modify: `src/main.c`

**Interfaces:**
- Consumes: `vu_block_ready()`, `vu_block_peak()` (Task 5); `vu_bar_px()`, `vu_color_be()` (Task 2); `st7796_fill_rect`, `st7796_draw_text` (Task 1).
- Produces: final firmware behavior (no new exports).

- [ ] **Step 1: Add the VU bar geometry + render to `main.c`**

In `src/main.c`, above `main()`, add the bar layout and a render helper:
```c
// VU bar geometry on the 480x320 panel.
#define VU_X      20
#define VU_Y      160
#define VU_W      440
#define VU_H      60

static void vu_draw(uint16_t peak) {
    int px = vu_bar_px(peak, VU_W);
    uint16_t fg = vu_color_be(peak);
    uint16_t bg = rgb565_be(0, 0, 40);
    if (px > 0) st7796_fill_rect(VU_X, VU_Y, px, VU_H, fg);          // filled
    if (px < VU_W) st7796_fill_rect(VU_X + px, VU_Y, VU_W - px, VU_H, bg); // tail
}
```

- [ ] **Step 2: Draw a static frame once, then drive the bar in the loop**

In `src/main.c`, after `vu_capture_start();` and its DIAG, replace the Task-5 `while` body so it also draws. The loop becomes:
```c
    st7796_draw_text(VU_X, VU_Y - 24, 2, rgb565_be(255,255,255),
                     rgb565_be(0,0,40), "MIC LEVEL");
    vu_draw(0);   // empty bar baseline

    uint32_t blk = 0;
    while (true) {
        if (vu_block_ready()) {
            uint16_t pk = vu_block_peak();
            vu_draw(pk);
            if ((blk++ & 0x1F) == 0)
                DIAG("vu: blk=%u peak=%u\n", (unsigned)blk, (unsigned)pk);
        }
        tight_loop_contents();
    }
```

- [ ] **Step 3: Build and flash**

Run: `powershell -File tools/build.ps1` then `powershell -File tools/flash.ps1`
Expected: `Build OK` then `Flashed + reset`.

- [ ] **Step 4: On-device acceptance test (primary success criterion)**

With the firmware running and a mic plugged into the 3.5mm jack:
- At rest the bar sits at/near the floor (empty/green).
- Speaking or tapping the mic makes the bar **grow and track** the level, turning yellow then red on loud input.
- Optionally confirm `vu: blk=.. peak=..` over `tools/rtt.ps1` agrees with the bar.

If the bar never moves: flip `MIC_I2S_SLOT` (Task 5) and rebuild; then apply the `i2s_rx.pio` alignment knob (Task 4). These are the only expected bring-up failure modes and are isolated to those two files.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Task 6: live VU bar on ST7796 LCD — external mic validated on-device"
```

---

## Self-Review Notes

- **Spec coverage:** I2S RX on GPIO 4 (Tasks 4), codec ADC verify (Task 3), ping-pong DMA + peak (Task 5), VU bar on LCD (Tasks 1+6), RTT secondary signal (Task 5), error/heartbeat + channel-slot + sample-alignment knobs (Tasks 4-6), 252 MHz/MCLK-lock constraints (Global Constraints + Task 4). Standalone project structure (Task 1). All spec sections map to a task.
- **No placeholders:** every code step has complete code; on-device steps state exact expected RTT/LCD output.
- **Type consistency:** `vu_sample/vu_peak/vu_bar_px/vu_color_be` signatures match between `vu_meter.h` (Task 2), the harness (Task 2), and consumers (`vu_capture.c` Task 5, `main.c` Task 6). `audio_i2s_rx_rxf/dreq` signatures match between Task 4 and Task 5. `VU_BLOCK_FRAMES`, `MIC_I2S_SLOT`, `VU_W` used consistently.
- **Known risk isolated:** I2S RX bit/slot alignment is confined to `i2s_rx.pio` + `MIC_I2S_SLOT`, with a written tuning procedure surfaced at every on-device step.
