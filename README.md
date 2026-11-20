# FM1-800

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

FM1-800 is a fan-made firmware that turns the **M-VAVE FM-1** into a **Korg Poly-800**: its 64 factory programs, its
voices, filter and envelopes, its chorus, and its 256-note step sequencer. It adds a phaser, a tape echo, 64 more
program slots, and MIDI clock from a drum machine or a computer.

It is a tribute by a Poly-800 owner. It is not made, endorsed or supported by Korg or by M-VAVE.

## Install

Connect the FM-1 directly to the computer by USB and open the FM1-800 web installer in Chrome or Edge, then press
**Install**. Do not unplug the cable while it writes. The installer also has a way back to the official FM-1
firmware (V15 or V16, from M-VAVE): the programs and sequence saved in FM1-800 may be lost when you go back.

## Playing it

| Control | What it does |
| --- | --- |
| PRESETS | the program: A11..A88 (the factory 64), then B11..B88 (64 INIT programs to make your own) |
| GLO + PRESETS | the same number in the other bank |
| OCT- / OCT+ | the keys an octave down / up (both together: back to 0) |
| SELECT, KNOB 1 | edit as on the Poly-800: SELECT steps through the parameters, KNOB 1 sets the value |
| SAVE | save the program: SAVE again to save here, or turn PRESETS to choose another slot first; any other button cancels |
| GLO + SAVE | put this slot back to its factory program |
| SAVE held while switching on | put all of bank A back to the factory programs |
| FX | the PHASER page, then the ECHO page, then home. On each, SELECT turns it on / off and KNOB 1..4 set it |
| GLO (tap) | the GLOBAL page: MIDI channel, pitch bend range, mod wheel (VIB, VCF or BOTH), tuning |
| REC | start a new sequence: each note or chord played is a step, ARP a tie, SEQ a rest; REC again to finish (saved) |
| SEQ | the SEQUENCE page: KNOB 1 TEMPO, KNOB 2 CLOCK (INT, or EXT to follow MIDI clock) |
| PLAY/STOP | play the sequence. On EXT it waits for the drum machine's or computer's START |
| HOME held | the CPU load |

The phaser and the echo are off at power-on, as on the Poly-800 itself. The battery is shown at the top right.

## Building

See [BUILDING.md](BUILDING.md) for the toolchain. In short, on Linux (or WSL):

```
./build.sh
python3 web/make_site.py build/felucca.fwsc VERSION SITE_FOLDER
```

The first makes `build/felucca.fwsc`, the package; the second makes the installer site around it.

How the sound was matched to the Poly-800, version by version, and the tests that check it are in
[tools/poly800_ref/README.md](tools/poly800_ref/README.md). `tests/poly800/run.sh` runs the FM1-800 tests on the computer.

## Built on Felucca

FM1-800 is built on [Felucca](https://github.com/hugelton/Felucca), the open-source FM-1 firmware by Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic), [Hügelton Instruments](https://hugelton.com)), and its hardware layer
[felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0). Felucca's own code is still in this repository:
the FM1-800 build uses its hardware, display, MIDI, sequencer timing and update code, and leaves the rest out.
Please send FM1-800 questions and problems here, not to Felucca.

## Credits and licence

FM1-800 is free software under [GPL-3.0-only](LICENSE), as Felucca is. The parts it uses keep their own licences:
see [CREDITS.md](CREDITS.md) and [LICENSING.md](LICENSING.md).

Korg and Poly-800 are trademarks of Korg Inc. M-VAVE and FM-1 are trademarks of their respective owners.

FM1-800 Copyright (C) 2026 morbious22. Felucca Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
