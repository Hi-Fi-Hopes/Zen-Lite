/* SPDX-License-Identifier: GPL-3.0-only */
/* Zen-Lite's voice, after Zenthesizer's (Source/dsp/Voice.h, Engine.h):
 *   Engine A  three analog oscillators (saw / square / triangle / sine; SHAPE; DETUNE spreads the two side ones)
 *   Engine B  four wavetables (AIR, GLASS, VOX, REED), POSITION scanning their eight frames, WARP bending the phase
 *   the 4-pole zero-delay ladder, an exponential ADSR, the plugin's stereo spread, and its look-ahead limiter.
 * Integers only (no FPU on the FM-1). Every curve that needs a float is in gen/zen_tables.h.
 *
 * Number formats:  Q15 = 1.0 is 32768 (the audio)   Q24 = 1.0 is 2^24 (inside the filter, oscillator phase)
 *                  Q30 = 1.0 is 2^30 (coefficients, envelope)   Q10 = a knob position, 0..1024
 * Gain staging is the plugin's: at DRIVE 0, A goes into the ladder x 0.3 and B x 0.15 (bLevel x 0.5 x inTrim
 * 0.3), it comes out x 2 (driveComp), and each voice sits 6 dB down (voiceHeadroom); DRIVE pushes the input up to
 * x 14 into the ladder's tanh and takes the output down to match (ZT_DRV_IN, ZT_DRV_OUT). The limiter after the
 * voices catches what chords add up to, so nothing reaches the platform's hard clip.
 * One difference: the plugin runs a ladder per side; here each voice has one (half the cost), and Engine A's
 * side oscillators are summed to it in the middle instead of spread left and right. */
#ifndef ZEN_VOICE_H
#define ZEN_VOICE_H
#include "zen_tables.h"

/* the audio code runs from RAM on the FM-1 (app.ld .fast_text), out of reach of what the screen does to the flash cache */
#ifdef DINGHY_HOST
#define ZFAST
#else
#define ZFAST __attribute__((section(".fast_text")))
#endif

#define ZV 6u                                   /* voices */
#define ZBLK 16u                                /* control rate: coefficients every 16 frames (0.36 ms) */
#define ONE30 (1 << 30)
#define M30(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 30))

/* ---- what the main loop sets (32-bit stores: atomic; the ISR only reads these). Knobs are Q10 (0..1024). */
static volatile int32_t zp_pos = 461;           /* B POSITION */
static volatile int32_t zp_cut = 635;           /* CUTOFF */
static volatile int32_t zp_res = 307;           /* RESONANCE */
static volatile int32_t zp_drive = 0;           /* DRIVE */
static volatile int32_t zp_envamt = 0;          /* ENV AMOUNT, -1024..1024 (ENV 1 to the cutoff, +-0.6) */
static volatile int32_t zp_ktrack = 516;        /* KEY TRACK */
static volatile int32_t zc_att, zc_dec, zc_rel, zc_sus;   /* the amp envelope, Q30 per frame */
static volatile int32_t ze_att[2], ze_dec[2], ze_rel[2], ze_sus[2];   /* ENV 1, ENV 2, Q30 per 16 frames */
static volatile int32_t zp_awave = 0;           /* 0 saw, 1 square, 2 triangle, 3 sine */
static volatile int32_t zp_ashape = 0;          /* SHAPE */
static volatile int32_t zp_adet = 205;          /* DETUNE */
static volatile int32_t zp_atune = 0;           /* TUNE, semitones -24..24 */
static volatile int32_t zp_alev = 1024, zp_blev = 1024;   /* A LEVEL, B LEVEL */
static volatile int32_t zp_btable = 1;          /* 0 AIR, 1 GLASS, 2 VOX, 3 REED */
static volatile int32_t zp_bwarp = 0;           /* WARP */
static volatile int32_t zp_glide = 0;           /* GLIDE, Q30 per 16 frames (0: none) */
static volatile int32_t zp_velsens = 516;       /* VEL SENS */
static volatile int32_t zp_spread = 516;        /* STEREO: the voices' spread */
static volatile int32_t zm_bend;                /* MIDI pitch bend, semitones Q10 */
typedef struct {
    /* main loop writes */
    volatile uint32_t trig;                     /* +1 for every note-on */
    volatile int32_t gate;                      /* 1 held, 0 let go */
    volatile int32_t velq;                      /* velocity, Q15 */
    volatile int32_t press_t;                   /* poly pressure, Q15 */
    volatile int32_t from;                      /* with GLIDE, a voice starting from silence glides from here (Q20) */
    volatile uint8_t note;
    uint8_t chord;                              /* 0 a key or MIDI note, 1..7 a chord's */
    uint32_t age, tag;
    /* the ISR's own */
    uint32_t seen, ph, pa[3];
    int32_t fb[3];                              /* the sine's self-FM memory, Q15 */
    volatile int32_t env;                       /* Q30; the main loop reads it to steal voices */
    volatile uint8_t stage;                     /* 0 idle, 1 attack, 2 decay/sustain, 3 release, 4 fading out (the guard) */
    uint8_t kill;                               /* the CPU guard wants this voice gone: a 4 ms fade */
    uint8_t es[2];                              /* ENV 1, 2: 0 idle, 1 attack, 2 decay, 3 release */
    int32_t e[2];                               /* ENV 1, 2, Q30 */
    int32_t pitch;                              /* where the glide is, semitones Q20 */
    int32_t press;                              /* pressure, smoothed (15 ms), Q15 */
    int32_t gl, gr;                             /* the output gains at the end of the last control block, Q15 */
    int32_t s[4];                               /* ladder states, Q24 */
} zvoice_t;

