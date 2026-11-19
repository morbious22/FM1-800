/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 5: the Poly-800 inside Felucca, on the computer. Felucca's own audio path, built as hostsim builds it
 * (engines.c, voice.c, fx.c, seq.c): part 1 on POLY800, notes through voice.c trk_note_on / trk_note_off, blocks
 * through fx.c mix_block (MASTER, DC block, limiter, soft clip). Checks, against p800.c on its own:
 *   - the notes reach the Poly-800 and no Felucca voice is taken
 *   - its level in the mix is the page's, 6 dB down (Q16 full scale = half the mix's): a quiet program to 0.25 dB
 *     (Felucca's soft clip, a tanh over the whole output, takes ~0.2 dB off its peaks)
 *   - a loud one (27's chord, 5.5 x full scale on the page) comes out limited, never past full scale
 *   - a program chosen as a preset (t->preset) is the program that plays
 *   - after a switch to another engine, its notes are let go and it falls silent
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 \
 *      -o build/poly800/in_felucca tests/poly800/in_felucca.c -lm        (after build/gen exists: ./build.sh) */
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../../firmware/bsp/chip/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;
#include "../../firmware/src/core.h"
#include "../../firmware/src/engines.c"
#include "../../firmware/src/params.c"
#include "../../firmware/src/mod.c"
#include "../../firmware/src/voice.c"
#include "../../firmware/src/slicer.c"
#include "../../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../../firmware/bsp/chip/usb.c"
#include "../../firmware/src/midi_uart.c"
#include "../../firmware/src/song_chain.c"
#if FM1_800
#include "../../firmware/src/seq800.c"
#endif
#include "../../firmware/src/seq.c"

static int fails;
static void ok(int c, const char *what)
{
    printf("  %s  %s\n", c ? "ok  " : "FAIL", what);
    fails += !c;
}

static void init(uint32_t prog)
{
    uint32_t i, k;
    memset(trk, 0, sizeof trk);
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_E0; i++) trk[k].p[i] = TP[i].def;
        for (i = 0; i < NSTEP; i++) trk[k].step[i].time = ST_REST;
        trk[k].engine = trk[k].eng_req = (uint8_t)(k ? 0 : ENGI_P800);
    }
    trk[0].preset = (uint8_t)prog;
    song.master_q12 = 4096;                       /* MASTER full: the mix x 1 */
    p800_init();
    p8e_prog = 0xFFu, p8e_live = 0;
}

static uint32_t felucca_voices(void)
{
    uint32_t k, i, n = 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++) n += trk[k].v[i].active;
    return n;
}

/* rms (dB) and peak (of Q15) of n blocks of the mix, notes at block 8 held for `hold` blocks */
static void play(const uint8_t *notes, uint32_t nn, uint32_t blocks, uint32_t hold, double *rms, int32_t *peak)
{
    int32_t o[2 * CTL];
    double e = 0;
    uint32_t b, i;
    *peak = 0;
    for (b = 0; b < blocks; b++) {
        if (b == 8) for (i = 0; i < nn; i++) trk_note_on(&trk[0], notes[i], 100);
        if (b == 8 + hold) for (i = 0; i < nn; i++) trk_note_off(&trk[0], notes[i]);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            if (a > *peak) *peak = a;
            if (!(i & 1u)) e += ((double)o[i] + o[i + 1]) * ((double)o[i] + o[i + 1]) / 4;
        }
    }
    *rms = 10 * log10(e / (blocks * CTL) + 1e-9) - 20 * log10(32768);
}

/* the same notes, p800.c alone (Q16, its full scale 65536) */
static double alone(uint32_t prog, const uint8_t *notes, uint32_t nn, uint32_t blocks, uint32_t hold)
{
    int32_t l[P800_BLK], r[P800_BLK];
    double e = 0;
    uint32_t b, i;
    p800_init();
    p800_program(prog);
    for (b = 0; b < blocks; b++) {
        if (b == 8) for (i = 0; i < nn; i++) p800_note_on(notes[i]);
        if (b == 8 + hold) for (i = 0; i < nn; i++) p800_note_off(notes[i]);
        p800_render(l, r);
        for (i = 0; i < P800_BLK; i++) e += ((double)l[i] + r[i]) * ((double)l[i] + r[i]) / 4;
    }
    return 10 * log10(e / (blocks * P800_BLK) + 1e-9) - 20 * log10(65536);
}

