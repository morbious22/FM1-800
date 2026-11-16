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
    printf("POLY800 inside Felucca (engine %u, %u presets, last in PRESETS: %s)\n", ENGI_P800,
           ENGINES[ENGI_P800]->npresets, ENGINES[ENGINE_ORDER[NENG_SHOWN - 1]]->name);
    ok(ENGINE_ORDER[NENG_SHOWN - 1] == ENGI_P800 && ENGINES[ENGI_P800]->npresets == 64,
       "POLY800 is an engine with the 64 programs, last in the list");

    init(idx12);                                  /* 12 ACOUSTIC PIANO, one note: quiet, no limiter */
    play(PIANO, 1, 1400, 700, &a, &pk);
    b = alone(idx12, PIANO, 1, 1400, 700);
    printf("  12 piano note: in Felucca %.2f dB, alone %.2f dB - 6 = %.2f dB, peak %d of 32768\n", a, b, b - 6.02, pk);
    ok(fabs(a - (b - 6.02)) < 0.25, "its level in the mix is the page's, 6 dB down (within 0.25 dB)");
    init(idx12);
    trk_note_on(&trk[0], 60, 100);
    ok(p8.nheld == 1 && felucca_voices() == 0, "a note reaches the Poly-800 and takes no Felucca voice");
    trk_note_off(&trk[0], 60);
    ok(p8.nheld == 0, "its note-off reaches it");

    init(idx27);                                  /* 27 BRIGHT PERC. ORGAN, the chord: 5.5 x full scale on the page */
    play(CHORD, 4, 1400, 700, &a, &pk);
    printf("  27 organ chord: in Felucca %.2f dB, peak %d of 32768\n", a, pk);
    ok(pk <= 32767, "the loudest chord is limited, never past full scale");
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        ok(p8.pg == &P800_PROGS[idx27], "the part's preset is the program that plays");
    }

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
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
