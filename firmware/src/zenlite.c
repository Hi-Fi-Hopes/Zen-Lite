/* SPDX-License-Identifier: GPL-3.0-only */
/* Zen-Lite: the Zenthesizer, cut down for the M-VAVE FM-1. Built on Dinghy (Felucca's and SLOOP's platform).
 *
 * Step 1, the proof: one Zenthesizer voice, six of them polyphonic.
 *   keys       play the GLASS wavetable through the ladder filter (6 voices); notes go out over USB-MIDI
 *   KNOB 1     POSITION  (scans the GLASS frames)
 *   KNOB 2     CUTOFF
 *   KNOB 3     RESONANCE
 *   KNOB 4     RELEASE
 *   OCT- OCT+  the octave        MASTER  the volume (platform.c)
 *   USB-MIDI   notes in play it, with their velocity
 * The screen shows the knobs, the CPU the sound takes (keep it under 50 %), and how hard the limiter works.
 *
 * Integers only: the chip has no FPU and no libgcc. tests/run_host.sh plays this file on a computer. */
#define APP_NAME "ZEN-LITE"
#include "platform.c"
#include "zen_voice.h"
#ifdef DINGHY_HOST
#define FM1_TICKS_PER_US 24u
#endif

/* ---- defaults (the host tests override them to try the worst cases) */
#ifndef ZL_POS
#define ZL_POS 57                               /* knobs 0..127 */
#endif
#ifndef ZL_CUT
#define ZL_CUT 79
#endif
#ifndef ZL_RES
#define ZL_RES 70
#endif
#ifndef ZL_REL
#define ZL_REL 76
#endif
#define ZL_ATT 300                              /* Q10: attack 0.30 (18 ms), decay 0.5, sustain 0.8 */
#define ZL_DEC 512
#define ZL_SUS 819

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

/* ---- notes (main loop) */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t ages;

static void play(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, inc, lv = 0;
    zvoice_t *z;
    if (note > 127u || !vel) return;
    for (v = 0; v < ZV; v++) {                  /* the same note, else a silent voice, else the quietest released, else the oldest */
        uint32_t score = zv[v].note == note && zv[v].gate ? 0u
                       : zv[v].gate ? 0x80000000u + zv[v].age
                       : zv[v].stage ? 1u + ((uint32_t)zv[v].env >> 2) : 0u;
        if (score < best) { best = score; pick = v; }
    }
    inc = INC_C9[note % 12u] >> (10u - note / 12u);
    while (lv < ZT_LEVELS - 1u && (uint64_t)(ZT_MAXH >> lv) * inc > 1932735283ull)   /* harmonics under 0.45 fs */
        lv++;
    z = &zv[pick];
    z->gate = 0;
    z->note = (uint8_t)note;
    z->age = ++ages;
    z->inc = inc;
    z->lv = lv;
    z->vel = (int32_t)(16384u + vel * 129u);    /* velocity sensitivity 0.5: 1 - 0.5 (1 - v), Q15 */
    z->ktrack = ((int32_t)note - 60) * 4348 / 1000;   /* key tracking 0.5, as the plugin */
    z->gate = 1;
    z->trig++;
}
static void stop(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < ZV; v++)
        if (zv[v].note == note)
            zv[v].gate = 0;
}
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < ZV; v++)
        if (zv[v].gate || zv[v].stage) return 0;
    return 1;
}

