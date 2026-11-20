/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 11: the Poly-800's sequencer, 256 notes, entered a step at a time (the FM-1's build only).
 *
 * A sequence is up to 256 steps, a 16th note each; a step is a note or a chord (up to 8 notes, all of them counting to
 * the 256 notes), a TIE (the step before held on) or a REST. It loops over the steps it has (12 steps: a bar of 3/4).
 * Entering (REC, ui800.c): every note or chord played, as the keys or MIDI hold it, is a step when the last note lets
 * go; ARP a TIE, SEQ a REST. Playing (PLAY/STOP): in step with part 1's steps (seq.c seq_tick: Felucca's transport,
 * its tempo or MIDI clock from USB or TRS, which it follows as soon as clock comes in), each note sounding 80 % of its
 * step, a TIE carrying it on through the next. The keys play over it.
 *
 * Stored as bytes: a step is its count (1..8, its notes after), SQ_REST or SQ_TIE. Saved (OBJ_P8SEQ, prog800.c) when
 * an entry ends; the tempo with the global settings. */

/* MIDI in (seq.c midi_event, FM1-800): the channel heard, 0 OMNI (every one) or 1..16. Notes, pitch bend and the mod
 * wheel (CC 1) go straight to the Poly-800; CC 120 / 123 let every note go; all else is not heard (program change too:
 * the programs are the A / B banks') */
static uint8_t p8_midi_ch;

#define SQ_MAXN 256u                             /* notes */
#define SQ_MAXS 256u                             /* steps */
#define SQ_BYTES (SQ_MAXN + SQ_MAXS)
#define SQ_REST 0x00u
#define SQ_TIE 0xFEu
#define SQ_CHORD 8u                              /* notes a step */

static struct {
    uint8_t buf[SQ_BYTES];                       /* the sequence */
    uint16_t len, nsteps, nnotes;                /* its bytes, steps, notes */
    uint16_t rd, at;                             /* playing: the next step's byte, the step it is on (0..) */
    uint8_t on[SQ_CHORD], non;                   /* the notes it has sounding */
    uint32_t off;                                /* when they let go (seq_tick's units: samples, or the clock's grid;
                                                  * 0: not yet, the next step is a TIE) */
    uint8_t rec;                                 /* entering */
    uint8_t grp[SQ_CHORD], ngrp;                 /* the chord being entered: every note held since the keys were bare */
    uint8_t last[SQ_CHORD], nlast, last_kind;    /* the step entered last (for the screen) */
    uint8_t dirty;                               /* entered since saved */
    uint32_t ext_ms;                             /* MIDI clock last seen */
} sq;

static void sq_release(void)
{
    uint32_t i;
    for (i = 0; i < sq.non; i++)
        p800_note_off(sq.on[i]);
    sq.non = 0;
    sq.off = 0;
}

static void sq_clear(void)
{
    sq_release();
    sq.len = sq.nsteps = sq.nnotes = 0;
    sq.rd = sq.at = 0;
    sq.ngrp = sq.nlast = 0;
}

/* a step onto the end: n notes (a chord), or SQ_REST / SQ_TIE (n 0); 0 when the sequence is full */
static int sq_add(uint8_t kind, const uint8_t *notes, uint32_t n)
{
    uint32_t i;
    if (sq.nsteps >= SQ_MAXS || sq.nnotes + n > SQ_MAXN || sq.len + 1u + n > SQ_BYTES)
        return 0;
    sq.buf[sq.len++] = n ? (uint8_t)n : kind;
    for (i = 0; i < n; i++)
        sq.buf[sq.len++] = notes[i];
    sq.nsteps++, sq.nnotes = (uint16_t)(sq.nnotes + n);
    sq.last_kind = n ? 1u : kind == SQ_TIE ? 2u : 3u;
    sq.nlast = (uint8_t)n;
    for (i = 0; i < n; i++)
        sq.last[i] = notes[i];
    sq.dirty = 1;
    return 1;
}

/* seq.c seq_tick, part 1, every block (n in its units): the notes' ends; stopped, nothing sounds */
static void sq_tick(uint32_t n)
{
    if (!song.playing) {
        if (sq.non)
            sq_release();
        return;
    }
    if (sq.non && sq.off) {
        if (sq.off <= n)
            sq_release();
        else
            sq.off -= n;
    }
}

/* the step after the one at byte rd (round the end): a TIE? Then the notes sounding now are not let go at 80 % */
static int sq_tie_next(uint32_t rd)
{
    uint32_t b = sq.buf[rd];
    rd += (b == SQ_TIE || b == SQ_REST || b > SQ_CHORD) ? 1u : 1u + b;
    return sq.buf[rd >= sq.len ? 0u : rd] == SQ_TIE;
}

/* seq.c seq_tick, part 1's step begins (top: PLAY, from the first; period: a step, seq_tick's units) */
static void sq_step(uint32_t top, uint32_t period)
{
    uint32_t b, n, i;
    if (!sq.nsteps || sq.rec)
        return;
    if (top || sq.rd >= sq.len)
        sq.rd = 0, sq.at = 0;
    b = sq.buf[sq.rd];
    if (b == SQ_TIE) {                           /* the step before held on through this one (and on, tied again) */
        if (sq.non)
            sq.off = sq_tie_next(sq.rd) ? 0u : period * 8u / 10u;
        sq.rd++;
    } else if (b == SQ_REST || b > SQ_CHORD) {
        sq_release();
        sq.rd++;
    } else {                                     /* a note or a chord */
        sq_release();
        n = b;
        for (i = 0; i < n; i++) {
            sq.on[i] = sq.buf[sq.rd + 1u + i];
            p800_note_on(sq.on[i]);
        }
        sq.non = (uint8_t)n;
        sq.off = sq_tie_next(sq.rd) ? 0u : period * 8u / 10u;   /* (0: held to the next step, a TIE) */
        sq.rd = (uint16_t)(sq.rd + 1u + n);
    }
    sq.at = (uint16_t)(sq.rd >= sq.len ? 0u : sq.at + 1u);
    if (sq.rd >= sq.len)
        sq.rd = 0;
}

/* main loop, entering: the chord held grows; when the last note lets go it is a step */
static void sq_rec_poll(void)
{
    uint32_t i, j;
    if (!sq.rec)
        return;
    for (i = 0; i < p8.nheld; i++) {
        uint8_t nt = p8.held[i], have = 0;
        for (j = 0; j < sq.ngrp; j++)
            have |= sq.grp[j] == nt;
        if (!have && sq.ngrp < SQ_CHORD)
            sq.grp[sq.ngrp++] = nt;
    }
    if (!p8.nheld && sq.ngrp) {
        (void)sq_add(1, sq.grp, sq.ngrp);
        sq.ngrp = 0;
    }
}
