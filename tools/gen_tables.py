#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write firmware/gen/zen_tables.h: every number Zen-Lite needs that takes a float to compute.

The FM-1 has no FPU, so the curves are computed here once and stored as integers.
The formulas are Zenthesizer's own (Source/dsp/Wavetables.h, Maps.h, Voice.h, Engine.h),
so the FM-1 voice follows the plugin's maths.

  python3 tools/gen_tables.py            (rewrites firmware/gen/zen_tables.h)
"""
import math
from pathlib import Path

FS = 44100.0
SIZE = 512          # samples per single cycle (the plugin uses 2048; 512 still holds 128 harmonics)
FRAMES = 8          # Position scans these
LEVELS = 8          # band-limited copies: level L keeps harmonics 1 .. 128 >> L
MAXH = 128

def glass(t):
    """GLASS: a bright partial cluster that rises with position, bell-like (Wavetables.h, case 1)."""
    a = [0.0] * (MAXH + 1)
    c = 2.0 + t * 22.0
    for h in range(1, MAXH + 1):
        keep = (h % 2 == 1) or h == 2 or h == 4
        g = math.exp(-((h - c) ** 2) / (2.0 * 2.2 * 2.2))
        a[h] = (0.8 if h == 1 else 0.0) + ((g * 0.9 + 0.15 / (h * h)) if keep else 0.0)
    return a

def air(t):
    """AIR: pure sine opening into a soft, breathy saw (case 0)."""
    p = 3.2 - 2.2 * t
    return [0.0] + [1.0 if h == 1 else h ** (-p) * (0.6 if h % 2 == 0 else 1.0) for h in range(1, MAXH + 1)]

def vox(t):
    """VOX: vowels a-e-i-o-u, formants for a ~220 Hz voice (case 2)."""
    vf = [[800, 1150, 2900], [400, 1600, 2700], [270, 2140, 2950], [450, 800, 2830], [325, 700, 2530]]
    x = t * 4.0
    i0 = min(3, int(x))
    fr = x - i0
    a = [0.0] * (MAXH + 1)
    for h in range(1, MAXH + 1):
        hz = 220.0 * h
        env = 0.0
        for k in range(3):
            f = vf[i0][k] + (vf[i0 + 1][k] - vf[i0][k]) * fr
            bw = 80.0 + 40.0 * k
            g = 1.0 if k == 0 else 0.6 if k == 1 else 0.35
            env += g * math.exp(-(hz - f) ** 2 / (2.0 * bw * bw))
        a[h] = (0.12 + env) * h ** -0.8
    return a

def reed(t):
    """REED: square-ish to a narrow reedy pulse (case 3)."""
    w = 0.5 - 0.42 * t
    return [0.0] + [abs(math.sin(math.pi * h * w)) / h * (1.0 - 0.25 * t * (1.0 if h % 3 == 0 else 0.0)) for h in range(1, MAXH + 1)]

def table(amp_fn):
    sine = [math.sin(2 * math.pi * i / SIZE) for i in range(SIZE)]
    out = []
    for fr in range(FRAMES):
        amp = amp_fn(fr / (FRAMES - 1))
        norm = 1.0
        frame = []
        for lv in range(LEVELS):
            hmax = MAXH >> lv
            tmp = [0.0] * SIZE
            for h in range(1, hmax + 1):
                a = amp[h]
                if a < 1e-5:
                    continue
                for i in range(SIZE):
                    tmp[i] += a * sine[(i * h) % SIZE]
            if lv == 0:
                norm = 1.0 / max(1e-6, max(abs(v) for v in tmp))
            frame.append([v * norm for v in tmp])
        out.append(frame)
    return out

def to_int16(tables):
    """the band-limited copies overshoot the full one a little (Gibbs): one scale for all tables, under int16"""
    worst = max(abs(v) for t in tables for fr in t for lv in fr for v in lv)
    scale = 32767 / worst
    return [[[[round(v * scale) for v in lv] for lv in fr] for fr in t] for t in tables], scale

def fast_tanh(x):                         # Voice.h fastTanh
    x = max(-4.5, min(4.5, x))
    return x * (27 + x * x) / (27 + 9 * x * x)

def q(v, bits):
    return int(round(v * (1 << bits)))

def main():
    tabs, gscale = to_int16([table(air), table(glass), table(vox), table(reed)])   # the plugin's order: AIR GLASS VOX REED
    tanh = [q(fast_tanh(-4.0 + 8.0 * i / 1024), 24) for i in range(1025)]   # input -4..4, Q24 out

    gtab = []                              # cutoff knob (Q10, 0..1024) -> G of the TPT one-pole, Q30
    for i in range(1025):
        hz = min(20.0 * 900.0 ** (i / 1024), FS * 0.45)   # Maps.h cutoffHz, clamped as Engine.h
        gg = math.tan(math.pi * hz / FS)
        gtab.append(q(gg / (1 + gg), 30))

    env_a, env_dr = [], []                 # envelope time knob (Q10) -> per-sample coefficient, Q30
    for i in range(1025):
        t = 0.001 * 15000.0 ** (i / 1024)                   # Maps.h envTimeSec: 1 ms .. 15 s
        env_a.append(q(1 - math.exp(-1.466 / (t * FS)), 30))
        env_dr.append(q(1 - math.exp(-4.6 / (t * FS)), 30))

    sin_t = [q(math.sin(2 * math.pi * i / 1024), 15) for i in range(1025)]
    sin_t = [max(-32767, min(32767, v)) for v in sin_t]

    det = []                               # Engine A DETUNE knob (Q10) -> the side oscillators (Engine.h)
    for i in range(1025):
        d = i / 1024
        cents = d ** 1.5 * 38.0
        side = min(1.0, d * 5.0)
        det += [q(2 ** (-cents / 1200), 30), q(2 ** (cents / 1200 * 1.07), 30), q(side, 15), q(1 / (1 + side * 1.2), 15)]

    HF = FS / 2
    rv_rt = [round(0.4 * 75.0 ** (k / 127) * HF) for k in range(128)]               # reverbSec, in half-rate samples
    rv_damp = []
    for k in range(128):                                                          # damping one-pole, Q30 at half rate
        hz = min(1500.0 * 2.0 ** ((1.0 - k / 127) * 3.5), HF * 0.45)
        gg = math.tan(math.pi * hz / HF)
        rv_damp.append(q(gg / (1 + gg), 30))
    rv_exp = [q(10 ** (-3.0 * (i / 1024)), 30) for i in range(1025)]             # 10^(-3x), x 0..1 (Q10)

    # ---- step 3.2: modulation, drive, glide, volume, delay (Engine.h, Maps.h, Fx.h)
    BLK = 16                                                                       # the control block, frames
    exp2f = [q(2 ** (i / 1024), 30) for i in range(1025)]                         # 2^(i/1024), Q30
    def env16(c): return 1 - (1 - c) ** BLK
    env_a16, env_dr16 = [], []
    for i in range(1025):                                                          # mod envelopes, per 16 frames
        t = 0.001 * 15000.0 ** (i / 1024)
        env_a16.append(q(env16(1 - math.exp(-1.466 / (t * FS))), 30))
        env_dr16.append(q(env16(1 - math.exp(-4.6 / (t * FS))), 30))
    lfo_inc = [round(0.02 * 1000.0 ** (i / 1024) * BLK / FS * 2 ** 32) for i in range(1025)]   # lfoHz, per block
    drv_in, drv_out = [], []
    for i in range(1025):                                                          # DRIVE: into and out of the ladder
        d = i / 1024
        gain, trim = 1 + 6 * d, 0.3 + 0.3 * d
        drv_in.append(q(gain * trim / 0.3, 12))                                   # relative to drive 0
        drv_out.append(q(0.6 / trim / (1 + 1.6 * d) * 0.5, 14))                   # driveComp x voiceHeadroom
    panlaw = []
    for i in range(1025):                                                          # pan -1..1 -> left, right gains
        pn = -1 + 2 * i / 1024
        panlaw.append((q(math.cos((pn + 1) * 0.25 * math.pi) * 1.41421, 14), q(math.sin((pn + 1) * 0.25 * math.pi) * 1.41421, 14)))
    vol = [q(10 ** ((-36 + k / 127 * 42) / 20), 14) for k in range(128)]          # volumeDb, Q14
    glide = []
    for k in range(128):                                                           # glide, per block, Q30 (0 = none)
        v = k / 127
        gs = 0.0 if v <= 0.001 else 0.002 * 1000 ** v
        glide.append(0 if gs == 0 else q(1 - math.exp(-BLK / (gs * 0.4 * FS)), 30))
    dl_time = [round(min(40.0 * 37.5 ** (k / 127) * 0.001 * HF, 32700) * 256) for k in range(128)]   # delayMs, half-rate frames Q8
    dl_lp = []
    for k in range(128):                                                           # the delay's feedback tone, Q30 at half rate
        hz = min(500.0 * 32.0 ** (k / 127), HF * 0.45)
        gg = math.tan(math.pi * hz / HF)
        dl_lp.append(q(gg / (1 + gg), 30))

    voice_pan = [-0.15, 0.15, -0.45, 0.45, -0.3, 0.3, -0.6, 0.6]       # Engine.h, times spread 0.5
    pan = []
    for p in voice_pan:
        p *= 0.5
        pan.append((q(math.cos((p + 1) * 0.25 * math.pi) * 1.41421, 14),
                    q(math.sin((p + 1) * 0.25 * math.pi) * 1.41421, 14)))

    L = ["/* SPDX-License-Identifier: GPL-3.0-only */",
         "/* Generated by tools/gen_tables.py from Zenthesizer's formulas. Do not edit. */",
         "#ifndef ZEN_TABLES_H", "#define ZEN_TABLES_H", "#include <stdint.h>",
         f"#define ZT_SIZE {SIZE}u", f"#define ZT_FRAMES {FRAMES}u", f"#define ZT_LEVELS {LEVELS}u", f"#define ZT_MAXH {MAXH}u",
         f"#define ZT_XGAIN {round(0.15 * 32768 * 32768 / gscale)}   /* the wave into the ladder: 0.15, undoing the table's scale ({gscale / 32768:.4f}) */", ""]
    L.append(f"static const int16_t ZT_TAB[4][{FRAMES}][{LEVELS}][{SIZE}] = {{   /* AIR, GLASS, VOX, REED */")
    for t in tabs:
        L.append("{")
        for fr in t:
            L.append(" {")
            for lv in fr:
                L.append("  {" + ",".join(map(str, lv)) + "},")
            L.append(" },")
        L.append("},")
    L.append("};")
    def arr(name, typ, vals, ram=False):     # ram: read every sample, so kept in RAM (no const), not the flash
        L.append(f"static {'' if ram else 'const '}{typ} {name}[{len(vals)}] = {{")
        for i in range(0, len(vals), 12):
            L.append("  " + ",".join(map(str, vals[i:i + 12])) + ",")
        L.append("};")
    arr("ZT_TANH", "int32_t", tanh, ram=True)
    arr("ZT_G", "int32_t", gtab)
    arr("ZT_ENV_A", "int32_t", env_a)
    arr("ZT_ENV_DR", "int32_t", env_dr)
    arr("ZT_SIN", "int16_t", sin_t, ram=True)
    arr("RV_RT", "int32_t", rv_rt)
    arr("ZT_EXP2F", "int32_t", exp2f)
    arr("ZT_ENV_A16", "int32_t", env_a16)
    arr("ZT_ENV_DR16", "int32_t", env_dr16)
    arr("ZT_LFO_INC", "uint32_t", lfo_inc)
    arr("ZT_DRV_IN", "int32_t", drv_in)
    arr("ZT_DRV_OUT", "int32_t", drv_out)
    L.append("static const int16_t ZT_PANLAW[1025][2] = {" + ",".join(f"{{{a},{b}}}" for a, b in panlaw) + "};")
    arr("ZT_VOL", "int32_t", vol)
    arr("ZT_GLIDE", "int32_t", glide)
    arr("DL_TIME", "int32_t", dl_time)
    arr("DL_LP", "int32_t", dl_lp)
    arr("RV_DAMP", "int32_t", rv_damp)
    arr("RV_EXP", "int32_t", rv_exp)
    L.append("static const int32_t ZT_DET[1025][4] = {")
    for i in range(0, len(det), 4):
        L.append("  {" + ",".join(map(str, det[i:i + 4])) + "},")
    L.append("};")
    L.append("static const int16_t ZT_PAN[8][2] = {" + ",".join(f"{{{a},{b}}}" for a, b in pan) + "};")
    L.append("#endif")
    out = Path(__file__).resolve().parent.parent / "firmware" / "gen" / "zen_tables.h"
    out.write_text("\n".join(L) + "\n")
    print("wrote", out, out.stat().st_size, "bytes")

if __name__ == "__main__":
    main()
