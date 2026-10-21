/* SPDX-License-Identifier: GPL-3.0-only */
/* Zen-Lite: the Zenthesizer, cut down for the M-VAVE FM-1. Built on Dinghy (Felucca's and SLOOP's platform).
 *
 * The keyboard, in SCALE keys (the default):
 *   white keys       the notes of the key and scale, the root on the leftmost
 *   black keys 1-7   the seven chords of the key (I .. vii), as the plugin builds them
 *   black keys 8-11  held while you press a chord: 7TH, 9TH, SUS, STRUM
 * In CHROMATIC keys every key plays its own note, as a piano. SEL switches between the two.
 *
 * Pages (the four knobs), by button; pressing the button again steps through its pages:
 *   HOME   the preset's name; the plates: BLOOM, MORPH, ECHOES, SPACE (macros, through mod slots 2-5)
 *   EDIT   FILTER: CUTOFF RESONANCE DRIVE ENV AMT    ENGINE A: WAVE SHAPE DETUNE LEVEL
 *          ENGINE B: TABLE POSITION WARP LEVEL       VOICE: TUNE GLIDE VEL SENS VOLUME
 *   ENV    AMP ENV, ENV 1, ENV 2: ATTACK DECAY SUSTAIN RELEASE
 *   LFO    LFO: 1 RATE 1 SHAPE 2 RATE 2 SHAPE        RANDOM + KEY: RANDOM SMOOTH KEY TRACK STEREO
 *          MOD: SLOT (1-12) SOURCE DEST AMOUNT
 *   FX     FX: CHORUS REVERB DECAY SHIMMER           SPACE: SIZE DAMP DEPTH SPEAKER (FLAT, SOFT, LOW CUT)
 *          DELAY: TIME FEEDBACK TONE MIX
 *   GLO    KEY SCALE KEYS (scale / chromatic) SPREAD (open chord voicing)
 * PRESETS (the big encoder) steps through the presets: Zenthesizer's, converted (tools/zen_presets.cpp).
 * OCT- OCT+ the octave; MASTER the volume. USB-MIDI in: notes (chromatic, with velocity), pitch bend (+-2),
 * mod wheel, channel and poly pressure, CC 74 (slide): all of them are mod sources.
 * The screen's last lines: CPU, LIMIT, the octave, the voices sounding.
 *
 * Integers only: the chip has no FPU and no libgcc. tests/run_host.sh plays this file on a computer. */
#define APP_NAME "ZEN-LITE"
#include "platform.c"
#include "zen_voice.h"
#include "zen_chords.h"
#include "zen_fx.h"
#include "zen_params.h"
#include "zen_presets.h"
#ifdef DINGHY_HOST
#define FM1_TICKS_PER_US 24u
#endif

/* ---- the pages: four knobs each (zen_params.h); the MOD page edits the slots, not a parameter */
enum { P_MSLOT = P_N, P_MSRC, P_MDST, P_MAMT, P_NONE = 255 };
#define NPAGES 15u
static const char *const PAGE_NAME[NPAGES] = {"HOME", "FILTER", "ENGINE A", "ENGINE B", "VOICE", "AMP ENV", "ENV 1",
    "ENV 2", "LFO", "RANDOM + KEY", "MOD", "FX", "SPACE", "DELAY", "KEY + SCALE"};
#define PG_HOME 0u
#define PG_EDIT 1u                              /* 1-4 */
#define PG_ENV 5u                               /* 5-7 */
#define PG_LFO 8u                               /* 8-10 */
#define PG_MOD 10u
#define PG_FX 11u                               /* 11-13 */
#define PG_GLO 14u
static uint32_t page_param(uint32_t page, uint32_t r)
{
    if (page < PG_MOD) return page * 4u + r;
    if (page == PG_MOD) return P_MSLOT + r;
    return (page - 1u) * 4u + r;
}
static const char *const SRC_NAME[ZS_N] = {"--", "ENV 1", "ENV 2", "LFO 1", "LFO 2", "RANDOM", "PRESSURE", "VELOCITY",
    "MOD WHEEL", "CH PRESS", "BLOOM", "MORPH", "ECHOES", "SPACE", "KEY", "SLIDE"};
static const char *const DST_NAME[ZD_N] = {"--", "PITCH", "A TUNE", "A DETUNE", "A SHAPE", "A LEVEL", "B POSITION",
    "B WARP", "-", "B LEVEL", "-", "CUTOFF", "RESONANCE", "DRIVE", "AMP", "PAN", "LFO 1 RATE", "LFO 2 RATE",
    "CHORUS", "DELAY", "REVERB"};
#define DST_SKIP(d) ((d) == ZD_BUNISON || (d) == ZD_AIR)   /* the plugin's, without an engine here */

#define SET_VER 5u
typedef struct { uint8_t ver; int8_t oct; uint16_t preset; uint8_t p[P_N]; zps_t slot[ZM_NSLOTS]; } set_t;
static set_t set;
static uint32_t mod_sel = 7;                    /* the MOD page's slot (0-11): the first after the plate kit */

static int32_t q10(uint32_t k) { return (int32_t)(k * 1024u / 127u); }
static int32_t bip10(uint32_t k)                /* a bipolar knob: 64 is 0, -1024..1024 */
{
    int32_t b = ((int32_t)k - 64) * 1024 / 63;
    return b < -1024 ? -1024 : b > 1024 ? 1024 : b;
}
static volatile int32_t zl_vol = 16384;         /* VOLUME, Q14 */

static void apply(void)
{
    uint32_t k;
    const uint8_t *p = set.p;
    zp_cut = q10(p[P_CUT]); zp_res = q10(p[P_RES]); zp_drive = q10(p[P_DRIVE]); zp_envamt = bip10(p[P_ENVAMT]);
    zp_awave = p[P_WAVE]; zp_ashape = q10(p[P_SHAPE]); zp_adet = q10(p[P_DET]); zp_alev = q10(p[P_ALEV]);
    zp_btable = p[P_TABLE]; zp_pos = q10(p[P_POS]); zp_bwarp = q10(p[P_WARP]); zp_blev = q10(p[P_BLEV]);
    zp_atune = (int32_t)p[P_TUNE] - 24; zp_glide = ZT_GLIDE[p[P_GLIDE] & 127]; zp_velsens = q10(p[P_VELS]);
    zl_vol = ZT_VOL[p[P_VOL] & 127];
    zen_set_env(q10(p[P_ATT]), q10(p[P_DEC]), q10(p[P_SUS]), q10(p[P_REL]));
    zen_set_modenv(0, q10(p[P_E1A]), q10(p[P_E1D]), q10(p[P_E1S]), q10(p[P_E1R]));
    zen_set_modenv(1, q10(p[P_E2A]), q10(p[P_E2D]), q10(p[P_E2S]), q10(p[P_E2R]));
    zm_lfo_rate[0] = q10(p[P_L1RATE]); zm_lfo_shape[0] = p[P_L1SHAPE];
    zm_lfo_rate[1] = q10(p[P_L2RATE]); zm_lfo_shape[1] = p[P_L2SHAPE];
    zm_set_rand(q10(p[P_RRATE]), q10(p[P_RSMOOTH]));
    zp_ktrack = q10(p[P_KTRACK]); zp_spread = q10(p[P_WIDTH]);
    zf_chorus = p[P_CHORUS]; zf_depth = p[P_DEPTH];
    zf_reverb = p[P_REVERB]; zf_decay = p[P_RDECAY]; zf_shimmer = p[P_SHIMMER];
    zf_size = p[P_SIZE]; zf_damp = p[P_DAMP]; zf_tone = p[P_TONE];
    zf_dtime = p[P_DTIME]; zf_dfb = p[P_DFB]; zf_dtone = p[P_DTONE]; zf_delay = p[P_DELAY];
    for (k = 0; k < 4u; k++) zm_plate_t[k] = p[P_PA + k] * 258;
    for (k = 0; k < ZM_NSLOTS; k++) {
        zm_slot[k].src = set.slot[k].src; zm_slot[k].dst = set.slot[k].dst; zm_slot[k].amt = set.slot[k].amt;
    }
    rv_set();
}

