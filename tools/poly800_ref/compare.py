#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 morbious22 (FM1-800)
"""FM1-800: compare two sets of measurements (measure.py), the web page's and the firmware's sound.

  compare.py REF.json NEW.json [--list N] [--prog P] [--allow PROG:PHRASE,..]

Per render (program x phrase), what differs:
  level   overall level (dB)                                       tolerance 1 dB
  bands   third-octave bands over the held part: the largest difference among the bands up to 16 kHz within 30 dB
          of the loudest band (dB)                                 tolerance 1.5 dB (3 dB under 100 Hz and in noise
                                                                   programs, 2 dB at 12.7 and 16 kHz)
  env     the loudness every 10 ms, where the page is within 40 dB of its peak: the mean difference (dB)
  timing  (chord, note) when the loudness first reaches 20 dB and 10 dB below the peak, and after the release falls 20
          dB and 40 dB below it: the largest difference, as a share of the time (10 % or 40 ms, whichever is longer:
          30 ms and the 10 ms frames' step).
          Not for the run: its peak is the burst of a note change, so "below the peak" moves with it
  env90   the loudness every 10 ms, where the page is within 30 dB of its peak: 90 % of the frames within (dB)
                                                                   tolerance 2 dB
Renders the page itself plays more than 60 dB down (program 48's run: only its noise, nearly shut) are listed, not
judged. Why these limits: content 30 dB under a sound's loudest band, and above 16 kHz, is masked or past most
hearing (and Chrome's square tables thin out near 20 kHz, which polyBLEP does not copy); 1 dB from the peak is where
a held note's slow wobble makes "first reached" jump by seconds for a tenth of a dB (so 10 and 20 dB). The page's
noise is one second of random numbers played on a loop: its spectrum is fixed random lines 1 Hz apart, so in a noise
program the bands under 100 Hz (a few lines each) are that recording's luck, not judged.
  width   stereo width (side vs mid, dB)
Exit status 0 when every render is within tolerance (or --allow names it: a known difference, kept in view), else 1.
"""
import json
import sys

FRAME_S = 0.01
NOISE = {}                # program -> its noise level (from p800_data.h): env and timing not judged
AGE = False               # the set has Age on (page): each note's random detune, so the beating differs
BANDS_HZ = []
TOL_LEVEL, TOL_BANDS, TOL_TIME, TOL_TIME_MIN, TOL_ENV90 = 1.0, 1.5, 0.10, 0.04, 2.0
TOL_BANDS_LOW = 3.0       # bands under 100 Hz: one or two FFT bins each, so a note change's thump swings them
TOL_BANDS_HIGH = 2.0      # 12.7 and 16 kHz: Chrome's square tables drop partials from 16 to 22 kHz, by pitch range
TOL_BANDS_NOISE = 3.0     # a noise program's bands: the page's noise is a looped one-second recording (below)
RELEASE_AT = {"chord": 0.0232 + 854 * 128 / 44100, "note": 0.0232 + 1026 * 128 / 44100, "run": 0.0232 + 8 * 52 * 128 / 44100}


def times(env, phrase):
    """seconds at which the loudness first comes within 1 and 10 dB of its peak, then after the release falls 20, 40
    dB below it (None if never)"""
    e = [v / 10 for v in env]
    pk = max(e)
    out = {}
    for name, thr in (("up20", pk - 20), ("up10", pk - 10)):
        out[name] = next((i * FRAME_S for i, v in enumerate(e) if v >= thr), None)
    r0 = int(RELEASE_AT[phrase] / FRAME_S)
    for name, thr in (("down20", pk - 20), ("down40", pk - 40)):
        out[name] = next((i * FRAME_S for i, v in enumerate(e) if i >= r0 and v <= thr), None)
    return out


def compare(a, b):
    res = {"level": b["level"] - a["level"], "width": b["width"] - a["width"]}
    top = max(a["bands"])
    use = [k for k, x in enumerate(a["bands"]) if x > top - 30 and x > -85 and BANDS_HZ[k] <= 16500
           and not (NOISE.get(a["prog"]) and BANDS_HZ[k] < 100)]
    diffs = [b["bands"][k] - a["bands"][k] for k in use]
    tol = [TOL_BANDS_LOW if BANDS_HZ[k] < 100 else TOL_BANDS_HIGH if BANDS_HZ[k] > 12000 else
           TOL_BANDS_NOISE if NOISE.get(a["prog"]) else TOL_BANDS for k in use]
    res["bands_x"] = max((abs(d) / t for d, t in zip(diffs, tol)), default=0.0)   # how far, in tolerances
    res["bands"] = max(diffs, key=abs) if diffs else 0.0
    res["band_at"] = use[[abs(d) for d in diffs].index(abs(res["bands"]))] if diffs else None
    pk = max(a["env"]) / 10
    n = min(len(a["env"]), len(b["env"]))
    d = [(b["env"][i] - a["env"][i]) / 10 for i in range(n) if a["env"][i] / 10 > pk - 40]
    res["env"] = sum(d) / len(d) if d else 0.0
    d30 = sorted(abs(b["env"][i] - a["env"][i]) / 10 for i in range(n) if a["env"][i] / 10 > pk - 30)
    res["env90"] = d30[int(len(d30) * .9)] if d30 else 0.0
    ta, tb = times(a["env"], a["phrase"]), times(b["env"], b["phrase"])
    if a["phrase"] == "run":
        ta, tb = {}, {}
    worst, what = 0.0, ""
    for k in ta:
        x, y = ta[k], tb[k]
        if x is None and y is None:
            continue
        if x is None or y is None:
            worst, what = 9.9, f"{k}: page {x} fw {y}"
            continue
        ref = RELEASE_AT[a["phrase"]] if k.startswith("down") else 0.0232
        span = max(abs(x - ref), TOL_TIME_MIN / TOL_TIME)
        share = abs(y - x) / span
        if share > worst:
            worst, what = share, f"{k}: page {x:.2f} s fw {y:.2f} s"
    res["timing"], res["timing_what"] = worst, what
    res["judged"] = a["level"] > -60
    if NOISE.get(a["prog"]) or AGE:       # random (noise, Age): frame by frame it cannot match, only level and spectrum
        res["env90"], res["timing"] = 0.0, 0.0
    res["ok"] = not res["judged"] or (abs(res["level"]) <= TOL_LEVEL and res["bands_x"] <= 1.0
                                      and res["timing"] <= TOL_TIME and res["env90"] <= TOL_ENV90)
    return res