static zvoice_t zv[ZV];
static uint32_t zrand = 0x1234567u;

static const uint32_t ZINC_C9[12] = {           /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
ZFAST static uint32_t zinc(int note)                  /* a MIDI note's phase step */
{
    if (note < 0) note = 0;
    if (note > 131) note = 131;
    return ZINC_C9[note % 12] >> (10 - note / 12);
}
ZFAST static uint32_t zinc_q(int32_t s)               /* a pitch in semitones Q10 -> phase step (<= 0.45 fs) */
{
    uint32_t x, i, r, base;
    int32_t e;
    if (s < 0) s = 0;
    if (s >= (130 << 10)) s = 130 << 10;
    base = zinc(s >> 10);
    x = ((uint32_t)(s & 1023) * 87381u) >> 10;   /* the fraction of a semitone, in 1/2^20 octaves */
    i = x >> 10;
    e = ZT_EXP2F[i] + (int32_t)(((ZT_EXP2F[i + 1u] - ZT_EXP2F[i]) * (int32_t)(x & 1023u)) >> 10);
    r = (uint32_t)(((uint64_t)base * (uint32_t)e) >> 30);
    return r > 1932735283u ? 1932735283u : r;
}

static inline __attribute__((always_inline)) int32_t ztanh(int32_t x)                 /* Voice.h fastTanh: Q24 in, Q24 out, from the table */
{
    uint32_t u, i, f;
    if (x >= (4 << 24)) return ZT_TANH[1024];
    if (x <= -(4 << 24)) return ZT_TANH[0];
    u = (uint32_t)(x + (4 << 24));
    i = u >> 17;
    f = u & 0x1FFFFu;
    return ZT_TANH[i] + (((ZT_TANH[i + 1] - ZT_TANH[i]) * (int32_t)(f >> 5)) >> 12);   /* 32-bit: |step| < 2^18 */
}

static inline __attribute__((always_inline)) int32_t zsin(uint32_t ph)                /* a full cycle over 2^32 -> Q15 */
{
    uint32_t i = ph >> 22;
    int32_t f = (int32_t)((ph >> 7) & 0x7FFFu);
    return ZT_SIN[i] + (((ZT_SIN[i + 1] - ZT_SIN[i]) * f) >> 15);
}

#include "zen_mod.h"

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
ZFAST static void zosc_block(int32_t wave, uint32_t *php, uint32_t inc, uint32_t dt, uint32_t invdt, int32_t shape,
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
        int32_t mem = *fbp;
        for (i = 0; i < ZBLK; i++, ph += inc) {
            int32_t s = zsin(ph + (uint32_t)(mem * kfm));
            mem = (mem + s) >> 1;
            out[i] += (s * g) >> 15;
        }
        *fbp = mem;
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

ZFAST static void limiter(int32_t *L, int32_t *R, uint32_t n)
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

#define ZKILL 27638143                          /* the guard's fade: -40 dB in 4 ms, Q30 */

/* ---- the CPU guard: when a block took too long, fade one voice out (a let-go tail first, the quietest; then the
 * oldest chord note; then the oldest note). Returns 1 if it found one. Runs in the audio interrupt. */
ZFAST static uint32_t zguard_shed(void)
{
    uint32_t v, pick = ZV, best = 0xFFFFFFFFu;
    for (v = 0; v < ZV; v++) {
        uint32_t score;
        if (!zv[v].stage || zv[v].kill) continue;
        score = zv[v].stage == 3 ? (uint32_t)zv[v].env >> 3
              : (zv[v].chord ? 0x40000000u : 0x80000000u) + (zv[v].age & 0x3FFFFFFFu);
        if (score < best) { best = score; pick = v; }
    }
    if (pick == ZV) return 0;
    zv[pick].kill = 1;
    return 1;
}

/* ---- the modulation of the global destinations (LFO rates, effect mixes): from the global sources, and the newest
 * voice for the per-voice ones (Engine.h controlTick). Once per audio block, before zm_block. */
static const int16_t ZV_PAN[ZV] = {-154, 154, -461, 461, -307, 307};   /* Engine.h voicePan, Q10 */
ZFAST static int32_t zv_src(const zvoice_t *v, uint32_t s, uint32_t sb)    /* a source for one voice, Q15 */
{
    int32_t k;
    switch (s) {
    case ZS_ENV1: return v->e[0] >> 15;
    case ZS_ENV2: return v->e[1] >> 15;
    case ZS_PRESS: return v->press;
    case ZS_VEL: return v->velq;
    case ZS_KEY:                                /* (pitch - 60) / 24, -1..1 */
        k = (((v->pitch >> 10) - (60 << 10)) * 4) / 3;
        return k > 32768 ? 32768 : k < -32768 ? -32768 : k;
    default: return zm_gsrc(s, sb);
    }
}
ZFAST static void zm_global(void)
{
    uint32_t v, k, best = 0;
    const zvoice_t *nv = 0;
    for (v = 0; v < ZV; v++)
        if (zv[v].stage && (!nv || zv[v].age - best < 0x80000000u)) { nv = &zv[v]; best = zv[v].age; }
    for (k = 0; k < ZD_N; k++) zm_g[k] = 0;
    for (k = 0; k < zm_gn; k++) {
        int32_t sv = nv ? zv_src(nv, zm_gs[k].src, ZSUB - 1u) : zm_gsrc(zm_gs[k].src, ZSUB - 1u);
        zm_g[zm_gs[k].dst] += (zm_gs[k].amt * sv) >> 15;
    }
}

ZFAST static int32_t zclamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

/* ---- one voice, n frames (n a multiple of ZBLK; ZSUB blocks at most), added into mixL / mixR (Q15) */
ZFAST static void zv_render(uint32_t vi, int32_t *mixL, int32_t *mixR, uint32_t n)
{
    zvoice_t *v = &zv[vi];
    uint32_t b, i, ph, pa0, pa1, pa2, j;
    int32_t env = v->env, s0, s1, s2, s3, fb0, fb1, fb2, ablk[ZBLK], gl = v->gl, gr = v->gr;
    uint8_t stage = v->stage;

    if (v->trig != v->seen) {                   /* a new note: from silence, a fresh start */
        v->seen = v->trig;
        if (!stage) {
            v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
            zrand = zrand * 1664525u + 1013904223u;
            v->ph = zrand;                      /* free-running feel: random start phases */
            for (i = 0; i < 3u; i++) { zrand = zrand * 1664525u + 1013904223u; v->pa[i] = zrand; v->fb[i] = 0; }
            env = 0;
            v->e[0] = v->e[1] = 0;
            v->press = 0;
            gl = gr = 0;
            v->pitch = zp_glide ? v->from : (int32_t)v->note << 20;
        }
        if (!zp_glide) v->pitch = (int32_t)v->note << 20;
        stage = 1;
        v->es[0] = v->es[1] = 1;
        v->kill = 0;
    } else if (v->kill && stage)
        stage = 4;
    else if (!v->gate && (stage == 1 || stage == 2)) {
        stage = 3;
        if (v->es[0]) v->es[0] = 3;
        if (v->es[1]) v->es[1] = 3;
    }
    if (!stage) { v->env = 0; v->kill = 0; return; }

    ph = v->ph; pa0 = v->pa[0]; pa1 = v->pa[1]; pa2 = v->pa[2];
    fb0 = v->fb[0]; fb1 = v->fb[1]; fb2 = v->fb[2];
    s0 = v->s[0]; s1 = v->s[1]; s2 = v->s[2]; s3 = v->s[3];
    for (b = 0; b < n; b += ZBLK) {
        int32_t mm[ZD_N], c, k, G, G2, G3, G4, omg, c0, c1, c2, c3, inv, comp, ff, res, drive, lev, semis, pan, g;
        int32_t wave = zp_awave, shape, pw, foldm, foldk, kfm, side, anorm, kA, kB, dq, bwarp, din, glT, grT, dgl, dgr;
        uint32_t D, posq, f0, lv = 0, inc, ia0, ia1, ia2, dt0, dt1, dt2, iv0, iv1, iv2, ws1, ws2, wk32;
        uint32_t tb = (uint32_t)zp_btable & 3u, sb = b / ZBLK;
        int32_t warpK;
        uint64_t effinc;
        const int16_t *t0, *t1;

        /* control rate: the mod envelopes, pressure and glide, then the slots (Engine.h renderVoice) */
        for (j = 0; j < 2u; j++) {
            int32_t e = v->e[j];
            if (v->es[j] == 1) {
                e += M30(1395864371 - e, ze_att[j]);
                if (e >= ONE30) { e = ONE30; v->es[j] = 2; }
            } else if (v->es[j] == 2)
                e += M30(ze_sus[j] - e, ze_dec[j]);
            else if (v->es[j] == 3) {
                e += M30(-e, ze_rel[j]) - 1;
                if (e < 107374) { e = 0; v->es[j] = 0; }
            }
            v->e[j] = e;
        }
        v->press += ((v->press_t - v->press) * 783) >> 15;
        if (zp_glide) v->pitch += M30(((int32_t)v->note << 20) - v->pitch, zp_glide);
        for (j = 0; j < ZD_N; j++) mm[j] = 0;
        for (j = 0; j < zm_vn; j++)
            mm[zm_vs[j].dst] += (zm_vs[j].amt * zv_src(v, zm_vs[j].src, sb)) >> 15;   /* Q10 */

        /* the pitches: the glide, the pitch slot (+-12 semitones) and the bend; A adds TUNE and A TUNE (+-24) */
        semis = (v->pitch >> 10) + mm[ZD_PITCH] * 12 + zm_bend;
        inc = zinc_q(semis);
        ia0 = zinc_q(semis + (zp_atune << 10) + mm[ZD_ATUNE] * 24);

        /* the filter: CUTOFF, its slots, ENV 1 x ENV AMOUNT (+-0.6), key tracking (Voice.h Ladder) */
        c = zp_cut + mm[ZD_CUT] + ((((zp_envamt * 19661) >> 15) * (v->e[0] >> 15)) >> 15)
          + (((((v->pitch >> 10) - (60 << 10)) * zp_ktrack) >> 10) * 557 >> 16);
        c = zclamp(c, 0, 1024);
        res = zclamp(zp_res + mm[ZD_RES], 0, 1024);
        k = (int32_t)(((int64_t)res * 66270167) >> 10);              /* res x 3.95, Q24 */
        G = ZT_G[c];
        G2 = M30(G, G); G3 = M30(G2, G); G4 = M30(G3, G);
        omg = ONE30 - G;
        c0 = M30(G3, omg); c1 = M30(G2, omg); c2 = M30(G, omg); c3 = omg;
        D = 32768u + (uint32_t)(((int64_t)k * G4) >> 39);           /* 1 + k G^4, Q15 */
        inv = (int32_t)((0x40000000u / D) << 15);                    /* 1 / (1 + k G^4), Q30 */
        comp = 4096 + (int32_t)((((int64_t)k >> 12) * 2253) >> 12); /* 1 + 0.55 k, Q12 */
        drive = zclamp(zp_drive + mm[ZD_DRIVE], 0, 1024);
        din = ZT_DRV_IN[drive];                                      /* into the ladder, Q12 (1 at DRIVE 0) */

        /* Engine B: which two frames, and how far between them; WARP */
        posq = (uint32_t)zclamp(zp_pos + mm[ZD_BPOS], 0, 1024) * (ZT_FRAMES - 1u);   /* frames, Q10 */
        f0 = posq >> 10;
        if (f0 > ZT_FRAMES - 2u) f0 = ZT_FRAMES - 2u;
        ff = (int32_t)((posq - (f0 << 10)) << 5);                    /* crossfade, Q15 */
        bwarp = zclamp(zp_bwarp + mm[ZD_BWARP], 0, 1024) << 5;       /* Q15 */
        warpK = 16384 - ((14746 * bwarp) >> 15);                     /* 0.5 - 0.45 warp, Q15 */
        ws1 = 0x40000000u / (uint32_t)warpK;                         /* 0.5 / warpK, Q16 */
        ws2 = 0x40000000u / (uint32_t)(32768 - warpK);               /* 0.5 / (1 - warpK), Q16 */
        wk32 = (uint32_t)warpK << 17;
        effinc = ((uint64_t)inc * ws1) >> 16;                          /* warp speeds the first half up */
        while (lv < ZT_LEVELS - 1u && (ZT_MAXH >> lv) * effinc > 1932735283ull)   /* harmonics under 0.45 fs */
            lv++;
        t0 = ZT_TAB[tb][f0][lv];
        t1 = ZT_TAB[tb][f0 + 1u][lv];
        lev = zclamp(zp_blev + mm[ZD_BLEVEL], 0, 1024);
        kB = (int32_t)(((int64_t)ZT_XGAIN * (lev << 5)) >> 15);      /* B: x 0.15 */
        kB = (kB * din) >> 12;

        /* Engine A: three pitches (centre, and the side pair DETUNE spreads), each <= 0.45 fs */
        dq = zclamp(zp_adet + mm[ZD_ADET], 0, 1024);
        shape = zclamp(zp_ashape + mm[ZD_ASHAPE], 0, 1024) << 5;     /* Q15 */
        ia1 = (uint32_t)(((uint64_t)ia0 * (uint32_t)ZT_DET[dq][0]) >> 30);
        ia2 = (uint32_t)(((uint64_t)ia0 * (uint32_t)ZT_DET[dq][1]) >> 30);
        if (ia1 > 1932735283u) ia1 = 1932735283u;
        if (ia2 > 1932735283u) ia2 = 1932735283u;
        dt0 = (ia0 >> 8) | 1u; dt1 = (ia1 >> 8) | 1u; dt2 = (ia2 >> 8) | 1u;
        iv0 = 0x80000000u / dt0; iv1 = 0x80000000u / dt1; iv2 = 0x80000000u / dt2;
        side = ZT_DET[dq][2]; anorm = ZT_DET[dq][3];
        pw = 16384 - ((13763 * shape) >> 15);                        /* 0.5 - 0.42 shape, Q15 */
        foldm = 32768 + 3 * shape;                                   /* 1 + 3 shape, Q15 */
        foldk = shape >= 8192 ? 32768 : shape * 4;                   /* min(1, 4 shape), Q15 */
        kfm = (int32_t)(((int64_t)shape * 33373) >> 15);             /* shape x 1.6 / 2 pi, as a phase */
        lev = zclamp(zp_alev + mm[ZD_ALEVEL], 0, 1024);
        kA = (int32_t)(((int64_t)9830 * (lev << 5)) >> 15);          /* A into the ladder: x 0.3 */
        kA = (int32_t)(((int64_t)kA * anorm) >> 15);
        kA = (kA * din) >> 12;
        side = (side * 19661) >> 15;                                 /* the side pair at 0.6 each, mono */

        for (i = 0; i < ZBLK; i++) ablk[i] = 0;
        if (kA) {
            zosc_block(wave, &pa0, ia0, dt0, iv0, shape, pw, foldm, foldk, kfm, &fb0, ablk, 32768);
            if (side) {
                zosc_block(wave, &pa1, ia1, dt1, iv1, shape, pw, foldm, foldk, kfm, &fb1, ablk, side);
                zosc_block(wave, &pa2, ia2, dt2, iv2, shape, pw, foldm, foldk, kfm, &fb2, ablk, side);
            } else { pa1 += ia1 * ZBLK; pa2 += ia2 * ZBLK; }
        } else { pa0 += ia0 * ZBLK; pa1 += ia1 * ZBLK; pa2 += ia2 * ZBLK; }
        kA >>= 4; kB >>= 4;                     /* (so a x kA + w x kB stays in 32 bits at full DRIVE) */

        /* the output: velocity (1 - VEL SENS (1 - vel)), the AMP slot (x 0..2), DRIVE's make-up and the pan,
         * as two gains (Q15) that glide across the block */
        g = 32768 - ((zp_velsens * (32768 - v->velq)) >> 10);
        g = (g * zclamp(1024 + mm[ZD_AMP], 0, 2048)) >> 10;
        g = (g * ZT_DRV_OUT[drive]) >> 14;
        pan = zclamp(((ZV_PAN[vi] * zp_spread) >> 10) + mm[ZD_PAN], -1024, 1024);
        glT = (g * ZT_PANLAW[(pan + 1024) >> 1][0]) >> 14;
        grT = (g * ZT_PANLAW[(pan + 1024) >> 1][1]) >> 14;
        dgl = (glT - gl) / (int32_t)ZBLK; dgr = (grT - gr) / (int32_t)ZBLK;

        for (i = b; i < b + ZBLK; i++) {
            uint32_t bph = !bwarp ? ph : ph < wk32 ? (uint32_t)(((uint64_t)ph * ws1) >> 16)
                        : 0x80000000u + (uint32_t)(((uint64_t)(ph - wk32) * ws2) >> 16);
            uint32_t ix = bph >> 23, ix2 = (ix + 1u) & (ZT_SIZE - 1u);
            int32_t fr = (int32_t)((bph >> 8) & 0x7FFFu), a0, a1, w = 0, a = ablk[i - b], x, sig, y4, u, vv, y, el;
            if (kB) {
                a0 = t0[ix] + (((t0[ix2] - t0[ix]) * fr) >> 15);
                a1 = t1[ix] + (((t1[ix2] - t1[ix]) * fr) >> 15);
                w = a0 + (((a1 - a0) * ff) >> 15);                 /* Q15 */
            }
            ph += inc;
            x = (a * kA + w * kB) >> 2;                             /* Q15 -> Q24, with the gains */

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
                env += M30(-env, stage == 4 ? ZKILL : zc_rel) - 1;
                if (env < 107374) { env = 0; stage = 0; }           /* 1e-4 */
            }

            /* the envelope (Q14) x the gains (Q15) -> Q15; then Q24 x Q15 -> Q15 */
            gl += dgl; gr += dgr;
            el = env >> 16;
            mixL[i] += (int32_t)(((int64_t)y * ((el * gl) >> 14)) >> 24);
            mixR[i] += (int32_t)(((int64_t)y * ((el * gr) >> 14)) >> 24);
        }
        gl = glT; gr = grT;
        if (!stage) break;
    }
    v->ph = ph; v->pa[0] = pa0; v->pa[1] = pa1; v->pa[2] = pa2;
    v->fb[0] = fb0; v->fb[1] = fb1; v->fb[2] = fb2;
    v->s[0] = s0; v->s[1] = s1; v->s[2] = s2; v->s[3] = s3;
    v->env = env;
    v->gl = gl; v->gr = gr;
    v->stage = stage;
}

/* ---- settings from the main loop (knobs Q10) */
static void zen_set_env(int32_t a, int32_t d, int32_t s, int32_t r)
{
    zc_att = ZT_ENV_A[a]; zc_dec = ZT_ENV_DR[d]; zc_rel = ZT_ENV_DR[r];
    zc_sus = s << 20;
}
static void zen_set_modenv(uint32_t j, int32_t a, int32_t d, int32_t s, int32_t r)   /* ENV 1 (j 0), ENV 2 (j 1) */
{
    ze_att[j] = ZT_ENV_A16[a]; ze_dec[j] = ZT_ENV_DR16[d]; ze_rel[j] = ZT_ENV_DR16[r];
    ze_sus[j] = s << 20;
}
#endif
