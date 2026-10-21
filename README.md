# Zen-Lite

The [Zenthesizer](https://github.com/Hi-Fi-Hopes/Zenthesizer), cut down to run on the M-VAVE FM-1.
Custom firmware, built on [Dinghy](https://github.com/zvenson/dinghy) (Felucca's and SLOOP's platform), so the
FM-1 keeps its updater, its USB rescue and its way back to the official firmware.

![Zen-Lite's screen](docs/screen.png)

## Status: step 3.2 (0.5)

Six voices of the plugin's two engines into its ladder filter, with its envelopes, modulation, effects and
look-ahead limiter at -1 dBFS, and 179 of its presets:

- **Engine A**: three analog oscillators, saw / square / triangle / sine, SHAPE (softens the saw, narrows the
  square, folds the triangle, self-FM on the sine), DETUNE (spreads the two side oscillators), LEVEL
- **Engine B**: the four wavetables (AIR, GLASS, VOX, REED), POSITION scanning their eight frames, WARP, LEVEL
- **Filter**: the 4-pole ladder with DRIVE, ENV AMT (ENV 1, either way) and KEY TRACK
- **Modulation**: ENV 1 and ENV 2, two LFOs (sine, triangle, saw, square, drift), a smoothed random, and 12 slots,
  source to destination, as the plugin's. Sources: the envelopes, LFOs, random, velocity, pressure (poly and
  channel), mod wheel, slide (CC 74), the key, and the four plates. Destinations: pitch, A tune / detune / shape /
  level, B position / warp / level, cutoff, resonance, drive, amp, pan, the LFO rates and the effect mixes
- **The plates** BLOOM, MORPH, ECHOES, SPACE are the HOME page's knobs: macros, through the plugin's plate kit in
  slots 1-7 (BLOOM opens the filter, MORPH moves B's position, ECHOES brings in the delay, SPACE the reverb)
- **Effects**: the plugin's chorus, ping-pong delay (up to 1.48 s, with its tape wow) and shimmer reverb (the delay
  and reverb run at half the sample rate), VOLUME, and a SPEAKER setting for the FM-1's speaker
- **Presets**: the PRESETS encoder steps through Zenthesizer's factory presets, converted
  (`tools/make_presets.sh`). Left out, as there is no engine for them here: B UNISON, AIR (breath noise) and the
  arpeggiator. The 26 presets built on the plucked STRING oscillator are skipped. A synced delay becomes the same
  time at 90 BPM, the plugin's tempo without a host. Every preset plays within 3 dB of the plugin's level
  (`tests/check_audio.sh`); those that leaned on unison sound thinner.
- **The keyboard** plays the key: white keys are the scale, black keys the seven chords

Integer maths only (the FM-1 has no FPU); every curve is computed once by `tools/gen_tables.py` from the plugin's
own formulas. Every build compares the sound with the plugin's own engine (38 cases, and every preset), and every chord with its
chord builder.

### The keyboard (SCALE keys)

| Keys | Do |
| --- | --- |
| White keys | the notes of the key and scale, the root on the leftmost |
| Black keys 1-7 (F#3 .. G#4) | the seven chords of the key, I to vii, as the plugin builds them |
| Black keys 8-11 (A#4, C#5, D#5, F#5), held | 7TH, 9TH, SUS, STRUM for the next chord |
| SEL | CHROMATIC keys (every key its own note, as a piano) and back |

Chord notes are the first to give way when all six voices are busy, so a melody over a chord keeps sounding.

### Pages

A second press of a button steps to its next page.

| Button | Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- | --- |
| HOME | the preset | BLOOM | MORPH | ECHOES | SPACE |
| EDIT | FILTER | CUTOFF | RESONANCE | DRIVE | ENV AMT |
| | ENGINE A | WAVE | SHAPE | DETUNE | LEVEL |
| | ENGINE B | TABLE | POSITION | WARP | LEVEL |
| | VOICE | TUNE | GLIDE | VEL SENS | VOLUME |
| ENV | AMP ENV, ENV 1, ENV 2 | ATTACK | DECAY | SUSTAIN | RELEASE |
| LFO | LFO | 1 RATE | 1 SHAPE | 2 RATE | 2 SHAPE |
| | RANDOM + KEY | RANDOM (rate) | SMOOTH | KEY TRACK | STEREO |
| | MOD | SLOT (1-12) | SOURCE | DEST | AMOUNT |
| FX | FX | CHORUS | REVERB | DECAY | SHIMMER |
| | SPACE | SIZE | DAMP | DEPTH (chorus) | SPEAKER: FLAT, SOFT, LOW CUT |
| | DELAY | TIME | FEEDBACK | TONE | MIX |
| GLO | KEY + SCALE | KEY | SCALE | KEYS | SPREAD |

Turning PRESETS loads a preset at once (the plates go back to 0; key, scale and SPEAKER stay). Edits are kept
(the FM-1 saves them when it is quiet) but there are no user preset slots yet: the next preset turn replaces them.
SPEAKER shapes the headphones and USB audio too: keep it on FLAT when recording.

**The CPU guard**: if the sound ever takes more than 85 % of an audio block, the oldest let-go tail, else the oldest
chord note, else the oldest note fades out in 4 ms (DROP shows on the screen), so the sound never breaks up.

OCT- / OCT+ the octave, MASTER the volume. USB-MIDI in: notes (chromatic, with velocity), pitch bend (+-2
semitones), mod wheel (CC 1), CC 74, channel and poly pressure. The screen's last lines: **CPU**, **LIMIT**,
octave, voices.

Next (step 3.3): the screens of the mock-ups, with a simple scope on HOME.

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
ZEN_SRC=../Zenthesizer sh tests/check_audio.sh          # Zen-Lite vs the plugin: 38 cases, every preset
ZEN_SRC=../Zenthesizer sh tools/make_presets.sh         # firmware/gen/zen_presets.h from the plugin's presets
```

Package identity `FM-1_950`. GPL-3.0-only; credits in `LICENSING.md`.
