/* SPDX-License-Identifier: GPL-3.0-only */
/* Zen-Lite's parameters: what a knob shows and holds (0..127 unless a list), in the order of the pages, four to a
 * page. Shared by the firmware (zenlite.c) and the preset converter (tools/zen_presets.cpp), so a preset's bytes
 * always land on the right knobs. C and C++. */
#ifndef ZEN_PARAMS_H
#define ZEN_PARAMS_H
#include <stdint.h>

enum {
    P_PA, P_PB, P_PC, P_PD,                     /* HOME: the plates (macros) A-D */
    P_CUT, P_RES, P_DRIVE, P_ENVAMT,            /* FILTER */
    P_WAVE, P_SHAPE, P_DET, P_ALEV,             /* ENGINE A */
    P_TABLE, P_POS, P_WARP, P_BLEV,             /* ENGINE B */
    P_TUNE, P_GLIDE, P_VELS, P_VOL,             /* VOICE */
    P_ATT, P_DEC, P_SUS, P_REL,                 /* AMP ENV */
    P_E1A, P_E1D, P_E1S, P_E1R,                 /* ENV 1 */
    P_E2A, P_E2D, P_E2S, P_E2R,                 /* ENV 2 */
    P_L1RATE, P_L1SHAPE, P_L2RATE, P_L2SHAPE,   /* LFO */
    P_RRATE, P_RSMOOTH, P_KTRACK, P_WIDTH,      /* RANDOM + KEY */
    P_CHORUS, P_REVERB, P_RDECAY, P_SHIMMER,    /* FX */
    P_SIZE, P_DAMP, P_DEPTH, P_TONE,            /* SPACE */
    P_DTIME, P_DFB, P_DTONE, P_DELAY,           /* DELAY */
    P_KEY, P_SCALE, P_KEYS, P_SPREAD,           /* KEY + SCALE */
    P_N,
    P_SOUND_FIRST = P_CUT, P_SOUND_END = P_KEY  /* a preset sets these, all but SPEAKER (not the plates, key, scale) */
};
#define ZM_NSLOTS 12

typedef struct { const char *name; uint8_t lo, hi, def; } param_t;
static const param_t PARAM[P_N] = {
    {"BLOOM", 0, 127, 0}, {"MORPH", 0, 127, 0}, {"ECHOES", 0, 127, 0}, {"SPACE", 0, 127, 0},
    {"CUTOFF", 0, 127, 79}, {"RESONANCE", 0, 127, 38}, {"DRIVE", 0, 127, 0}, {"ENV AMT", 0, 127, 64},
    {"WAVE", 0, 3, 0}, {"SHAPE", 0, 127, 0}, {"DETUNE", 0, 127, 25}, {"LEVEL", 0, 127, 127},
    {"TABLE", 0, 3, 1}, {"POSITION", 0, 127, 57}, {"WARP", 0, 127, 0}, {"LEVEL", 0, 127, 127},
    {"TUNE", 0, 48, 24}, {"GLIDE", 0, 127, 0}, {"VEL SENS", 0, 127, 64}, {"VOLUME", 0, 127, 109},
    {"ATTACK", 0, 127, 38}, {"DECAY", 0, 127, 64}, {"SUSTAIN", 0, 127, 102}, {"RELEASE", 0, 127, 76},
    {"ATTACK", 0, 127, 6}, {"DECAY", 0, 127, 57}, {"SUSTAIN", 0, 127, 38}, {"RELEASE", 0, 127, 57},
    {"ATTACK", 0, 127, 64}, {"DECAY", 0, 127, 76}, {"SUSTAIN", 0, 127, 76}, {"RELEASE", 0, 127, 76},
    {"1 RATE", 0, 127, 44}, {"1 SHAPE", 0, 4, 0}, {"2 RATE", 0, 127, 25}, {"2 SHAPE", 0, 4, 4},
    {"RANDOM", 0, 127, 51}, {"SMOOTH", 0, 127, 64}, {"KEY TRACK", 0, 127, 64}, {"STEREO", 0, 127, 64},
    {"CHORUS", 0, 127, 32}, {"REVERB", 0, 127, 38}, {"DECAY", 0, 127, 64}, {"SHIMMER", 0, 127, 0},
    {"SIZE", 0, 127, 76}, {"DAMP", 0, 127, 64}, {"DEPTH", 0, 127, 64}, {"SPEAKER", 0, 2, 0},
    {"TIME", 0, 127, 57}, {"FEEDBACK", 0, 127, 44}, {"TONE", 0, 127, 64}, {"MIX", 0, 127, 0},
    {"KEY", 0, 11, 2}, {"SCALE", 1, 10, 3}, {"KEYS", 0, 1, 0}, {"SPREAD", 0, 1, 0},
};
/* the plate kit (Presets.h kit()): slots 1-7 in every preset; a preset's own go in 8-12. Sources and
 * destinations are numbered as zen_mod.h's ZS_ / ZD_ (the plugin's order); amounts Q10 (1024 = 100 %). */
typedef struct { uint8_t src, dst; int16_t amt; } zps_t;
static const zps_t ZKIT[7] = {
    {6, 11, 256},                               /* pressure -> cutoff 25 % */
    {10, 11, 358},                              /* A BLOOM  -> cutoff 35 % */
    {11, 6, 512},                               /* B MORPH  -> B position 50 % */
    {12, 19, 512},                              /* C ECHOES -> delay mix 50 % */
    {13, 20, 512},                              /* D SPACE  -> reverb mix 50 % */
    {8, 11, 307},                               /* mod wheel -> cutoff 30 % */
    {9, 11, 205},                               /* channel pressure -> cutoff 20 % */
};
typedef struct { const char *name, *cat; uint8_t p[P_N]; zps_t s[ZM_NSLOTS]; } zpreset_t;
#endif
