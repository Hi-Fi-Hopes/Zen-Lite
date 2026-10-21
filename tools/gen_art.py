#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Zen-Lite's look, drawn once on a computer and stored in the firmware: firmware/gen/zen_art.h
  - the slab: the mock-ups' tree-ring cookie (from their renderer), 240 x 174, 256 colours (RGB565)
  - four fonts from Zenthesizer's own (Instrument Serif Italic, Instrument Sans; SIL OFL 1.1), smooth edged,
    in Felucca's font format (4-bit coverage, gfx.c felucca_font_t)
Run: ZEN_SRC=/path/to/Zenthesizer python3 tools/gen_art.py   (needs Pillow)"""
import math, os, random
from PIL import Image, ImageDraw, ImageFont

ZEN = os.environ.get("ZEN_SRC", os.path.join(os.path.dirname(__file__), "../../hi-fi-hopes/zenthesizer"))
RES = os.path.join(ZEN, "Resources")
OUT = os.path.join(os.path.dirname(__file__), "../firmware/gen/zen_art.h")
S = 4                                           # supersampling

# ---- the theme (Zenthesizer Source/ui/Theme.h, as the mock-ups)
table, glass = 0x14100C, 0x0D0906
heart = [0xB48150, 0xC79865, 0xDBB788, 0xEAD0A8]

def C(h, a=255): return ((h >> 16) & 255, (h >> 8) & 255, h & 255, a)
def lerp(a, b, t):
    ca, cb = C(a), C(b)
    return tuple(int(ca[i] + (cb[i] - ca[i]) * t) for i in range(3))
def heartwood(t):
    t = max(0.0, min(1.0, t)) * 3
    i = min(2, int(t))
    return lerp(heart[i], heart[i + 1], t - i)

def wobble_poly(cx, cy, r, seed, amp=0.022, n=180):
    rnd = random.Random(seed)
    ph = [rnd.uniform(0, 6.28) for _ in range(4)]
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        rr = r * (1 + amp * (math.sin(2 * t + ph[0]) * .6 + math.sin(3 * t + ph[1]) * .3 + math.sin(7 * t + ph[2]) * .15))
        pts.append(((cx + rr * math.cos(t)) * S, (cy + rr * math.sin(t)) * S))
    return pts

W, H = 240, 174                                 # the middle of the screen, under the header, over the cards
CX, CY, R = 120, 82, 62                         # the slab (zenlite.c draws around it: keep these in step)

def slab_image():
    img = Image.new("RGBA", (W * S, H * S), C(table))
    d = ImageDraw.Draw(img)
    rnd = random.Random(3)                      # the table it lies on: faint long grain
    for k in range(70):
        y = rnd.uniform(-10, H + 10) * S
        col = lerp(table, 0x2A1C10, rnd.uniform(0.2, 0.7))
        pts = [(x * S, y + math.sin(x / rnd.uniform(18, 40) + k) * S * rnd.uniform(0.5, 2.5)) for x in range(-4, W + 5, 4)]
        d.line(pts, fill=col + (110,), width=max(1, int(S * rnd.uniform(0.4, 1.2))))
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    seed, rings, r = 7, 22, R                   # the cookie: bark, rings, pith, a few checks (render.py slab)
    d.polygon(wobble_poly(CX, CY, r * 1.07, seed, .03), fill=C(0x120A05))
    d.polygon(wobble_poly(CX, CY, r * 1.035, seed, .028), fill=C(0x3A2414))
    for k in range(rings, 0, -1):
        t = k / rings
        poly = wobble_poly(CX, CY, r * t, seed + k, .02 + .01 * t)
        d.polygon(poly, fill=heartwood(1 - t * 0.85) + (255,))
        d.line(poly + [poly[0]], fill=C(0x6B3F1C, 150), width=max(1, int(S * 0.6)))
    d.ellipse([(CX - 2) * S, (CY - 2) * S, (CX + 2) * S, (CY + 2) * S], fill=C(0x4A2A12))
    rnd = random.Random(seed)
    for _ in range(3):
        a = rnd.uniform(0, 6.28); l = rnd.uniform(.35, .8)
        d.line([(CX + r * .9 * math.cos(a)) * S, (CY + r * .9 * math.sin(a)) * S,
                (CX + r * l * math.cos(a)) * S, (CY + r * l * math.sin(a)) * S], fill=C(0x2A160A, 200), width=int(S * 0.9))
    img.alpha_composite(lay)
    small = img.convert("RGB").resize((W, H), Image.LANCZOS)
    q = small.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    rgb565 = [((pal[3 * i] >> 3) << 11) | ((pal[3 * i + 1] >> 2) << 5) | (pal[3 * i + 2] >> 3) for i in range(256)]
    return list(q.getdata()), rgb565, small

# ---- fonts: (C name, file, size in px, first, last)
FONTS = [("ZF_NAME", "InstrumentSerif-Italic.ttf", 23, 32, 126),     # preset names, chords
         ("ZF_VAL", "InstrumentSans-Medium.ttf", 14, 32, 126),       # card values
         ("ZF_TXT", "InstrumentSans-Medium.ttf", 10.5, 32, 126),     # the header's right, the foot
         ("ZF_LAB", "InstrumentSans-SemiBold.ttf", 8.5, 32, 95)]     # labels: capitals, tracked

def font(cname, file, px, first, last):
    f = ImageFont.truetype(os.path.join(RES, file), int(round(px * S)))
    asc, desc = f.getmetrics()
    h = int(math.ceil((asc + desc) / S))
    chars = [chr(c) for c in range(first, last + 1)]
    lefts = [f.getbbox(ch, anchor="la")[0] for ch in chars if ch.strip()]
    pad = int(math.ceil(max(0, -min(lefts)) / S)) + 1
    adv, bw, off, data = [], [], [], bytearray()
    for ch in chars:
        a = int(round(f.getlength(ch) / S))
        box = f.getbbox(ch, anchor="la")
        wide = int(math.ceil(box[2] / S)) + pad + 2
        im = Image.new("L", (wide * S, h * S), 0)
        ImageDraw.Draw(im).text((pad * S, 0), ch, font=f, fill=255, anchor="la")
        im = im.resize((wide, h), Image.BOX)
        px = im.load()
        w = 0
        for x in range(wide):
            if any(px[x, y] > 8 for y in range(h)): w = x + 1
        bpr = (w + 1) // 2
        off.append(len(data))
        for y in range(h):
            row = bytearray(bpr)
            for x in range(w):
                v = min(15, (px[x, y] * 15 + 127) // 255)
                row[x // 2] |= v << 4 if x % 2 == 0 else v
            data += row
        adv.append(a); bw.append(w)
    return h, pad, adv, bw, off, data

def main():
    idx, pal, preview = slab_image()
    L = ["/* SPDX-License-Identifier: GPL-3.0-only */",
         "/* Generated by tools/gen_art.py: the slab (from the mock-ups' renderer) and fonts from Zenthesizer's",
         " * Instrument Serif and Instrument Sans (SIL OFL 1.1, assets/fonts/). Do not edit. */",
         "#ifndef ZEN_ART_H", "#define ZEN_ART_H", "#include <stdint.h>",
         f"#define ZA_W {W}u", f"#define ZA_H {H}u", f"#define ZA_CX {CX}", f"#define ZA_CY {CY}", f"#define ZA_R {R}",
         "static const uint16_t ZA_PAL[256] = {"]
    for i in range(0, 256, 16): L.append("  " + ",".join(f"0x{v:04X}" for v in pal[i:i + 16]) + ",")
    L.append("};")
    L.append(f"static const uint8_t ZA_SLAB[{W * H}] = {{")
    for i in range(0, len(idx), 40): L.append("  " + ",".join(map(str, idx[i:i + 40])) + ",")
    L.append("};")
    sizes = {}
    for cname, file, px, first, last in FONTS:
        h, pad, adv, bw, off, data = font(cname, file, px, first, last)
        sizes[cname] = len(data)
        n = cname.lower()
        L.append(f"static const uint8_t {n}_adv[{len(adv)}] = {{{','.join(map(str, adv))}}};")
        L.append(f"static const uint8_t {n}_bw[{len(bw)}] = {{{','.join(map(str, bw))}}};")
        L.append(f"static const uint16_t {n}_off[{len(off)}] = {{{','.join(map(str, off))}}};")
        L.append(f"static const uint8_t {n}_data[{len(data)}] = {{")
        for i in range(0, len(data), 40): L.append("  " + ",".join(map(str, data[i:i + 40])) + ",")
        L.append("};")
        L.append(f"static const felucca_font_t {cname} = {{{h}, {pad}, {first}, {last}, {n}_adv, {n}_bw, {n}_off, {n}_data}};")
    L.append("#endif")
    open(OUT, "w").write("\n".join(L) + "\n")
    preview.save(os.path.join(os.path.dirname(OUT), "../../build/slab_preview.png")) if os.path.isdir(
        os.path.join(os.path.dirname(OUT), "../../build")) else None
    print(f"slab {W * H} B + palette 512 B; fonts", {k: v for k, v in sizes.items()})

if __name__ == "__main__":
    main()
