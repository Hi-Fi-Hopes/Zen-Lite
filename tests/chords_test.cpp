// SPDX-License-Identifier: GPL-3.0-only
// Every chord Zen-Lite can play, against the plugin's own buildChord / chordName / chordNumeral / keyUsesFlats.
//   c++ -std=c++17 -O2 -I$ZEN_SRC/Source -Ifirmware/src tests/chords_test.cpp -o build/host/chords_test
#include "dsp/Maps.h"
#include <cstdio>
#include <string>
extern "C" {
#include "zen_chords.h"
}

int main()
{
    int checked = 0, bad = 0;
    for (int sc = 0; sc < 11; ++sc)
        for (int root = 0; root < 12; ++root)
        {
            if (zen::keyUsesFlats (root, sc) != (bool) zkey_flats (root, sc)) { ++bad; std::printf ("flats differ: root %d scale %d\n", root, sc); }
            for (int oct = -2; oct <= 2; ++oct)
                for (int deg = 0; deg < 7; ++deg)
                    for (int opt = 0; opt < 16; ++opt)
                    {
                        const bool a7 = opt & 1, a9 = opt & 2, su = opt & 4, sp = opt & 8;
                        const auto z = zen::buildChord (deg, root, sc, oct, a7, a9, su, sp);
                        const auto c = zchord_build (deg, root, sc, oct, a7, a9, su, sp);
                        bool same = z.n == c.n && z.rootNote == c.root_note;
                        for (int i = 0; same && i < z.n; ++i) same = z.notes[i] == c.notes[i];
                        char name[16], num[8];
                        zchord_name (&c, zkey_flats (root, sc), name);
                        const std::string zn = zen::chordName (z, zen::keyUsesFlats (root, sc));
                        if (zn != name) same = false;
                        zchord_numeral (deg, sc, num);
                        std::string zr = zen::chordNumeral (deg, sc);
                        if (zr.size() >= 2 && (unsigned char) zr[zr.size() - 2] == 0xC2) zr = zr.substr (0, zr.size() - 2) + "*";   // the degree sign
                        if (zr != num) same = false;
                        ++checked;
                        if (! same && bad++ < 10)
                            std::printf ("differ: deg %d root %d scale %d oct %d opt %d: plugin %s %s, Zen-Lite %s %s\n",
                                         deg, root, sc, oct, opt, zr.c_str(), zn.c_str(), num, name);
                    }
        }
    std::printf ("chords: %d checked, %d differ\n", checked, bad);
    return bad ? 1 : 0;
}
