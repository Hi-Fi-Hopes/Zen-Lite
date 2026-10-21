#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare a Zen-Lite host render with the plugin's own render of the same notes.
   tests/compare.py zenlite.wav reference.wav       prints levels, peaks, clicks, and how alike the spectra are"""
import sys, wave
import numpy as np

def load(p):
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(float) / 32768

def db(x):
    return 20 * np.log10(max(x, 1e-9))

a, b = load(sys.argv[1]), load(sys.argv[2])
n = min(len(a), len(b)); a, b = a[:n], b[:n]
print(f"{'':12}{'Zen-Lite':>12}{'plugin':>12}")
print(f"{'peak dBFS':12}{db(abs(a).max()):12.1f}{db(abs(b).max()):12.1f}")
print(f"{'rms dBFS':12}{db(np.sqrt((a**2).mean())):12.1f}{db(np.sqrt((b**2).mean())):12.1f}")
fa = int((abs(a) > 0.999).sum()); fb = int((abs(b) > 0.999).sum())
print(f"{'at the rail':12}{fa:12d}{fb:12d}   (samples at full scale: must be 0)")
# a wrapped integer shows as a jump no filtered synth makes: compare the biggest sample-to-sample step
ja = np.abs(np.diff(a, axis=0)).max(); jb = np.abs(np.diff(b, axis=0)).max()
print(f"{'max step':12}{ja:12.3f}{jb:12.3f}   (a much bigger step than the plugin's = a click)")
# spectra over the whole render, 1/3-octave bands from 50 Hz to 16 kHz
def bands(x):
    m = x.mean(axis=1)
    win = 8192; acc = np.zeros(win // 2 + 1)
    for i in range(0, len(m) - win, win // 2):
        acc += np.abs(np.fft.rfft(m[i:i + win] * np.hanning(win))) ** 2
    f = np.fft.rfftfreq(win, 1 / 44100)
    edges = 50 * 2 ** (np.arange(0, 24.1) / 3)
    return np.array([acc[(f >= lo) & (f < hi)].sum() for lo, hi in zip(edges[:-1], edges[1:])]) + 1e-12
ba, bb = bands(a), bands(b)
la, lb = 10 * np.log10(ba / ba.sum()), 10 * np.log10(bb / bb.sum())
mask = lb > -50
if len(sys.argv) > 3:
    edges = 50 * 2 ** (np.arange(0, 24.1) / 3)
    for e, x, y in zip(edges, la, lb):
        if y > -50: print(f"   {e:7.0f} Hz  {x:6.1f} {y:6.1f}")
print(f"spectrum: bands within 3 dB of the plugin {int((abs(la - lb)[mask] < 3).sum())}/{int(mask.sum())}, "
      f"largest difference {abs(la - lb)[mask].max():.1f} dB")

if fa:
    sys.exit("FAIL: Zen-Lite reached full scale")
