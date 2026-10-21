/* SPDX-License-Identifier: GPL-3.0-only */
/* Zen-Lite: the Zenthesizer, cut down for the M-VAVE FM-1. Built on Dinghy (Felucca's and SLOOP's platform).
 *
 * The keyboard, in SCALE keys (the default):
 *   white keys       the notes of the key and scale, the root on the leftmost
 *   black keys 1-7   the seven chords of the key (I .. vii), as the plugin builds them
 *   black keys 8-11  held while you press a chord: 7TH, 9TH, SUS, STRUM
 * In CHROMATIC keys every key plays its own note, as a piano. SEL switches between the two.
 *
 * Pages (the four knobs), by button:
 *   HOME  CUTOFF  RESONANCE  POSITION  A-B MIX
 *   EDIT  Engine A: WAVE  SHAPE  DETUNE  TUNE
 *   ENV   ATTACK  DECAY  SUSTAIN  RELEASE
 *   GLO   KEY  SCALE  KEYS (scale / chromatic)  SPREAD (open chord voicing)
 * OCT- OCT+ the octave; MASTER the volume; USB-MIDI notes in play chromatically, with velocity.
 * The screen's last lines: CPU (keep it under 50 %), LIMIT, the octave, the voices sounding.
 *
 * Integers only: the chip has no FPU and no libgcc. tests/run_host.sh plays this file on a computer. */
#define APP_NAME "ZEN-LITE"
#include "platform.c"
#include "zen_voice.h"
#include "zen_chords.h"
#ifdef DINGHY_HOST
#define FM1_TICKS_PER_US 24u
#endif

/* ---- the parameters: four pages of four knobs */
enum { P_CUT, P_RES, P_POS, P_MIX, P_WAVE, P_SHAPE, P_DET, P_TUNE, P_ATT, P_DEC, P_SUS, P_REL,
       P_KEY, P_SCALE, P_KEYS, P_SPREAD, P_N };
typedef struct { const char *name; uint8_t lo, hi, def; } param_t;
static const param_t PARAM[P_N] = {
    {"CUTOFF", 0, 127, 79}, {"RESONANCE", 0, 127, 38}, {"POSITION", 0, 127, 57}, {"A-B MIX", 0, 127, 64},
    {"WAVE", 0, 3, 0}, {"SHAPE", 0, 127, 0}, {"DETUNE", 0, 127, 25}, {"TUNE", 0, 48, 24},
    {"ATTACK", 0, 127, 38}, {"DECAY", 0, 127, 64}, {"SUSTAIN", 0, 127, 102}, {"RELEASE", 0, 127, 76},
    {"KEY", 0, 11, 2}, {"SCALE", 1, 10, 3}, {"KEYS", 0, 1, 0}, {"SPREAD", 0, 1, 0},
};
static const char *const PAGE_NAME[4] = {"HOME", "ENGINE A", "ENVELOPE", "KEY + SCALE"};

typedef struct { int8_t oct; uint8_t p[P_N]; } set_t;
static set_t set;

static int32_t q10(uint32_t k) { return (int32_t)(k * 1024u / 127u); }
static void apply(void)
{
    zp_cut = q10(set.p[P_CUT]);
    zen_set_res(q10(set.p[P_RES]));
    zp_pos = q10(set.p[P_POS]);
    zen_set_mix(q10(set.p[P_MIX]));
    zp_awave = set.p[P_WAVE];
    zp_ashape = q10(set.p[P_SHAPE]) << 5;
    zp_adet = q10(set.p[P_DET]);
    zp_atune = (int32_t)set.p[P_TUNE] - 24;
    zen_set_env(q10(set.p[P_ATT]), q10(set.p[P_DEC]), q10(set.p[P_SUS]), q10(set.p[P_REL]));
}

/* ---- the sound */
static int32_t mixL[HALF_FRAMES], mixR[HALF_FRAMES];
static volatile uint32_t cpu_ticks_max;

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, v;
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks(), dt;
#endif
    for (i = 0; i < n; i++)
        mixL[i] = mixR[i] = 0;
    for (v = 0; v < ZV; v++)
        zv_render(v, mixL, mixR, n);
    limiter(mixL, mixR, n);
    for (i = 0; i < n; i++) {
        out[2u * i] += mixL[i];
        out[2u * i + 1u] += mixR[i];
    }
#ifndef DINGHY_HOST
    dt = fm1_ticks() - t0;
    if (dt > cpu_ticks_max) cpu_ticks_max = dt;
#endif
}

/* ---- voices: a note gets a tag, and is let go by its tag (a chord and a key may share a note) */
static uint32_t ages, tags;

static uint32_t play(uint32_t note, uint32_t vel, uint32_t chord)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, inc, lv = 0;
    zvoice_t *z;
    if (note > 127u || !vel) return 0;
    for (v = 0; v < ZV; v++) {                  /* a silent voice, else the quietest let-go one, else the oldest */
        uint32_t score = !zv[v].gate ? (zv[v].stage ? 1u + ((uint32_t)zv[v].env >> 3) : 0u)
                       : (zv[v].chord ? 0x40000000u : 0x80000000u) + (zv[v].age & 0x3FFFFFFFu);
        if (score < best) { best = score; pick = v; }    /* (chord notes go before melody notes) */
    }
    inc = zinc((int)note);
    while (lv < ZT_LEVELS - 1u && (uint64_t)(ZT_MAXH >> lv) * inc > 1932735283ull)   /* harmonics under 0.45 fs */
        lv++;
    z = &zv[pick];
    z->gate = 0;
    z->note = (uint8_t)note;
    z->chord = (uint8_t)chord;
    z->age = ++ages;
    z->tag = ++tags;
    z->inc = inc;
    z->lv = lv;
    z->vel = (int32_t)(16384u + vel * 129u);    /* velocity sensitivity 0.5: 1 - 0.5 (1 - v), Q15 */
    z->ktrack = ((int32_t)note - 60) * 4348 / 1000;   /* key tracking 0.5, as the plugin */
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

