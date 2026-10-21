// SPDX-License-Identifier: GPL-3.0-only
// The reference: the real Zenthesizer engine (float, from the plugin's source) playing the same patch and
// the same notes as a Zen-Lite host script, so the two WAVs can be compared (tests/compare.py).
//   c++ -std=c++17 -O2 -I$ZEN_SRC/Source tests/ref/zen_ref.cpp -o build/host/zen_ref
//   build/host/zen_ref script.txt out.wav POS CUT RES REL      (knobs 0..127, as Zen-Lite's)
#include "dsp/Engine.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace zen;

int main (int argc, char** argv)
{
    if (argc < 7) { std::fprintf (stderr, "usage: zen_ref script out.wav POS CUT RES REL\n"); return 1; }
    const float fs = 44100.0f;
    Engine eng; eng.prepare (fs);
    Settings s; s.padNotes.fill (-1);           // no pads: every note is a key (notes 36-51 would be the plates)
    s.aLevel = 0.0f; s.bLevel = 1.0f; s.bTable = 1; s.bUnison = 0.0f; s.bWarp = 0.0f; s.air = 0.0f;
    s.bPos = std::atoi (argv[3]) / 127.0f; s.cutoff = std::atoi (argv[4]) / 127.0f;
    s.res = std::atoi (argv[5]) / 127.0f; s.ampR = std::atoi (argv[6]) / 127.0f;
    s.drive = 0.0f; s.envAmt = 0.5f; s.keyTrack = 0.5f;
    s.ampA = 300.0f / 1024.0f; s.ampD = 0.5f; s.ampS = 0.8f;
    s.spread = 0.5f; s.volume = 0.857f; s.velSens = 0.5f;
    s.chorusMix = 0.0f; s.delayMix = 0.0f; s.reverbMix = 0.0f; s.glide = 0.0f;

    // the script: "T keys MASK" (bit k = F3 + k), "T midi ST D1 D2", "T btn 1" (octave up), "T end"
    struct Ev { int t; std::vector<MidiEvent> ev; };
    std::vector<int> times; std::vector<MidiEvent> evs; int endMs = 6500;
    FILE* f = std::fopen (argv[1], "r"); if (! f) { std::perror (argv[1]); return 1; }
    char line[256], what[16]; unsigned prevKeys = 0; int oct = 0;
    std::vector<std::pair<int, MidiEvent>> sched;
    unsigned char held[27] {};
    while (std::fgets (line, sizeof line, f))
    {
        int t; if (line[0] == '#' || std::sscanf (line, "%d %15s", &t, what) != 2) continue;
        const int at = (int) ((long long) t * 44100 / 1000);
        if (! std::strcmp (what, "end")) { endMs = t; continue; }
        if (! std::strcmp (what, "btn")) { int b; std::sscanf (line, "%*d %*s %d", &b); if (b == 1) ++oct; if (b == 0) --oct; continue; }
        if (! std::strcmp (what, "midi")) { unsigned a, b, c; std::sscanf (line, "%*d %*s %i %u %u", (int*) &a, &b, &c);
            sched.push_back ({ at, { 0, (uint8_t) a, (uint8_t) b, (uint8_t) c } }); continue; }
        if (! std::strcmp (what, "keys")) {
            unsigned k; std::sscanf (line, "%*d %*s %i", (int*) &k);
            for (int i = 0; i < 27; ++i) {
                const bool was = prevKeys >> i & 1u, is = k >> i & 1u;
                if (is && ! was) { held[i] = (unsigned char) (53 + 12 * oct + i); sched.push_back ({ at, { 0, 0x90, held[i], 100 } }); }
                if (was && ! is) sched.push_back ({ at, { 0, 0x80, held[i], 0 } });
            }
            prevKeys = k;
        }
    }
    std::fclose (f);

    const int total = (int) ((long long) endMs * 44100 / 1000), blk = 256;
    std::vector<int16_t> out; out.reserve ((size_t) total * 2);
    std::vector<float> L (blk), R (blk);
    for (int pos = 0; pos < total; pos += blk)
    {
        std::vector<MidiEvent> here;
        for (auto& e : sched) if (e.first >= pos && e.first < pos + blk) { auto c = e.second; c.offset = e.first - pos; here.push_back (c); }
        eng.process (s, here.data(), (int) here.size(), L.data(), R.data(), blk);
        for (int i = 0; i < blk; ++i) for (float v : { L[(size_t) i], R[(size_t) i] })
        {
            const float c = std::max (-1.0f, std::min (1.0f, v));
            out.push_back ((int16_t) std::lround (c * 32767.0f));
        }
    }
    FILE* w = std::fopen (argv[2], "wb");
    const uint32_t bytes = (uint32_t) out.size() * 2, v[] = { 16, 0x00020001u, 44100, 44100 * 4, 0x00100004u };
    const uint32_t riff = bytes + 36;
    std::fwrite ("RIFF", 1, 4, w); std::fwrite (&riff, 4, 1, w); std::fwrite ("WAVEfmt ", 1, 8, w); std::fwrite (v, 4, 5, w);
    std::fwrite ("data", 1, 4, w); std::fwrite (&bytes, 4, 1, w); std::fwrite (out.data(), 2, out.size(), w);
    std::fclose (w);
    return 0;
}