static void load_preset(uint32_t i)            /* the sound of preset i; the plates back to 0, key and scale kept */
{
    const zpreset_t *pr = &ZPRESET[i % ZPRESETS];
    uint32_t k;
    for (k = P_SOUND_FIRST; k < P_SOUND_END; k++)
        if (k != P_TONE) set.p[k] = pr->p[k];
    for (k = 0; k < 4u; k++) set.p[P_PA + k] = 0;
    for (k = 0; k < ZM_NSLOTS; k++) set.slot[k] = pr->s[k];
    set.preset = (uint16_t)(i % ZPRESETS);
    apply();
}

/* ---- the sound */
static int32_t mixL[HALF_FRAMES], mixR[HALF_FRAMES];
#define SC_N 1024u                              /* the scope's last 1024 output samples (mono) */
static int16_t sc_ring[SC_N];
static volatile uint32_t sc_w;
static volatile uint32_t cpu_ticks_max, guard_drops;
#define GUARD_TICKS (5805u * FM1_TICKS_PER_US * 85u / 100u)   /* over 85 % of a block: shed a voice */
#ifdef DINGHY_HOST
static int host_load;                           /* the host tests pretend the FM-1 is overloaded ("T load 1") */
static void host_set_load(int on) { host_load = on; }
#endif

ZFAST static void zl_audio(int32_t *out, uint32_t n)   /* the sound, in RAM (app_audio below calls it) */
{
    uint32_t i, v;
    int32_t vol = zl_vol;
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks();
#endif
    zm_global();                                /* the slots aimed at the LFO rates and the effect mixes */
    zf_cmod = zm_g[ZD_CHORUS] * 32; zf_dmod = zm_g[ZD_DELAY] * 32; zf_rmod = zm_g[ZD_REVERB] * 32;
    zm_block();                                 /* the LFOs, random and plates for this block's 16 control steps */
    for (i = 0; i < n; i++)                     /* (n is always HALF_FRAMES, 256: the effects count on it) */
        mixL[i] = mixR[i] = 0;
    for (v = 0; v < ZV; v++)
        zv_render(v, mixL, mixR, n);
    chorus(mixL, mixR, n);                      /* the plugin's order: voices, chorus, delay, reverb, volume, limiter */
    delay(mixL, mixR, n);
    reverb(mixL, mixR, n);
    if (vol != 16384)
        for (i = 0; i < n; i++) {
            mixL[i] = (int32_t)(((int64_t)mixL[i] * vol) >> 14);
            mixR[i] = (int32_t)(((int64_t)mixR[i] * vol) >> 14);
        }
    tone(mixL, mixR, n);
    limiter(mixL, mixR, n);
    for (i = 0; i < n; i++) {
        out[2u * i] += mixL[i];
        out[2u * i + 1u] += mixR[i];
        sc_ring[(sc_w + i) & (SC_N - 1u)] = (int16_t)((mixL[i] + mixR[i]) >> 1);   /* for the scope */
    }
    sc_w = (sc_w + n) & (SC_N - 1u);
    {
        static uint32_t hold;                   /* one voice per 8 blocks (46 ms): a fade takes 4 ms to free the time */
#ifdef DINGHY_HOST
        uint32_t dt = host_load ? GUARD_TICKS + 1u : 0u;
#else
        uint32_t dt = fm1_ticks() - t0;
#endif
        if (dt > cpu_ticks_max) cpu_ticks_max = dt;
        if (hold) hold--;
        else if (dt > GUARD_TICKS && zguard_shed()) { guard_drops++; hold = 8u; }
    }
}

/* the platform's audio interrupt (in the flash) calls this; a direct call cannot reach the RAM code (the jump's range
 * is too short), a call through a pointer can */
static void (*volatile zl_audio_fn)(int32_t *, uint32_t) = zl_audio;
static void app_audio(int32_t *out, uint32_t n) { zl_audio_fn(out, n); }

/* ---- voices: a note gets a tag, and is let go by its tag (a chord and a key may share a note) */
static uint32_t ages, tags, last_started = 60;
static void ripple_start(void);

static uint32_t play(uint32_t note, uint32_t vel, uint32_t chord)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    zvoice_t *z;
    if (note > 127u || !vel) return 0;
    for (v = 0; v < ZV; v++) {                  /* a silent voice, else the quietest let-go one, else the oldest */
        uint32_t score = !zv[v].stage && zv[v].seen == zv[v].trig ? 0u       /* idle (also one the guard faded out) */
                       : !zv[v].gate ? 1u + ((uint32_t)zv[v].env >> 3)
                       : (zv[v].chord ? 0x40000000u : 0x80000000u) + (zv[v].age & 0x3FFFFFFFu);
        if (score < best) { best = score; pick = v; }    /* (chord notes go before melody notes) */
    }
    z = &zv[pick];
    z->gate = 0;
    z->note = (uint8_t)note;
    z->chord = (uint8_t)chord;
    z->age = ++ages;
    z->tag = ++tags;
    z->velq = (int32_t)(vel * 258u);            /* Q15 */
    z->press_t = 0;
    z->from = (int32_t)(last_started << 20);    /* with GLIDE, from the last note played */
    last_started = note;
    z->gate = 1;
    z->trig++;
    ripple_start();
    return z->tag;
}
static void release_tag(uint32_t tag)
{
    uint32_t v;
    if (!tag) return;
    for (v = 0; v < ZV; v++)
        if (zv[v].tag == tag) zv[v].gate = 0;
}
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < ZV; v++)
        if (zv[v].gate || zv[v].stage) return 0;
    return 1;
}

/* ---- the keyboard: 27 keys, F3 .. G5; 16 white, 11 black */
static int8_t white_of[27], black_of[27];
static void map_keys(void)
{
    uint32_t k;
    int8_t w = 0, b = 0;
    for (k = 0; k < 27u; k++) {
        int blk = (0x54Au >> ((53u + k) % 12u)) & 1u;
        white_of[k] = blk ? -1 : w++;
        black_of[k] = blk ? b++ : -1;
    }
}

