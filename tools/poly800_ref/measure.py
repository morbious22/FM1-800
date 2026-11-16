#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 morbious22 (FM1-800)
"""FM1-800 step 3: measure renders, so two sets of them can be compared by numbers.

  measure.py SET_DIR OUT.json        SET_DIR as render.js writes it (index.json + the WAV files)

The same measurements are taken of the web page's renders (the reference) and, from step 4 on, of the firmware's
sound rendered on the computer; compare.py then says where they differ. Per render:

  level     RMS of the whole render, dB full scale (mid, (L + R) / 2), and the peak of either side
  env       loudness every 10 ms (RMS of mid, dB, 0.1 dB steps, floor -90 dB): attack, decay, release timing
  bands     third-octave band levels (dB) over the held part of the phrase (31 bands, 20 Hz .. 20 kHz centres):
            the filter, the footages and the tone shaping
  width     side / mid level over the held part (dB): how much the chorus spreads it

Needs numpy.
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np

FS = 44100
FRAME = 441                                      # 10 ms
CENTRES = [1000 * 2 ** (k / 3) for k in range(-17, 14)]   # 20 Hz .. 20 kHz
HELD = {"chord": (0.5, 2.4), "note": (0.5, 2.9), "run": (0.1, 1.2)}   # seconds: inside the notes' hold


def read_wav(path):
    """32-bit float (format 3) or 16-bit stereo WAV -> float array (n, 2)"""
    raw = Path(path).read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a WAV file")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, _, _, bits = fmt
    if rate != FS:
        raise SystemExit(f"{path}: {rate} Hz, expected {FS}")
    if tag == 3 and bits == 32:
        a = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif tag == 1 and bits == 16:
        a = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise SystemExit(f"{path}: format {tag}/{bits} bits not handled")
    return a.reshape(-1, ch) if ch > 1 else np.stack([a, a], axis=1)


def db(x, floor=-90.0):
    return max(floor, 10.0 * np.log10(x)) if x > 0 else floor


def band_levels(x):
    """third-octave band power of x (dB), from an averaged periodogram (Hann, 8192, half overlap)"""
    n = 8192
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    win = np.hanning(n)
    norm = (win ** 2).sum()
    psd, count = np.zeros(n // 2 + 1), 0
    for s in range(0, len(x) - n + 1, n // 2):
        psd += np.abs(np.fft.rfft(x[s:s + n] * win)) ** 2
        count += 1
    psd /= count * norm
    f = np.fft.rfftfreq(n, 1 / FS)
    out = []
    for c in CENTRES:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        out.append(round(db(psd[(f >= lo) & (f < hi)].sum() * 2 / n), 1))
    return out


def measure(a, phrase):
    mid, side = (a[:, 0] + a[:, 1]) / 2, (a[:, 0] - a[:, 1]) / 2
    frames = len(mid) // FRAME
    env = [int(round(db((mid[i * FRAME:(i + 1) * FRAME] ** 2).mean()) * 10)) for i in range(frames)]
    s0, s1 = (int(t * FS) for t in HELD[phrase])
    held_mid, held_side = mid[s0:s1], side[s0:s1]
    return {
        "level": round(db((mid ** 2).mean()), 2),
        "peak": round(float(np.abs(a).max()), 4),
        "env": env,
        "bands": band_levels(held_mid),
        "width": round(db((held_side ** 2).mean()) - db((held_mid ** 2).mean()), 2),
    }


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: measure.py SET_DIR OUT.json")
    d = Path(sys.argv[1])
    index = json.loads((d / "index.json").read_text())
    out = {"set": index["set"], "fs": FS, "frame": FRAME, "bands_hz": [round(c, 1) for c in CENTRES],
           "held_s": HELD, "renders": []}
    for r in index["renders"]:
        m = measure(read_wav(d / r["file"]), r["phrase"])
        out["renders"].append({k: r[k] for k in ("prog", "name", "phrase", "vary")} | m)
    Path(sys.argv[2]).write_text(json.dumps(out, separators=(",", ":")))
    print(f"{len(out['renders'])} renders measured -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
