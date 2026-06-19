# Bench Validation Checklist — External Microphone (FreeWili 2)

The firmware is **code-complete and build-verified** (every task gated on a clean
`tools/build.ps1`; the pure VU logic also passes 8/8 host tests). No physical board
was attached during development, so all **on-device acceptance steps were deferred
here**. Run these at the bench with a FreeWili 2 connected via the CMSIS-DAP debug
probe and a microphone plugged into the 3.5 mm jack.

## Setup
1. Build (if not already): `powershell -File tools/build.ps1`
2. Flash + reset: `powershell -File tools/flash.ps1`
3. Open the RTT console in a second terminal: `powershell -File tools/rtt.ps1`

## Acceptance steps (in order)

### 1. Scaffold (Task 1)
- [ ] LCD shows a dark-blue screen with white **"EXT MIC VALIDATION"**, backlight on.
- [ ] RTT prints:
  ```
  === externalmicvalid: boot ===
  scaffold: display up, backlight on
  ```

### 2. Codec ADC path (Task 3)
- [ ] RTT prints `codec: nau88c10 init done (16 kHz, speaker)`.
- [ ] RTT prints `codec: rev(0x3F)=0x0...  pm2(0x02)=0x015` — **rev must be nonzero**, **pm2 must be 0x015**.
- [ ] RTT prints `codec: input path ready` and the LCD shows green **"CODEC OK"**.
  - If instead you see red **"CODEC FAIL"** / `INPUT PATH NOT READY`: the codec isn't answering or `R2 != 0x0015`. Check I2C1 wiring (GPIO 26/27) and that init wasn't altered.

### 3. Live capture (Tasks 4–5)
- [ ] RTT prints `vu_capture: streaming; tap the mic...` then periodic `vu: blk=.. peak=..` lines (~twice/sec).
- [ ] **Tap or speak into the mic** → `peak` jumps from small (<~200 at rest) to thousands.
- [ ] If RTT shows `vu: no audio blocks for >1s (RX/clock dead?)` → the RX/clock path is dead (no DMA blocks). Check MCLK on GPIO 22, BCLK/LRCK on GPIO 7/6, and that the codec is clocked.

### 4. VU bar — primary success (Task 6)
- [ ] LCD shows **"MIC LEVEL"** label with a bar below it.
- [ ] At rest the bar sits at/near the floor; speaking/tapping makes it **grow and track** the level, green → yellow → red on loud input.

## If the bar / peak stays flat while the mic is driven (the one known risk)
The RX bit/slot alignment is isolated to two knobs — apply **in this order**, rebuild + reflash after each:
1. **Wrong channel slot:** flip `MIC_I2S_SLOT` (0↔1) in `src/audio/vu_capture.h`.
2. **Sample point off by one BCLK / inverted LRCK phase:** apply the alignment-knob notes documented at the top of `src/audio/i2s_rx.pio` (move the `in pins,1` from the BCLK-high to BCLK-low instruction, or swap the starting `side` value).
3. **Input path not actually enabled:** re-confirm `R2 (0x02)=0x0015` and `R47 (0x2F)=0x0100` via `codec_nau88c10_dump()`.

These are the only expected bring-up failure modes; everything else (interfaces,
init order, DMA/PIO wiring, frame layout) was statically verified in review.