/* chords: the notes each chord key started, and strummed notes still to come */
static uint32_t chord_tag[7][6];
static struct { uint32_t at; uint8_t note, vel, chord, slot, used; } pend[12];
static int8_t last_chord = -1;
static char chord_text[32];
#define STRUM_MS 78u                            /* the plugin's STRUM at its default speed (0.4) */

static void chord_up(uint32_t d)
{
    uint32_t i;
    for (i = 0; i < 6u; i++) { release_tag(chord_tag[d][i]); chord_tag[d][i] = 0; }
    for (i = 0; i < 12u; i++) if (pend[i].used && pend[i].chord == d) pend[i].used = 0;
}

static void chord_down(uint32_t d, int m7, int m9, int sus, int strum)
{
    zchord_t c = zchord_build((int)d, set.p[P_KEY], set.p[P_SCALE], set.oct - 1, m7, m9, sus, set.p[P_SPREAD]);
    char num[8], name[16];
    uint32_t i, j;
    chord_up(d);
    for (i = 0; i < (uint32_t)c.n; i++) {
        if (!i || !strum)
            chord_tag[d][i] = play(c.notes[i], 100, d + 1u);
        else
            for (j = 0; j < 12u; j++)
                if (!pend[j].used) {
                    pend[j].at = fm1_ms + STRUM_MS * i; pend[j].note = c.notes[i]; pend[j].vel = 95;
                    pend[j].chord = (uint8_t)d; pend[j].slot = (uint8_t)i; pend[j].used = 1;
                    break;
                }
    }
    zchord_numeral((int)d, set.p[P_SCALE], num);
    zchord_name(&c, zkey_flats(set.p[P_KEY], set.p[P_SCALE]), name);
    chord_text[0] = 0;
    zcat(chord_text, num); zcat(chord_text, "  "); zcat(chord_text, name);
    last_chord = (int8_t)d;
}

static void strum_tick(void)
{
    uint32_t i;
    for (i = 0; i < 12u; i++)
        if (pend[i].used && (int32_t)(fm1_ms - pend[i].at) >= 0) {
            chord_tag[pend[i].chord][pend[i].slot] = play(pend[i].note, pend[i].vel, pend[i].chord + 1u);
            pend[i].used = 0;
        }
}

/* ---- the screen */

static void num(char *s, int32_t v)            /* v -> decimal text */
{
    char t[12];
    uint32_t i = 0, j = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    do { t[i++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) s[j++] = '-';
    while (i) s[j++] = t[--i];
    s[j] = 0;
}

static void fixed2(char *s, uint32_t v)         /* v hundredths -> "1.23" */
{
    char t[12];
    num(t, (int32_t)(v / 100u));
    zcat(s, t);
    zcat(s, ".");
    t[0] = (char)('0' + v / 10u % 10u); t[1] = (char)('0' + v % 10u); t[2] = 0;
    zcat(s, t);
}
static void percent(char *s, int32_t q)         /* Q10 -> "+35%", or "+0.6%" under 10 % */
{
    char t[12];
    int32_t m = (q * 1000 + (q < 0 ? -512 : 512)) / 1024;   /* tenths of a percent, rounded */
    uint32_t a = (uint32_t)(m < 0 ? -m : m);
    zcat(s, m < 0 ? "-" : m > 0 ? "+" : "");
    if (a < 100u) { num(t, (int32_t)(a / 10u)); zcat(s, t); zcat(s, "."); t[0] = (char)('0' + a % 10u); t[1] = 0; zcat(s, t); }
    else { num(t, (int32_t)((a + 5u) / 10u)); zcat(s, t); }
    zcat(s, "%");
}

static void value_text(uint32_t p, char *s)
{
    static const char *const WAVE[4] = {"SAW", "SQUARE", "TRIANGLE", "SINE"};
    static const char *const TABLE[4] = {"AIR", "GLASS", "VOX", "REED"};
    static const char *const TONE[3] = {"FLAT", "SOFT", "LOW CUT"};
    static const char *const LFO[5] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "DRIFT"};
    uint32_t v = p < P_N ? set.p[p] : 0;
    const zps_t *sl = &set.slot[mod_sel];
    s[0] = 0;
    switch (p) {
    case P_WAVE: zcat(s, WAVE[v & 3u]); break;
    case P_TABLE: zcat(s, TABLE[v & 3u]); break;
    case P_TONE: zcat(s, TONE[v > 2u ? 0u : v]); break;
    case P_L1SHAPE: case P_L2SHAPE: zcat(s, LFO[v > 4u ? 0u : v]); break;
    case P_TUNE: if (v > 24u) zcat(s, "+"); num(s + (v > 24u), (int32_t)v - 24); break;
    case P_ENVAMT: percent(s, bip10(v)); break;
    case P_L1RATE: case P_L2RATE: case P_RRATE:     /* Hz: the per-block step x 44100 / 16 / 2^32 */
        fixed2(s, (uint32_t)(((uint64_t)ZT_LFO_INC[q10(v)] * 275625u) >> 32)); zcat(s, " Hz"); break;
    case P_DTIME: {
        uint32_t ms = (uint32_t)(DL_TIME[v & 127u] >> 8) * 1000u / 22050u;
        if (ms < 1000u) { num(s, (int32_t)ms); zcat(s, " ms"); } else { fixed2(s, ms / 10u); zcat(s, " s"); }
        break;
    }
    case P_KEY: zcat(s, znote_name((int)v, zkey_flats((int)v, set.p[P_SCALE]))); break;
    case P_SCALE: zcat(s, ZSCALES[v].name); break;
    case P_KEYS: zcat(s, v ? "CHROMATIC" : "SCALE"); break;
    case P_SPREAD: zcat(s, v ? "ON" : "OFF"); break;
    case P_MSLOT: num(s, (int32_t)mod_sel + 1); if (mod_sel < 7u) zcat(s, " KIT"); break;
    case P_MSRC: zcat(s, SRC_NAME[sl->src < ZS_N ? sl->src : 0]); break;
    case P_MDST: zcat(s, DST_NAME[sl->dst < ZD_N ? sl->dst : 0]); break;
    case P_MAMT: percent(s, sl->amt); break;
    default: num(s, (int32_t)v);
    }
}

/* ---- the screen, after the mock-ups (Zenthesizer's theme and fonts): a plank along the top (the page, the CPU),
 * the wood in the middle (on HOME the slab, the preset's name on its band, a ring around it that breathes with the
 * sound, a ripple for each note, a bead for each voice; elsewhere the slab dimmed, the sound burned across it), and
 * a plank of four cards along the bottom, one a knob. Drawn in strips of 64 rows (the canvas), text and lines
 * blended onto the wood. The pictures and fonts are made once on a computer: tools/gen_art.py -> gen/zen_art.h. */