static void value_text(uint32_t p, char *s)
{
    static const char *const WAVE[4] = {"SAW", "SQUARE", "TRIANGLE", "SINE"};
    uint32_t v = set.p[p];
    s[0] = 0;
    switch (p) {
    case P_WAVE: zcat(s, WAVE[v & 3u]); break;
    case P_TUNE: if (v > 24u) zcat(s, "+"); num(s + (v > 24u), (int32_t)v - 24); break;
    case P_KEY: zcat(s, znote_name((int)v, zkey_flats((int)v, set.p[P_SCALE]))); break;
    case P_SCALE: zcat(s, ZSCALES[v].name); break;
    case P_KEYS: zcat(s, v ? "CHROMATIC" : "SCALE"); break;
    case P_SPREAD: zcat(s, v ? "ON" : "OFF"); break;
    default: num(s, (int32_t)v);
    }
}

static void draw_row(uint32_t page, uint32_t r, int hot)
{
    uint32_t p = page * 4u + r;
    char s[16];
    int32_t y = 88 + (int32_t)r * 24;
    char lab[16] = "1  ";
    lab[0] = (char)('1' + r);
    zcat(lab, PARAM[p].name);
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
    draw_text_box(0, 14, 240, &FONT_L, PAGE_NAME[page], ZC_BRASS, 1);
    draw_info();
    for (r = 0; r < 4u; r++) draw_row(page, r, 0);
}

static void draw_status(uint32_t cpu_pct, int32_t lim_tenths, uint32_t voices)
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
    draw_text_box(0, 212, 240, &FONT_S, c, ZC_DIM, 1);
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
#ifdef ZL_SET                                   /* the host tests set their own: -DZL_SET='set.p[P_CUT]=100;...' */
    ZL_SET;
#else
    if (settings_load(&set, sizeof set)) set.oct = 0;
#endif
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    for (k = 0; k < P_N; k++)
        if (set.p[k] < PARAM[k].lo || set.p[k] > PARAM[k].hi) set.p[k] = PARAM[k].def;
    for (k = 0; k < 27u; k++) key_chord[k] = -1;
    map_keys();
    apply();

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_page(page);
    draw_status(0, 0, 0);

    for (;;) {
        uint32_t keys, down, up, b, rel, lit, voices = 0, chromatic = set.p[P_KEYS];
        uint8_t st, d1, d2;
        int m7, m9, sus, strum, redraw_info = 0;
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
        {
            uint32_t np = b & (1u << BTN_HOME) ? 0u : b & (1u << BTN_EDIT) ? 1u : b & (1u << BTN_ENV) ? 2u
                        : b & (1u << BTN_GLO) ? 3u : page;
            if (np != page) { page = np; hot = 9; draw_page(page); }
        }
        if (b & (1u << BTN_SEL)) {              /* SEL: scale keys <-> chromatic keys */
            set.p[P_KEYS] ^= 1u; dirty = 1; t_change = fm1_ms;
            if (page == 3u) draw_row(3, 2, 0);
            redraw_info = 1;
        }

        for (k = 0; k < 4u; k++) {              /* the four knobs: one step a click, faster when turned fast */
            uint32_t p = page * 4u + k;
            int32_t d = fm1_enc_take(ENC_KNOB1 + k), nv;
            if (!d) continue;
            if (PARAM[p].hi > 20u && (d > 1 || d < -1)) d *= 2;
            nv = (int32_t)set.p[p] + d;
            if (nv < PARAM[p].lo) nv = PARAM[p].lo;
            if (nv > PARAM[p].hi) nv = PARAM[p].hi;
            set.p[p] = (uint8_t)nv;
            apply();
            if (hot != k && hot < 4u) draw_row(page, hot, 0);
            hot = k; t_hot = fm1_ms;
            draw_row(page, k, 1);
            if (p >= P_KEY) { last_chord = -1; redraw_info = 1; }
            dirty = 1; t_change = fm1_ms;
        }
        if (hot < 4u && fm1_ms - t_hot > 1500u) { draw_row(page, hot, 0); hot = 9; }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI: chromatic, with velocity */
            if ((st & 0xF0u) == 0x90u && d2) { release_tag(midi_tag[d1 & 127u]); midi_tag[d1 & 127u] = play(d1, d2, 0); }
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) { release_tag(midi_tag[d1 & 127u]); midi_tag[d1 & 127u] = 0; }
        }

        if (redraw_info) draw_info();
        lit = keys;
        for (k = 0; k < ZV; k++) if (zv[k].stage) voices++;
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);
        fm1_led_key(BTN_HOME, page == 0u);
        fm1_led_key(BTN_EDIT, page == 1u);
        fm1_led_key(BTN_ENV, page == 2u);
        fm1_led_key(BTN_GLO, page == 3u);
        fm1_led_key(BTN_SEL, set.p[P_KEYS] != 0u);

        if (fm1_ms - t_stat >= 250u || voices != voices_drawn) {     /* 4 times a second: CPU and limiter */
            uint32_t cpu = cpu_ticks_max * 100u / (5805u * FM1_TICKS_PER_US);
            int32_t g = lim_min_seen;
            cpu_ticks_max = 0;
            lim_min_seen = ONE30;
            draw_status(cpu, lim_db_tenths(g), voices);
            t_stat = fm1_ms;
            voices_drawn = voices;
        }

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
