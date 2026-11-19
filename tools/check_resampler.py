"""
Name:        check_resampler.py
Description: the window's resampler (ports/sdl2/resampler.c, run by build/bin/resample_check) against scipy's
             resample_poly on the board's mix: the error, by second, and what is left above the band
Version:     0.1.0
Created:     19-11-2026

python tools/check_resampler.py BOARD.wav [RATE]   (BOARD.wav: gng_headless --sound F --wav BOARD.wav; RATE 48000)

scipy's resample_poly (a long Kaiser FIR, beta 10, at the exact ratio) is the reference; the two filters are not the same (cut
at 20 kHz here, at the output's Nyquist there), so the error is measured below 18 kHz, where both pass.
"""
import math
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    board = sys.argv[1]
    rate = int(sys.argv[2]) if len(sys.argv) > 2 else 48000
    with wave.open(board) as w:
        assert w.getframerate() == 125000
        mix = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    with tempfile.TemporaryDirectory() as folder:
        raw = os.path.join(folder, "out.raw")
        subprocess.run([os.path.join(ROOT, "build", "bin", "resample_check"), board, str(rate), raw], check=True)
        ours = np.fromfile(raw, dtype="<i2").astype(np.float64)
    g = math.gcd(rate, 125000)
    theirs = signal.resample_poly(mix, rate // g, 125000 // g, window=("kaiser", 10.0))
    n = min(len(ours), len(theirs))
    # both low-passed at 18 kHz: the band they both pass
    sos = signal.butter(10, 18000, fs=rate, output="sos")
    a = signal.sosfiltfilt(sos, ours[:n])
    b = signal.sosfiltfilt(sos, theirs[:n])
    worst = math.inf
    for second in range(int(n / rate)):
        x, y = a[second * rate:(second + 1) * rate], b[second * rate:(second + 1) * rate]
        power = np.sum(y ** 2)
        if power < 1e3 * rate:    # (silence: nothing to measure)
            continue
        snr = 10 * math.log10(power / max(np.sum((x - y) ** 2), 1e-9))
        worst = min(worst, snr)
    f, p = signal.welch(ours, fs=rate, nperseg=8192)
    above = 10 * math.log10(p[f > 21000].sum() / p[(f > 100) & (f < 16000)].sum()) if rate >= 44100 else float("nan")
    print(f"{n} samples at {rate} Hz; worst second against scipy (below 18 kHz): {worst:.1f} dB; "
          f"power above 21 kHz: {above:.1f} dB")
    return 0 if worst > 60 else 1


if __name__ == "__main__":
    sys.exit(main())
