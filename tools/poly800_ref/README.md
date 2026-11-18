# Poly-800 reference sound (FM1-800 step 3)

FM1-800's sound has to match the Poly-800 web prototype (built in an earlier session from the Korg Poly-800's
circuit diagrams, so what it copies is the circuit's design: footages summed as squares, one shared filter and DEG 3). These tools record that page's sound and measure it, so the
firmware's sound (rendered on the computer from step 4 on) can be checked against it by numbers before anyone listens.

| File | What |
| --- | --- |
| `render.js` | Runs the page's own code in headless Chromium (Chrome's Web Audio), offline, and records every factory program played three ways |
| `measure.py` | Measures a set of recordings: level, loudness every 10 ms, third-octave spectrum, stereo width |
| `compare.py` | Compares two sets of measurements, render by render, against the tolerances |
| `gen_p800.py` | Reads the 64 programs out of the page and writes them, with the maths tables, as `firmware/src/p800_data.h` |
| `../../tests/poly800/ref_*.json` | The measurements of the page: the target |
| `../../tests/poly800/host.c`, `run.sh` | The firmware's sound (`firmware/src/p800.c`) played on the computer the same way, measured and compared |

## The recordings

Each of the 64 programs is played three ways, at fixed sample times (Web Audio's 128-sample blocks):

| Phrase | Notes | Length |
| --- | --- | --- |
| `chord` | C3 G3 C4 E4 held ~2.48 s | 4.5 s |
| `note` | C4 held ~2.98 s, then its release | 8 s |
| `run` | C4 D4 E4 F4 G4 A4 B4 C5, ~151 ms each | 2.5 s |

Three sets:

| Set | Settings | For |
| --- | --- | --- |
| `page` | the page as it opens: Age 3, phaser on at mix 5 | the sound as heard |
| `dry` | Age 0, phaser mix 0 | comparing voices, filter, tone shaping and chorus, with no randomness from Age |
| `phaser` | programs 11 17 15 33, mix 10, feedback 0 / 10, and off | measuring the phaser |

Randomness (the noise, Age) is seeded, so every render is the same, sample for sample, every time.

## Making them again

```
node tools/poly800_ref/render.js PAGE.html OUT_DIR --set dry
python3 tools/poly800_ref/measure.py OUT_DIR/dry tests/poly800/ref_dry.json
```

`PAGE.html` is the prototype page itself, which is not in this repository. Needs Node, Playwright with Chromium,
Python 3 and numpy. Recorded with Chromium 153.

## What the recordings showed

- **The phaser's feedback has a 128-sample delay.** The page wires the phaser's output straight back to its input.
  Web Audio's specification says such a loop is muted, but Chromium runs it with one 128-sample block of delay
  (2.9 ms at 44.1 kHz): an impulse fed into a loop of gain 0.5 comes back at 128, 256, 384 samples. So in Chrome the
  phaser works, and its feedback is partly a short comb. The firmware copies this: feedback through a 128-sample delay.
- **Loud programs go past full scale.** In 85 of the 192 `page` renders the peak is above 1.0 (up to 5.5, program
  27's chord); Chrome cuts those peaks off at the speakers. At most 8.4 % of a render's samples are cut, mostly
  organ chords as they start. The measurements are of the uncut sound.
- Program 48 (Jet Plane) has no footages on: only noise sounds, and in the run it is nearly silent (DEG 3 single trigger).

## The firmware's sound against the page (step 4)

`tests/poly800/run.sh` builds `firmware/src/p800.c` for the computer, checks its pitch, plays the `dry` and `phaser`
sets, measures them and compares them with the page's:

| Check | Result |
| --- | --- |
| Pitch: every key 17..108, octave, interval, DCO 2 detune, tune ±50 | within 0.003 cents |
| `dry`, 192 renders: level | median 0.01 dB, worst 0.63 dB (tolerance 1 dB) |
| `dry`: third-octave bands | median 0.2 dB; 191 within tolerance, 1 near-miss allowed (23's run, 31 Hz: 3.1 dB against 3) |
| `dry`: loudness every 10 ms (90 % of frames) | median 0.1 dB |
| `phaser`, 36 renders (mix 10, feedback 0 and 10, off) | all within; level worst 0.18 dB, bands worst 0.5 dB |

The `page` set has Age on: each note gets a random detune, so the page itself never plays a chord the same way twice.
It is judged against the page's own spread instead: six takes of the page (`--seed`) and five of the firmware give
the same average levels within 0.3 dB (program 34's run: 0.9 dB), and render by render the firmware differs from the
page no more than one take of the page differs from another (bands median 1.4 dB against 1.3 dB, 90 % 4.3 against 4.3).

What it took, beyond the page's own numbers:
- **Chrome's filter, not a textbook one.** The page's filter is Chrome's BiquadFilterNode, a direct-form biquad: when
  DEG 3 starts again on a key, the cutoff jumps and the old outputs it holds ring through the new coefficients, a
  burst the page has on every note of a run. A state-variable filter (smoother, and what Felucca uses) leaves the
  bursts out, so every filter is direct form, as Chrome's; their 1 / a0 is worked to 30 bits (at 40 Hz, 15 bits moved
  the poles enough to ring). Against Chrome's own impulse responses: within 0.1 dB, low-pass, high-pass, shelves and
  allpass. (In step 5 the shelves, high-pass and allpasses were still state-variable filters: with rounded products
  they cycled on at -58 dB in silence after some programs, and the 10 Hz high-pass held an offset at -75 dB.)
- **Pitch in twelfths of a cent.** DCO 2's detune steps are 20/3 cents; rounded to quarter cents, the two DCOs' beating
  drifted out of step with the page's within a held note.
- **The polyBLEP's high end.** The 2-point polyBLEP smooths each step over two samples (harmonics fall as
  sinc²(f / fs): -2.4 dB at 12.7 kHz); a fixed 3-tap filter on the voices puts most of it back.
- **Fixed point at low cutoffs.** Through the filters the signal is 64 times finer (Q22), with rounding and the
  direct form's error carried to the next sample, so after the last note the output is exactly zero.

## Inside Felucca (step 5)

`firmware/src/eng_p800.c` makes the Poly-800 one more Felucca engine, POLY800 (engine 14 in this build; on the screen
**P-800**: Felucca's PRESETS card holds six characters, so "POLY800" came out "POLY8…"), so Felucca's
screens offer it as they are: it comes last in PRESETS (its 64 programs, the names shortened to Felucca's 12
characters; PRESETS' KNOB 2 picks the engine), with no EDIT layer key, as Felucca's ADPCM (first, or with a key, it
pushed CLEAN INIT off the keyboard in a FELUCCA_FM4 build). A part on POLY800 sends its notes to the one Poly-800
(`voice.c trk_note_on / trk_note_off`, no Felucca voice taken) and the Poly-800's stereo output joins the dry mix after
the parts (`fx.c mix_block`), at half the mix's full scale: Felucca's output stage limits from 0.55 of full scale and
soft-clips (tanh) everything, so the page's full scale sits under the limiter and only the loud organ chords meet it.
Felucca's per-part LEVEL, PAN, FX sends and envelopes do not reach it (step 6 gives it its own path).

`tests/poly800/in_felucca.c` (run by `run.sh` once `build/gen` exists) plays it through Felucca's own code: notes reach
it and take no Felucca voice, its level is the page's 6 dB down (within 0.25 dB: the tanh), the 27 organ chord stays
under full scale, the part's preset is the program that plays, and after a switch away it lets go and goes quiet.
Felucca's UI test (`tests/ui_test.c`) passes as before: part 1 still starts on ANALOG (making it start on POLY800 broke
32 of its checks, which press keys on part 1 and count Felucca's voices; that waits for step 7's own screens).

Felucca's own suite (`tests/run_tests.sh`, all but the tests that need the JieLi build) passes as it did on the step 4
code. Felucca's regression renders play every POLY800 program too (`tests/golden.txt`: 64 renders added, none of
Felucca's changed); their health check waits for the Poly-800's voices and its noise (`p800_busy`), then wants silence.

## The FM-1 is the Poly-800 alone (step 6, v0.3.0)

The FM-1's build (`tools/build.py`, `FM1_800=1`; `FM1_800=0` builds it as step 5 left it, one engine of Felucca's,
which Felucca's own tests still use until step 9) makes the FM-1 the Poly-800 and nothing else:

- every note, from any part, the keys, the sequencer or MIDI, goes to the Poly-800 (`voice.c`); no Felucca voice plays
- only the Poly-800 is rendered (`fx.c mix_block`): Felucca's voices, chorus, delay and reverb cost nothing
- it goes straight to the output: MASTER, the DC block, LOW CUT and the limiter as before, but no tanh. In its place a
  knee: straight up to 0.75 of full scale, then bent smoothly under full scale. The piano note is now the page's level
  6 dB down within 0.01 dB (it was 0.03 dB off through the tanh)
- at power-on part 1 plays P-800 (STRINGS)

`tests/poly800/in_felucca.c` runs in both builds; in the FM-1's it also checks that a part on another engine plays the
Poly-800, and that the knee never passes full scale.

**v0.3.1, the limiter's timing.** On the FM-1 the loud programs went "dumpf" on every chord. Felucca's limiter caught
the first few ms of a chord (all its notes starting in step) at once, pulled the level 6 to 14 dB down and let it
swell back within ~0.3 s. In the FM-1's build it now rises over ~3 ms and falls over ~0.7 s: those first peaks go to
the knee (at most 0.32 % of the samples, program 27's chord), and over all 64 programs, with a 4-note chord, one note
and an 8-note chord, the largest swell is 2.9 dB (was 10.2), the peak 32473 of 32767.

## The Poly-800's own home screen (step 7a, v0.4.0)

In the FM-1's build `firmware/src/ui800.c` takes over the screen and the controls from Felucca's UI (`main.c` calls
`u8_input` and `u8_draw` in place of `ui_input`, `ui_leds`, `ui_draw`). The home screen is a sunset-city scene drawn
from shapes (no picture stored): a purple sky with stars, a striped sun going yellow to pink, a skyline, a glowing
grid; the stars twinkle (each on its own slow cycle, a pixel redrawn only when its brightness steps; those
under the text stay still, v0.4.1). Over it the program's number and name, WHOLE or DOUBLE, the octave and PHASER when it is on; the version for
4 s after power-on. It is drawn only when one of those changes. PRESETS turns through the 64 programs (round), OCT- /
OCT+ move the keys an octave on release (-3 .. +3; both: 0). The keys and MIDI play as before. Text sits on the
picture by blending each glyph pixel onto what is there (`gfx.c cv_over`; Felucca's own drawing is unchanged).
`tests/poly800/ui800_test.c` checks the scene, the drawing only on change, PRESETS and OCT, and writes the screens.

**Editing (step 7b, v0.5.0).** SELECT steps through the program's 47 parameters in the page's order (DCO 1: wave, the
four footages, octave, level; DCO 2 the same with interval and detune; noise; VCF: cutoff, resonance, key tracking, EG
intensity and polarity, trigger; DEG 1..3: attack, decay, break point, slope, sustain, release; MG: frequency, delay, DCO
and VCF intensity; chorus; voices). Its first turn shows the one it is on. KNOB 1 sets the value, held at the ends,
and it sounds at once: the edits go into a copy of the program the Poly-800 plays (`ui800.c u8_ed`), the 64 stay as
they are; the top shows EDITED, PRESETS leaves the edits (saving: step 8). GLO held + KNOB 1: the tuning, -50 .. +50
cents. The screen's top half (the program) and bottom half (the parameter, octave, phaser) are drawn apart, each only
when what it shows changes. `ui800_test.c` checks SELECT, KNOB 1 on every parameter's range, that a cutoff edit makes a
note brighter, the tuning, and that PRESETS leaves the edits.

**The FX button (step 7c, v0.6.0).** FX goes to the PHASER page and back. The phaser is one setting for every
program, as a pedal on the floor: SELECT clockwise on, anticlockwise off (its MIX kept); KNOB 1..4 RATE, DEPTH,
FEEDBACK, MIX (0..10), GLO + KNOB 1 STAGES (2..12). At power-on it is off (v0.8.1: the Poly-800 as it was, only each program's chorus), ready at the page's settings. The lights are
`ui800.c u8_leds`: the buttons and keys glow dim, FX lit while the phaser is on, OCT- / OCT+ lit when the keys are
moved, a key lit while held.

**The deep phaser (v0.7.0, by ear, not the page's).** On the FM-1 the page's phaser never went deep or wide: its
stages sit 1.5 times apart (their notches spread, none deep), its feedback comes back one Chrome block (2.9 ms) late
(a faint comb, not a resonance) and both sides sweep alike. The FM-1 now plays a deep one (`P800_PH_DEEP` 1): every
stage at one pitch (Q .7), swept about 1 kHz +- 3 octaves at DEPTH 10 (125 Hz .. 8 kHz), its feedback straight back
(a sample, up to .9 at FEEDBACK 10), the right side's sweep a quarter turn ahead. The same STRINGS chord at STAGES 6,
RATE 2, DEPTH 10, FEEDBACK 7, MIX 10: the bands from 200 Hz to 6 kHz move 19.0 dB (median; the page's 14.2) and the
two sides are far less alike (correlation +0.39, the page's +0.80). The tests that hold the firmware to the page play
the page's phaser (`P800_PH_DEEP` 0: `host.c`), and still pass as before.

## The tape echo (v0.8.0, by ear)

`firmware/src/echo800.c`, after the Poly-800 in the FM-1's mix (`fx.c mix_block`), on Felucca's delay line (`dly_buf`,
1.49 s, unused in this build). The controls of a small 1970s tape echo, on the ECHO page after PHASER on the FX
button: SELECT on / off (off at power-on), KNOB 1 DELAY (0..99: 40 ms .. 1 s, shown in ms), KNOB 2 REPEAT (0..10:
up to 1.08, at the top it runs away slowly), KNOB 3 BALANCE (0 dry .. 5 both full .. 10 the echo alone), KNOB 4
INPUT 0 / 20 / 40 dB (the input x1 / x3 / x9 before the tape, as a unit's input stage: louder and more driven; the
repeats go round without it). Mono, into both sides. The tape saturates (tanh, never past its full level); each
repeat goes through a ~2 kHz low-pass and a ~110 Hz high-pass (darker and thinner each time round); the tape wavers
(~0.6 Hz wow, ~7 Hz flutter, +-0.2 %) and is read between samples; DELAY slides the tape's speed (~90 ms), so the
repeats bend. The filters' state is 8 bits finer than the tape: what is left of a repeat dies to exactly 0, and once
the echo is off and the tape quiet for 1.5 s it costs nothing. 32-bit arithmetic only (CPU.md).
`in_felucca.c` checks DELAY's range, the repeats' times, that each is softer and darker, the INPUT switch on a quiet
and a loud sound, REPEAT 10 held by the saturation, and that off it dies away and rests; `ui800_test.c` the page.

**The high-pass add-on's controls (v0.8.2, the owner's builds).** `FM1_HP` (tools/build.py: 1; `FM1_HP=0 ./build.sh`
leaves them out, the high-pass off, for the version to share). One setting for every program, on GLO: GLO + SEL on /
off, GLO + KNOB 2 CUTOFF (0..99: 40 Hz .. 10 kHz, the page's), GLO + LFO whether the MG sweeps it with the low-pass
(`P800_HP_MG`, 1 as the page). Shown while GLO is held; HPF at the bottom of the home screen while on. `ui800_test.c`
runs in both builds (a low C 13 dB thinner at CUTOFF 75).
