# Zen-Lite

The [Zenthesizer](https://github.com/Hi-Fi-Hopes/Zenthesizer), cut down to run on the M-VAVE FM-1.
Custom firmware, built on [Dinghy](https://github.com/zvenson/dinghy) (Felucca's and SLOOP's platform), so the
FM-1 keeps its updater, its USB rescue and its way back to the official firmware.

![Zen-Lite's screen](docs/screen.png)

## Status: step 2b

Six voices of the plugin's two engines into its ladder filter, with its envelope, stereo spread and look-ahead
limiter at -1 dBFS:

- **Engine A**: three analog oscillators, saw / square / triangle / sine, SHAPE (softens the saw, narrows the
  square, folds the triangle, self-FM on the sine) and DETUNE (spreads the two side oscillators), TUNE in semitones
- **Engine B**: the four wavetables (AIR, GLASS, VOX, REED), POSITION scanning their eight frames, WARP
- **Effects**: the plugin's chorus and shimmer reverb (the reverb runs at half the sample rate: half the CPU),
  and a TONE setting for the speaker
- **The keyboard** plays the key: white keys are the scale, black keys the seven chords

Integer maths only (the FM-1 has no FPU); every curve is computed once by `tools/gen_tables.py` from the plugin's
own formulas. Every build compares the sound with the plugin's own engine, and every chord with its chord builder.

### The keyboard (SCALE keys)

| Keys | Do |
| --- | --- |
| White keys | the notes of the key and scale, the root on the leftmost |
| Black keys 1-7 (F#3 .. G#4) | the seven chords of the key, I to vii, as the plugin builds them |
| Black keys 8-11 (A#4, C#5, D#5, F#5), held | 7TH, 9TH, SUS, STRUM for the next chord |
| SEL | CHROMATIC keys (every key its own note, as a piano) and back |

Chord notes are the first to give way when all six voices are busy, so a melody over a chord keeps sounding.

### Pages

| Button | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| HOME | CUTOFF | RESONANCE | POSITION | A-B MIX |
| EDIT | Engine A: WAVE | SHAPE | DETUNE | TUNE |
| EDIT again | Engine B: TABLE | POSITION | WARP | |
| ENV | ATTACK | DECAY | SUSTAIN | RELEASE |
| FX | CHORUS | REVERB | DECAY | SHIMMER |
| FX again | SIZE | DAMP | DEPTH (chorus) | TONE: FLAT, SOFT, LOW CUT |
| GLO | KEY | SCALE | KEYS | SPREAD |

TONE shapes the headphones and USB audio too: keep it on FLAT when recording.

OCT- / OCT+ the octave, MASTER the volume; USB-MIDI notes in play chromatically, with velocity. Everything is kept
when the FM-1 is switched off. The screen's last lines: **CPU** (keep it under 50 %), **LIMIT**, octave, voices.

Next (step 3): the screens of the mock-ups with a scope on HOME, plates A-D as macros, LFOs and mod envelopes, presets.

## Install, test, go back

Chrome or Edge, the FM-1 on USB, Ableton and other MIDI apps closed.

1. Download `zenlite.fwsc` from the [`builds` branch](../../tree/builds) (the newest build of `main`).
   With `curl`: `curl -L -o zenlite.fwsc https://raw.githubusercontent.com/Hi-Fi-Hopes/Zen-Lite/builds/zenlite.fwsc`
2. Install it with any FM-1 web installer that takes a package file, or from a terminal:
   `python3 tools/fm1_install.py zenlite.fwsc` (`pip3 install mido python-rtmidi` once; on Windows use Python 3.12,
   `py -V:3.12 fm1_install.py zenlite.fwsc`, as python-rtmidi has no Windows builds for newer Pythons).
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
