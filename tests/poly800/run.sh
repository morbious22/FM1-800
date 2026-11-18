#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 morbious22 (FM1-800)
# FM1-800 step 4: the Poly-800 sound (firmware/src/p800.c) against the web page, on the computer.
#   tests/poly800/run.sh            (from the repository root; needs cc, Python 3 with numpy)
# Pitch against the page's formula; then the dry set (Age 0, no phaser: the voices, filter, envelopes, chorus) and the
# phaser set rendered, measured and compared with the page's measurements (tests/poly800/ref_*.json).
# The page set (Age on) is random note by note on the page too: see tools/poly800_ref/README.md for how it compares.
set -e
OUT="${OUT:-build/poly800}"
mkdir -p "$OUT"
cc -std=c99 -O2 -Wall -Wextra -Werror -Ifirmware/src -o "$OUT/host" tests/poly800/host.c -lm
"$OUT/host" pitch
for set in dry phaser; do
    "$OUT/host" "$OUT" $set
    python3 tools/poly800_ref/measure.py "$OUT/$set" "$OUT/$set.json"
    # 23:run, one known near-miss: its 31 Hz band, 3.1 dB against 3 (a note change's thump in a fast run)
    python3 tools/poly800_ref/compare.py tests/poly800/ref_$set.json "$OUT/$set.json" --list 5 --allow 23:run
done
# step 5: inside Felucca (its engine list, voice.c, fx.c); needs Felucca's generated headers (build/gen: ./build.sh)
if [ -f build/gen/felucca_tables.h ]; then
    for m in 1 0; do                              # FM1_800=1: the FM-1's build (step 6); 0: P-800 as one Felucca engine
        cc -O2 -w -DFM1_800=$m -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 \
            -o "$OUT/in_felucca$m" tests/poly800/in_felucca.c -lm
        "$OUT/in_felucca$m"
    done
    mkdir -p "$OUT/screens"
    for hp in 1 0; do                             # step 7: the screen and its controls; FM1_HP=0: the shared build
        cc -O1 -w -DFM1_800=1 -DFM1_HP=$hp -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip \
            -Ifirmware/bsp/boards/fm1 -Itests -o "$OUT/ui800_test$hp" tests/poly800/ui800_test.c -lm
        "$OUT/ui800_test$hp" "$OUT/screens"
    done
else
    echo "(inside Felucca: skipped, no build/gen: run ./build.sh first)"
fi
echo "poly800: all passed"