int main(void)
{
    static const uint8_t PIANO[1] = {60}, CHORD[4] = {48, 55, 60, 64};
    double a, b;
    int32_t pk;
    uint32_t i, idx27 = 0, idx12 = 0;
    for (i = 0; i < P800_NPROG; i++) {
        if (P800_PROGS[i].num == 27) idx27 = i;
        if (P800_PROGS[i].num == 12) idx12 = i;
    }
    printf("POLY800 inside Felucca, FM1_800=%d (engine %u, %u presets, last in PRESETS: %s)\n", FM1_800, ENGI_P800,
           ENGINES[ENGI_P800]->npresets, ENGINES[ENGINE_ORDER[NENG_SHOWN - 1]]->name);
    ok(ENGINE_ORDER[NENG_SHOWN - 1] == ENGI_P800 && ENGINES[ENGI_P800]->npresets == 64,
       "POLY800 is an engine with the 64 programs, last in the list");

    init(idx12);                                  /* 12 ACOUSTIC PIANO, one note: quiet, no limiter */
    play(PIANO, 1, 1400, 700, &a, &pk);
    b = alone(idx12, PIANO, 1, 1400, 700);
    printf("  12 piano note: in Felucca %.2f dB, alone %.2f dB - 6 = %.2f dB, peak %d of 32768\n", a, b, b - 6.02, pk);
#if FM1_800
    ok(fabs(a - (b - 6.02)) < 0.05, "its level at the output is the page's, 6 dB down (within 0.05 dB: no tanh)");
#else
    ok(fabs(a - (b - 6.02)) < 0.25, "its level in the mix is the page's, 6 dB down (within 0.25 dB)");
#endif
    init(idx12);
    trk_note_on(&trk[0], 60, 100);
    ok(p8.nheld == 1 && felucca_voices() == 0, "a note reaches the Poly-800 and takes no Felucca voice");
    trk_note_off(&trk[0], 60);
    ok(p8.nheld == 0, "its note-off reaches it");

    init(idx12);                                  /* on P-800, no note: it rests (the shared chain costs ~30 % of the
                                                   * FM-1 running in silence); a note wakes it */
    {
        int32_t o[2 * CTL];
        uint32_t b2;
        for (b2 = 0; b2 < 1500u; b2++) mix_block(o, CTL);
        ok(p8e_live == 0, "with no note it rests (renders nothing) once the 1 s tail is over");
        trk_note_on(&trk[0], 60, 100);
        mix_block(o, CTL);
        ok(p8e_live > 0 && p800_busy(), "a note wakes it at once");
        trk_note_off(&trk[0], 60);
    }

    init(idx27);                                  /* 27 BRIGHT PERC. ORGAN, the chord: 5.5 x full scale on the page */
    play(CHORD, 4, 1400, 700, &a, &pk);
    printf("  27 organ chord: in Felucca %.2f dB, peak %d of 32768\n", a, pk);
    ok(pk <= 32767, "the loudest chord is limited, never past full scale");
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        ok(p8.pg == &P800_PROGS[idx27], "the part's preset is the program that plays");
    }

