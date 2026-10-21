# Licensing

Zen-Lite is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| `firmware/src/midi_uart.c` (MIDI IN on the TRS jack), from [Melodee](https://github.com/keremimo/melodee)'s, adapted | Copyright (C) 2026 Kerem Kilic (Ellic Studio), portions Leo Kuroshita |
| The wiring `platform.c`, the host mode `tests/host.c`, `tests/run_host.sh`, `docs/API.md`, `docs/RULES.md` ([Dinghy](https://github.com/zvenson/dinghy)) | zvenson |
| Zen-Lite: `firmware/src/zenlite.c`, `zen_voice.h`, `zen_mod.h`, `zen_fx.h`, `zen_chords.h`, `zen_params.h`, `tools/gen_tables.py`, `tools/zen_presets.cpp`, `tools/make_presets.sh` and the files they write, `tests/ref/`, `tests/scripts/`, `tests/compare.py`, `tests/check_audio.sh`; the sound (wavetables, ladder, envelopes, modulation, effects, limiter) and the factory presets are ported from [Zenthesizer](https://github.com/Hi-Fi-Hopes/Zenthesizer) | Hi-Fi Hopes |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| Instrument Serif and Instrument Sans (from Zenthesizer's Resources), rendered into `firmware/gen/zen_art.h` by `tools/gen_art.py` | SIL OFL 1.1 | `assets/fonts/OFL-InstrumentSerif.txt`, `assets/fonts/OFL-InstrumentSans.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. Zen-Lite is not made or endorsed by them.
