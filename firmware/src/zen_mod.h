/* SPDX-License-Identifier: GPL-3.0-only */
/* Modulation, after Zenthesizer's (Settings.h, Engine.h controlTick / voiceSrc / globalSrc):
 *   sources       ENV 1, ENV 2, LFO 1, LFO 2, RANDOM, PRESSURE, VELOCITY, WHEEL, CHANNEL PRESSURE,
 *                 PLATES A-D, KEY, SLIDE (MIDI CC 74)
 *   destinations  pitch, A tune / detune / shape / level, B position / warp / level, cutoff, resonance, drive,
 *                 amp, pan, and the global ones: LFO 1 and 2 rate, chorus, delay and reverb mix
 *   12 slots      source -> destination x amount; slots 1-7 hold the plate kit, as in the plugin
 * The enums keep the plugin's order, so its presets convert one to one (B UNISON and AIR have no effect here).
 * Everything runs at the control rate: once per 16 frames (2756 times a second). Integers only. */
#ifndef ZEN_MOD_H
#define ZEN_MOD_H
#include "zen_tables.h"   /* (included by zen_voice.h, after zsin) */

enum { ZS_NONE, ZS_ENV1, ZS_ENV2, ZS_LFO1, ZS_LFO2, ZS_RAND, ZS_PRESS, ZS_VEL, ZS_WHEEL, ZS_CHANAT,
       ZS_PLATEA, ZS_PLATEB, ZS_PLATEC, ZS_PLATED, ZS_KEY, ZS_SLIDE, ZS_N };
enum { ZD_NONE, ZD_PITCH, ZD_ATUNE, ZD_ADET, ZD_ASHAPE, ZD_ALEVEL, ZD_BPOS, ZD_BWARP, ZD_BUNISON, ZD_BLEVEL, ZD_AIR,
       ZD_CUT, ZD_RES, ZD_DRIVE, ZD_AMP, ZD_PAN, ZD_LFO1RATE, ZD_LFO2RATE, ZD_CHORUS, ZD_DELAY, ZD_REVERB, ZD_N };
#define ZD_GLOBAL(d) ((d) >= ZD_LFO1RATE)
#define ZM_SLOTS 12
#define ZSUB 16u                                /* control blocks per audio block (256 / 16) */

typedef struct { uint8_t src, dst; int16_t amt; } zslot_t;   /* amt: -1024..1024 (= -1..1) */

/* ---- what the main loop sets */
static volatile zslot_t zm_slot[ZM_SLOTS];
static volatile int32_t zm_lfo_rate[2] = {359, 410}; /* LFO 1, 2 rate knobs, Q10 */
static volatile int32_t zm_lfo_shape[2] = {0, 4};    /* sine, triangle, saw, square, drift */
static volatile int32_t zm_rand_rate = 410;          /* Q10 */
static volatile int32_t zm_rand_coef = 1638;         /* the random's smoothing per control block, Q15 */
static volatile int32_t zm_plate_t[4];          /* the plates' knob positions, Q15 */
static volatile int32_t zm_wheel, zm_chanat, zm_slide;   /* MIDI mod wheel, channel pressure, CC 74 (slide), Q15 */

/* ---- worked out each audio block, for its 16 control blocks */
static int32_t zm_lfo[2][ZSUB], zm_rnd[ZSUB], zm_plate[4][ZSUB];
static int32_t zm_g[ZD_N];                      /* the global destinations' modulation, Q10 */
static uint32_t zm_lfo_ph[2], zm_rnd_ph;
static int32_t zm_rnd_held, zm_rnd_out, zm_plate_now[4];
static struct { uint32_t count, period, step; int32_t from, to; } zm_drift[2];
static uint32_t zm_seed = 0x2545F491u;
ZFAST static int32_t zm_random(void)                  /* -1..1, Q15 */
{
    zm_seed = zm_seed * 1664525u + 1013904223u;
    return (int32_t)(zm_seed >> 16) - 32768;
}

ZFAST static uint32_t zm_exp2_mul(uint32_t x, int32_t oct_q10)   /* x times 2^(oct_q10 / 1024); x < 2^31 */
{
    int32_t o = oct_q10 >> 10;                  /* (no 64-bit shifts by a variable: they would need libgcc) */
    uint32_t y = (uint32_t)(((uint64_t)x * (uint32_t)ZT_EXP2F[oct_q10 & 1023]) >> 30);
    if (o > 0) return o > 30 || y > (0xFFFFFFFFu >> o) ? 0xFFFFFFFFu : y << o;
    return o < -31 ? 0u : y >> -o;
}

