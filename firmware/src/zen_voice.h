/* SPDX-License-Identifier: GPL-3.0-only */
/* Zen-Lite's voice, after Zenthesizer's (Source/dsp/Voice.h, Engine.h):
 *   Engine A  three analog oscillators (saw / square / triangle / sine; SHAPE; DETUNE spreads the two side ones)
 *   Engine B  the GLASS wavetable, POSITION scanning its eight frames
 *   the 4-pole zero-delay ladder, an exponential ADSR, the plugin's stereo spread, and its look-ahead limiter.
 * Integers only (no FPU on the FM-1). Every curve that needs a float is in gen/zen_tables.h.
 *
 * Number formats:  Q15 = 1.0 is 32768 (the audio)   Q24 = 1.0 is 2^24 (inside the filter, oscillator phase)
 *                  Q30 = 1.0 is 2^30 (coefficients, envelope)   Q10 = a knob position, 0..1024
 * Gain staging is the plugin's, with DRIVE at 0: A goes into the ladder x 0.3 and B x 0.15 (bLevel x 0.5 x
 * inTrim 0.3), it comes out x 2 (driveComp), and each voice sits 6 dB down (voiceHeadroom). The limiter after
 * the voices catches what chords add up to, so nothing reaches the platform's hard clip.
 * One difference: the plugin runs a ladder per side; here each voice has one (half the cost), and Engine A's
 * side oscillators are summed to it in the middle instead of spread left and right. */
#ifndef ZEN_VOICE_H
#define ZEN_VOICE_H
#include "zen_tables.h"

#define ZV 6u                                   /* voices */
#define ZBLK 16u                                /* control rate: coefficients every 16 frames (0.36 ms) */
#define ONE30 (1 << 30)
#define M30(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 30))

/* ---- what the main loop sets (32-bit stores: atomic; the ISR only reads these) */
static volatile int32_t zp_pos = 461;           /* POSITION, Q10 */
static volatile int32_t zp_cut = 635;           /* CUTOFF, Q10 */
static volatile int32_t zp_k = 0;               /* resonance k = res x 3.95, Q24 (zen_set_res) */
static volatile int32_t zc_att, zc_dec, zc_rel, zc_sus;   /* envelope, Q30 */
static volatile int32_t zp_awave = 0;           /* 0 saw, 1 square, 2 triangle, 3 sine */
static volatile int32_t zp_ashape = 0;          /* SHAPE, Q15 */
static volatile int32_t zp_adet = 205;          /* DETUNE, Q10 */
static volatile int32_t zp_atune = 0;           /* TUNE, semitones -24..24 */
static volatile int32_t zp_again = 32768, zp_bgain = 32768;   /* levels, Q15 (from the A-B mix) */

typedef struct {
    /* main loop writes */
    volatile uint32_t inc;                      /* Engine B's phase step: f * 2^32 / 44100 */
    volatile uint32_t trig;                     /* +1 for every note-on */
    volatile int32_t gate;                      /* 1 held, 0 let go */
    volatile int32_t vel;                       /* velocity gain, Q15 */
    volatile int32_t ktrack;                    /* key tracking, added to the cutoff knob (Q10) */
    volatile uint32_t lv;                       /* band-limited level for this pitch */
    volatile uint8_t note;
    uint8_t chord;                              /* 0 a key or MIDI note, 1..7 a chord's */
    uint32_t age, tag;
    /* the ISR's own */
    uint32_t seen, ph, pa[3];
    int32_t fb[3];                              /* the sine's self-FM memory, Q15 */
    volatile int32_t env;                       /* Q30; the main loop reads it to steal voices */
    volatile uint8_t stage;                     /* 0 idle, 1 attack, 2 decay/sustain, 3 release */
    int32_t s[4];                               /* ladder states, Q24 */
} zvoice_t;

static zvoice_t zv[ZV];
static uint32_t zrand = 0x1234567u;

