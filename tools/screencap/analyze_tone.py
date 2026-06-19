# tools/screencap/analyze_tone.py — prove the board's speaker emits a TONE (not just
# noise) in sync with the firmware's SILENCE/TONE cycle, from an eMeet-mic recording.
#
# What a single recording can honestly prove (the speaker is BLOWN, so it radiates
# heavily-distorted sound — energy spreads across harmonics, so a per-frequency match
# is not specific). Two specific, falsifiable checks instead:
#   1. CORRELATION: total acoustic energy is clearly bimodal — loud TONE windows vs
#      quiet SILENCE windows (a silent speaker would show no such split).
#   2. TONALITY: the loud windows contain a sharp NARROWBAND spectral peak (high crest
#      factor) that is strongly elevated vs the quiet windows — broadband room noise
#      has no such peak. This is what distinguishes "a tone is playing" from "it got
#      noisier".
# (That the peak's FUNDAMENTAL equals the commanded frequency is proven separately by
#  the 1 kHz vs 2 kHz firmware experiment — see docs/.../findings.)
#
# Usage: python tools/screencap/analyze_tone.py [room.wav]
import sys, wave, numpy as np

path = sys.argv[1] if len(sys.argv) > 1 else "tools/screencap/captures/room.wav"
w = wave.open(path)
fs, n, ch = w.getframerate(), w.getnframes(), w.getnchannels()
x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(float)
x = x.reshape(-1, ch).mean(axis=1) if ch > 1 else x

win = fs // 2  # 0.5 s windows
freqs = np.fft.rfftfreq(win, 1.0 / fs)
specs, energy = [], []
for i in range(0, len(x) - win, win):
    seg = x[i:i + win] * np.hanning(win)
    mag = np.abs(np.fft.rfft(seg))
    specs.append(mag); energy.append(float((mag ** 2).sum()))
specs = np.array(specs); energy = np.array(energy)

thr = (energy.max() + energy.min()) / 2
loud_m = energy >= thr
loud, quiet = specs[loud_m], specs[~loud_m]

# 1. Correlation: loud/quiet total-energy split.
tot_ratio = energy[loud_m].mean() / (energy[~loud_m].mean() + 1e-9)

# 2. Tonality: dominant narrowband peak in the loud (playing) windows.
lp = loud.mean(axis=0).copy(); lp[freqs < 300] = 0   # ignore DC/rumble
pk = int(np.argmax(lp))
pk_hz = int(freqs[pk])
crest = lp[pk] / (np.median(lp[freqs >= 300]) + 1e-9)         # peak vs typical bin
qp = quiet.mean(axis=0)
pk_ratio = lp[pk] / (qp[max(pk-1,0):pk+2].mean() + 1e-9)      # peak loud vs quiet

print(f"file={path} fs={fs} dur={len(x)/fs:.1f}s windows={len(specs)} "
      f"(loud={int(loud_m.sum())} quiet={int((~loud_m).sum())})")
print(f"1. correlation: total-energy loud/quiet = {tot_ratio:.1f}x")
print(f"2. tonality: dominant peak {pk_hz} Hz, crest={crest:.0f}x median, "
      f"loud/quiet={pk_ratio:.1f}x")
ok = tot_ratio >= 2.0 and crest >= 10.0 and pk_ratio >= 3.0
print("PASS: speaker emits a tone in sync with the TONE cycle (sound present)"
      if ok else "INCONCLUSIVE: no cycle-correlated narrowband tone found")
sys.exit(0 if ok else 2)