#include "zen_art.h"
#define Y_MID 20u                               /* the middle: 174 rows (ZA_H) */
#define Y_CARDS 194u
#define H_CARDS 46u
#define STRIP 64u                               /* the canvas: 240 x 64 at most (platform.c CV_MAX) */
#define T_GLASS 0x0D0906u                       /* the theme (Zenthesizer Source/ui/Theme.h), 24-bit */
#define T_BRASS 0xD9A94Au
#define T_BRASSLO 0x9A6A1Fu
#define T_BRASSDK 0x6E4A14u
#define T_BRASSHI 0xFFF1C2u
#define T_BONE 0xF0E6D0u
#define T_STEEL 0xB8BCC2u
#define T_WALNUT 0x4A2E18u
#define T_WALNUTLO 0x2E1A0Bu
#define T_GLOW 0xF3D9A4u
#define T_AMBER 0xFFB347u
static const uint32_t PLATE_T[4] = {0xFF5A3Cu, 0xFFB347u, 0x7CC7FFu, 0x8BE07Au};   /* BLOOM MORPH ECHOES SPACE */
static const char *const SHORT[P_N] = {"BLOOM", "MORPH", "ECHOES", "SPACE", "CUTOFF", "RES", "DRIVE", "ENV AMT",
    "WAVE", "SHAPE", "DETUNE", "LEVEL", "TABLE", "POS", "WARP", "LEVEL", "TUNE", "GLIDE", "VEL", "VOLUME",
    "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE",
    "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "1 RATE", "1 SHAPE", "2 RATE", "2 SHAPE",
    "RANDOM", "SMOOTH", "KEYTRK", "STEREO", "CHORUS", "REVERB", "DECAY", "SHIMMER", "SIZE", "DAMP", "DEPTH", "SPEAKER",
    "TIME", "FEEDBK", "TONE", "MIX", "KEY", "SCALE", "KEYS", "SPREAD"};
static const char *const SRC_SHORT[ZS_N] = {"--", "ENV 1", "ENV 2", "LFO 1", "LFO 2", "RANDOM", "PRESS", "VEL",
    "WHEEL", "CH PRS", "BLOOM", "MORPH", "ECHOES", "SPACE", "KEY", "SLIDE"};
static const char *const DST_SHORT[ZD_N] = {"--", "PITCH", "A TUNE", "A DET", "A SHAPE", "A LEVEL", "B POS",
    "B WARP", "-", "B LEVEL", "-", "CUTOFF", "RES", "DRIVE", "AMP", "PAN", "L1 RATE", "L2 RATE", "CHORUS", "DELAY",
    "REVERB"};
static const char *const SCALE_SHORT[11] = {"CHROM", "MAJOR", "MINOR", "DORIAN", "LYDIAN", "MAJPENT", "MINPENT",
    "HIRAJO", "IN-SEN", "CELTIC", "WHOLE"};

static void value_short(uint32_t p, char *s)    /* a card's value: 7 characters at most */
{
    static const char *const WAVE[4] = {"SAW", "SQUARE", "TRI", "SINE"};
    static const char *const LFO[5] = {"SINE", "TRI", "SAW", "SQUARE", "DRIFT"};
    static const char *const TONE[3] = {"FLAT", "SOFT", "LO CUT"};
    uint32_t v = p < P_N ? set.p[p] : 0;
    const zps_t *sl = &set.slot[mod_sel];
    s[0] = 0;
    switch (p) {
    case P_WAVE: zcat(s, WAVE[v & 3u]); break;
    case P_L1SHAPE: case P_L2SHAPE: zcat(s, LFO[v > 4u ? 0u : v]); break;
    case P_TONE: zcat(s, TONE[v > 2u ? 0u : v]); break;
    case P_L1RATE: case P_L2RATE: case P_RRATE:
        fixed2(s, (uint32_t)(((uint64_t)ZT_LFO_INC[q10(v)] * 275625u) >> 32)); break;
    case P_DTIME: {
        uint32_t ms = (uint32_t)(DL_TIME[v & 127u] >> 8) * 1000u / 22050u;
        if (ms < 1000u) { num(s, (int32_t)ms); zcat(s, " ms"); } else { fixed2(s, ms / 10u); zcat(s, " s"); }
        break;
    }
    case P_SCALE: zcat(s, SCALE_SHORT[v > 10u ? 0u : v]); break;
    case P_KEYS: zcat(s, v ? "CHROM" : "SCALE"); break;
    case P_MSLOT: num(s, (int32_t)mod_sel + 1); break;
    case P_MSRC: zcat(s, SRC_SHORT[sl->src < ZS_N ? sl->src : 0]); break;
    case P_MDST: zcat(s, DST_SHORT[sl->dst < ZD_N ? sl->dst : 0]); break;
    default: value_text(p, s);
    }
}
static uint32_t bar_of(uint32_t p)              /* a card's bar, 0..1024 */
{
    const zps_t *sl = &set.slot[mod_sel];
    int32_t a;
    if (p < P_N) return (uint32_t)(((uint32_t)set.p[p] - PARAM[p].lo) * 1024u / (uint32_t)(PARAM[p].hi - PARAM[p].lo));
    if (p == P_MSLOT) return mod_sel * 1024u / (ZM_NSLOTS - 1u);
    if (p == P_MSRC) return sl->src * 1024u / (ZS_N - 1u);
    if (p == P_MDST) return sl->dst * 1024u / (ZD_N - 1u);
    a = sl->amt < 0 ? -sl->amt : sl->amt;
    return (uint32_t)a;
}

