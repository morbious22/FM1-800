/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 5: the Poly-800 sound (p800.c) inside Felucca, as one more engine, POLY800.
 *
 * It is an engine in name only, so Felucca's PRESETS list and screens offer it as they are: its 64 "presets" are the
 * Poly-800's programs, last in PRESETS (engines.c ENGINE_ORDER; PRESETS' KNOB 2 picks it, no EDIT key: as ADPCM). Its sound is not Felucca's: a part playing POLY800 sends its notes to the one
 * Poly-800 (voice.c trk_note_on / trk_note_off: no Felucca voice is taken), and its stereo output joins the dry mix
 * after the parts (fx.c mix_block), so MASTER, Felucca's limiter and the USB audio take it as any part. The Poly-800
 * has its own voices, filter, chorus and phaser; Felucca's per-part LEVEL, PAN, FX sends and envelopes do not apply.
 * Its scale: the page's full scale (Q16 65536) is half the mix's (16384 of 32768, -6 dB): Felucca's output stage
 * limits from 18000 and soft-clips (tanh) all of it, so the programs as loud as the page's full scale stay under the
 * limiter and almost straight; the organ chords (up to 5.5 times) meet the limiter, not a hard clip. MASTER sets the
 * rest. p800_init: main.c felucca_init,
 * before the audio starts (a note can come in the first block, before its mix). */
#include "p800.c"

static void p8e_note_on(struct track *t, voice_t *v) { (void)t, (void)v; }
static void p8e_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t, (void)v, (void)out, (void)n, (void)m;
}
static const preset_t P8E_PRESETS[P800_NPROG] = {P800_FELUCCA_PRESETS};

static const engine_t ENG_P800 = {
    "POLY800",
    {"POLY-800", "POLY-800"},
    {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
     {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    P8E_PRESETS,
    P800_NPROG,
    {P_LEVEL, P_ATK, P_REL, P_PAN},
    0, 0, 0, 0,
    p8e_note_on,
    p8e_render,
};

static uint8_t p8e_prog = 0xFFu;                 /* the program loaded, 0xFF = none yet */
static uint16_t p8e_live;                        /* blocks to keep rendering after the last part let it go (tails) */
#define P8E_TAIL ((uint16_t)(8u * P800_FS / P800_BLK))   /* 8 s: the longest release, 5 s x 1.5 + .1, and the phaser */

/* a part plays POLY800 (its notes go to the Poly-800): the engine it asked for, so a note during the switch's fade
 * already goes there (voice.c) */
#define P8E_PART(t) ((t)->eng_req == ENGI_P800)

static void p800_all_off(void)
{
    while (p8.nheld)
        p800_note_off(p8.held[p8.nheld - 1u]);
}

/* once a block, after the parts (fx.c mix_block): the program of the first part playing POLY800, and its sound added
 * to the dry mix. With no part on it: its held notes let go, and it renders on for 8 s (releases, chorus, phaser
 * tail), then costs nothing */
static void p8e_mix(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t bl[P800_BLK], br[P800_BLK];
    uint32_t i, k;
    const track_t *on = 0;
    for (k = 0; k < NTRK && !on; k++)
        if (P8E_PART(&trk[k]) && !trk[k].p[P_MUTE])
            on = &trk[k];
    if (on) {
        if (on->preset != p8e_prog) {
            p8e_prog = on->preset;
            p800_program(p8e_prog);
        }
        p8e_live = P8E_TAIL;
    } else if (p8.nheld) {
        p800_all_off();
    }
    if (!p8e_live)
        return;
    if (!on)
        p8e_live--;
    for (k = 0; k + P800_BLK <= n; k += P800_BLK) {     /* (n is CTL, 32: one block) */
        p800_render(bl, br);
        for (i = 0; i < P800_BLK; i++) {
            l[k + i] += bl[i] >> 2;
            r[k + i] += br[i] >> 2;
        }
    }
}
