/* SPDX-License-Identifier: GPL-3.0-only */
/* Scales and chords, as Zenthesizer has them (Source/dsp/Maps.h): the scale list, the seven chords of the key
 * built by stacking thirds in a seven-note scale (five-note scales borrow the one they sit in), 7th / 9th /
 * sus / spread, their names and Roman numerals. Plain C with no platform: tests/chords_test.cpp checks every
 * chord against the plugin's own buildChord. */
#ifndef ZEN_CHORDS_H
#define ZEN_CHORDS_H
#include <stdint.h>

typedef struct { const char *name; uint8_t n; int8_t steps[12]; } zscale_t;

#define ZS_COUNT 11
static const zscale_t ZSCALES[ZS_COUNT] = {
    {"CHROMATIC", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"MAJOR", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"MINOR", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"DORIAN", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"LYDIAN", 7, {0, 2, 4, 6, 7, 9, 11}},
    {"MAJ PENT", 5, {0, 2, 4, 7, 9}},
    {"MIN PENT", 5, {0, 3, 5, 7, 10}},
    {"HIRAJOSHI", 5, {0, 2, 3, 7, 8}},
    {"IN-SEN", 5, {0, 1, 5, 7, 10}},
    {"CELTIC", 5, {0, 2, 3, 7, 9}},
    {"WHOLE TONE", 6, {0, 2, 4, 6, 8, 10}},
};

static const zscale_t *zchord_scale(int sc)    /* the seven-note scale a scale's chords come from */
{
    static const zscale_t major = {"Major", 7, {0, 2, 4, 5, 7, 9, 11}}, minor = {"Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
                          dorian = {"Dorian", 7, {0, 2, 3, 5, 7, 9, 10}}, lydian = {"Lydian", 7, {0, 2, 4, 6, 7, 9, 11}},
                          phrygian = {"Phrygian", 7, {0, 1, 3, 5, 7, 8, 10}};
    switch (sc < 0 ? 0 : sc > 10 ? 10 : sc) {
    case 2: case 6: case 7: return &minor;
    case 3: case 9: return &dorian;
    case 4: case 10: return &lydian;
    case 8: return &phrygian;
    default: return &major;
    }
}

typedef struct {
    int8_t n;
    uint8_t notes[6];
    int16_t root_note;
    int8_t third, fifth, seventh, ninth;        /* intervals above the root; -1 = not in it */
} zchord_t;

static int zc_step(const zscale_t *s, int k) { return s->steps[k % 7] + 12 * (k / 7); }

static zchord_t zchord_build(int degree, int root, int sc, int octave, int add7, int add9, int sus, int spread)
{
    const zscale_t *s = zchord_scale(sc);
    zchord_t c = {0, {0}, 48, 4, 7, -1, -1};
    int iv[6], m = 0, i, j, base, at;
    if (degree < 0) degree = 0;
    if (degree > 6) degree = 6;
    base = 60 + root + octave * 12 + zc_step(s, degree);
    if (base > 60 + octave * 12 + 7) base -= 12;
    c.root_note = (int16_t)base;
    at = zc_step(s, degree);
    c.third = (int8_t)(zc_step(s, degree + 2) - at);
    c.fifth = (int8_t)(zc_step(s, degree + 4) - at);
    if (sus) {
        int second = zc_step(s, degree + 1) - at, fourth = zc_step(s, degree + 3) - at;
        if (second == 2) c.third = 2;
        else if (fourth == 5) c.third = 5;
    }
    if (add7) c.seventh = (int8_t)(zc_step(s, degree + 6) - at);
    if (add9) c.ninth = (int8_t)(zc_step(s, degree + 8) - at);
    if (spread) {
        iv[m++] = -12; iv[m++] = c.fifth; iv[m++] = c.third + 12;
        if (c.seventh >= 0) iv[m++] = c.seventh + 12;
        if (c.ninth >= 0) iv[m++] = c.ninth + 12;
    } else {
        iv[m++] = 0; iv[m++] = c.third; iv[m++] = c.fifth;
        if (c.seventh >= 0) iv[m++] = c.seventh;
        if (c.ninth >= 0) iv[m++] = c.ninth;
    }
    for (i = 1; i < m; i++)                     /* sort, low to high */
        for (j = i; j > 0 && iv[j - 1] > iv[j]; j--) { int t = iv[j]; iv[j] = iv[j - 1]; iv[j - 1] = t; }
    for (i = 0; i < m && c.n < 6; i++) {
        int note = base + iv[i];
        c.notes[c.n++] = (uint8_t)(note < 0 ? 0 : note > 127 ? 127 : note);
    }
    return c;
}

static int zkey_flats(int root, int sc)         /* spell with flats in flat keys */
{
    const zscale_t *s = zchord_scale(sc);
    int to_major = s->steps[2] == 3 ? (s->steps[1] == 1 ? 8 : s->steps[5] == 9 ? 10 : 3) : (s->steps[3] == 6 ? 7 : 0);
    int rel = (root + to_major) % 12;
    return rel == 5 || rel == 10 || rel == 3 || rel == 8 || rel == 1;
}

static const char *znote_name(int pc, int flats)
{
    static const char *const SH[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const FL[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    return (flats ? FL : SH)[((pc % 12) + 12) % 12];
}

static void zcat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }

static void zchord_name(const zchord_t *c, int flats, char *out)   /* "Cm7", "Ebmaj9", "Gsus2", "Bdim" (out: 16 bytes) */
{
    int sus = c->third == 2 || c->third == 5, minor = c->third == 3;
    int dim = minor && c->fifth == 6, aug = c->third == 4 && c->fifth == 8;
    const char *ext = c->ninth >= 0 && c->seventh >= 0 ? "9" : c->seventh >= 0 ? "7" : "";
    out[0] = 0;
    zcat(out, znote_name(c->root_note, flats));
    if (sus) {
        if (c->seventh >= 0) { if (c->seventh == 11) zcat(out, "maj"); zcat(out, ext); }
        zcat(out, c->third == 2 ? "sus2" : "sus4");
    } else if (dim) {
        if (c->seventh == 10) { zcat(out, "m"); zcat(out, ext); zcat(out, "b5"); }
        else if (c->seventh == 9) zcat(out, "dim7");
        else zcat(out, "dim");
    } else if (aug) { zcat(out, "+"); zcat(out, ext); }
    else if (minor) {
        if (c->seventh == 11) { zcat(out, "m(maj"); zcat(out, ext); zcat(out, ")"); }
        else { zcat(out, "m"); zcat(out, ext); }
    } else { if (c->seventh == 11) zcat(out, "maj"); zcat(out, ext); }
    if (c->ninth >= 0 && c->seventh < 0) zcat(out, "add9");
}

static void zchord_numeral(int degree, int sc, char *out)   /* "IV", "ii", "vii*" (* = diminished) */
{
    static const char *const UP[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char *const LO[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    zchord_t c = zchord_build(degree, 0, sc, 0, 0, 0, 0, 0);
    if (degree < 0) degree = 0;
    if (degree > 6) degree = 6;
    out[0] = 0;
    if (c.third == 3) { zcat(out, LO[degree]); if (c.fifth == 6) zcat(out, "*"); }
    else { zcat(out, UP[degree]); if (c.fifth == 8) zcat(out, "+"); }
}

static int zscale_note(int j, int root, int sc, int base)   /* the j-th note of the scale from root, above base */
{
    const zscale_t *s = &ZSCALES[sc < 0 ? 0 : sc >= ZS_COUNT ? ZS_COUNT - 1 : sc];
    return base + root + s->steps[j % s->n] + 12 * (j / s->n);
}
#endif
