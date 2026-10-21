// SPDX-License-Identifier: GPL-3.0-only
// The reference: the real Zenthesizer engine (float, from the plugin's source) playing the same patch and
// the same notes as a Zen-Lite host script, so the two WAVs can be compared (tests/compare.py).
//   c++ -std=c++17 -O2 -I$ZEN_SRC/Source tests/ref/zen_ref.cpp -o build/host/zen_ref
//   build/host/zen_ref script.txt out.wav NAME=V ...      (Zen-Lite's knobs, 0..127: CUT RES POS MIX WAVE SHAPE
//                                                          DET TUNE ATT DEC SUS REL; unset ones take Zen-Lite's defaults)
#include "dsp/Engine.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace zen;

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: zen_ref script out.wav NAME=V ...\n"); return 1; }
    const float fs = 44100.0f;
    Engine eng; eng.prepare (fs);
    Settings s; s.padNotes.fill (-1);           // no pads: every note is a key (notes 36-51 would be the plates)
    // Zen-Lite's knob defaults, then the ones given; each knob maps exactly as the firmware maps it (k * 1024 / 127)
    struct { const char* n; int v; } k[] = { {"CUT",79},{"RES",38},{"POS",57},{"MIX",64},{"WAVE",0},{"SHAPE",0},{"DET",25},
                                             {"TUNE",24},{"ATT",38},{"DEC",64},{"SUS",102},{"REL",76},
                                             {"TABLE",1},{"WARP",0},{"CHORUS",0},{"REVERB",0},{"RDECAY",64},{"SHIMMER",0},
                                             {"SIZE",76},{"DAMP",64},{"DEPTH",64} };
    for (int a = 3; a < argc; ++a)
        for (auto& e : k)
            if (! std::strncmp (argv[a], e.n, std::strlen (e.n)) && argv[a][std::strlen (e.n)] == '=') e.v = std::atoi (argv[a] + std::strlen (e.n) + 1);
    auto kv = [&] (const char* n) { for (auto& e : k) if (! std::strcmp (e.n, n)) return e.v; return 0; };
    auto q = [&] (const char* n) { return (float) (kv (n) * 1024 / 127) / 1024.0f; };
    const float mix = q ("MIX");
    s.aLevel = std::min (1.0f, (1.0f - mix) * 2.0f); s.bLevel = std::min (1.0f, mix * 2.0f);
    s.aWave = (WaveA) kv ("WAVE"); s.aShape = q ("SHAPE"); s.aDetune = q ("DET"); s.aTune = 0.5f + (kv ("TUNE") - 24) / 48.0f;
    s.bTable = kv ("TABLE"); s.bUnison = 0.0f; s.bWarp = q ("WARP"); s.air = 0.0f;
    s.bPos = q ("POS"); s.cutoff = q ("CUT"); s.res = q ("RES");
    s.drive = 0.0f; s.envAmt = 0.5f; s.keyTrack = 0.5f;
    s.ampA = q ("ATT"); s.ampD = q ("DEC"); s.ampS = q ("SUS"); s.ampR = q ("REL");
    s.spread = 0.5f; s.volume = 0.857f; s.velSens = 0.5f;
    auto fx = [&] (const char* n) { return kv (n) / 127.0f; };   // the effects take the knob straight, as Zen-Lite's
    s.chorusMix = fx ("CHORUS"); s.chorusDepth = fx ("DEPTH"); s.chorusRate = 0.3f; s.delayMix = 0.0f; s.glide = 0.0f;
    s.reverbMix = fx ("REVERB"); s.reverbDecay = fx ("RDECAY"); s.reverbShimmer = fx ("SHIMMER");
    s.reverbSize = fx ("SIZE"); s.reverbDamp = fx ("DAMP");

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
