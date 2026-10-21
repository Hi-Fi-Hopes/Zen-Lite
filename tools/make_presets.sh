#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Converts Zenthesizer's factory presets for Zen-Lite: writes firmware/gen/zen_presets.h (tools/zen_presets.cpp).
#   ZEN_SRC=/path/to/Zenthesizer tools/make_presets.sh
# The plugin's Presets.h includes its JUCE processor for the parameter ids; here a stub stands in for it, with
# the ids copied from the real header.
set -e
cd "$(dirname "$0")/.."
ZEN_SRC="${ZEN_SRC:-../hi-fi-hopes/zenthesizer}"
B=build/presets
rm -rf "$B"; mkdir -p "$B"
cp "$ZEN_SRC/Source/Presets.h" "$ZEN_SRC/Source/PresetsMore.h" "$B/"
{
    echo '#pragma once'
    echo '#include "dsp/Settings.h"'
    echo '#include "dsp/Maps.h"'
    sed -n '/^namespace ids/,/^}/p' "$ZEN_SRC/Source/PluginProcessor.h" | grep -v 'juce::'
} > "$B/PluginProcessor.h"
c++ -std=c++17 -O1 -I"$B" -I"$ZEN_SRC/Source" -Ifirmware/src tools/zen_presets.cpp -o "$B/zen_presets"
"$B/zen_presets" > firmware/gen/zen_presets.h