#if FM1_800
    ok(TRK_DEF[0][0] == ENGI_P800, "at power-on part 1 is P-800 (STRINGS)");
    init(idx12);                                  /* every part's notes go to it, none to Felucca's voices */
    trk[1].engine = trk[1].eng_req = ENGI_FM6;
    trk_note_on(&trk[1], 64, 100);
    ok(p8.nheld == 1 && felucca_voices() == 0, "a note of part 2 (FM6) plays the Poly-800 too, no Felucca voice");
    trk_note_off(&trk[1], 64);
    ok(p8.nheld == 0, "and its note-off");
    {                                             /* the knee: straight to 24576, then bent under 32767 */
        int32_t x;
        int ok_k = knee(24576) == 24576 && knee(-12000) == -12000 && knee(1 << 20) <= 32767 && knee(-(1 << 20)) >= -32767;
        for (x = 24577; x < 200000; x += 7)
            ok_k &= knee(x) >= knee(x - 7) && knee(x) <= 32767;
        ok(ok_k, "the output knee: straight to 0.75, then rising to under full scale, never past");
    }
    {                                             /* the tape echo (echo800.c), straight through echo_block */
        static int32_t l[3 * P800_FS], r[3 * P800_FS];
        uint32_t i, n = 3u * P800_FS / 32u * 32u, D = EC_DELAY[50], p1 = 0, p2 = 0, ag;   /* (whole blocks) */
        int32_t a1 = 0, a2 = 0, big = 0;
        double hf1 = 0, hf2 = 0, e1 = 0, e2 = 0;
        ok(EC_DELAY[0] == 1764 && EC_DELAY[99] == 44100, "DELAY: 40 ms at 0 .. 1 s at 99");
        ec.delay = 50, ec.repeat = 4, ec.balance = 5;
        memset(l, 0, sizeof l), memset(r, 0, sizeof r);      /* a loud click: the repeats' times, levels, colour */
        memset(dly_buf, 0, sizeof dly_buf);
        ec.on = 1, ec.age = 0, ec.quiet = 0, ec.d = (int32_t)D << 8, ec.lp = ec.hpx = ec.hpy = 0, ec.w = ec.wow = ec.flt = 0;
        for (i = 100; i < 140u; i++) l[i] = r[i] = (i & 1u) ? 6000 : -6000;   /* (a bright burst) */
        for (i = 0; i < n; i += 32u) echo_block(l + i, r + i, 32u);
        for (i = 300u; i < 100u + D + D / 2u; i++) if (abs(l[i]) > a1) a1 = abs(l[i]), p1 = i;
        for (i = 100u + D + D / 2u; i < 100u + 2u * D + D / 2u; i++) if (abs(l[i]) > a2) a2 = abs(l[i]), p2 = i;
        for (i = p1 - 300u; i < p1 + 300u; i++) e1 += (double)l[i] * l[i], hf1 += (double)(l[i] - l[i - 1]) * (l[i] - l[i - 1]);
        for (i = p2 - 300u; i < p2 + 300u; i++) e2 += (double)l[i] * l[i], hf2 += (double)(l[i] - l[i - 1]) * (l[i] - l[i - 1]);
        printf("  echo: DELAY 50 = %u samples; repeats at +%u and +%u, peaks %d and %d, top end %.2f then %.2f\n",
               D, p1 - 100u, p2 - 100u, a1, a2, hf1 / e1, hf2 / e2);
        ok(p1 - 100u > D - D / 50u && p1 - 100u < D + D / 50u && p2 - 100u > 2u * D - D / 25u && p2 - 100u < 2u * D + D / 25u,
           "the repeats come at DELAY and twice DELAY (within the wow)");
        ok(a2 < a1 && hf2 / e2 < hf1 / e1, "each repeat softer and darker than the one before (less top end for its level)");
        {                                         /* AGE: 0 a new tape, 10 a worn one */
            double wob[2], top[2], dip[2];
            for (ag = 0; ag < 2u; ag++) {
                double m = 0, v = 0, prev = 0, lo = 1e30, hi = 0;
                uint32_t nz = 0, k;
                memset(l, 0, sizeof l), memset(r, 0, sizeof r);
                memset(dly_buf, 0, sizeof dly_buf);
                ec.age = (uint8_t)(ag * 10u), ec.repeat = 0, ec.balance = 10, ec.dg = ec.dt = 32768;
                ec.d = (int32_t)D << 8, ec.lp = ec.hpx = ec.hpy = 0, ec.w = ec.wow = ec.flt = 0;
                for (i = 0; i < n; i++) l[i] = r[i] = (int32_t)(p800_sin(i * 97391548u) / 4);   /* (~1 kHz, steady) */
                for (i = 0; i < n; i += 32u) echo_block(l + i, r + i, 32u);
                for (i = D + 4410u; i < n; i++)  /* the waver: the spread of the time between upward crossings */
                    if (l[i - 1] < 0 && l[i] >= 0) {
                        double t = i - 1 + (double)-l[i - 1] / (l[i] - l[i - 1]);
                        if (prev) { m += t - prev, v += (t - prev) * (t - prev), nz++; }
                        prev = t;
                    }
                wob[ag] = sqrt(v / nz - (m / nz) * (m / nz));
                for (k = D + 4410u; k + 441u < n; k += 441u) {   /* the dropouts: the level of each 10 ms */
                    double e = 0;
                    for (i = k; i < k + 441u; i++) e += (double)l[i] * l[i];
                    if (e < lo) lo = e;
                    if (e > hi) hi = e;
                }
                dip[ag] = 10 * log10(hi / lo);
                memset(l, 0, sizeof l), memset(r, 0, sizeof r);   /* the colour: a bright burst's echo */
                memset(dly_buf, 0, sizeof dly_buf);
                ec.d = (int32_t)D << 8, ec.lp = ec.hpx = ec.hpy = 0, ec.w = ec.wow = ec.flt = 0, ec.dg = ec.dt = 32768;
                {                                 /* (white noise) */
                    uint32_t z = 12345;
                    for (i = 100; i < 600u; i++) z = z * 1664525u + 1013904223u, l[i] = r[i] = (int32_t)(z >> 20) - 2048;
                }
                for (i = 0; i < n; i += 32u) echo_block(l + i, r + i, 32u);
                {
                    double e = 0, h = 0;
                    for (i = 100u + D - 200u; i < 100u + D + 900u; i++) e += (double)l[i] * l[i], h += (double)(l[i] - l[i - 1]) * (l[i] - l[i - 1]);
                    top[ag] = h / e;
                }
            }
            printf("  echo AGE 0 / 10: waver %.3f / %.3f samples, level swing %.1f / %.1f dB, top end %.2f / %.2f\n",
                   wob[0], wob[1], dip[0], dip[1], top[0], top[1]);
            ok(wob[1] > 5 * wob[0], "AGE 10: the echo wavers far more than a new tape's");
            ok(dip[1] > dip[0] + 1.5 && dip[1] < 6, "AGE 10: dropouts (the level dips, a few dB at most)");
            ok(top[1] < top[0] * 0.6, "AGE 10: the echo duller");
            ec.age = 3, ec.balance = 5, ec.repeat = 4;
        }
        memset(l, 0, sizeof l), memset(r, 0, sizeof r);      /* REPEAT 10, a loud burst: it runs away slowly */
        memset(dly_buf, 0, sizeof dly_buf);                  /* and the tape's saturation holds it */
        ec.repeat = 10, ec.age = 3, ec.delay = 10, ec.quiet = 0, ec.d = (int32_t)EC_DELAY[10] << 8;
        for (i = 100; i < 4000u; i++) l[i] = r[i] = (int32_t)((i * 2654435761u) >> 16) - 32768;   /* (noise, 2 x full) */
        for (i = 0; i < n; i += 32u) echo_block(l + i, r + i, 32u);
        for (i = n - P800_FS; i < n; i++) if (abs(l[i]) > big) big = abs(l[i]);
        printf("  echo: REPEAT 10, 2 s after a loud burst: peak %d (the mix's full scale 16384)\n", big);
        ok(big > 2000 && big <= 16384 + 16384, "REPEAT 10 runs away and the tape's saturation holds it");
        ec.on = 0;                               /* off: no more input, the repeats die away, then it rests */
        ec.repeat = 6;
        for (i = 0; i < n; i += 32u) { memset(l + i, 0, 128), memset(r + i, 0, 128); echo_block(l + i, r + i, 32u); }
        for (i = 0; i < n; i += 32u) { memset(l + i, 0, 128), memset(r + i, 0, 128); echo_block(l + i, r + i, 32u); }
        ok(ec.quiet > EC_LEN, "switched off, its repeats die away and the echo rests (costs nothing)");
        ec.repeat = 4, ec.balance = 4, ec.delay = 50, ec.age = 3;
    }
#else
    init(idx12);                                  /* a switch to another engine with a note held: let go, then silent */
    trk_note_on(&trk[0], 60, 100);
    trk[0].eng_req = 0;
    {
        int32_t o[2 * CTL];
        uint32_t b2, k, quiet = 0;
        for (b2 = 0; b2 < 12000u; b2++) {
            mix_block(o, CTL);
            if (b2 > 11000u) for (k = 0; k < 2u * CTL; k++) quiet |= (uint32_t)(o[k] < 0 ? -o[k] : o[k]) > 2u;
        }
        ok(p8.nheld == 0 && !quiet && !p8e_live, "after a switch away its notes end; 8 s later it is silent and idle");
    }
#endif
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