/* ---- drawing onto the canvas, blended (colours 24-bit 0xRRGGBB, alpha 0..256) */
static uint16_t za_pal[256], za_dim[256];       /* the slab's colours as the canvas holds them; dimmed for the pages */
static void za_init(void)
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        uint32_t c = ZA_PAL[i], r = c >> 11, g = (c >> 5) & 63u, b = c & 31u;
        za_pal[i] = swap16(c);
        za_dim[i] = swap16(((r * 70u >> 8) << 11) | ((g * 70u >> 8) << 5) | (b * 70u >> 8));
    }
}
static void zb_px(int32_t x, int32_t y, uint32_t c, uint32_t a)
{
    uint16_t *p;
    uint32_t b;
    int32_t r0, g0, b0;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || !a) return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    b = swap16(*p);
    r0 = (int32_t)(b >> 11); g0 = (int32_t)((b >> 5) & 63u); b0 = (int32_t)(b & 31u);
    r0 += ((((int32_t)(c >> 19) & 31) - r0) * (int32_t)a) >> 8;
    g0 += ((((int32_t)(c >> 10) & 63) - g0) * (int32_t)a) >> 8;
    b0 += ((((int32_t)(c >> 3) & 31) - b0) * (int32_t)a) >> 8;
    *p = swap16(((uint32_t)r0 << 11) | ((uint32_t)g0 << 5) | (uint32_t)b0);
}
static void zb_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c, uint32_t a)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        if ((uint32_t)(j + cv_oy) < cv_h)
            for (i = x; i < x + w; i++) zb_px(i, j, c, a);
}
static int32_t zb_textw(const felucca_font_t *f, const char *s, int32_t track)
{
    int32_t w = 0;
    for (; *s; s++) w += f->adv[glyph(f, (uint8_t)*s)] + track;
    return w ? w - track : 0;
}
static int32_t zb_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t c, int32_t track)
{
    if (y + (int32_t)f->h + cv_oy < 0 || y + cv_oy >= (int32_t)cv_h) return x;   /* not in this strip */
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v) zb_px(x - f->pad + (int32_t)gx, y + (int32_t)gy, c, v * 17u + 1u);
            }
        x += f->adv[gi] + track;
    }
    return x;
}
static void zb_textc(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint32_t c, int32_t track)
{
    zb_text(cx - zb_textw(f, s, track) / 2, y, f, s, c, track);
}
static void zb_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c, uint32_t a)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, err = dx + dy, guard = 600;
    while (guard--) {
        int32_t e2 = 2 * err;
        zb_px(x0, y0, c, a);
        if (x0 == x1 && y0 == y1) break;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void zb_ring(int32_t cx, int32_t cy, int32_t r, uint32_t c, uint32_t a)   /* a circle, dot by dot */
{
    uint32_t n = (uint32_t)(r > 4 ? r : 4) * 7u, i, step = 0xFFFFFFFFu / n;
    for (i = 0; i < n; i++) {
        uint32_t ph = i * step;
        zb_px(cx + ((r * zsin(ph)) >> 15), cy + ((r * zsin(ph + 0x40000000u)) >> 15), c, a);
    }
}
static void zb_disc(int32_t cx, int32_t cy, int32_t r, uint32_t c, uint32_t a)    /* a dot, its edge softened */
{
    int32_t x, y, rr = r * r, ro = (r + 1) * (r + 1);
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            int32_t d = x * x + y * y;
            if (d <= rr) zb_px(cx + x, cy + y, c, a);
            else if (d <= ro) zb_px(cx + x, cy + y, c, a / 3u);
        }
}

/* ---- the header: a plank; the page's name in brass capitals; HOME: the preset's family and number; the rest:
 * the limiter (when it works), DROP (the guard), CPU */
static uint32_t ui_cpu, ui_drop;
static int32_t ui_lim;
static void ui_header(uint32_t page)
{
    char r[40], t[12];
    const felucca_font_t *lf = &ZF_LAB, *tf = &ZF_TXT;
    r[0] = 0;
    cv_begin(240, Y_MID, swap16(0));
    zb_rect(0, 0, 240, (int32_t)Y_MID - 1, T_GLASS, 256);
    zb_rect(0, (int32_t)Y_MID - 1, 240, 1, T_BRASSDK, 256);
    zb_text(8, 10 - (int32_t)lf->h / 2, lf, page == PG_HOME ? "ZEN" : PAGE_NAME[page], T_BRASS, 1);
    if (page == PG_HOME) {
        const char *cat = ZPRESET[set.preset % ZPRESETS].cat, *sl = cat;
        char u[40];
        uint32_t i = 0;
        while (*sl) if (*sl++ == '/') cat = sl;    /* "World/Asia": ASIA */
        while (cat[i] && i < 39u) { u[i] = (char)(cat[i] >= 'a' && cat[i] <= 'z' ? cat[i] - 32 : cat[i]); i++; }
        u[i] = 0;
        zb_textc(120, 10 - (int32_t)lf->h / 2, lf, u, T_STEEL, 1);
        num(t, (int32_t)set.preset); zcat(r, t); zcat(r, " / "); num(t, (int32_t)ZPRESETS - 1); zcat(r, t);
        zb_text(232 - zb_textw(tf, r, 0), 10 - (int32_t)tf->h / 2, tf, r, T_STEEL, 0);
    } else {
        if (ui_lim > 0) {
            zcat(r, "-"); num(t, ui_lim / 10); zcat(r, t); zcat(r, "."); t[0] = (char)('0' + ui_lim % 10); t[1] = 0;
            zcat(r, t); zcat(r, " dB   ");
        }
        if (ui_drop) zcat(r, "DROP   ");
        zcat(r, "CPU "); num(t, (int32_t)ui_cpu); zcat(r, t); zcat(r, "%");
        zb_text(232 - zb_textw(tf, r, 0), 10 - (int32_t)tf->h / 2, tf, r, ui_drop || ui_cpu > 75u ? T_AMBER : T_STEEL, 0);
    }
    cv_blit(0, 0);
}

/* ---- the cards: a plank, knob 1-4 left to right; a small capital name, the value, a bar (from the middle for the
 * two-way ones); the one being turned is lit */
static void ui_cards(uint32_t page, uint32_t hot)
{
    uint32_t r;
    const felucca_font_t *lf = &ZF_LAB, *vf = &ZF_VAL;
    cv_begin(240, H_CARDS, swap16(0));
    zb_rect(0, 0, 240, (int32_t)H_CARDS, T_GLASS, 256);
    zb_rect(0, 0, 240, 1, T_BRASSDK, 256);
    for (r = 0; r < 4u; r++) {
        uint32_t p = page_param(page, r), c = page == PG_HOME ? PLATE_T[r] : T_BRASS;
        int32_t x = (int32_t)r * 60, bx0 = x + 9, bx1 = x + 51, w;
        char v[16];
        const char *lab = p < P_N ? SHORT[p] : (const char *[]){"SLOT", "SOURCE", "DEST", "AMOUNT"}[p - P_MSLOT];
        if (r) zb_rect(x, 6, 1, (int32_t)H_CARDS - 10, T_WALNUT, 256);
        zb_textc(x + 30, 11 - (int32_t)lf->h / 2, lf, lab, r == hot ? T_AMBER : T_BRASSLO, 1);
        value_short(p, v);
        if (zb_textw(vf, v, 0) <= 56) zb_textc(x + 30, 26 - (int32_t)vf->h / 2, vf, v, r == hot ? T_BRASSHI : T_BONE, 0);
        else zb_textc(x + 30, 26 - (int32_t)ZF_TXT.h / 2, &ZF_TXT, v, r == hot ? T_BRASSHI : T_BONE, 0);
        zb_rect(bx0, 39, bx1 - bx0, 2, T_WALNUTLO, 256);
        if (p == P_ENVAMT || p == P_MAMT) {     /* two-way: from the middle */
            int32_t q = p == P_ENVAMT ? bip10(set.p[p]) : set.slot[mod_sel].amt, mid = (bx0 + bx1) / 2;
            w = q * (bx1 - bx0) / 2048;
            if (w >= 0) zb_rect(mid, 39, w, 2, c, 256); else zb_rect(mid + w, 39, -w, 2, c, 256);
        } else {
            w = (int32_t)(bar_of(p) * (uint32_t)(bx1 - bx0) / 1024u);
            zb_rect(bx0, 39, w, 2, c, 256);
        }
    }
    cv_blit(0, Y_CARDS);
}

