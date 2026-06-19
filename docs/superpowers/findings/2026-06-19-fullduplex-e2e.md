# Full-Duplex Speaker + Mic — On-Device E2E Findings (2026-06-19)

Validated on the FreeWili 2 (RP2350B) bench unit, debug probe + eMeet SmartCam C960 4K.
Firmware: `feat/fullduplex-speaker-mic`. fs = 16 kHz, 1 kHz sine out the speaker while
the 3.5 mm external mic captures, both on one I2S bus (PIO0 SM0).

## Result: PASS — full-duplex confirmed, speaker emits the commanded tone

The board plays a tone out its (blown) speaker **and** captures the external mic
simultaneously. All four evidence channels agree.

### 1. Duplex RX is intact (mic capture runs while the speaker plays)

RTT over a full SILENCE→TONE→SILENCE cycle (`tools/rtt.ps1 -Seconds 16`):
- `codec: nau88c10 init done (16 kHz, speaker)`, `CODEC OK` (`rev!=0`, `pm2=0x015`).
- `state=TONE` / `state=SILENCE` alternate on the 3 s/6 s schedule.
- `vu:` peak lines stream continuously with **no `no audio blocks` starvation** — the
  duplex merge did not break the proven RX path. (This is the key regression gate.)
- On-device mic peaks stay near the noise floor (tens out of 32767) in **both** states:
  the 3.5 mm mic does not acoustically hear the onboard speaker at bench distance
  (expected — see the original mic-validation bench checklist). The eMeet mic below is
  the authoritative acoustic proof.

### 2. The speaker emits a cycle-correlated narrowband tone (eMeet mic)

`tools/screencap/analyze_tone.py` on a 16 s eMeet-mic recording:
- total-energy loud/quiet = **3.1×** (the speaker clearly radiates during TONE windows)
- dominant emitted peak = 5000 Hz, **crest = 2630× median** (sharp narrowband tone, not
  broadband room noise), peak loud/quiet = 3.7× → **PASS**.

### 3. The fundamental tracks the commanded frequency (fs is correct, 16 kHz)

Controlled experiment — rebuilt with a 2 kHz buffer, re-measured the harmonic ladder
(`diag_spectrum.py`), loud (TONE) vs quiet (SILENCE) windows:

| Build (buffer) | Fundamental peak (loud/quiet) | Dominant radiated peak |
|----------------|-------------------------------|------------------------|
| 1 kHz          | 1000 Hz (4.2×)                | 5000 Hz (5th harmonic) |
| 2 kHz          | 2000 Hz (41.4×)               | 6000 Hz (3rd harmonic) |

The fundamental moved 1 k→2 k exactly with the command, ruling out a clock/divider bug
(a 5× fs error would have put the 1 kHz buffer's fundamental at 5 kHz and moved it to
10 kHz under the 2 kHz buffer — it did not). The huge 5–6 kHz energy is the **blown
speaker's odd-harmonic distortion exciting a ~5–6 kHz mechanical resonance**: with a
1 kHz tone the 5th harmonic lands in that band; with 2 kHz the 3rd harmonic does. This
matches the reported "lots of distortion because it's blown."

### 4. The firmware runs on the real panel (eMeet camera)

Un-mirrored (`hflip`) contrast-boosted stills:
- `captures/evidence_TONE_ON.png` — green **"TONE ON 1kHz"** banner + green VU bar.
- `captures/evidence_TONE_OFF.png` — red **"TONE OFF"** banner during the silence window.

## Notes / non-issues

- The on-device VU does not rise during TONE: the 3.5 mm mic isn't positioned to hear
  the speaker. Not a defect; the simultaneous-capture path is proven healthy by the
  continuous RTT `vu:` stream and the original mic-validation work.
- `analyze_tone.py` reports the *dominant* radiated peak (5 kHz resonance), not the
  fundamental — the fundamental-tracking proof is the 1 k/2 k experiment above.
- PNG/WAV evidence is gitignored; regenerate with `tools/screencap/capture_e2e.ps1`.
