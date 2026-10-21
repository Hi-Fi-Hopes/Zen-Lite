# Zen-Lite

The [Zenthesizer](https://github.com/Hi-Fi-Hopes/Zenthesizer), cut down to run on the M-VAVE FM-1.
Custom firmware, built on [Dinghy](https://github.com/zvenson/dinghy) (Felucca's and SLOOP's platform), so the
FM-1 keeps its updater, its USB rescue and its way back to the official firmware.

![Zen-Lite's screen](docs/screen.png)

## Status: step 1, the proof

One Zenthesizer voice, six of them at once: Engine B's **GLASS** wavetable through the plugin's 4-pole ladder
filter, its amp envelope, its stereo spread, and its look-ahead limiter at -1 dBFS. Integer maths only (the FM-1
has no FPU); every curve is computed once by `tools/gen_tables.py` from the plugin's own formulas.

| Control | Does |
| --- | --- |
| Keys | play it (chromatic, F3 to G5); notes go out over USB-MIDI |
| KNOB 1 | POSITION: scans the GLASS frames |
| KNOB 2 | CUTOFF |
| KNOB 3 | RESONANCE |
| KNOB 4 | RELEASE |
| OCT- / OCT+ | octave |
| MASTER | volume |
| USB-MIDI in | plays it, with velocity |

The screen shows the four knobs, **CPU** (the share of each audio block the sound takes: keep it under 50 %) and
**LIMIT** (how far the limiter is pulling the level down).

Planned next: Engine A, the other wavetables, plates A-D on the knobs, the seven chords on the black keys with the
scale on the white keys, mod envelopes and LFOs, the FX, presets.

## Install, test, go back

Chrome or Edge, the FM-1 on USB, Ableton and other MIDI apps closed.

1. Download `zenlite.fwsc` from the [`builds` branch](../../tree/builds) (the newest build of `main`).
2. Install it with any FM-1 web installer that takes a package file, or from a terminal:
   `python3 tools/fm1_install.py zenlite.fwsc` (`pip3 install mido python-rtmidi` once).
3. Play. Watch CPU on the screen with all six voices sounding.
4. Go back: the [FM-1 Firmware Hub](https://thefirmwarehub.com/)'s **Back to official V15**, or M-VAVE's M-UPGRADE
   with V15. If anything ever hangs: hold **OCT-** while switching on (USB rescue) and install again.

## Build

Every push builds on GitHub (`.github/workflows/build.yml`): the package, a host render and a comparison against
the plugin's own engine. Locally (Linux x86-64, or Docker): see `docs/` and `tools/get_toolchain.sh`, then
`./build.sh`. On the computer, no FM-1 needed:

```sh
sh tests/run_host.sh                                    # build/host/zenlite.wav and screens
ZEN_SRC=../Zenthesizer sh tests/check_audio.sh          # Zen-Lite vs the plugin, six test cases
```

Package identity `FM-1_950`. GPL-3.0-only; credits in `LICENSING.md`.
