# tools/screencap/diag_spectrum.py — diagnostic: split a room recording into the
# loud vs quiet windows (by ~1 kHz band energy), then compare their average spectra
# to see whether a real tonal peak (1 kHz + distortion harmonics) appears only when
# the firmware is playing. Prints the top spectral peaks of each group.
import sys, wave, numpy as np

path = sys.argv[1] if len(sys.argv) > 1 else "tools/screencap/captures/room.wav"
w = wave.open(path)
fs, n, ch = w.getframerate(), w.getnframes(), w.getnchannels()
x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(float)
x = x.reshape(-1, ch).mean(axis=1) if ch > 1 else x

win = fs // 2  # 0.5 s windows
freqs = np.fft.rfftfreq(win, 1.0 / fs)
k1 = int(round(1000.0 / fs * win))
specs, band = [], []
for i in range(0, len(x) - win, win):
    seg = x[i:i + win] * np.hanning(win)
    mag = np.abs(np.fft.rfft(seg))
    specs.append(mag)
    band.append(mag[k1 - 1:k1 + 2].sum() / (mag.sum() + 1e-9))
specs = np.array(specs); band = np.array(band)
thr = (band.max() + band.min()) / 2
loud = specs[band >= thr].mean(axis=0)
quiet = specs[band < thr].mean(axis=0)
print(f"{path}: {len(specs)} windows, loud={int((band>=thr).sum())} quiet={int((band<thr).sum())}")


def top(mag, label):
    # ignore DC/low rumble below 200 Hz
    m = mag.copy(); m[freqs < 200] = 0
    idx = np.argsort(m)[::-1][:6]
    print(f"  {label} top peaks (Hz@rel):",
          ", ".join(f"{int(freqs[j])}@{m[j]/ (mag.sum()+1e-9):.3f}" for j in sorted(idx, key=lambda j:-m[j])))


top(loud, "LOUD (tone?) ")
top(quiet, "QUIET (silence)")
# ratio of 1 kHz energy loud vs quiet
r = (loud[k1-1:k1+2].sum()) / (quiet[k1-1:k1+2].sum() + 1e-9)
print(f"  1kHz loud/quiet ratio = {r:.1f}x")