static const uint32_t ZINC_C9[12] = {           /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t zinc(int note)                  /* a MIDI note's phase step */
{
    if (note < 0) note = 0;
    if (note > 131) note = 131;
    return ZINC_C9[note % 12] >> (10 - note / 12);
}

static inline __attribute__((always_inline)) int32_t ztanh(int32_t x)                 /* Voice.h fastTanh: Q24 in, Q24 out, from the table */
{
    uint32_t u, i, f;
    if (x >= (4 << 24)) return ZT_TANH[1024];
    if (x <= -(4 << 24)) return ZT_TANH[0];
    u = (uint32_t)(x + (4 << 24));
    i = u >> 17;
    f = u & 0x1FFFFu;
    return ZT_TANH[i] + (int32_t)(((int64_t)(ZT_TANH[i + 1] - ZT_TANH[i]) * (int32_t)f) >> 17);
}

static inline __attribute__((always_inline)) int32_t zsin(uint32_t ph)                /* a full cycle over 2^32 -> Q15 */
{
    uint32_t i = ph >> 22;
    int32_t f = (int32_t)((ph >> 7) & 0x7FFFu);
    return ZT_SIN[i] + (((ZT_SIN[i + 1] - ZT_SIN[i]) * f) >> 15);
}

/* PolyBLEP (Voice.h polyBlep): t and dt in Q24 (one cycle = 2^24), invdt = 2^31 / dt; Q15 out */
static inline __attribute__((always_inline)) int32_t zblep(uint32_t t, uint32_t dt, uint32_t invdt)
{
    if (t < dt) {
        int32_t x = (int32_t)(((uint64_t)t * invdt) >> 16);
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (t > (1u << 24) - dt) {
        int32_t x = (int32_t)((((int64_t)t - (1 << 24)) * (int64_t)invdt) >> 16);
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

/* one of Engine A's oscillators for a block of ZBLK frames (AnalogOsc::process), added into out[] at gain g (Q15).
 * The wave is chosen once a block, so each loop below is straight-line work. */
static void zosc_block(int32_t wave, uint32_t *php, uint32_t inc, uint32_t dt, uint32_t invdt, int32_t shape,
                       int32_t pw, int32_t foldm, int32_t foldk, int32_t kfm, int32_t *fbp, int32_t *out, int32_t g)
{
    uint32_t ph = *php, i;
    switch (wave) {
    case 0:                                     /* saw; SHAPE softens it toward a triangle */
        for (i = 0; i < ZBLK; i++, ph += inc) {
            int32_t p = (int32_t)(ph >> 17), tri = 32768 - 4 * (p > 16384 ? p - 16384 : 16384 - p);
            int32_t saw = (p << 1) - 32768 - zblep(ph >> 8, dt, invdt);
            out[i] += ((saw + (((tri - saw) * shape) >> 15)) * g) >> 15;
        }
        break;
    case 1:                                     /* square; SHAPE narrows the pulse */
        for (i = 0; i < ZBLK; i++, ph += inc) {
            int32_t p = (int32_t)(ph >> 17);
            uint32_t t24 = ph >> 8, q = (t24 - ((uint32_t)pw << 9)) & 0xFFFFFFu;
            int32_t sq = (p < pw ? 32767 : -32768) + zblep(t24, dt, invdt) - zblep(q, dt, invdt);
            out[i] += ((sq - (2 * pw - 32768)) * g) >> 15;
        }
        break;
    case 2:                                     /* triangle; SHAPE folds it (West Coast) */
        for (i = 0; i < ZBLK; i++, ph += inc) {
            int32_t p = (int32_t)(ph >> 17), tri = 32768 - 4 * (p > 16384 ? p - 16384 : 16384 - p), y = tri;
            if (foldk)                          /* sin(pi/2 x tri x (1 + 3 shape)): tri x foldm/4 is that, as a phase */
                y = tri + (((zsin((uint32_t)(tri * (foldm >> 2)) << 2) - tri) * foldk) >> 15);
            out[i] += (y * g) >> 15;
        }
        break;
    default: {                                  /* sine; SHAPE adds self-FM */
        int32_t fb = *fbp;
        for (i = 0; i < ZBLK; i++, ph += inc) {
            int32_t s = zsin(ph + (uint32_t)(fb * kfm));
            fb = (fb + s) >> 1;
            out[i] += (s * g) >> 15;
        }
        *fbp = fb;
        break;
    }
    }
    *php = ph;
}

/* ---- the look-ahead limiter (Fx.h Limiter): ceiling -1 dBFS, 1.45 ms look-ahead (64 frames), the gain
 * comes all the way down before the peak arrives and recovers over 150 ms. The plugin takes the most
 * reduction of the next 1.5 ms sample by sample; here it is taken over the next four 16-frame blocks. */
#define LIM_LA 64u
#define LIM_CEIL 29204                          /* 0.891 x 32768 */
static int32_t lim_dl[LIM_LA], lim_dr[LIM_LA];
static int32_t lim_req[LIM_LA / ZBLK + 1u];     /* Q30 gain each block needs */
static int32_t lim_g = ONE30;                   /* Q30 */
static uint32_t lim_w, lim_rq;
static volatile int32_t lim_min_seen = ONE30;   /* the deepest gain since the screen last looked */
#define LIM_ATT 126181474                       /* (1 - exp(-8/64)) Q30 */
#define LIM_REL 1073579434                      /* exp(-1 / (0.15 * 44100)) Q30 */

static void limiter(int32_t *L, int32_t *R, uint32_t n)
{
    static int ready;
    uint32_t i, j;
    if (!ready) {                               /* nothing seen yet needs any reduction */
        for (j = 0; j < LIM_LA / ZBLK + 1u; j++) lim_req[j] = ONE30;
        ready = 1;
    }
    for (i = 0; i < n; i += ZBLK) {
        int32_t pk = 0, m = ONE30;
        for (j = i; j < i + ZBLK; j++) {
            int32_t a = L[j] < 0 ? -L[j] : L[j], b = R[j] < 0 ? -R[j] : R[j];
            if (a > pk) pk = a;
            if (b > pk) pk = b;
        }
        lim_req[lim_rq] = pk > LIM_CEIL ? (int32_t)(((uint32_t)LIM_CEIL << 15) / (uint32_t)pk) << 15 : ONE30;
        lim_rq = (lim_rq + 1u) % (LIM_LA / ZBLK + 1u);
        for (j = 0; j < LIM_LA / ZBLK + 1u; j++)
            if (lim_req[j] < m) m = lim_req[j];
        for (j = i; j < i + ZBLK; j++) {
            int32_t oL, oR;
            if (m < lim_g) lim_g += M30(m - lim_g, LIM_ATT);
            else lim_g = m + M30(lim_g - m, LIM_REL);
            oL = (int32_t)(((int64_t)lim_dl[lim_w] * lim_g) >> 30);
            oR = (int32_t)(((int64_t)lim_dr[lim_w] * lim_g) >> 30);
            lim_dl[lim_w] = L[j];
            lim_dr[lim_w] = R[j];
            lim_w = (lim_w + 1u) % LIM_LA;
            L[j] = oL > 32767 ? 32767 : oL < -32767 ? -32767 : oL;   /* the safety net: the gain has normally handled it */
            R[j] = oR > 32767 ? 32767 : oR < -32767 ? -32767 : oR;
        }
        if (lim_g < lim_min_seen) lim_min_seen = lim_g;
    }
}

/* ---- one voice, n frames (n a multiple of ZBLK), added into mixL / mixR (Q15) */
static void zv_render(uint32_t vi, int32_t *mixL, int32_t *mixR, uint32_t n)
{
    zvoice_t *v = &zv[vi];
    uint32_t b, i, ph, pa0, pa1, pa2, inc = v->inc;
    int32_t env = v->env, vel = v->vel, s0, s1, s2, s3, fb0, fb1, fb2, ablk[ZBLK];
    uint8_t stage = v->stage;
    const int32_t pl = ZT_PAN[vi][0], pr = ZT_PAN[vi][1];

    if (v->trig != v->seen) {                   /* a new note: from silence, a fresh start */
        v->seen = v->trig;
        if (!stage) {
            v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
            zrand = zrand * 1664525u + 1013904223u;
            v->ph = zrand;                      /* free-running feel: random start phases */
            for (i = 0; i < 3u; i++) { zrand = zrand * 1664525u + 1013904223u; v->pa[i] = zrand; v->fb[i] = 0; }
            env = 0;
        }
        stage = 1;
    } else if (!v->gate && (stage == 1 || stage == 2))
        stage = 3;
    if (!stage) { v->env = 0; return; }

    ph = v->ph; pa0 = v->pa[0]; pa1 = v->pa[1]; pa2 = v->pa[2];
    fb0 = v->fb[0]; fb1 = v->fb[1]; fb2 = v->fb[2];
    s0 = v->s[0]; s1 = v->s[1]; s2 = v->s[2]; s3 = v->s[3];
    for (b = 0; b < n; b += ZBLK) {
        /* control rate: the ladder's coefficients for this cutoff and resonance (Voice.h Ladder) */
        int32_t c = zp_cut + v->ktrack, k = zp_k, G, G2, G3, G4, omg, c0, c1, c2, c3, inv, comp, ff;
        int32_t wave = zp_awave, shape = zp_ashape, pw, foldm, foldk, kfm, side, anorm, kA, kB, dq = zp_adet;
        uint32_t D, posq, f0, lv = v->lv, ia0, ia1, ia2, dt0, dt1, dt2, iv0, iv1, iv2;
        const int16_t *t0, *t1;
        if (c < 0) c = 0;
        if (c > 1024) c = 1024;
        G = ZT_G[c];
        G2 = M30(G, G); G3 = M30(G2, G); G4 = M30(G3, G);
        omg = ONE30 - G;
        c0 = M30(G3, omg); c1 = M30(G2, omg); c2 = M30(G, omg); c3 = omg;
        D = 32768u + (uint32_t)(((int64_t)k * G4) >> 39);           /* 1 + k G^4, Q15 */
        inv = (int32_t)((0x40000000u / D) << 15);                    /* 1 / (1 + k G^4), Q30 */
        comp = 4096 + (int32_t)((((int64_t)k >> 12) * 2253) >> 12); /* 1 + 0.55 k, Q12 */

        /* Engine B: which two GLASS frames, and how far between them */
        posq = (uint32_t)zp_pos * (ZT_FRAMES - 1u);                  /* frames, Q10 */
        f0 = posq >> 10;
        if (f0 > ZT_FRAMES - 2u) f0 = ZT_FRAMES - 2u;
        ff = (int32_t)((posq - (f0 << 10)) << 5);                    /* crossfade, Q15 */
        t0 = ZT_GLASS[f0][lv];
        t1 = ZT_GLASS[f0 + 1u][lv];

        /* Engine A: three pitches (centre, and the side pair DETUNE spreads), each <= 0.45 fs */
        if (dq < 0) dq = 0;
        if (dq > 1024) dq = 1024;
        ia0 = zinc(v->note + zp_atune);
        ia1 = (uint32_t)(((uint64_t)ia0 * (uint32_t)ZT_DET[dq][0]) >> 30);
        ia2 = (uint32_t)(((uint64_t)ia0 * (uint32_t)ZT_DET[dq][1]) >> 30);
        if (ia0 > 1932735283u) ia0 = 1932735283u;
        if (ia1 > 1932735283u) ia1 = 1932735283u;
        if (ia2 > 1932735283u) ia2 = 1932735283u;
        dt0 = (ia0 >> 8) | 1u; dt1 = (ia1 >> 8) | 1u; dt2 = (ia2 >> 8) | 1u;
        iv0 = 0x80000000u / dt0; iv1 = 0x80000000u / dt1; iv2 = 0x80000000u / dt2;
        side = ZT_DET[dq][2]; anorm = ZT_DET[dq][3];
        pw = 16384 - ((13763 * shape) >> 15);                        /* 0.5 - 0.42 shape, Q15 */
        foldm = 32768 + 3 * shape;                                   /* 1 + 3 shape, Q15 */
        foldk = shape >= 8192 ? 32768 : shape * 4;                   /* min(1, 4 shape), Q15 */
        kfm = (int32_t)(((int64_t)shape * 33373) >> 15);             /* shape x 1.6 / 2 pi, as a phase */
        kA = (int32_t)(((int64_t)9830 * zp_again) >> 15);            /* A into the ladder: x 0.3 */
        kA = (int32_t)(((int64_t)kA * anorm) >> 15);
        kB = (int32_t)(((int64_t)ZT_XGAIN * zp_bgain) >> 15);        /* B: x 0.15 */
        side = (side * 19661) >> 15;                                 /* the side pair at 0.6 each, mono */

        for (i = 0; i < ZBLK; i++) ablk[i] = 0;
        if (kA) {
            zosc_block(wave, &pa0, ia0, dt0, iv0, shape, pw, foldm, foldk, kfm, &fb0, ablk, 32768);
            if (side) {
                zosc_block(wave, &pa1, ia1, dt1, iv1, shape, pw, foldm, foldk, kfm, &fb1, ablk, side);
                zosc_block(wave, &pa2, ia2, dt2, iv2, shape, pw, foldm, foldk, kfm, &fb2, ablk, side);
            } else { pa1 += ia1 * ZBLK; pa2 += ia2 * ZBLK; }
        } else { pa0 += ia0 * ZBLK; pa1 += ia1 * ZBLK; pa2 += ia2 * ZBLK; }

        for (i = b; i < b + ZBLK; i++) {
            uint32_t ix = ph >> 23, ix2 = (ix + 1u) & (ZT_SIZE - 1u);
            int32_t fr = (int32_t)((ph >> 8) & 0x7FFFu), a0, a1, w = 0, a = ablk[i - b], x, sig, y4, u, vv, y, o;
            if (kB) {
                a0 = t0[ix] + (((t0[ix2] - t0[ix]) * fr) >> 15);
                a1 = t1[ix] + (((t1[ix2] - t1[ix]) * fr) >> 15);
                w = a0 + (((a1 - a0) * ff) >> 15);                 /* Q15 */
            }
            ph += inc;
            x = (a * kA + w * kB) >> 6;                             /* Q15 -> Q24, with the gains */

            /* the ladder: zero-delay feedback, tanh on the input (Zavalishin, as the plugin) */
            sig = (int32_t)(((int64_t)c0 * s0 + (int64_t)c1 * s1 + (int64_t)c2 * s2 + (int64_t)c3 * s3) >> 30);
            y4 = M30(M30(G4, x) + sig, inv);
            u = ztanh(x - (int32_t)(((int64_t)k * y4) >> 24));
            vv = M30(u - s0, G); y = vv + s0; s0 = y + vv; u = y;
            vv = M30(u - s1, G); y = vv + s1; s1 = y + vv; u = y;
            vv = M30(u - s2, G); y = vv + s2; s2 = y + vv; u = y;
            vv = M30(u - s3, G); y = vv + s3; s3 = y + vv; u = y;
            y = (int32_t)(((int64_t)u * comp) >> 12);               /* Q24, resonance makeup */

            /* the amp envelope (Voice.h Env), per frame */
            if (stage == 1) {
                env += M30(1395864371 - env, zc_att);              /* towards 1.3, as the plugin */
                if (env >= ONE30) { env = ONE30; stage = 2; }
            } else if (stage == 2)
                env += M30(zc_sus - env, zc_dec);
            else {
                env += M30(-env, zc_rel) - 1;
                if (env < 107374) { env = 0; stage = 0; }           /* 1e-4 */
            }

            /* x2 driveComp x 0.5 headroom = 1; Q24 x Q30 -> Q15, then velocity and the voice's place */
            o = (int32_t)(((int64_t)y * env) >> 39);
            o = (int32_t)(((int64_t)o * vel) >> 15);
            mixL[i] += (int32_t)(((int64_t)o * pl) >> 14);
            mixR[i] += (int32_t)(((int64_t)o * pr) >> 14);
        }
        if (!stage) break;
    }
    v->ph = ph; v->pa[0] = pa0; v->pa[1] = pa1; v->pa[2] = pa2;
    v->fb[0] = fb0; v->fb[1] = fb1; v->fb[2] = fb2;
    v->s[0] = s0; v->s[1] = s1; v->s[2] = s2; v->s[3] = s3;
    v->env = env;
    v->stage = stage;
}

/* ---- settings from the main loop */
static void zen_set_res(int32_t res_q10) { zp_k = (int32_t)(((int64_t)res_q10 * 66270167) >> 10); }  /* x 3.95 in Q24 */
static void zen_set_env(int32_t a, int32_t d, int32_t s, int32_t r)                           /* knobs, Q10 */
{
    zc_att = ZT_ENV_A[a]; zc_dec = ZT_ENV_DR[d]; zc_rel = ZT_ENV_DR[r];
    zc_sus = s << 20;
}
static void zen_set_mix(int32_t m_q10)          /* A-B: 0 all A, the middle both full, 1024 all B */
{
    int32_t a = (1024 - m_q10) * 64, bb = m_q10 * 64;
    zp_again = a > 32768 ? 32768 : a;
    zp_bgain = bb > 32768 ? 32768 : bb;
}
#endif