/* ---- the knobs */
typedef struct { int8_t oct; uint8_t knob[4]; } set_t;
static int32_t q10(uint32_t k) { return (int32_t)(k * 1024u / 127u); }
static void apply(const set_t *s)
{
    zp_pos = q10(s->knob[0]);
    zp_cut = q10(s->knob[1]);
    zen_set_res(q10(s->knob[2]));
    zen_set_env(ZL_ATT, ZL_DEC, ZL_SUS, q10(s->knob[3]));
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

static void draw_knob(uint32_t k, uint32_t val, int hot)
{
    static const char *const LBL[4] = {"1  POSITION", "2  CUTOFF", "3  RESONANCE", "4  RELEASE"};
    char s[8];
    int32_t y = 92 + (int32_t)k * 22;
    num(s, (int32_t)val);
    draw_text_box(14, y, 150, &FONT_S, LBL[k], hot ? ZC_AMBER : ZC_BRASS, 0);
    draw_text_box(164, y, 62, &FONT_S, s, ZC_BONE, 2);
}

static void draw_status(int32_t oct, uint32_t cpu_pct, int32_t lim_tenths, uint32_t voices)
{
    char a[40] = "CPU ", b[8];
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
    {
        char c[32] = "OCT +0   VOICES ";
        c[4] = oct < 0 ? '-' : '+';
        c[5] = (char)('0' + (oct < 0 ? -oct : oct));
        c[16] = (char)('0' + voices);
        c[17] = '/';
        c[18] = (char)('0' + ZV);
        c[19] = 0;
        draw_text_box(0, 212, 240, &FONT_S, c, ZC_DIM, 1);
    }
}

static int32_t lim_db_tenths(int32_t g)        /* gain Q30 -> reduction in tenths of a dB (0.1 dB = x 0.98855) */
{
    int32_t t = 0, x = ONE30;
    while (x > g && t < 400) { x = M30(x, 1061442400); t++; }
    return t;
}

static void app_main(void)
{
    set_t set = {0, {ZL_POS, ZL_CUT, ZL_RES, ZL_REL}};
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_stat = 0, voices_drawn = 99, hot = 9, t_hot = 0;
    uint8_t held[27] = {0};
    settings_load(&set, sizeof set);
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    for (k = 0; k < 4u; k++) if (set.knob[k] > 127u) set.knob[k] = 64u;
    apply(&set);

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 16, 240, &FONT_L, APP_NAME, ZC_BRASS, 1);
    draw_text_box(0, 54, 240, &FONT_S, "glass voice test  v" APP_VER, ZC_DIM, 1);
    for (k = 0; k < 4u; k++) draw_knob(k, set.knob[k], 0);
    draw_status(set.oct, 0, 0, 0);

    for (;;) {
        uint32_t keys, down, up, b, rel, lit = 0, base = (uint32_t)(53 + 12 * set.oct), voices = 0;
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); play(held[k], 100); midi_send(0x90, held[k], 100); }
            if (up >> k & 1u) { stop(held[k]); midi_send(0x80, held[k], 0); }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; draw_status(set.oct, 0, 0, 0); }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; draw_status(set.oct, 0, 0, 0); }

        for (k = 0; k < 4u; k++) {              /* the four knobs: one step a click, faster when turned fast */
            int32_t d = fm1_enc_take(ENC_KNOB1 + k), nv;
            if (!d) continue;
            if (d > 1 || d < -1) d *= 2;
            nv = (int32_t)set.knob[k] + d;
            if (nv < 0) nv = 0;
            if (nv > 127) nv = 127;
            set.knob[k] = (uint8_t)nv;
            apply(&set);
            if (hot != k && hot < 4u) draw_knob(hot, set.knob[hot], 0);
            hot = k; t_hot = fm1_ms;
            draw_knob(k, set.knob[k], 1);
            dirty = 1; t_change = fm1_ms;
        }
        if (hot < 4u && fm1_ms - t_hot > 1500u) { draw_knob(hot, set.knob[hot], 0); hot = 9; }

        while (midi_get(&st, &d1, &d2)) {
            if ((st & 0xF0u) == 0x90u && d2) play(d1, d2);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) stop(d1);
        }

        for (k = 0; k < ZV; k++) {
            if (zv[k].stage) voices++;
            if (zv[k].gate && zv[k].note >= base && zv[k].note < base + 27u)
                lit |= 1u << (zv[k].note - base);
        }
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);

        if (fm1_ms - t_stat >= 250u || voices != voices_drawn) {     /* 4 times a second: CPU and limiter */
            uint32_t cpu = cpu_ticks_max * 100u / (5805u * FM1_TICKS_PER_US);
            int32_t g = lim_min_seen;
            cpu_ticks_max = 0;
            lim_min_seen = ONE30;
            draw_status(set.oct, cpu, lim_db_tenths(g), voices);
            t_stat = fm1_ms;
            voices_drawn = voices;
        }

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
