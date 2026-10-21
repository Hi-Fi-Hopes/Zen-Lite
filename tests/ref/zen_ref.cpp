// SPDX-License-Identifier: GPL-3.0-only
// The reference: the real Zenthesizer engine (float, from the plugin's source) playing the same patch and
// the same notes as a Zen-Lite host script, so the two WAVs can be compared (tests/compare.py).
//   tools/make_presets.sh   (once: the stub that lets Presets.h compile without JUCE, in build/presets)
//   c++ -std=c++17 -O2 -Ibuild/presets -I$ZEN_SRC/Source -Ifirmware/src -Ifirmware/gen tests/ref/zen_ref.cpp -o build/host/zen_ref
//   build/host/zen_ref script.txt out.wav NAME=V ...   Zen-Lite's knobs (zen_params.h, without P_), unset ones at
//                                                       Zen-Lite's defaults; S8=src,dst,amt sets mod slot 8 (amt Q10)
//   build/host/zen_ref script.txt out.wav PRESET=i     preset i of firmware/gen/zen_presets.h, from the plugin's values
#include "dsp/Engine.h"
#include "Presets.h"                           // (with the stub processor header: tools/make_presets.sh)
#include "zen_params.h"
#include "zen_presets.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace zen;

// the firmware's parameter names (zen_params.h, in order): NAME=V on the command line sets set.p[P_NAME]
static const char* const PNAME[P_N] = { "PA","PB","PC","PD", "CUT","RES","DRIVE","ENVAMT", "WAVE","SHAPE","DET","ALEV",
    "TABLE","POS","WARP","BLEV", "TUNE","GLIDE","VELS","VOL", "ATT","DEC","SUS","REL", "E1A","E1D","E1S","E1R",
    "E2A","E2D","E2S","E2R", "L1RATE","L1SHAPE","L2RATE","L2SHAPE", "RRATE","RSMOOTH","KTRACK","WIDTH",
    "CHORUS","REVERB","RDECAY","SHIMMER", "SIZE","DAMP","DEPTH","TONE", "DTIME","DFB","DTONE","DELAY",
    "KEY","SCALE","KEYS","SPREAD" };

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: zen_ref script out.wav [NAME=V ...] [S8=src,dst,amt] [PRESET=i]\n"); return 1; }
    const float fs = 44100.0f;
    Engine eng; eng.prepare (fs);
    Settings s; s.padNotes.fill (-1);           // no pads: every note is a key (notes 36-51 would be the plates)
    int k[P_N]; zps_t sl[ZM_NSLOTS] {};
    for (int i = 0; i < P_N; ++i) k[i] = PARAM[i].def;
    for (int i = 0; i < 7; ++i) sl[i] = ZKIT[i];
    int preset = -1;
    for (int a = 3; a < argc; ++a)
    {
        const char* eq = std::strchr (argv[a], '=');
        if (! eq) continue;
        const std::string n (argv[a], (size_t) (eq - argv[a]));
        if (n == "PRESET") { preset = std::atoi (eq + 1); continue; }
        if (n[0] == 'S' && n.size() <= 3 && std::isdigit ((unsigned char) n[1]))
        {
            const int i = std::atoi (n.c_str() + 1) - 1, src = std::atoi (eq + 1);
            const char* c1 = std::strchr (eq + 1, ','); const char* c2 = c1 ? std::strchr (c1 + 1, ',') : nullptr;
            if (i >= 0 && i < ZM_NSLOTS && c2) sl[i] = { (uint8_t) src, (uint8_t) std::atoi (c1 + 1), (int16_t) std::atoi (c2 + 1) };
            continue;
        }
        bool found = false;
        for (int i = 0; i < P_N; ++i) if (n == PNAME[i]) { k[i] = std::atoi (eq + 1); found = true; }
        if (! found) { std::fprintf (stderr, "zen_ref: no knob %s\n", n.c_str()); return 1; }
    }
    if (preset >= 0)
    {
        // a converted preset: the plugin's own values (not the converted knobs), less what Zen-Lite has no engine for
        std::string want = ZPRESET[preset % ZPRESETS].name;
        const presets::Preset* pr = nullptr;
        for (const auto& c : presets::all())
        {
            std::string u; for (const char* q = c.name; *q; ++q) u += (*q == '"' || *q == '\\') ? ' ' : (char) std::toupper ((unsigned char) *q);
            if (u == want) { pr = &c; break; }
        }
        if (! pr) { std::fprintf (stderr, "zen_ref: no plugin preset %s\n", want.c_str()); return 1; }
        Settings d; d.padNotes.fill (-1);
        s = d;
        presets::Slot kitSlots[7]; int ki = 0;
        for (auto& e : presets::kit()) kitSlots[ki++] = e;
        for (int i = 0; i < 7; ++i) s.slots[(size_t) i] = { kitSlots[i].s, kitSlots[i].d, kitSlots[i].amt };
        int n = 7;
        for (auto& e : pr->slots) if (e.d != Dst::bUnison && e.d != Dst::air && n < numSlots) s.slots[(size_t) n++] = { e.s, e.d, e.amt };
        // the values, through the plugin's own apply (by id), from a fresh processor-free table
        for (auto& e : pr->values)
        {
            const std::string id = e.id; const float v = e.v;
            #define F(x) if (id == #x) s.x = v;
            F(aTune) F(aDetune) F(aShape) F(aLevel) F(bPos) F(bWarp) F(bLevel) F(cutoff) F(res) F(drive) F(envAmt)
            F(keyTrack) F(ampA) F(ampD) F(ampS) F(ampR) F(e1A) F(e1D) F(e1S) F(e1R) F(e2A) F(e2D) F(e2S) F(e2R)
            F(lfo1Rate) F(lfo2Rate) F(randRate) F(randSmooth) F(glide) F(spread) F(volume) F(velSens)
            F(chorusMix) F(chorusDepth) F(delayTime) F(delayFeedback) F(delayTone) F(delayMix)
            F(reverbSize) F(reverbDecay) F(reverbDamp) F(reverbShimmer) F(reverbMix)
            #undef F
            if (id == "aWave") s.aWave = (WaveA) (int) v;
            if (id == "bTable") s.bTable = (int) v;
            if (id == "lfo1Shape") s.lfo1Shape = (LfoShape) (int) v;
            if (id == "lfo2Shape") s.lfo2Shape = (LfoShape) (int) v;
            if (id == "delaySync") s.delaySync = (int) v;
        }
        s.bUnison = 0.0f; s.air = 0.0f; s.arpOn = false; s.chorusRate = 0.3f;
    }
    else
    {
        // knobs as the firmware reads them: most as k x 1024 / 127 (q), a few straight as k / 127 (fx)
        auto q = [&] (int i) { return (float) (k[i] * 1024 / 127) / 1024.0f; };
        auto fx = [&] (int i) { return (float) k[i] / 127.0f; };
        s.aWave = (WaveA) k[P_WAVE]; s.aShape = q (P_SHAPE); s.aDetune = q (P_DET); s.aLevel = q (P_ALEV);
        s.aTune = 0.5f + (k[P_TUNE] - 24) / 48.0f;
        s.bTable = k[P_TABLE]; s.bPos = q (P_POS); s.bWarp = q (P_WARP); s.bLevel = q (P_BLEV); s.bUnison = 0.0f; s.air = 0.0f;
        s.cutoff = q (P_CUT); s.res = q (P_RES); s.drive = q (P_DRIVE);
        s.envAmt = 0.5f + (float) std::max (-1024, std::min (1024, (k[P_ENVAMT] - 64) * 1024 / 63)) / 2048.0f;
        s.keyTrack = q (P_KTRACK); s.spread = q (P_WIDTH); s.velSens = q (P_VELS);
        s.volume = fx (P_VOL); s.glide = fx (P_GLIDE);
        s.ampA = q (P_ATT); s.ampD = q (P_DEC); s.ampS = q (P_SUS); s.ampR = q (P_REL);
        s.e1A = q (P_E1A); s.e1D = q (P_E1D); s.e1S = q (P_E1S); s.e1R = q (P_E1R);
        s.e2A = q (P_E2A); s.e2D = q (P_E2D); s.e2S = q (P_E2S); s.e2R = q (P_E2R);
        s.lfo1Rate = q (P_L1RATE); s.lfo1Shape = (LfoShape) k[P_L1SHAPE];
        s.lfo2Rate = q (P_L2RATE); s.lfo2Shape = (LfoShape) k[P_L2SHAPE];
        s.randRate = q (P_RRATE); s.randSmooth = q (P_RSMOOTH);
        s.chorusMix = fx (P_CHORUS); s.chorusDepth = fx (P_DEPTH); s.chorusRate = 0.3f;
        s.reverbMix = fx (P_REVERB); s.reverbDecay = fx (P_RDECAY); s.reverbShimmer = fx (P_SHIMMER);
        s.reverbSize = fx (P_SIZE); s.reverbDamp = fx (P_DAMP);
        s.delayTime = fx (P_DTIME); s.delaySync = 0; s.delayFeedback = fx (P_DFB); s.delayTone = fx (P_DTONE); s.delayMix = fx (P_DELAY);
        for (int i = 0; i < ZM_NSLOTS; ++i) s.slots[(size_t) i] = { (Src) sl[i].src, (Dst) sl[i].dst, sl[i].amt / 1024.0f };
        for (int j = 0; j < 4; ++j) if (k[P_PA + j] > 0) eng.setUiPlate (timbrePlate0 + j, fx (P_PA + j));
    }

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
