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
static volatile uint32_t cpu_ticks_max, guard_drops;
#define GUARD_TICKS (5805u * FM1_TICKS_PER_US * 85u / 100u)   /* over 85 % of a block: shed a voice */
#ifdef DINGHY_HOST
static int host_load;                           /* the host tests pretend the FM-1 is overloaded ("T load 1") */
static void host_set_load(int on) { host_load = on; }
#endif

static void app_audio(int32_t *out, uint32_t n)
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
    }
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

/* ---- voices: a note gets a tag, and is let go by its tag (a chord and a key may share a note) */
static uint32_t ages, tags, last_started = 60;

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
#define ZC_BRASS RGB(217, 169, 74)
#define ZC_BONE RGB(240, 230, 208)
#define ZC_DIM RGB(120, 110, 96)
#define ZC_AMBER RGB(255, 179, 71)

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
        fixed2(s, (uint32_t)(((uint64_t)ZT_LFO_INC[q10(v)] * 275625u) >> 32)); zcat(s, " HZ"); break;
    case P_DTIME: {
        uint32_t ms = (uint32_t)(DL_TIME[v & 127u] >> 8) * 1000u / 22050u;
        if (ms < 1000u) { num(s, (int32_t)ms); zcat(s, " MS"); } else { fixed2(s, ms / 10u); zcat(s, " S"); }
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

static void draw_row(uint32_t page, uint32_t r, int hot)
{
    static const char *const MODROW[4] = {"SLOT", "SOURCE", "DEST", "AMOUNT"};
    uint32_t p = page_param(page, r);
    char s[16];
    int32_t y = 88 + (int32_t)r * 24;
    char lab[16] = "1  ";
    lab[0] = (char)('1' + r);
    zcat(lab, p < P_N ? PARAM[p].name : MODROW[p - P_MSLOT]);
    value_text(p, s);
    draw_text_box(10, y, 120, &FONT_S, lab, hot ? ZC_AMBER : ZC_BRASS, 0);
    draw_text_box(130, y, 100, &FONT_S, s, ZC_BONE, 2);
}

static void draw_info(void)                     /* the line under the title: the chord playing, or the key */
{
    char s[40];
    s[0] = 0;
    if (set.p[P_KEYS]) zcat(s, "CHROMATIC KEYS");
    else if (last_chord >= 0) zcat(s, chord_text);
    else {
        zcat(s, znote_name(set.p[P_KEY], zkey_flats(set.p[P_KEY], set.p[P_SCALE])));
        zcat(s, " ");
        zcat(s, ZSCALES[set.p[P_SCALE]].name);
    }
    draw_text_box(0, 54, 240, &FONT_S, s, last_chord >= 0 && !set.p[P_KEYS] ? ZC_AMBER : ZC_DIM, 1);
}

static void draw_page(uint32_t page)
{
    uint32_t r;
    lcd_fill(0, 0, 240, 186, C_BLACK);
    if (page == PG_HOME) {                      /* HOME: the preset's name, and its number */
        const char *nm = ZPRESET[set.preset % ZPRESETS].name;
        char t[12];
        if (text_w(&FONT_L, nm) <= 236) draw_text_box(0, 8, 240, &FONT_L, nm, ZC_BRASS, 1);
        else draw_text_box(0, 16, 240, &FONT_S, nm, ZC_BRASS, 1);
        char u[24];
        u[0] = 0;
        num(t, (int32_t)set.preset); zcat(u, t); zcat(u, " / "); num(t, (int32_t)ZPRESETS - 1); zcat(u, t);
        draw_text_box(0, 38, 240, &FONT_S, u, ZC_DIM, 1);
    } else
        draw_text_box(0, 14, 240, &FONT_L, PAGE_NAME[page], ZC_BRASS, 1);
    draw_info();
    for (r = 0; r < 4u; r++) draw_row(page, r, 0);
}

static void draw_status(uint32_t cpu_pct, int32_t lim_tenths, uint32_t voices, int dropping)
{
    char a[40] = "CPU ", b[8], c[32] = "OCT +0   VOICES ";
    uint32_t i = 4, j;
    num(b, (int32_t)cpu_pct);
    for (j = 0; b[j]; j++) a[i++] = b[j];
    a[i++] = '%'; a[i++] = ' '; a[i++] = ' ';
    { const char *p = "LIMIT "; while (*p) a[i++] = *p++; }
    if (lim_tenths <= 0) { a[i++] = '-'; a[i++] = '-'; }
    else {
        a[i++] = '-';
        num(b, lim_tenths / 10);
        for (j = 0; b[j]; j++) a[i++] = b[j];
        a[i++] = '.';
        a[i++] = (char)('0' + lim_tenths % 10);
        a[i++] = ' '; a[i++] = 'd'; a[i++] = 'B';
    }
    a[i] = 0;
    draw_text_box(0, 190, 240, &FONT_S, a, cpu_pct > 50u ? ZC_AMBER : ZC_DIM, 1);
    c[4] = set.oct < 0 ? '-' : '+';
    c[5] = (char)('0' + (set.oct < 0 ? -set.oct : set.oct));
    c[16] = (char)('0' + voices); c[17] = '/'; c[18] = (char)('0' + ZV); c[19] = 0;
    if (dropping) zcat(c, "  DROP");            /* the guard faded a voice out: the CPU was nearly full */
    draw_text_box(0, 212, 240, &FONT_S, c, dropping ? ZC_AMBER : ZC_DIM, 1);
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
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_stat = 0, voices_drawn = 99, hot = 9, t_hot = 0, page = 0;
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

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_page(page);
    draw_status(0, 0, 0, 0);

    for (;;) {
        uint32_t keys, down, up, b, rel, lit, voices = 0, chromatic = set.p[P_KEYS];
        uint8_t st, d1, d2;
        int m7, m9, sus, strum, redraw_info = 0;
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
                    redraw_info = 1;
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
            if (page == PG_GLO) draw_row(PG_GLO, 2, 0);
            redraw_info = 1;
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
            if (p == P_MSLOT) { draw_row(page, 1, 0); draw_row(page, 2, 0); draw_row(page, 3, 0); }
            if (hot != k && hot < 4u) draw_row(page, hot, 0);
            hot = k; t_hot = fm1_ms;
            draw_row(page, k, 1);
            if (p >= P_KEY && p <= P_SPREAD) { last_chord = -1; redraw_info = 1; }
            if (p != P_MSLOT) { dirty = 1; t_change = fm1_ms; }
        }
        if (hot < 4u && fm1_ms - t_hot > 1500u) { draw_row(page, hot, 0); hot = 9; }

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

        if (redraw_info) draw_info();
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

        if (fm1_ms - t_stat >= 250u || voices != voices_drawn) {     /* 4 times a second: CPU and limiter */
            uint32_t cpu = cpu_ticks_max * 100u / (5805u * FM1_TICKS_PER_US);
            int32_t g = lim_min_seen;
            cpu_ticks_max = 0;
            lim_min_seen = ONE30;
            {
                static uint32_t drops_seen, t_drop = 0xFFFF0000u;
                if (guard_drops != drops_seen) { drops_seen = guard_drops; t_drop = fm1_ms; }
                draw_status(cpu, lim_db_tenths(g), voices, fm1_ms - t_drop < 2000u);
            }
            t_stat = fm1_ms;
            voices_drawn = voices;
        }

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