ZFAST static int32_t zm_lfo_value(uint32_t k, uint32_t inc)   /* one LFO, one control block on (Engine.h lfoShape) */
{
    uint32_t ph = zm_lfo_ph[k] += inc;
    int32_t p = (int32_t)(ph >> 17);            /* Q15 */
    switch (zm_lfo_shape[k]) {
    case 0: return zsin(ph);
    case 1: return 32768 - 4 * (p > 16384 ? p - 16384 : 16384 - p);
    case 2: return 32768 - 2 * p;
    case 3: return p < 16384 ? 32767 : -32768;
    default: {                                  /* drift: a new random point each cycle, eased toward (Common.h Wander) */
        int32_t t, s;
        if (++zm_drift[k].count >= zm_drift[k].period) {
            zm_drift[k].count = 0;
            zm_drift[k].period = inc ? 0xFFFFFFFFu / inc : 1u;
            if (!zm_drift[k].period) zm_drift[k].period = 1;
            zm_drift[k].step = 0x80000000u / zm_drift[k].period;   /* so t = count x step / 2^16, no overflow */
            zm_drift[k].from = zm_drift[k].to;
            zm_drift[k].to = zm_random();
        }
        t = (int32_t)((zm_drift[k].count * zm_drift[k].step) >> 16);
        s = (int32_t)(((int64_t)t * t >> 15) * (98304 - 2 * t) >> 15);   /* t^2 (3 - 2t) */
        return zm_drift[k].from + (int32_t)(((int64_t)(zm_drift[k].to - zm_drift[k].from) * s) >> 15);
    }
    }
}

/* the slots in use, split into per-voice and global destinations (copied once per audio block) */
static zslot_t zm_vs[ZM_SLOTS], zm_gs[ZM_SLOTS];
static uint32_t zm_vn, zm_gn;

/* the random's smoothing (Engine.h: 2 ms + SMOOTH / rate, as a one-pole per control block), from the main loop */
static void zm_set_rand(int32_t rate_q10, int32_t smooth_q10)
{
    uint32_t inc = ZT_LFO_INC[rate_q10 < 0 ? 0 : rate_q10 > 1024 ? 1024 : rate_q10], cyc, S, x;
    cyc = inc ? (0xFFFFFFFFu / inc) * ZSUB : 882000u;          /* frames per cycle */
    if (cyc > 882000u) cyc = 882000u;                           /* (the rate counts as 0.05 Hz at least) */
    S = 88u + (uint32_t)(((uint64_t)cyc * (uint32_t)smooth_q10) >> 10);   /* the time constant, frames */
    x = (16u << 15) / S;                                        /* 16 / S, Q15 (<= 0.18) */
    zm_rand_rate = rate_q10;
    zm_rand_coef = (int32_t)(x - ((x * x) >> 16) + ((((x * x) >> 15) * x) / 196608u));   /* 1 - e^-x */
}

/* the global sources for the next 16 control blocks (call at the top of each audio block) */
ZFAST static void zm_block(void)
{
    uint32_t sb, k, inc[2];
    zm_vn = zm_gn = 0;
    for (k = 0; k < ZM_SLOTS; k++) {
        zslot_t sl;
        sl.src = zm_slot[k].src; sl.dst = zm_slot[k].dst; sl.amt = zm_slot[k].amt;
        if (!sl.src || sl.src >= ZS_N || !sl.dst || sl.dst >= ZD_N || !sl.amt) continue;
        if (ZD_GLOBAL(sl.dst)) zm_gs[zm_gn++] = sl;
        else zm_vs[zm_vn++] = sl;
    }
    for (k = 0; k < 2u; k++)                    /* rates, with their global modulation (x 2^(4 m)) */
        inc[k] = zm_exp2_mul(ZT_LFO_INC[zm_lfo_rate[k] < 0 ? 0 : zm_lfo_rate[k] > 1024 ? 1024 : zm_lfo_rate[k]],
                             zm_g[ZD_LFO1RATE + k] * 4);
    for (sb = 0; sb < ZSUB; sb++) {
        zm_lfo[0][sb] = zm_lfo_value(0, inc[0]);
        zm_lfo[1][sb] = zm_lfo_value(1, inc[1]);
        zm_rnd_ph += ZT_LFO_INC[zm_rand_rate];  /* smoothed random: a new value each cycle, glided to */
        if (zm_rnd_ph < ZT_LFO_INC[zm_rand_rate]) zm_rnd_held = zm_random();
        zm_rnd_out += (int32_t)(((int64_t)(zm_rnd_held - zm_rnd_out) * zm_rand_coef) >> 15);
        zm_rnd[sb] = zm_rnd_out;
        for (k = 0; k < 4u; k++) {              /* the plates glide to their knobs in ~15 ms */
            zm_plate_now[k] += ((zm_plate_t[k] - zm_plate_now[k]) * 783) >> 15;
            zm_plate[k][sb] = zm_plate_now[k];
        }
    }
}

/* a global source in control block sb, Q15 */
ZFAST static int32_t zm_gsrc(uint32_t s, uint32_t sb)
{
    switch (s) {
    case ZS_LFO1: return zm_lfo[0][sb];
    case ZS_LFO2: return zm_lfo[1][sb];
    case ZS_RAND: return zm_rnd[sb];
    case ZS_WHEEL: return zm_wheel;
    case ZS_CHANAT: return zm_chanat;
    case ZS_SLIDE: return zm_slide;
    case ZS_PLATEA: case ZS_PLATEB: case ZS_PLATEC: case ZS_PLATED: return zm_plate[s - ZS_PLATEA][sb];
    default: return 0;
    }
}
#endif
