#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Render each test script through Zen-Lite (host) and through the plugin's own engine, and compare them.
#   ZEN_SRC=/path/to/Zenthesizer tests/check_audio.sh
set -e
cd "$(dirname "$0")/.."
ZEN_SRC="${ZEN_SRC:-../hi-fi-hopes/zenthesizer}"
mkdir -p build/host
c++ -std=c++17 -O2 -I"$ZEN_SRC/Source" tests/ref/zen_ref.cpp -o build/host/zen_ref
run() {   # name script POS CUT RES REL
    cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -DZL_POS=$3 -DZL_CUT=$4 -DZL_RES=$5 -DZL_REL=$6 \
       -Ifirmware/src -Ifirmware/gen -o build/host/zl_$1 firmware/src/zenlite.c
    build/host/zl_$1 build/host/zl_$1 tests/scripts/$2.txt > /dev/null
    build/host/zen_ref tests/scripts/$2.txt build/host/ref_$1.wav $3 $4 $5 $6
    echo "== $1 ($2: pos $3 cutoff $4 res $5 release $6)"
    python3 tests/compare.py build/host/zl_$1.wav build/host/ref_$1.wav
}
run default   basic  57 79 70 76
run chords    chords 57 79 70 76
run hot       chords 127 127 127 76
run screamer  chords 90 100 127 76
run dark      chords 0 20 127 76
run open      chords 127 127 0 76
