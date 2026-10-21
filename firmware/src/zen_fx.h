/* SPDX-License-Identifier: GPL-3.0-only */
/* The space around the voices, after Zenthesizer's Fx.h: chorus, the ping-pong delay, the shimmer reverb, then a
 * tone setting for the FM-1's speaker.
 *   Chorus  two modulated delay taps, a quarter cycle apart left and right, 0.08..3.2 Hz.
 *   Delay   the plugin's ping-pong: the input into the left line, each line feeding the other through a tone
 *           low-pass and a soft clip, 40 ms .. 1.48 s, a touch of tape wow. At half the sample rate, in 16 bits.
 *   Reverb  the plugin's: eight delay lines through a Hadamard matrix, input diffusion, damping, slow
 *           modulation, and an octave-up shimmer fed back into it. Here it runs at half the sample rate
 *           (22.05 kHz): half the CPU and memory, and it loses only what its damping takes away anyway.
 * Integers only. Audio is Q15 (1.0 = 32768) in int32. */
#ifndef ZEN_FX_H
#define ZEN_FX_H
#include "zen_tables.h"

#ifdef DINGHY_HOST
#define ZPOOL
#else
#define ZPOOL __attribute__((section(".pool")))
#endif

/* ---- what the main loop sets (from the knobs, 0..127) */
static volatile int32_t zf_chorus = 0, zf_depth = 64, zf_reverb = 0, zf_decay = 64, zf_shimmer = 0;
static volatile int32_t zf_size = 76, zf_damp = 64, zf_tone = 0;
static volatile int32_t zf_dtime = 57, zf_dfb = 44, zf_dtone = 64, zf_delay = 0;
static int32_t zf_cmod, zf_dmod, zf_rmod;       /* the mixes' modulation (the slots' CHORUS, DELAY, REVERB), Q15 */
ZFAST static int32_t zf_mix(int32_t knob, int32_t mod)   /* a knob (0..127) and its modulation -> a mix, Q15 */
{
    int32_t m = knob * 258 + mod;
    return m < 0 ? 0 : m > 32767 ? 32767 : m;
}

/* ---- chorus (Fx.h Chorus) */
#define CH_LEN 2048u                            /* > 0.03 s, a power of two */
static int16_t ch_l[CH_LEN] ZPOOL, ch_r[CH_LEN] ZPOOL;
static uint32_t ch_w, ch_ph;
static int32_t ch_mix_prev;
#define CH_INC 23563u                           /* rate 0.3: 0.08 x 40^0.3 = 0.242 Hz, as a phase step */

ZFAST static int32_t ch_read(const int16_t *b, int32_t d_q8)   /* d frames ago (Q8), linear between the two neighbours */
{
    uint32_t i0 = (ch_w - 1u - (uint32_t)(d_q8 >> 8)) & (CH_LEN - 1u), i1 = (i0 - 1u) & (CH_LEN - 1u);
    int32_t f = d_q8 & 255;
    return b[i0] + (((b[i1] - b[i0]) * f) >> 8);
}