/* ---- the middle */
#define SC_PTS 240u
static int32_t sc_peak = 4096;
static struct { uint32_t t0, on; } ripple[4];
static uint32_t ripple_last, mid_dirty = 1;
static void ripple_start(void)                  /* a note: a ripple from the pith (one per 60 ms: a chord makes one) */
{
    uint32_t k, old = 0;
    if (fm1_ms - ripple_last < 60u) return;
    ripple_last = fm1_ms;
    for (k = 1; k < 4u; k++) if (!ripple[k].on || (ripple[old].on && ripple[k].t0 < ripple[old].t0)) old = k;
    ripple[old].t0 = fm1_ms; ripple[old].on = 1;
}
static uint32_t ui_live(void)                   /* is anything moving? */
{
    uint32_t k;
    for (k = 0; k < 4u; k++) if (ripple[k].on) return 1;
    return sc_peak > 300;
}

/* the sound, as 240 points: the last 1024 output samples (the audio interrupt writes them), from a rising zero
 * crossing, two samples a point; the height follows the level */
static void sc_points(int32_t *ys, int32_t half)
{
    static int16_t snap[SC_N];
    uint32_t w = sc_w, i, t0 = 0, x;
    int32_t pk = 0, g;
    for (i = 0; i < SC_N; i++) {
        snap[i] = sc_ring[(w + i) & (SC_N - 1u)];
        if (snap[i] > pk) pk = snap[i];
        if (-snap[i] > pk) pk = -snap[i];
    }
    sc_peak = pk > sc_peak ? pk : sc_peak - ((sc_peak - pk) >> 3);
    g = (half << 16) / (sc_peak < 2048 ? 2048 : sc_peak);
    for (i = 1; i < SC_N - 2u * SC_PTS; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) { t0 = i; break; }
    for (x = 0; x < SC_PTS; x++) {
        int32_t a = (snap[t0 + 2u * x] + snap[t0 + 2u * x + 1u]) >> 1, y = (a * g) >> 16;
        ys[x] = y > half ? half : y < -half ? -half : y;
    }
    if (pk < 64) sc_peak = sc_peak > 2048 ? sc_peak : 0;   /* silence: nothing moves */
}

static uint32_t scope_on = 1;                   /* SAVE: the motion off (a still picture) and on */
static void ui_mid(uint32_t page, uint32_t hot)
{
    static int32_t ys[SC_PTS];
    uint32_t r0, k, x;
    char s[48];
    uint32_t foot_c;
    const felucca_font_t *nf = &ZF_NAME, *tf = &ZF_TXT;
    const char *nm = ZPRESET[set.preset % ZPRESETS].name;
    int32_t foot_y, i;
    if (scope_on) sc_points(ys, page == PG_HOME ? 9 : 52);
    else for (x = 0; x < SC_PTS; x++) ys[x] = 0;
    for (k = 0; k < 4u; k++)                    /* ripples: 0.9 s from the pith to the bark */
        if (ripple[k].on && fm1_ms - ripple[k].t0 > 900u) ripple[k].on = 0;

    s[0] = 0;                                   /* the foot: the knob being turned, else the chord, else the key */
    if (hot < 4u) {
        uint32_t p = page_param(page, hot);
        char v[24];
        zcat(s, p < P_N ? PARAM[p].name : (const char *[]){"SLOT", "SOURCE", "DEST", "AMOUNT"}[p - P_MSLOT]);
        zcat(s, "   "); value_text(p, v); zcat(s, v);
        foot_c = T_BRASSHI;
    } else if (set.p[P_KEYS]) { zcat(s, "chromatic keys"); foot_c = T_STEEL; }
    else if (last_chord >= 0) { zcat(s, chord_text); foot_c = T_AMBER; }
    else {
        zcat(s, znote_name(set.p[P_KEY], zkey_flats(set.p[P_KEY], set.p[P_SCALE])));
        zcat(s, " "); zcat(s, ZSCALES[set.p[P_SCALE]].name);
        foot_c = T_STEEL;
    }
    if (set.oct) { char t[12] = "   oct +"; t[7] = set.oct < 0 ? '-' : '+'; zcat(s, t);
                   t[0] = (char)('0' + (set.oct < 0 ? -set.oct : set.oct)); t[1] = 0; zcat(s, t); }
    foot_y = page == PG_HOME ? 158 : 152;

    for (r0 = 0; r0 < ZA_H; r0 += STRIP) {
        uint32_t h = ZA_H - r0 > STRIP ? STRIP : ZA_H - r0, row;
        const uint16_t *wpal = page == PG_HOME ? za_pal : za_dim;
        cv_begin(240, h, swap16(0));
        for (row = 0; row < h; row++) {         /* the wood */
            const uint8_t *src = ZA_SLAB + (r0 + row) * ZA_W;
            uint16_t *dst = cv_px + row * 240u;
            for (x = 0; x < 240u; x++) dst[x] = wpal[src[x]];
        }
        cv_oy = -(int32_t)r0;
        if (page == PG_HOME) {
            for (k = 0; k < 4u; k++)            /* ripples, from the pith outward, fading */
                if (ripple[k].on) {
                    uint32_t t = fm1_ms - ripple[k].t0;
                    int32_t rr = 4 + (int32_t)(t * (ZA_R + 2) / 900u);
                    uint32_t a = 256u - t * 256u / 900u;
                    zb_ring(ZA_CX, ZA_CY, rr, 0x3B1F0Cu, a);           /* burned into the rings */
                    zb_ring(ZA_CX, ZA_CY, rr + 1, 0x3B1F0Cu, a * 3u / 4u);
                    zb_ring(ZA_CX, ZA_CY, rr - 1, T_BRASSHI, a / 2u);
                }
            for (x = 0; x < SC_PTS; x++) {      /* the ring around the bark: the sound, breathing */
                uint32_t ph0 = x * (0xFFFFFFFFu / SC_PTS), ph1 = (x + 1u) * (0xFFFFFFFFu / SC_PTS);
                int32_t ea = x < 24u ? (int32_t)x : x > SC_PTS - 25u ? (int32_t)(SC_PTS - 1u - x) : 24;   /* at rest at */
                int32_t eb = x + 1u < 24u ? (int32_t)x + 1 : x + 1u > SC_PTS - 25u ? (int32_t)(SC_PTS - 2u - x) : 24;   /* the seam */
                int32_t ra = ZA_R + 9 + ys[x] * ea / 24, rb = ZA_R + 9 + ys[(x + 1u) % SC_PTS] * (eb < 0 ? 0 : eb) / 24;
                int32_t xa = ZA_CX + ((ra * zsin(ph0)) >> 15), ya = ZA_CY - ((ra * zsin(ph0 + 0x40000000u)) >> 15);
                int32_t xb = ZA_CX + ((rb * zsin(ph1)) >> 15), yb = ZA_CY - ((rb * zsin(ph1 + 0x40000000u)) >> 15);
                zb_line(xa, ya + 1, xb, yb + 1, T_AMBER, 70);
                zb_line(xa, ya, xb, yb, T_GLOW, 220);
            }
            {                                   /* the band across the slab, and the name on it */
                int32_t bh = (int32_t)nf->h + 2, by = ZA_CY - bh / 2, bw = zb_textw(nf, nm, 0) + 28;
                if (bw > 220) bw = 220;
                if (bw < 120) bw = 120;
                zb_rect(120 - bw / 2 + 3, by, bw - 6, bh, T_GLASS, 190);
                zb_rect(120 - bw / 2, by + 3, 3, bh - 6, T_GLASS, 190);
                zb_rect(120 + bw / 2 - 3, by + 3, 3, bh - 6, T_GLASS, 190);
                if (zb_textw(nf, nm, 0) <= 216) zb_textc(120, by + 1, nf, nm, T_BONE, 0);
                else zb_textc(120, ZA_CY - (int32_t)ZF_VAL.h / 2, &ZF_VAL, nm, T_BONE, 0);
            }
            for (i = 0; i < (int32_t)ZV; i++) { /* a bead for each voice: lit while it sounds */
                static const int8_t BX[ZV] = {82, 41, -41, -82, -41, 41}, BY[ZV] = {0, -71, -71, 0, 71, 71};
                if (zv[i].stage) { zb_disc(ZA_CX + BX[i], ZA_CY + BY[i], 4, T_AMBER, 70); zb_disc(ZA_CX + BX[i], ZA_CY + BY[i], 2, T_GLOW, 256); }
                else zb_disc(ZA_CX + BX[i], ZA_CY + BY[i], 2, 0x3A2414u, 256);
            }
        } else {                                /* the sound, burned across the wood */
            for (x = 1; x < SC_PTS; x++) {
                zb_line((int32_t)x - 1, 78 - ys[x - 1] + 1, (int32_t)x, 78 - ys[x] + 1, T_AMBER, 80);
                zb_line((int32_t)x - 1, 78 - ys[x - 1] - 1, (int32_t)x, 78 - ys[x] - 1, T_AMBER, 50);
                zb_line((int32_t)x - 1, 78 - ys[x - 1], (int32_t)x, 78 - ys[x], T_GLOW, 230);
            }
        }
        if (last_chord >= 0 && hot >= 4u && !set.p[P_KEYS] && page != PG_HOME)
            zb_textc(120, foot_y - 6, nf, s, foot_c, 0);   /* a chord: in the serif */
        else
            zb_textc(120, foot_y, tf, s, foot_c, 0);
        if (page == PG_HOME) {                  /* HOME has no CPU in its header: here, small */
            char c[24], t[8];
            c[0] = 0;
            if (ui_drop) zcat(c, "DROP  ");
            num(t, (int32_t)ui_cpu); zcat(c, t); zcat(c, "%");
            zb_text(234 - zb_textw(tf, c, 0), 4, tf, c, ui_drop || ui_cpu > 75u ? T_AMBER : T_BRASSLO, 0);
        }
        cv_blit(0, Y_MID + r0);
    }
    cv_oy = 0;
    mid_dirty = 0;
}