def main():
    args = sys.argv[1:]
    n_list = int(args[args.index("--list") + 1]) if "--list" in args else 20
    only = int(args[args.index("--prog") + 1]) if "--prog" in args else None
    allow = set(args[args.index("--allow") + 1].split(",")) if "--allow" in args else set()
    ref, new = (json.load(open(p)) for p in args[:2])
    import re
    from pathlib import Path
    h = Path(__file__).resolve().parents[2] / "firmware" / "src" / "p800_data.h"
    for m in re.finditer(r'^\s*\{(\d+), "[^"]*", \d, \{\{.*?\}\}, \d+, \d+, (\d+),', h.read_text(), re.M):
        NOISE[int(m.group(1))] = int(m.group(2))     # {num, name, mode, {dco 1, dco 2}, iv, dt, nz, ..
    if len(NOISE) != 64:
        raise SystemExit(f"read {len(NOISE)} programs' noise from {h}, expected 64")
    key = lambda r: (r["prog"], r["phrase"], r["vary"])
    nb = {key(r): r for r in new["renders"]}
    hz = ref["bands_hz"]
    global AGE
    AGE = ref["set"] == "page"
    BANDS_HZ[:] = hz
    rows = []
    for a in ref["renders"]:
        if only is not None and a["prog"] != only:
            continue
        b = nb.get(key(a))
        if b is None:
            print("missing in the new set:", key(a))
            continue
        rows.append((a, compare(a, b)))
    bad = [x for x in rows if not x[1]["ok"]]
    n_ok = len(rows) - len(bad)
    for a, r in bad:
        if f"{a['prog']}:{a['phrase']}" in allow:
            print(f"  allowed, outside tolerance: {a['prog']} {a['phrase']} {a['vary']}")
    bad = [x for x in bad if f"{x[0]['prog']}:{x[0]['phrase']}" not in allow]
    print(f"{n_ok} of {len(rows)} renders within tolerance{f', {len(rows) - n_ok - len(bad)} allowed' if len(rows) - n_ok - len(bad) else ''} "
          f"(level {TOL_LEVEL} dB, bands {TOL_BANDS} dB, timing {TOL_TIME:.0%}, env90 {TOL_ENV90} dB)")
    judged = [x for x in rows if x[1]["judged"]]
    for a, r in rows:
        if not r["judged"]:
            print(f"  not judged ({a['level']:.0f} dB on the page): {a['prog']} {a['phrase']} {a['vary']}")
    for name, f in (("level", lambda r: abs(r["level"])), ("bands", lambda r: abs(r["bands"])),
                    ("timing", lambda r: r["timing"]), ("env90", lambda r: r["env90"])):
        v = sorted(f(r) for _, r in judged)
        print(f"  {name:7s} median {v[len(v) // 2]:6.2f}  90% {v[int(len(v) * .9)]:6.2f}  worst {v[-1]:6.2f}")
    print(f"\nworst {n_list} (by how far outside):")
    score = lambda r: max(abs(r["level"]) / TOL_LEVEL, r["bands_x"], r["timing"] / TOL_TIME,
                          r["env90"] / TOL_ENV90)
    for a, r in sorted(judged, key=lambda x: -score(x[1]))[:n_list]:
        band = f"{r['bands']:+5.1f} dB @ {hz[r['band_at']]:7.0f} Hz" if r["band_at"] is not None else "-"
        print(f"  {a['prog']:2d} {a['name'][:20]:20s} {a['phrase']:5s} {a['vary']:4s} level {r['level']:+5.2f}  bands {band}"
              f"  env {r['env']:+5.2f} env90 {r['env90']:4.1f}  width {r['width']:+5.2f}  timing {r['timing']:4.2f} {r['timing_what']}")
    sys.exit(0 if not bad else 1)


if __name__ == "__main__":
    main()