ZFAST static void chorus(int32_t *L, int32_t *R, uint32_t n)
{
    int32_t mix = zf_mix(zf_chorus, zf_cmod), prev = ch_mix_prev;   /* Q15 */
    int32_t base = 79027, swing = 16934 + zf_depth * 445;     /* 0.007 fs and (0.0015 + 0.005 depth) fs, Q8 frames */
    uint32_t i;
    if (!mix && !prev) {
        for (i = 0; i < n; i++, ch_w = (ch_w + 1u) & (CH_LEN - 1u)) {
            ch_l[ch_w] = (int16_t)(L[i] > 32767 ? 32767 : L[i] < -32768 ? -32768 : L[i]);
            ch_r[ch_w] = (int16_t)(R[i] > 32767 ? 32767 : R[i] < -32768 ? -32768 : R[i]);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t m, wl, wr, sl, sr;
        static int32_t norm;
        if (!(i & 15u)) {                       /* the mix glides; its level correction is worked out every 16 frames */
            m = prev + (mix - prev) * (int32_t)i / (int32_t)n;
            norm = (int32_t)(((uint32_t)32768u << 15) / (uint32_t)(32768 + ((m * 11469) >> 15)));   /* 1 / (1 + 0.35 m) */
        }
        m = prev + (int32_t)(((mix - prev) * (int32_t)i) >> 8);   /* n is 256: no divide per frame */
        ch_l[ch_w] = (int16_t)(L[i] > 32767 ? 32767 : L[i] < -32768 ? -32768 : L[i]);
        ch_r[ch_w] = (int16_t)(R[i] > 32767 ? 32767 : R[i] < -32768 ? -32768 : R[i]);
        ch_w = (ch_w + 1u) & (CH_LEN - 1u);
        sl = 16384 + (zsin(ch_ph) >> 1);                      /* 0.5 + 0.5 sin, Q15 */
        sr = 16384 + (zsin(ch_ph + 0x40000000u) >> 1);        /* a quarter cycle on */
        ch_ph += CH_INC;
        wl = ch_read(ch_l, base + (int32_t)(((int64_t)swing * sl) >> 15));
        wr = ch_read(ch_r, base + (int32_t)(((int64_t)swing * sr) >> 15));
        L[i] = (int32_t)(((int64_t)(L[i] + ((m * wl) >> 15)) * norm) >> 15);
        R[i] = (int32_t)(((int64_t)(R[i] + ((m * wr) >> 15)) * norm) >> 15);
    }
    ch_mix_prev = mix;
}

/* ---- the ping-pong delay (Fx.h PingPong), at half rate. The lines hold Q14 (half scale): the soft clip lets the
 * feedback reach 1.2. */
#define DL_LEN 32768u                           /* 1.49 s at 22.05 kHz */
static int16_t dl_bl[DL_LEN] ZPOOL, dl_br[DL_LEN] ZPOOL;
static uint32_t dl_w, dl_wow_n, dl_seed = 77u;
static int32_t dl_t = 6615 << 16;               /* the delay time, gliding (0.12 s), half-rate frames Q16 */
static int32_t dl_lpl, dl_lpr, dl_prevL, dl_prevR, dl_mix_prev, dl_wow_from, dl_wow_to, dl_d8 = 6615 << 8;
#define DL_GLIDE8 3241482                       /* 1 - exp(-8 / (0.12 x 22050)): 0.12 s, stepped every 8 frames, Q30 */

ZFAST static int32_t dl_soft(int32_t x)               /* Common.h softClip: linear to 0.8, then bending toward 1.2 */
{
    int32_t a = x < 0 ? -x : x, y;
    uint32_t over;
    if (a < 26214) return x;
    if (a > 4 * 32768) a = 4 * 32768;
    over = (uint32_t)(a - 26214);
    y = 26214 + (int32_t)((over << 15) / (32768u + ((over * 5u) >> 1)));   /* over / (1 + 2.5 over) */
    return x < 0 ? -y : y;
}
ZFAST static int32_t dl_read(const int16_t *b, int32_t d_q8)   /* d frames ago (Q8), linear; Q14 -> Q15 */
{
    uint32_t i0 = (dl_w - 1u - (uint32_t)(d_q8 >> 8)) & (DL_LEN - 1u), i1 = (i0 - 1u) & (DL_LEN - 1u);
    int32_t f = d_q8 & 255;
    return (b[i0] + (((b[i1] - b[i0]) * f) >> 8)) << 1;
}
ZFAST static int16_t dl_q14(int32_t x) { x >>= 1; return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

ZFAST static void delay(int32_t *L, int32_t *R, uint32_t n)
{
    int32_t mix = zf_mix(zf_delay, zf_dmod), prev = dl_mix_prev, target = DL_TIME[zf_dtime & 127] << 8;
    int32_t fbk = (zf_dfb & 127) * 237, G = DL_LP[zf_dtone & 127];       /* feedback x 0.92, Q15 */
    uint32_t i;
    for (i = 0; i + 1u < n; i += 2u) {
        int32_t m = prev + (int32_t)(((mix - prev) * (int32_t)i) >> 8), in, d8, yl, yr, v, fl, fr;
        if (!(i & 15u)) {                       /* every 8 frames (0.36 ms): the time's glide and the wow */
            int32_t t, sm, w;
            dl_t += M30(target - dl_t, DL_GLIDE8);
            dl_wow_n += 8u;
            if (dl_wow_n >= 36750u) {           /* the wow: a new random point every 1.7 s (0.6 Hz), eased toward */
                dl_wow_n = 0;
                dl_wow_from = dl_wow_to;
                dl_seed = dl_seed * 1664525u + 1013904223u;
                dl_wow_to = (int32_t)(dl_seed >> 16) - 32768;
            }
            t = (int32_t)((dl_wow_n * 58435u) >> 16);                        /* Q15 (2^31 / 36750) */
            sm = (int32_t)(((int64_t)t * t >> 15) * (98304 - 2 * t) >> 15); /* t^2 (3 - 2t) */
            w = dl_wow_from + (int32_t)(((int64_t)(dl_wow_to - dl_wow_from) * sm) >> 15);
            d8 = dl_t >> 8;
            dl_d8 = d8 + (((d8 >> 4) * ((w * 79) >> 16)) >> 11);           /* x (1 + 0.0012 wow) */
        }
        d8 = dl_d8;
        yl = dl_read(dl_bl, d8);
        yr = dl_read(dl_br, d8);
        in = (L[i] + R[i] + L[i + 1u] + R[i + 1u]) >> 2;                     /* 0.5 (L + R), two frames */
        v = M30(yr - dl_lpr, G); fr = v + dl_lpr; dl_lpr = fr + v;           /* the tone: TPT one-pole low-passes */
        v = M30(yl - dl_lpl, G); fl = v + dl_lpl; dl_lpl = fl + v;
        dl_bl[dl_w] = dl_q14(dl_soft(in + ((fbk * fr) >> 15)));             /* in, and the right line's echo, */
        dl_br[dl_w] = dl_q14(dl_soft((fbk * fl) >> 15));                     /* into the left; the left's into the right */
        dl_w = (dl_w + 1u) & (DL_LEN - 1u);
        if (m) {                                /* back to the full rate, as the reverb */
            L[i] += (((dl_prevL + yl) >> 1) * m) >> 15; L[i + 1u] += (yl * m) >> 15;
            R[i] += (((dl_prevR + yr) >> 1) * m) >> 15; R[i + 1u] += (yr * m) >> 15;
        }
        dl_prevL = yl; dl_prevR = yr;
    }
    dl_mix_prev = mix;
}

/* ---- reverb (Fx.h Reverb), at half rate */
#define RV_N 8
static const uint16_t RV_BASE[RV_N] = {655, 818, 906, 964, 1175, 1321, 1480, 1625};   /* 29.7 .. 73.7 ms at 22.05 kHz */
#define RV_CAP 4096u                            /* > 1625 x 2.0 (largest size) + modulation; a power of two */
static int32_t rv_line[RV_N][RV_CAP] ZPOOL;
static uint32_t rv_w;
static const uint16_t RV_APLEN[4] = {104, 79, 280, 205};   /* 4.7, 3.6, 12.7, 9.3 ms */
static int32_t rv_ap[4][288] ZPOOL;
static uint16_t rv_apw[4];
#define SH_LEN 2048u                            /* the shimmer's pitch shifter: 1500-frame windows */
#define SH_W 1500
static int32_t rv_sh[SH_LEN] ZPOOL;
static uint32_t rv_shw, rv_shph, rv_modph;
static int32_t rv_damp_y[RV_N], rv_shhp_x, rv_shhp_y, rv_feed, rv_sz = 32768 * 3 / 2, rv_prevL, rv_prevR;
static int32_t rv_mix_prev, rv_idle;
static int32_t rv_gain[RV_N];                   /* Q30, the main loop's (rv_set) */
static volatile int32_t rv_sz_target = 49152;   /* 0.5 + 1.5 size, Q15 */
#define SH_HP 985766336                         /* the shimmer's 300 Hz high-pass: exp(-2 pi 300 / 22050), Q30 */

static void rv_set(void)                        /* from the knobs: line gains for this decay and size */
{
    int32_t rt = RV_RT[zf_decay & 127], k;
    int32_t sz = 16384 + 49152 * zf_size / 127;               /* 0.5 + 1.5 size, Q15 */
    rv_sz_target = sz;
    for (k = 0; k < RV_N; k++) {
        int32_t x = ((int32_t)RV_BASE[k] * sz / rt) >> 5;         /* delay / rt, Q10 (32-bit: < 2^27) */
        if (x > 1024) x = 1024;
        rv_gain[k] = RV_EXP[x];                 /* 10^(-3 delay/rt): -60 dB after rt */
    }
}

ZFAST static int32_t rv_read(const int32_t *b, int32_t d_q8)
{
    uint32_t i0 = (rv_w - 1u - (uint32_t)(d_q8 >> 8)) & (RV_CAP - 1u), i1 = (i0 - 1u) & (RV_CAP - 1u);
    int32_t f = d_q8 & 255;
    return b[i0] + (((b[i1] - b[i0]) * f) >> 8);        /* 32-bit: the tail stays well under 2^22 */
}

ZFAST static void reverb(int32_t *L, int32_t *R, uint32_t n)
{
    int32_t mix = zf_mix(zf_reverb, zf_rmod), prev = rv_mix_prev, shim = zf_shimmer * 258, G = RV_DAMP[zf_damp & 127];
    int32_t mod, y[RV_N], h[RV_N];
    uint32_t i, k, len, s, j;
    if (!mix && !prev) {                        /* off: let the tail ring out for 2 s, then sleep */
        if (rv_idle > 2 * 44100) return;
        rv_idle += (int32_t)n;
    } else
        rv_idle = 0;
    for (i = 0; i + 1u < n; i += 2u) {
        int32_t in, oL, oR, m = prev + (int32_t)(((mix - prev) * (int32_t)i) >> 8), shimout = 0, uL, uR;   /* n is 256 */
        rv_sz += (rv_sz_target - rv_sz) >> 9;   /* SIZE glides (about 0.3 s) */
        mod = zsin(rv_modph);
        rv_modph += 60383u;                     /* 0.31 Hz at 22.05 kHz */
        for (k = 0; k < RV_N; k++) {
            int32_t d = (int32_t)(((int64_t)RV_BASE[k] * rv_sz) >> 7);   /* Q8 frames */
            if (k == 0) d += (mod * 6) >> 7;    /* +-6 frames (the plugin's 12 at full rate) */
            if (k == 5) d -= (mod * 9) >> 8;
            y[k] = rv_read(rv_line[k], d);
        }
        oL = (int32_t)(((int64_t)(y[0] + y[2] + y[4] + y[6]) * 11469) >> 15);   /* x 0.35 */
        oR = (int32_t)(((int64_t)(y[1] + y[3] + y[5] + y[7]) * 11469) >> 15);

        if (shim) {                             /* an octave-up pitch shifter on the tail */
            uint32_t p2 = rv_shph + 0x80000000u, i1, i2;
            int32_t t1, t2, w1, w2, x;
            rv_sh[rv_shw] = (oL + oR) >> 1;
            rv_shw = (rv_shw + 1u) & (SH_LEN - 1u);
            rv_shph += 2863311u;                /* 2^32 / 1500 */
            i1 = (rv_shw - 1u - (uint32_t)(((uint64_t)(0xFFFFFFFFu - rv_shph) * SH_W) >> 32) - 4u) & (SH_LEN - 1u);
            i2 = (rv_shw - 1u - (uint32_t)(((uint64_t)(0xFFFFFFFFu - p2) * SH_W) >> 32) - 4u) & (SH_LEN - 1u);
            w1 = zsin(rv_shph >> 1); w2 = zsin(p2 >> 1);           /* sin(pi phase) windows */
            t1 = rv_sh[i1]; t2 = rv_sh[i2];
            x = (int32_t)(((int64_t)t1 * w1 + (int64_t)t2 * w2) >> 15);
            rv_shhp_y = M30(rv_shhp_y + x - rv_shhp_x, SH_HP);     /* one-pole high-pass */
            rv_shhp_x = x;
            shimout = rv_shhp_y;
        }

        in = ((L[i] + R[i] + L[i + 1u] + R[i + 1u]) >> 2) + rv_feed;
        for (k = 0; k < 4u; k++) {              /* diffusion: four allpasses, g 0.62 (Common.h Allpass) */
            int32_t *b = rv_ap[k], d = b[rv_apw[k]], v = in + (int32_t)(((int64_t)d * 20316) >> 15);
            b[rv_apw[k]] = v;
            if (++rv_apw[k] == RV_APLEN[k]) rv_apw[k] = 0;
            in = d - (int32_t)(((int64_t)v * 20316) >> 15);
        }
        rv_feed = (int32_t)(((int64_t)shimout * shim >> 15) * 18022 >> 15);   /* x shimmer x 0.55 */

        for (k = 0; k < RV_N; k++) h[k] = y[k];
        for (len = 1; len < RV_N; len <<= 1)    /* Hadamard */
            for (s = 0; s < RV_N; s += len * 2u)
                for (j = s; j < s + len; j++) { int32_t a = h[j], c = h[j + len]; h[j] = a + c; h[j + len] = a - c; }
        for (k = 0; k < RV_N; k++) {
            int32_t x = (int32_t)(((int64_t)h[k] * 11585) >> 15), fbk;  /* x 1/sqrt(8) */
            int32_t vv = M30(x - rv_damp_y[k], G), yy = vv + rv_damp_y[k];   /* damping: TPT one-pole */
            rv_damp_y[k] = yy + vv;
            fbk = M30(yy, rv_gain[k]);
            rv_line[k][rv_w] = fbk + ((k & 1u) ? -(in >> 1) : (in >> 1));
        }
        rv_w = (rv_w + 1u) & (RV_CAP - 1u);

        /* back to the full rate: the first frame halfway from the last output, the second at this one */
        uL = (rv_prevL + oL) >> 1; uR = (rv_prevR + oR) >> 1;
        rv_prevL = oL; rv_prevR = oR;
        {
            int32_t dry = 32768 - (m >> 2);     /* 1 - 0.25 mix */
            L[i] = (int32_t)(((int64_t)L[i] * dry + (int64_t)uL * m) >> 15);
            R[i] = (int32_t)(((int64_t)R[i] * dry + (int64_t)uR * m) >> 15);
            L[i + 1u] = (int32_t)(((int64_t)L[i + 1u] * dry + (int64_t)oL * m) >> 15);
            R[i + 1u] = (int32_t)(((int64_t)R[i + 1u] * dry + (int64_t)oR * m) >> 15);
        }
    }
    rv_mix_prev = mix;
}

/* ---- the speaker tone: FLAT, SOFT (rounds off the top for the little speaker), LOW CUT (takes the lows it
 * cannot play, which only make it buzz). It shapes the headphones and USB audio too: FLAT when recording. */
static int32_t tn_lpL, tn_lpR, tn_hpL, tn_hpR, tn_hxL, tn_hxR;
#define TN_LP 547097416                         /* one-pole low-pass, ~5 kHz: 1 - exp(-2 pi 5000 / 44100), Q30 */
#define TN_HP 1046555113                        /* one-pole high-pass, ~180 Hz: exp(-2 pi 180 / 44100), Q30 */
ZFAST static void tone(int32_t *L, int32_t *R, uint32_t n)
{
    uint32_t i;
    if (zf_tone == 1)                           /* SOFT: half the signal through the low-pass (~-6 dB up top) */
        for (i = 0; i < n; i++) {
            tn_lpL += M30(L[i] - tn_lpL, TN_LP); tn_lpR += M30(R[i] - tn_lpR, TN_LP);
            L[i] = (L[i] + tn_lpL) >> 1; R[i] = (R[i] + tn_lpR) >> 1;
        }
    else if (zf_tone == 2)                      /* LOW CUT */
        for (i = 0; i < n; i++) {
            tn_hpL = M30(tn_hpL + L[i] - tn_hxL, TN_HP); tn_hxL = L[i]; L[i] = tn_hpL;
            tn_hpR = M30(tn_hpR + R[i] - tn_hxR, TN_HP); tn_hxR = R[i]; R[i] = tn_hpR;
        }
}
#endif