static void draw_page(uint32_t page)
{
    ui_header(page);
    ui_cards(page, 9);
    ui_mid(page, 9);
}

static int32_t lim_db_tenths(int32_t g)        /* gain Q30 -> reduction in tenths of a dB (0.1 dB = x 0.98855) */
{
    int32_t t = 0, x = ONE30;
    while (x > g && t < 400) { x = M30(x, 1061442400); t++; }
    return t;
}

/* ---- the main loop */
static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_stat = 0, voices_drawn = 99, hot = 9, t_hot = 0, page = 0, t_scope = 0;
    uint32_t key_tag[27] = {0}, midi_tag[128] = {0};
    int8_t key_chord[27];
    for (k = 0; k < P_N; k++) set.p[k] = PARAM[k].def;
    load_preset(0);
#ifdef ZL_SET                                   /* the host tests set their own: -DZL_SET='set.p[P_CUT]=100;...' */
    ZL_SET;
#else
    if (settings_load(&set, sizeof set) || set.ver != SET_VER) {   /* nothing saved (or from an older Zen-Lite) */
        for (k = 0; k < P_N; k++) set.p[k] = PARAM[k].def;
        set.oct = 0;
        load_preset(0);
    }
#endif
    set.ver = SET_VER;
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    if (set.preset >= ZPRESETS) set.preset = 0;
    for (k = 0; k < P_N; k++)
        if (set.p[k] < PARAM[k].lo || set.p[k] > PARAM[k].hi) set.p[k] = PARAM[k].def;
    for (k = 0; k < ZM_NSLOTS; k++)
        if (set.slot[k].src >= ZS_N || set.slot[k].dst >= ZD_N || set.slot[k].amt > 1024 || set.slot[k].amt < -1024)
            set.slot[k].src = set.slot[k].dst = 0, set.slot[k].amt = 0;
    for (k = 0; k < 27u; k++) key_chord[k] = -1;
    map_keys();
    apply();

    za_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_page(page);

    for (;;) {
        uint32_t keys, down, up, b, rel, lit, voices = 0, chromatic = set.p[P_KEYS];
        uint8_t st, d1, d2;
        int m7, m9, sus, strum;
        int32_t pd;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        m7 = m9 = sus = strum = 0;              /* the held modifier keys (black keys 8..11) */
        for (k = 0; k < 27u; k++)
            if (keys >> k & 1u && black_of[k] >= 7) {
                if (black_of[k] == 7) m7 = 1;
                if (black_of[k] == 8) m9 = 1;
                if (black_of[k] == 9) sus = 1;
                if (black_of[k] == 10) strum = 1;
            }
        for (k = 0; k < 27u; k++) {
            if (up >> k & 1u) {                 /* let go of what this key started */
                if (key_chord[k] >= 0) { chord_up((uint32_t)key_chord[k]); key_chord[k] = -1; }
                release_tag(key_tag[k]);
                key_tag[k] = 0;
            }
            if (down >> k & 1u) {
                if (chromatic) {
                    uint32_t note = (uint32_t)(53 + 12 * set.oct + (int32_t)k);
                    key_tag[k] = play(note, 100, 0);
                    midi_send(0x90, note, 100);
                } else if (white_of[k] >= 0) {
                    int note = zscale_note(white_of[k], set.p[P_KEY], set.p[P_SCALE], 48 + 12 * set.oct);
                    if (note >= 0 && note < 128) { key_tag[k] = play((uint32_t)note, 100, 0); midi_send(0x90, (uint32_t)note, 100); }
                } else if (black_of[k] < 7) {
                    chord_down((uint32_t)black_of[k], m7, m9, sus, strum);
                    key_chord[k] = black_of[k];
                    mid_dirty = 1;
                }
            }
        }
        strum_tick();

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
        {                                       /* a second press of a button steps through its pages */
            uint32_t np = b & (1u << BTN_HOME) ? PG_HOME
                        : b & (1u << BTN_EDIT) ? (page >= PG_EDIT && page < PG_EDIT + 3u ? page + 1u : PG_EDIT)
                        : b & (1u << BTN_ENV) ? (page >= PG_ENV && page < PG_ENV + 2u ? page + 1u : PG_ENV)
                        : b & (1u << BTN_LFO) ? (page >= PG_LFO && page < PG_LFO + 2u ? page + 1u : PG_LFO)
                        : b & (1u << BTN_FX) ? (page >= PG_FX && page < PG_FX + 2u ? page + 1u : PG_FX)
                        : b & (1u << BTN_GLO) ? PG_GLO : page;
            if (np != page) { page = np; hot = 9; draw_page(page); }
        }
        if (b & (1u << BTN_SEL)) {              /* SEL: scale keys <-> chromatic keys */
            set.p[P_KEYS] ^= 1u; dirty = 1; t_change = fm1_ms;
            if (page == PG_GLO) ui_cards(page, hot);
            mid_dirty = 1;
        }

        pd = fm1_enc_take(ENC_PRESETS);         /* PRESETS: the next or previous preset, shown on HOME */
        if (pd) {
            int32_t np = ((int32_t)set.preset + pd) % (int32_t)ZPRESETS;
            load_preset((uint32_t)(np < 0 ? np + (int32_t)ZPRESETS : np));
            page = PG_HOME; hot = 9;
            draw_page(page);
            dirty = 1; t_change = fm1_ms;
        }

        for (k = 0; k < 4u; k++) {              /* the four knobs: one step a click, faster when turned fast */
            uint32_t p = page_param(page, k);
            int32_t d = fm1_enc_take(ENC_KNOB1 + k), nv;
            zps_t *sl = &set.slot[mod_sel];
            if (!d) continue;
            if (p < P_N) {
                if (PARAM[p].hi > 20u && (d > 1 || d < -1)) d *= 2;
                nv = (int32_t)set.p[p] + d;
                if (nv < PARAM[p].lo) nv = PARAM[p].lo;
                if (nv > PARAM[p].hi) nv = PARAM[p].hi;
                set.p[p] = (uint8_t)nv;
            } else if (p == P_MSLOT) {          /* which slot the page shows */
                nv = (int32_t)mod_sel + d;
                mod_sel = (uint32_t)(nv < 0 ? 0 : nv > ZM_NSLOTS - 1 ? ZM_NSLOTS - 1 : nv);
            } else if (p == P_MSRC) {
                nv = ((int32_t)sl->src + d) % (int32_t)ZS_N;
                sl->src = (uint8_t)(nv < 0 ? nv + (int32_t)ZS_N : nv);
            } else if (p == P_MDST) {
                int32_t step = d > 0 ? 1 : -1, n = d > 0 ? d : -d, dd = sl->dst;
                while (n--) do dd = (dd + step + (int32_t)ZD_N) % (int32_t)ZD_N; while (DST_SKIP(dd));
                sl->dst = (uint8_t)dd;
            } else {                            /* AMOUNT: 1 % a click, 2 % fast */
                nv = sl->amt + d * ((d > 1 || d < -1) ? 20 : 10);
                sl->amt = (int16_t)(nv < -1024 ? -1024 : nv > 1024 ? 1024 : nv);
            }
            apply();
            hot = k; t_hot = fm1_ms; mid_dirty = 1;
            ui_cards(page, hot);
            if (p >= P_KEY && p <= P_SPREAD) last_chord = -1;
            if (p != P_MSLOT) { dirty = 1; t_change = fm1_ms; }
        }
        if (hot < 4u && fm1_ms - t_hot > 1500u) { hot = 9; ui_cards(page, hot); mid_dirty = 1; }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI: chromatic notes with velocity, and expression */
            uint32_t ty = st & 0xF0u;
            if (ty == 0x90u && d2) { release_tag(midi_tag[d1 & 127u]); midi_tag[d1 & 127u] = play(d1, d2, 0); }
            else if (ty == 0x80u || ty == 0x90u) { release_tag(midi_tag[d1 & 127u]); midi_tag[d1 & 127u] = 0; }
            else if (ty == 0xA0u) {             /* poly pressure: that note's PRESSURE */
                for (k = 0; k < ZV; k++)
                    if (midi_tag[d1 & 127u] && zv[k].tag == midi_tag[d1 & 127u]) zv[k].press_t = (int32_t)d2 * 258;
            } else if (ty == 0xD0u) zm_chanat = (int32_t)d1 * 258;
            else if (ty == 0xE0u) zm_bend = ((int32_t)((uint32_t)d2 << 7 | d1) - 8192) / 4;   /* +-2 semitones */
            else if (ty == 0xB0u && d1 == 1u) zm_wheel = (int32_t)d2 * 258;
            else if (ty == 0xB0u && d1 == 74u) zm_slide = (int32_t)d2 * 258;
        }

        if (b & (1u << BTN_SAVE)) { scope_on ^= 1u; mid_dirty = 1; }   /* SAVE: the motion off (a still picture) and on */
        {                                       /* the middle: 15 times a second while anything moves, else when it changes */
            static uint32_t was_live;
            uint32_t live = scope_on && ui_live();
            if (fm1_ms - t_scope >= 66u && (mid_dirty || live || was_live)) {
                ui_mid(page, hot); t_scope = fm1_ms; was_live = live;
            }
        }
        lit = keys;
        for (k = 0; k < ZV; k++) if (zv[k].stage) voices++;
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);
        fm1_led_key(BTN_HOME, page == PG_HOME);
        fm1_led_key(BTN_EDIT, page >= PG_EDIT && page < PG_ENV);
        fm1_led_key(BTN_ENV, page >= PG_ENV && page < PG_LFO);
        fm1_led_key(BTN_LFO, page >= PG_LFO && page < PG_FX);
        fm1_led_key(BTN_FX, page >= PG_FX && page < PG_GLO);
        fm1_led_key(BTN_GLO, page == PG_GLO);
        fm1_led_key(BTN_SEL, set.p[P_KEYS] != 0u);
        fm1_led_key(BTN_SAVE, !scope_on);

        if (fm1_ms - t_stat >= 250u || voices != voices_drawn) {     /* 4 times a second: CPU and limiter */
            uint32_t cpu = cpu_ticks_max * 100u / (5805u * FM1_TICKS_PER_US);
            int32_t g = lim_min_seen;
            cpu_ticks_max = 0;
            lim_min_seen = ONE30;
            {
                static uint32_t drops_seen, t_drop = 0xFFFF0000u;
                if (guard_drops != drops_seen) { drops_seen = guard_drops; t_drop = fm1_ms; }
                ui_cpu = cpu; ui_lim = lim_db_tenths(g); ui_drop = fm1_ms - t_drop < 2000u;
                ui_header(page);
            }
            t_stat = fm1_ms;
            if (voices != voices_drawn || page == PG_HOME) mid_dirty = 1;   /* the beads; HOME's CPU */
            voices_drawn = voices;
        }

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
