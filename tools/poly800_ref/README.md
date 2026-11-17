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
