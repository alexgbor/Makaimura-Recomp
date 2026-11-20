#!/usr/bin/env python3
"""
Name:        compare_mame_sound.py
Description: the sound the window plays against MAME's own: MAME 0.289's -wavwrite of makaimurg (its mixer and its
             resampler to 48 kHz) and the board's mix (gng_headless --wav, 125 kHz) through the window's resampler
             (ports/sdl2/resampler.c, run by build/bin/resample_check); aligned, then compared by level, by
             correlation second by second, and by spectrum (third-octave bands)
Version:     0.1.0
Created:     20-11-2026

    python tools/compare_mame_sound.py [--seconds N] [--mame PATH]

The two cannot be the same sample for sample: MAME's resampler is not the window's (they are compared on the 125 kHz
mix, which is the same as MAME's chips' byte for byte: tools/check_translation.py, tools/mame_trace.py --sound).
What this shows is what is left between them: the mixer's gain, a filter, a time offset. The WAVs are derived from
the ROM: they stay in a temporary folder.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
EXE = ".exe" if os.name == "nt" else ""
DEFAULT_MAME = os.environ.get("MAME", "D:/repositories/mame0289/mame.exe")
RATE = 48000
FRAME_RATE = 6000000 / (384 * 262)


def read_wav(path):
    with wave.open(path) as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1, path
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64), w.getframerate()


def mame_wav(mame, seconds, folder):
    out = os.path.join(folder, "mame.wav")
    subprocess.run([mame, "makaimurg", "-rompath", os.path.join(ROOT, "rom"), "-video", "none", "-sound", "none",
                    "-nothrottle", "-seconds_to_run", str(seconds), "-skip_gameinfo", "-nonvram_save",
                    "-noreadconfig", "-cfg_directory", os.path.join(folder, "cfg"), "-nvram_directory",
                    os.path.join(folder, "nvram"), "-snapshot_directory", os.path.join(folder, "snap"), "-wavwrite", out, "-samplerate", str(RATE)],
                   check=True, capture_output=True)
    return read_wav(out)


def board_wav(seconds, folder):
    board = os.path.join(folder, "board.wav")
    raw = os.path.join(folder, "board48.raw")
    frames = int(seconds * FRAME_RATE) + 2
    subprocess.run([os.path.join(ROOT, "build", "bin", "gng_headless" + EXE), "--frames", str(frames),
                    "--sound", os.devnull, "--wav", board], check=True, capture_output=True)
    subprocess.run([os.path.join(ROOT, "build", "bin", "resample_check" + EXE), board, str(RATE), raw],
                   check=True, capture_output=True)
    return np.fromfile(raw, dtype="<i2").astype(np.float64)


def align(a, b, window):
    """the lag of b against a (b[n + lag] ~ a[n]), by cross-correlation of their first 'window' samples"""
    n = 1 << int(np.ceil(np.log2(2 * window)))
    fa = np.fft.rfft(a[:window], n)
    fb = np.fft.rfft(b[:window], n)
    corr = np.fft.irfft(fb * np.conj(fa), n)
    lag = int(np.argmax(corr))
    return lag - n if lag > n // 2 else lag


def bands(x):
    """third-octave bands' energy (dB), 50 Hz to 20 kHz"""
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    freqs = np.fft.rfftfreq(len(x), 1 / RATE)
    centres = [50 * 2 ** (k / 3) for k in range(0, 27)]
    result = []
    for c in centres:
        low, high = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        energy = spectrum[(freqs >= low) & (freqs < high)].sum()
        result.append((c, 10 * np.log10(energy + 1e-9)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    parser.add_argument("--seconds", type=int, default=25)
    parser.add_argument("--mame", default=DEFAULT_MAME)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        mame, rate = mame_wav(args.mame, args.seconds, folder)
        assert rate == RATE
        ours = board_wav(args.seconds, folder)
    # the sound starts some seconds in (the boot is silent): align on a window from where MAME's starts
    start = int(np.argmax(np.abs(mame) > 64))
    lag = align(mame[start:], ours[start:], min(RATE * 4, len(mame) - start))
    if lag >= 0:
        a, b = mame[start:], ours[start + lag:]
    else:
        a, b = mame[start - lag:], ours[start:]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    print(f"MAME: {len(mame) / RATE:.2f} s, the window's: {len(ours) / RATE:.2f} s; sound from {start / RATE:.3f} s;"
          f" the window's {lag} samples ({1000 * lag / RATE:+.3f} ms) {'later' if lag >= 0 else 'earlier'}")
    rms_a, rms_b = np.sqrt(np.mean(a ** 2)), np.sqrt(np.mean(b ** 2))
    gain = np.dot(a, b) / np.dot(b, b)
    residue = a - gain * b
    print(f"level: MAME {20 * np.log10(rms_a / 32768):.2f} dBFS, the window's {20 * np.log10(rms_b / 32768):.2f} dBFS"
          f" ({20 * np.log10(rms_a / rms_b):+.3f} dB); peaks {np.max(np.abs(a)):.0f} and {np.max(np.abs(b)):.0f}")
    print(f"correlation {np.corrcoef(a, b)[0, 1]:.6f}; best gain {gain:.4f}; what is left "
          f"{20 * np.log10(np.sqrt(np.mean(residue ** 2)) / rms_a):.1f} dB under MAME's level")
    print("by second (correlation):", " ".join(f"{np.corrcoef(a[i:i + RATE], b[i:i + RATE])[0, 1]:.4f}"
                                              for i in range(0, n - RATE, RATE) if np.std(a[i:i + RATE]) > 1))
    print("third-octave bands, MAME minus the window's (dB):")
    line = []
    for (c, ea), (_, eb) in zip(bands(a), bands(b)):
        line.append(f"{c:.0f} Hz {ea - eb:+.2f}")
    for i in range(0, len(line), 6):
        print("  " + ", ".join(line[i:i + 6]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
