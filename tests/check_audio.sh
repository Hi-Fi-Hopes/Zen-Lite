#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Render each case through Zen-Lite (on the host) and through the plugin's own engine, and compare them.
#   ZEN_SRC=/path/to/Zenthesizer tests/check_audio.sh
# A case: a name, a script (tests/scripts/), and knobs as NAME=V (0..127; the names are zen_params.h's, without P_),
# mod slots as Sn=source,dest,amount (zen_mod.h's numbers, amount Q10), or PRESET=i (a converted preset). The keys
# play chromatically here, as the plugin's MIDI input does, and the effects are off unless a case turns them on.
set -e
cd "$(dirname "$0")/.."
ZEN_SRC="${ZEN_SRC:-../hi-fi-hopes/zenthesizer}"
mkdir -p build/host
ZEN_SRC="$ZEN_SRC" sh tools/make_presets.sh 2>/dev/null   # (also the stub zen_ref needs for Presets.h)
git diff --quiet -- firmware/gen/zen_presets.h 2>/dev/null || echo "NOTE: firmware/gen/zen_presets.h changed: commit it"
c++ -std=c++17 -O2 -Ibuild/presets -I"$ZEN_SRC/Source" -Ifirmware/src -Ifirmware/gen tests/ref/zen_ref.cpp -o build/host/zen_ref
c++ -std=c++17 -O2 -I"$ZEN_SRC/Source" -Ifirmware/src tests/chords_test.cpp -o build/host/chords_test
build/host/chords_test
fails=0
run() {
    name=$1; script=$2; shift 2
    zset="set.p[P_KEYS]=1;set.p[P_CHORUS]=0;set.p[P_REVERB]=0;"
    for kv in "$@"; do
        case "$kv" in
            PRESET=*) zset="load_preset(${kv#*=});set.p[P_KEYS]=1;" ;;
            S[0-9]*=*) n=${kv%%=*}; n=${n#S}; v=${kv#*=}
                       zset="${zset}set.slot[$((n - 1))].src=${v%%,*};set.slot[$((n - 1))].dst=$(echo "$v" | cut -d, -f2);set.slot[$((n - 1))].amt=${v##*,};" ;;
            *) zset="${zset}set.p[P_${kv%%=*}]=${kv#*=};" ;;
        esac
    done
    cc -O2 -Wall -Wshadow -Werror=shadow -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' "-DZL_SET=$zset" \
       -Ifirmware/src -Ifirmware/gen -o build/host/zl_$name firmware/src/zenlite.c
    build/host/zl_$name build/host/zl_$name tests/scripts/$script.txt > /dev/null
    case "$1" in PRESET=*) fxoff= ;; *) fxoff="CHORUS=0 REVERB=0" ;; esac   # (as zset: the effects off, unless asked)
    build/host/zen_ref tests/scripts/$script.txt build/host/ref_$name.wav $fxoff "$@"
    echo "== $name ($script: $*)"
    python3 tests/compare.py build/host/zl_$name.wav build/host/ref_$name.wav || fails=$((fails + 1))
}
# Engine B (GLASS) alone
run glass       basic  ALEV=0 CUT=79 RES=70 POS=57
run glass_hot   chords ALEV=0 CUT=127 RES=127 POS=127
run glass_dark  chords ALEV=0 CUT=20 RES=127 POS=0
# Engine A alone, each wave
run saw         basic  BLEV=0 WAVE=0 DET=25
run saw_wide    chords BLEV=0 WAVE=0 DET=127 CUT=127 RES=127
run square      chords BLEV=0 WAVE=1 SHAPE=100 CUT=110
run triangle    chords BLEV=0 WAVE=2 SHAPE=90 CUT=110
run sine_fm     chords BLEV=0 WAVE=3 SHAPE=127 TUNE=36 CUT=127
# both engines, hot, and the envelope
run mix_hot     chords WAVE=0 CUT=110 RES=90 DET=60
run envelope    basic  ATT=90 DEC=30 SUS=40 REL=110
# the other wavetables, warp
run air         chords ALEV=0 TABLE=0 POS=90 CUT=110
run vox         chords ALEV=0 TABLE=2 POS=60 CUT=110
run reed        chords ALEV=0 TABLE=3 POS=100 CUT=110 RES=60
run warp        chords ALEV=0 TABLE=1 POS=64 WARP=110 CUT=127
# the filter: drive, ENV 1 both ways, key tracking; the voice: glide, velocity, volume, stereo
run drive       chords BLEV=0 DRIVE=100 CUT=100 RES=60
run drive_b     chords ALEV=0 DRIVE=127 CUT=90 RES=90
run env_amt     basic  ENVAMT=115 CUT=35 E1A=10 E1D=75 E1S=25 RES=70
run env_neg     chords ENVAMT=15 CUT=110 E1A=40 E1S=60
run keytrack    chords KTRACK=127 CUT=60 RES=80
run glide       basic  GLIDE=70
run velocity    basic  VELS=127 VOL=90 WIDTH=127
# modulation: LFOs (sine, triangle, square) on the cutoff, pitch, amp and pan; ENV 2 on B; the key; the plates
run lfo_cut     chords S8=3,11,300 L1RATE=70 RES=80
run vibrato     basic  S8=3,1,25 L1RATE=85
run tremolo     chords S8=3,14,-600 L1RATE=88 L1SHAPE=1
run lfo_pan     chords S8=3,15,900 L1RATE=60 L1SHAPE=3
run env2_pos    chords ALEV=0 S8=2,6,700 E2A=70 E2D=90 E2S=10
run key_shape   chords BLEV=0 WAVE=1 S8=14,4,800 S9=7,11,200
run plates      chords PA=110 PB=127 PC=100 PD=90 DTIME=60 DFB=60
# the effects
run chorus      basic  ALEV=0 CHORUS=100 DEPTH=100
run reverb      basic  ALEV=0 REVERB=90 RDECAY=90 SIZE=100
run shimmer     basic  ALEV=0 REVERB=100 RDECAY=110 SHIMMER=127 SIZE=127
run space_hot   chords CHORUS=127 REVERB=127 RDECAY=127 SHIMMER=127 CUT=110 RES=90
run delay       basic  DELAY=90 DTIME=70 DFB=80 DTONE=80
run delay_dark  chords DELAY=127 DTIME=40 DFB=127 DTONE=10
run delay_lfo   basic  DELAY=40 S8=3,19,500 L1RATE=60
# converted presets (the plugin playing its own values, without B UNISON and AIR)
run p_init      chords PRESET=1
run p_temple    chords PRESET=2
run p_halo      basic  PRESET=3
[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s)"; exit 1; }

