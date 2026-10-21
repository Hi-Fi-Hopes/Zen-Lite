#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Render each case through Zen-Lite (on the host) and through the plugin's own engine, and compare them.
#   ZEN_SRC=/path/to/Zenthesizer tests/check_audio.sh
# A case: a name, a script (tests/scripts/), and knobs as NAME=V (0..127): CUT RES POS MIX WAVE SHAPE DET TUNE
# ATT DEC SUS REL TABLE WARP CHORUS REVERB RDECAY SHIMMER SIZE DAMP DEPTH. The keys play chromatically here, as
# the plugin's MIDI input does, and the effects are off unless a case turns them on.
set -e
cd "$(dirname "$0")/.."
ZEN_SRC="${ZEN_SRC:-../hi-fi-hopes/zenthesizer}"
mkdir -p build/host
c++ -std=c++17 -O2 -I"$ZEN_SRC/Source" tests/ref/zen_ref.cpp -o build/host/zen_ref
c++ -std=c++17 -O2 -I"$ZEN_SRC/Source" -Ifirmware/src tests/chords_test.cpp -o build/host/chords_test
build/host/chords_test
fails=0
run() {
    name=$1; script=$2; shift 2
    zset="set.p[P_KEYS]=1;set.p[P_CHORUS]=0;set.p[P_REVERB]=0;"
    for kv in "$@"; do zset="${zset}set.p[P_${kv%%=*}]=${kv#*=};"; done
    cc -O2 -Wall -Wshadow -Werror=shadow -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' "-DZL_SET=$zset" \
       -Ifirmware/src -Ifirmware/gen -o build/host/zl_$name firmware/src/zenlite.c
    build/host/zl_$name build/host/zl_$name tests/scripts/$script.txt > /dev/null
    build/host/zen_ref tests/scripts/$script.txt build/host/ref_$name.wav "$@"
    echo "== $name ($script: $*)"
    python3 tests/compare.py build/host/zl_$name.wav build/host/ref_$name.wav || fails=$((fails + 1))
}
# Engine B (GLASS) alone
run glass       basic  MIX=127 CUT=79 RES=70 POS=57
run glass_hot   chords MIX=127 CUT=127 RES=127 POS=127
run glass_dark  chords MIX=127 CUT=20 RES=127 POS=0
# Engine A alone, each wave
run saw         basic  MIX=0 WAVE=0 DET=25
run saw_wide    chords MIX=0 WAVE=0 DET=127 CUT=127 RES=127
run square      chords MIX=0 WAVE=1 SHAPE=100 CUT=110
run triangle    chords MIX=0 WAVE=2 SHAPE=90 CUT=110
run sine_fm     chords MIX=0 WAVE=3 SHAPE=127 TUNE=36 CUT=127
# both engines, hot, and the envelope
run mix_hot     chords MIX=64 WAVE=0 CUT=110 RES=90 DET=60
run envelope    basic  MIX=64 ATT=90 DEC=30 SUS=40 REL=110
# the other wavetables, warp
run air         chords MIX=127 TABLE=0 POS=90 CUT=110
run vox         chords MIX=127 TABLE=2 POS=60 CUT=110
run reed        chords MIX=127 TABLE=3 POS=100 CUT=110 RES=60
run warp        chords MIX=127 TABLE=1 POS=64 WARP=110 CUT=127
# the effects
run chorus      basic  MIX=127 CHORUS=100 DEPTH=100
run reverb      basic  MIX=127 REVERB=90 RDECAY=90 SIZE=100
run shimmer     basic  MIX=127 REVERB=100 RDECAY=110 SHIMMER=127 SIZE=127
run space_hot   chords MIX=64 CHORUS=127 REVERB=127 RDECAY=127 SHIMMER=127 CUT=110 RES=90
[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s)"; exit 1; }
