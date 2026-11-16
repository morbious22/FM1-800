# Poly-800 reference sound (FM1-800 step 3)

FM1-800's sound has to match the Poly-800 web prototype. These tools record that page's sound and measure it, so the
firmware's sound (rendered on the computer from step 4 on) can be checked against it by numbers before anyone listens.

| File | What |
| --- | --- |
| `render.js` | Runs the page's own code in headless Chromium (Chrome's Web Audio), offline, and records every factory program played three ways |
| `measure.py` | Measures a set of recordings: level, loudness every 10 ms, third-octave spectrum, stereo width |
| `../../tests/poly800/ref_*.json` | The measurements of the page: the target |

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