# the CPU guard: under (pretend) overload it fades voices out one by one, without clicks, and plays again after
cc -O2 -Wall -Wshadow -Werror=shadow -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' \
   '-DZL_SET=set.p[P_KEYS]=1;set.p[P_CHORUS]=0;set.p[P_REVERB]=0;set.p[P_REL]=110;' \
   -Ifirmware/src -Ifirmware/gen -o build/host/zl_guard firmware/src/zenlite.c
build/host/zl_guard build/host/zl_guard tests/scripts/guard.txt > /dev/null
python3 - <<'PY'
import sys, wave
import numpy as np
w = wave.open("build/host/zl_guard.wav")
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(float).mean(axis=1) / 32768
lvl = lambda a, b: 20 * np.log10(np.sqrt((x[int(a * 44100):int(b * 44100)] ** 2).mean()) + 1e-9)
d = np.abs(np.diff(x))
fade, play = d[int(1.45 * 44100):int(1.95 * 44100)].max(), d[int(1.0 * 44100):int(1.45 * 44100)].max()
ok = lvl(1.8, 2.4) < -60 and lvl(2.7, 3.3) > -40 and fade <= play * 1.2
print(f"== guard: fades {'quietly' if fade <= play * 1.2 else 'WITH A CLICK'} (step {fade:.3f} vs {play:.3f}), "
      f"silent under load {lvl(1.8, 2.4):.0f} dB, plays after {lvl(2.7, 3.3):.0f} dB")
sys.exit(0 if ok else "FAIL: the guard")
PY

# every preset, through both engines: none may reach full scale, or be more than 3 dB from the plugin's level
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' \
   '-DZL_SET=load_preset((uint32_t)atoi(getenv("ZLP")));set.p[P_KEYS]=1;' \
   -Ifirmware/src -Ifirmware/gen -o build/host/zl_preset firmware/src/zenlite.c
mkdir -p build/host/presets
n=$(sed -n 's/^#define ZPRESETS \([0-9]*\)/\1/p' firmware/gen/zen_presets.h)
i=0
while [ $i -lt "$n" ]; do
    ZLP=$i build/host/zl_preset build/host/presets/z$i tests/scripts/chords.txt > /dev/null
    if [ $i -eq 0 ]; then build/host/zen_ref tests/scripts/chords.txt build/host/presets/r$i.wav   # 0: Zen-Lite's defaults
    else build/host/zen_ref tests/scripts/chords.txt build/host/presets/r$i.wav PRESET=$i; fi
    i=$((i + 1))
done
python3 - "$n" <<'PY'
import sys, wave, re
import numpy as np
n = int(sys.argv[1])
names = re.findall(r'^  \{"([^"]*)"', open("firmware/gen/zen_presets.h").read(), re.M)
def ld(p):
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(float) / 32768
bad, d = [], []
for i in range(n):
    a, b = ld(f"build/host/presets/z{i}.wav"), ld(f"build/host/presets/r{i}.wav")
    m = min(len(a), len(b)); a, b = a[:m], b[:m]
    diff = 20 * np.log10(np.sqrt((a ** 2).mean()) + 1e-9) - 20 * np.log10(np.sqrt((b ** 2).mean()) + 1e-9)
    d.append(diff)
    if (abs(a) > 0.999).any() or abs(diff) > 3: bad.append(f"{i} {names[i]} ({diff:+.1f} dB)")
d = np.array(d)
print(f"== presets: {n}, level vs the plugin: median {np.median(d):+.1f} dB, within 2 dB {(abs(d) < 2).sum()}/{n}, "
      f"within 3 dB {(abs(d) < 3).sum()}/{n}; at full scale: none" if not bad else "")
sys.exit(f"FAIL: presets {bad}" if bad else 0)
PY
