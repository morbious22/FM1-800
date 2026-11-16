/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800: the Poly-800 sound, made to match the web prototype (tools/poly800_ref, tests/poly800).
 *
 * Self-contained: integer only (no float: Felucca keeps none, and the audio interrupt saves no FPU state), no
 * Felucca headers, so the host test (tests/poly800/host.c) builds it as it is. The tables are generated
 * (tools/poly800_ref/gen_p800.py -> p800_data.h) from the page, with the 64 programs read out of it.
 *
 *   p800_init()                       once
 *   p800_program(i)                   program index 0..63 (11, 12 .. 88)
 *   p800_set(P800_TUNE.., v)          the settings no program holds (tune, volume, age, phaser, high-pass)
 *   p800_note_on(n) / p800_note_off(n)    between blocks
 *   p800_render(l, r)                 one block of P800_BLK samples, stereo, Q16 (65536 = 1.0, the page's full scale;
 *                                     louder programs go past it, as on the page: the caller scales or limits)
 *
 * What it copies of the page, part for part (the page's lines are quoted where it matters):
 *   voices    8 (Whole: DCO 1) or 4 (Double: DCO 1 + DCO 2), allocated as the page's on(): a free voice, else
 *             one releasing, else the oldest; a stolen voice fades over 5 ms (kill()) while the new note starts
 *   DCO       four square footages (16' 8' 4' 2') from one phase counter at 16' (each footage is one bit of it),
 *             summed as a staircase with a 2-point polyBLEP at each step; Saw = levels 1 .667 .333 .167; Chrome's
 *             square plateau (0.848); footages above Nyquist silent, as Chrome's band-limited tables
 *   DEG       6 stages, the page's stage(): from 0.0001 a linear attack to 1, a linear decay to the break point
 *             (skipped at 31), a linear slope to sustain; release: exponential, tau = tm(r) / 4 (relp())
 *   shared    one noise (gated by DEG 3 only), one filter (two TPT state-variable low-passes = Chrome's biquads, Q in
 *             dB), DEG 3 and the MG on its cutoff, low shelf -5 dB / 220 Hz, high shelf +3 dB / 6 kHz, the high-pass
 *             (10 Hz when off), the chorus (13 and 17 ms, swept 4 ms, hard left and right), the volume, then the
 *             phaser: allpasses swept by its LFO, its feedback one 128-sample block late, as Chrome runs the page's
 *             delay-less loop (tools/poly800_ref/README.md)
 *   control   per block of 32 samples (pitch, cutoff, LFOs); envelopes and the oscillators per sample
 *
 * Numbers: signals Q16 (65536 = 1.0), Q22 through the filter chain (FILT_SH: at low cutoffs a Q16 state-variable
 * filter sticks a little off zero, its corrections rounding away); filter coefficients P800_ONE, damping Q28;
 * envelopes P800_ONE. Products of a
 * coefficient and a signal need 64 bits: they go through mulq30 / mulq28, out of line on the FM-1
 * (firmware/bsp/docs/CPU.md: a 64-bit multiply among 16-bit loads now and then leaves a register a bit off). */
#include <stdint.h>

#ifndef P800_NOINLINE
#define P800_NOINLINE __attribute__((noinline))
#endif
#define P800_BLK 32u
#ifndef P800_POOL
#define P800_POOL __attribute__((section(".pool")))   /* the delay lines: Felucca's pool, not its 72 KiB of fast RAM,
                                                        * which keeps 4 KiB spare (tools/build.py) */
#endif

typedef struct { uint8_t oct, saw, foot[4], level; } p800_dco_t;
typedef struct {
    uint8_t num;
    char name[24];
    uint8_t mode;                /* 0 Whole (8 voices), 1 Double (4) */
    p800_dco_t dco[2];
    uint8_t iv, dt;              /* DCO 2 interval (semitones), detune 0..3 */
    uint8_t nz;                  /* noise 0..15 */
    uint8_t co, rs, kt, ep, ei, tr;   /* cutoff 0..99, resonance 0..15, key track 0..2, EG polarity (1 inverted),
                                       * EG intensity 0..15, DEG 3 trigger (0 single, 1 multi) */
    uint8_t ch;                  /* chorus */
    uint8_t deg[3][6];           /* attack, decay, break point, slope, sustain, release: 0..31 */
    uint8_t mg[4];               /* frequency, delay, DCO intensity, VCF intensity: 0..15 */
} p800_prog_t;

#include "p800_data.h"

enum { P800_TUNE, P800_VOL, P800_AGE, P800_PH_STAGES, P800_PH_RATE, P800_PH_DEPTH, P800_PH_FB, P800_PH_MIX,
       P800_HP_ON, P800_HP_CUT, P800_NSET };
static const int16_t P800_SET_DEF[P800_NSET] = {0, 22, 3, 0, 4, 6, 3, 5, 0, 25};   /* the page's controls as it opens */

#define P800_ONE (1 << 30)

P800_NOINLINE static int32_t mulq30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 29)) >> 30); }
P800_NOINLINE static int32_t mulq28(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 27)) >> 28); }
P800_NOINLINE static uint32_t mulshr(uint32_t a, uint32_t b, uint32_t sh) { return (uint32_t)(((uint64_t)a * b) >> sh); }
P800_NOINLINE static int64_t mul64(int32_t a, int32_t b) { return (int64_t)a * b; }

static int32_t p800_sin(uint32_t ph)             /* Q15, linear between the table's points */
{
    uint32_t i = ph >> 22, f = (ph >> 12) & 1023u;
    int32_t a = P800_SIN[i], b = P800_SIN[i + 1];
    return a + (((b - a) * (int32_t)f) >> 10);
}

/* ------------------------------------------------------------ envelopes --- */
enum { E_OFF, E_ATK, E_DEC, E_SLOPE, E_SUS, E_REL };
typedef struct {
    int32_t lvl;                 /* P800_ONE */
    uint8_t st;
    int32_t ra, rd, rs, bp, su;  /* rates (P800_ONE a sample), break point and sustain (P800_ONE) */
    int32_t mul;                 /* release: the level's factor a block */
} p800_env_t;

static void env_trigger(p800_env_t *e, const uint8_t *d)
{
    e->lvl = P800_ENV_START;     /* page stage(): setValueAtTime(0.0001,t), even when it was sounding */
    e->st = E_ATK;
    e->ra = P800_ATK[d[0]];
    e->rd = P800_DEC[d[1]];
    e->rs = P800_DEC[d[3]];
    e->bp = d[2] >= 31 ? P800_ONE : d[2] * 34636833;   /* x / 31, P800_ONE (2^30 / 31) */
    e->su = d[4] >= 31 ? P800_ONE : d[4] * 34636833;
}
static void env_release(p800_env_t *e, int32_t mul)
{
    if (e->st != E_OFF) {        /* page relp(): cancelAndHoldAtTime, then setTargetAtTime(0, ..) */
        e->st = E_REL;
        e->mul = mul;
    }
}
/* one block of the envelope into o[] (Q14) */
static void env_block(p800_env_t *e, int16_t *o)
{
    uint32_t i;
    int32_t l = e->lvl;
    if (e->st == E_REL) {        /* exponential: its end of block exactly, straight lines between */
        int32_t end = mulq30(l, e->mul), d = (end - l) >> 5;
        for (i = 0; i < P800_BLK; i++)
            o[i] = (int16_t)((l + d * (int32_t)(i + 1)) >> 16);
        e->lvl = end;
        return;
    }
    for (i = 0; i < P800_BLK; i++) {
        switch (e->st) {
        case E_ATK:
            l += e->ra;
            if (l >= P800_ONE) {
                l = P800_ONE;
                e->st = e->bp < P800_ONE ? E_DEC : E_SLOPE;   /* break point 31: no decay */
            }
            break;
        case E_DEC:
            l -= e->rd;
            if (l <= e->bp) {
                l = e->bp;
                e->st = E_SLOPE;
            }
            break;
        case E_SLOPE:            /* towards sustain, up or down (page: Math.abs) */
            if (l < e->su) {
                l += e->rs;
                if (l >= e->su) l = e->su, e->st = E_SUS;
            } else {
                l -= e->rs;
                if (l <= e->su) l = e->su, e->st = E_SUS;
            }
            break;
        default:
            break;
        }
        o[i] = (int16_t)(l >> 16);
    }
    e->lvl = l;
}

/* ------------------------------------------------------------------ DCO --- */
typedef struct {
    uint32_t ph, inc;            /* the 16' phase */
    int32_t lev[4];              /* footage levels, Q16 (0 = off) */
    int32_t act[4];              /* .. as sounding: a footage above Nyquist silent (Chrome's band-limited tables) */
    int32_t w, prev;             /* the staircase now; the sample held back for the BLEP */
    int32_t base12;              /* pitch of the 16' counter in twelfths of a cent above note 0, before modulation
                                  * (twelfths: DCO 2's detune steps are 20/3 cents, Age and the MG quarters) */
    uint32_t wph, winc;          /* Age: its wobble LFO */
} p800_osc_t;

static int32_t osc_wave(const int32_t *lev, uint32_t ph)      /* footage f is bit 31 - f: + in its first half */
{
    int32_t w = 0;
    uint32_t f;
    for (f = 0; f < 4u; f++)
        w += (ph >> (31u - f)) & 1u ? -lev[f] : lev[f];
    return w;
}

static uint32_t inc_of(int32_t p12)              /* twelfths of a cent above note 0 -> phase increment */
{
    int32_t oct, rem;
    uint32_t c, fr, a, b;
    p12 += 2 * 14400;                            /* two octaves under note 0 too (a 16' of the lowest keys) */
    if (p12 < 0) p12 = 0;
    oct = p12 / 14400;                           /* the table's octave is 12 here */
    rem = p12 - oct * 14400;
    c = (uint32_t)rem / 12u;
    fr = (uint32_t)rem % 12u;
    a = P800_INC_C[c];
    b = P800_INC_C[c + 1u];
    a += (b - a) * fr / 12u;
    if (oct > 12)                                /* 16 kHz and up: one octave more fits, past it all is muted */
        return oct == 13 && a < 0x80000000u ? a << 1 : 0xFFFFFFFFu;
    return a >> (12 - oct);
}

/* one block of one DCO, x env (Q14), added to out */
static void osc_block(p800_osc_t *o, const int16_t *env, int32_t *out)
{
    uint32_t i, f, ph = o->ph, inc = o->inc, step = inc >> 13, changed = 0;
    int32_t w, prev = o->prev;
    for (f = 0; f < 4u; f++) {
        int32_t a = inc >= (0x80000000u >> f) ? 0 : o->lev[f];
        changed |= a != o->act[f];
        o->act[f] = a;
    }
    w = changed ? osc_wave(o->act, ph) : o->w;
    if (!step) step = 1;
    for (i = 0; i < P800_BLK; i++) {
        uint32_t nph = ph + inc, cross = ((nph >> 28) - (ph >> 28)) & 15u;
        int32_t cp = 0, cc = 0;
        if (cross) {
            uint32_t k, bnd = (ph >> 28) << 28;
            for (k = 0; k < cross; k++) {        /* each multiple of 2^28 passed: a step of the staircase */
                int32_t h, x, x2, y2;
                bnd += 1u << 28;
                h = osc_wave(o->act, bnd) - w;
                if (!h) continue;
                x = (int32_t)((nph - bnd) / step);            /* time since the step, Q13 of a sample */
                if (x > 8191) x = 8191;
                x2 = (x * x) >> 13;                           /* x^2, Q13 */
                y2 = ((8192 - x) * (8192 - x)) >> 13;         /* (1 - x)^2 */
                cp += (h * x2) >> 14;                         /* + h x^2 / 2 on the sample before */
                cc -= (h * y2) >> 14;                         /* - h (1 - x)^2 / 2 on this one */
                w += h;
            }
        }
        out[i] += ((prev + cp) * env[i]) >> 14;
        prev = w + cc;
        ph = nph;
    }
    o->ph = ph, o->w = w, o->prev = prev;
}

/* ---------------------------------------------------------------- voices --- */
typedef struct {
    p800_osc_t osc[2];
    p800_env_t env[2];
    uint8_t ndco;
    uint32_t end;                /* sample time its oscillators stop (page: o.stop(..)) */
    uint8_t on;
} p800_snd_t;

typedef struct {                 /* one of the page's V[]: the voice it allocates, and a stolen one fading out */
    p800_snd_t cur, ghost;
    uint8_t note, gate;
    uint32_t until, at;          /* page v.until, v.at (sample times) */
} p800_voice_t;

/* ----------------------------------------------------------------- state --- */
typedef struct { int32_t b0, b1, b2, a1, a2; } p800_df1_t;
typedef struct { int32_t x1, x2, y1, y2; int64_t e; } p800_dfs_t;

#define FILT_SH 6                /* Q16 -> Q22 through the filters */
#define CH_LEN 1024u
#define FB_LEN 128u              /* Chrome's block: the phaser's feedback comes back this much later */

static struct {
    const p800_prog_t *pg;
    uint8_t ready;               /* p800_init has run (each entry below runs it first if not: a host that never called
                                  * it, a test, gets the page's defaults, not a silent volume 0) */
    int16_t set[P800_NSET];
    p800_voice_t v[8];
    uint8_t held[16], nheld, last;
    uint32_t now;                /* samples rendered */
    uint32_t rng;
    p800_env_t e3;
    uint32_t mg_ph;
    int32_t mg_gain;             /* Q15 */
    int32_t mg_wait, mg_fade;    /* blocks of its delay still to go; blocks into its .4 s ramp (-1: done) */
    int32_t base_cents;          /* the cutoff before DEG 3 and the MG (page apply()) */
    p800_dfs_t f1, f2;
    p800_dfs_t ls, hs, hp;
    int32_t hf1, hf2;            /* the voices' last two samples (the polyBLEP's high-end correction) */
    uint32_t ch_w, ch_ph[2];
    int32_t ch_d[2];             /* the delays at the end of the last block, Q10 samples */
    p800_dfs_t ap[2][6];
    uint32_t fb_i, ph_ph;
} p8;

static int32_t p800_ch_buf[CH_LEN] P800_POOL;    /* the chorus's delay line (mono, before it) */
static int32_t p800_fb[2][FB_LEN] P800_POOL;     /* the phaser's feedback, one Chrome block late */
static p800_df1_t LSC, HSC;                       /* the fixed shelves (p800_data.h, Chrome's RBJ shelves) */

static uint32_t p800_rand(void)
{
    uint32_t s = p8.rng;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return p8.rng = s;
}
static int32_t rand_bi(int32_t amt)             /* -amt .. +amt (Math.random()*2-1)*amt */
{
    return (int32_t)mulshr(p800_rand(), (uint32_t)(2 * amt + 1), 32) - amt;
}

/* Every filter is Chrome's BiquadFilterNode: a direct-form-I biquad (RBJ), its coefficients worked out again as the
 * cutoff moves. When a cutoff jumps (DEG 3 starting again on a key) the old outputs it holds ring through the new
 * coefficients: a burst the page has, which a state-variable filter leaves out. Coefficients Q29; the remainder of
 * each output is carried to the next sample (first-order error feedback), so nothing sticks a little off zero or
 * cycles on in silence (a state-variable filter with rounded products did both, at -58 to -75 dB). */

/* 1 / d, d P800_ONE in 1.0 .. 1.5: P800_ONE, to the last bit. A 16-bit division alone is not enough: at a 40 Hz cutoff the
 * direct form's 1 + a1 + a2 is 3e-5, so an error of that size in 1 / a0 moves the poles (a resonance that rings
 * where Chrome's does not); one Newton step, x (2 - d x), takes it from 15 bits to 30 */
static uint32_t inv_q30(uint32_t d)
{
    uint32_t s, q;
    int32_t t;
    for (s = 0; (d >> s) >= 65536u; s++) {}      /* d' = d >> s in [2^15, 2^16) */
    q = (0x80000000u / (d >> s)) << (29u - s);   /* 2^60 / d to 15 bits */
    t = (int32_t)(mul64((int32_t)d, (int32_t)q) >> 30);           /* d x, ~1.0 */
    return (uint32_t)(mul64((int32_t)q, (int32_t)(0x80000000u - (uint32_t)t)) >> 30);   /* x (2 - d x) */
}

enum { DF_LP, DF_HP, DF_AP };
/* RBJ low-pass, high-pass (Chrome: Q in dB) or allpass (a linear Q) at `cents` above note 0; k = 1 / Q, Q28 */
static void df1_set(p800_df1_t *c, uint32_t type, int32_t cents, int32_t k)
{
    uint32_t i, fr, omc, inv;
    int32_t sw, alpha;
    if (cents >= P800_NYQ_CENTS) {               /* Chrome: a cutoff at or past Nyquist passes all */
        c->b0 = type == DF_HP ? 0 : 1 << 29;
        c->b1 = c->b2 = c->a1 = c->a2 = 0;
        return;
    }
    if (cents < 0) cents = 0;
    i = (uint32_t)cents / P800_G_STEP;
    fr = (uint32_t)cents % P800_G_STEP;
    omc = P800_OMC[i] + (P800_OMC[i + 1] - P800_OMC[i]) * fr / P800_G_STEP;              /* 1 - cos w0, Q30 */
    sw = P800_SINW[i] + (P800_SINW[i + 1] - P800_SINW[i]) * (int32_t)fr / P800_G_STEP;   /* sin w0, Q30 */
    alpha = mulq28(sw, k) / 2;                   /* sin w0 / 2Q, Q30 */
    if (alpha > P800_ONE - 1) alpha = P800_ONE - 1;
    inv = inv_q30((uint32_t)(P800_ONE + alpha)); /* 1 / a0 */
    c->a1 = -(int32_t)(mul64(P800_ONE - (int32_t)omc, (int32_t)inv) >> 30);   /* -2 cos / a0, Q29 */
    c->a2 = (int32_t)mulshr((uint32_t)(P800_ONE - alpha), inv, 31);           /* (1 - alpha) / a0 */
    if (type == DF_AP) {                         /* (1 - alpha, -2 cos, 1 + alpha) / a0 */
        c->b0 = c->a2, c->b1 = c->a1, c->b2 = 1 << 29;
    } else if (type == DF_HP) {                  /* (1 + cos) / 2, -(1 + cos), (1 + cos) / 2, / a0 */
        c->b0 = (int32_t)mulshr(0x80000000u - omc, inv, 32);
        c->b1 = -2 * c->b0, c->b2 = c->b0;
    } else {                                     /* (1 - cos) / 2, 1 - cos, (1 - cos) / 2, / a0 */
        c->b0 = (int32_t)mulshr(omc, inv, 32);
        c->b1 = 2 * c->b0, c->b2 = c->b0;
    }
}
static inline int32_t df1_run(const p800_df1_t *c, p800_dfs_t *s, int32_t x)
{
    int64_t acc = s->e + mul64(c->b0, x) + mul64(c->b1, s->x1) + mul64(c->b2, s->x2)
                  - mul64(c->a1, s->y1) - mul64(c->a2, s->y2);
    int32_t y = (int32_t)(acc >> 29);
    s->e = acc - ((int64_t)y << 29);
    s->x2 = s->x1, s->x1 = x;
    s->y2 = s->y1, s->y1 = y;
    return y;
}

/* page apply(): the cutoff from CUTOFF and key tracking, set when a key goes down or up */
static void p800_apply(void)
{
    const p800_prog_t *g = p8.pg;
    int32_t hi = p8.last, i, c;                   /* page: the highest key held, else the last one */
    if (p8.nheld) {
        hi = p8.held[0];
        for (i = 1; i < p8.nheld; i++)
            if (p8.held[i] > hi) hi = p8.held[i];
    }
    c = P800_CO_CENTS[g->co];
    if (g->kt) {                 /* page: [0,.5,1][kt]*(hi-60)/12 octaves, + 2*max(0,(hi-60)/24)^2 */
        int32_t d = hi - 60;
        c += d * 50 * g->kt;
        if (d > 0) c += d * d * 25 / 6;
    }
    p8.base_cents = c < P800_CAP_CENTS ? c : P800_CAP_CENTS;
}

static void p800_init(void)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)&p8;
    for (i = 0; i < sizeof p8; i++) b[i] = 0;
    for (i = 0; i < CH_LEN; i++) p800_ch_buf[i] = 0;
    for (i = 0; i < 2u * FB_LEN; i++) p800_fb[i / FB_LEN][i % FB_LEN] = 0;
    for (i = 0; i < P800_NSET; i++) p8.set[i] = P800_SET_DEF[i];
    p8.ready = 1;
    p8.rng = 0x2545F491u;
    p8.last = 60;
    p8.pg = &P800_PROGS[0];
    p8.ch_d[0] = P800_CH_D_L, p8.ch_d[1] = P800_CH_D_R;
    LSC.b0 = P800_LS[0], LSC.b1 = P800_LS[1], LSC.b2 = P800_LS[2], LSC.a1 = P800_LS[3], LSC.a2 = P800_LS[4];
    HSC.b0 = P800_HS[0], HSC.b1 = P800_HS[1], HSC.b2 = P800_HS[2], HSC.a1 = P800_HS[3], HSC.a2 = P800_HS[4];
    p800_apply();
}

static void p800_ready(void)
{
    if (!p8.ready) p800_init();
}

/* it still sounds: a voice (held, releasing to the page's v.until, or a stolen one fading), or the noise, which
 * follows DEG 3's release, not the voices (page: ng.gain = e3 x level), down to -90 dB */
static int p800_busy(void)
{
    uint32_t k;
    for (k = 0; k < 8u; k++)
        if (p8.v[k].cur.on || p8.v[k].ghost.on) return 1;
    return p8.pg && p8.pg->nz && p8.e3.lvl > (P800_ONE >> 15);
}

static void p800_set(uint32_t id, int32_t v)
{
    p800_ready();
    if (id < P800_NSET) p8.set[id] = (int16_t)v;
}

static void p800_program(uint32_t idx)
{
    p800_ready();
    p8.pg = &P800_PROGS[idx < P800_NPROG ? idx : 0];
    p800_apply();
}

/* the voice's DCO d (0, 1) as the page's on() builds it */
static void osc_start(p800_osc_t *o, uint32_t d, uint32_t note)
{
    static const int32_t W[4] = {32768, 21856, 10912, 5472};      /* page Saw: [1,.667,.333,.167] */
    static const int32_t ISQ[5] = {32768, 32768, 23170, 18919, 16384};   /* 1 / sqrt(n) */
    const p800_prog_t *g = p8.pg;
    const p800_dco_t *c = &g->dco[d];
    uint32_t f, n = 0;
    int32_t lv, age = p8.set[P800_AGE];
    for (f = 0; f < 4u; f++) n += c->foot[f] != 0;
    /* page: lv=P['l'+d]/31*.5/Math.sqrt(n), n = footages on (1 if none); x Chrome's square plateau */
    lv = ((((int32_t)c->level * P800_SQ_AMP) / 31) * ISQ[n ? n : 1]) >> 16;
    for (f = 0; f < 4u; f++)
        o->lev[f] = c->foot[f] ? (lv * (c->saw ? W[f] : 32768)) >> 15 : 0;
    /* page: base=440*2^((m-69+(d==2?iv:0))/12)*[.5,1,2][o]; 16' = base / 2. det = tune + dt*20/3 + age random */
    o->base12 = 12 * (100 * (int32_t)note + (d ? 100 * g->iv : 0) + 1200 * ((int32_t)c->oct - 1) - 1200 + p8.set[P800_TUNE])
                + (d ? g->dt * 80 : 0) + 3 * rand_bi(4 * age);
    o->wph = 0;                  /* page wl.frequency .15+Math.random()*.5, wl.start(t): phase 0 */
    o->winc = P800_WOB_INC0 + mulshr(p800_rand(), P800_WOB_INCR, 32);
    o->ph = 0;
    o->inc = inc_of(o->base12);
    o->act[0] = o->act[1] = o->act[2] = o->act[3] = 0;
    o->w = 0;
    o->prev = 0;
}

static void snd_kill(p800_voice_t *v)            /* page kill(): what still sounds fades out over 5 ms */
{
    uint32_t d;
    if (v->cur.on && (int32_t)(v->cur.end - p8.now) > 0) {
        v->ghost = v->cur;
        for (d = 0; d < v->ghost.ndco; d++) {
            v->ghost.env[d].st = E_REL;
            v->ghost.env[d].mul = P800_KILL;
        }
        v->ghost.end = p8.now + P800_KILL_END;
    }
    v->cur.on = 0;
}

static void p800_note_on(uint32_t m)
{
    const p800_prog_t *g;
    uint32_t i, d, first, lim;
    p800_voice_t *v = 0;
    p800_ready();
    g = p8.pg;
    first = p8.nheld == 0;
    lim = g->mode ? 4u : 8u;
    for (i = 0; i < p8.nheld; i++)
        if (p8.held[i] == m) return;
    if (p8.nheld < sizeof p8.held) p8.held[p8.nheld++] = (uint8_t)m;
    p8.last = (uint8_t)m;
    p800_apply();
    for (i = 0; i < lim && !v; i++)              /* free */
        if (!p8.v[i].gate && (int32_t)(p8.v[i].until - p8.now) < 0) v = &p8.v[i];
    for (i = 0; i < lim && !v; i++)              /* releasing */
        if (!p8.v[i].gate) v = &p8.v[i];
    if (!v) {                                    /* the oldest */
        v = &p8.v[0];
        for (i = 1; i < lim; i++)
            if ((int32_t)(p8.v[i].at - v->at) < 0) v = &p8.v[i];
    }
    snd_kill(v);
    v->note = (uint8_t)m, v->gate = 1, v->at = p8.now, v->until = p8.now + 999u * P800_FS;
    v->cur.ndco = g->mode ? 2 : 1;
    for (d = 0; d < v->cur.ndco; d++) {
        osc_start(&v->cur.osc[d], d, m);
        env_trigger(&v->cur.env[d], g->deg[d]);
    }
    v->cur.end = v->until;
    v->cur.on = 1;
    if (g->tr || first)                          /* page: if(p8.tr===1||first)stage(N.e3.offset,t,3) */
        env_trigger(&p8.e3, g->deg[2]);
    if (first) {                                 /* the MG's delay, from the first key */
        p8.mg_wait = P800_MGDLY[g->mg[1]];
        p8.mg_fade = g->mg[1] ? 0 : -1;
        p8.mg_gain = g->mg[1] ? 0 : 32767;
    }
}

static void p800_note_off(uint32_t m)
{
    const p800_prog_t *g;
    uint32_t i, d, k;
    p800_ready();
    g = p8.pg;
    for (i = 0; i < p8.nheld && p8.held[i] != m; i++) {}
    if (i == p8.nheld) return;
    for (; i + 1 < p8.nheld; i++) p8.held[i] = p8.held[i + 1];
    p8.nheld--;
    p800_apply();
    for (k = 0; k < 8u; k++) {
        p800_voice_t *v = &p8.v[k];
        if (v->note == m && v->gate) {
            int32_t rt = 0;
            v->gate = 0, v->note = 0;
            for (d = 0; d < v->cur.ndco; d++) {
                uint8_t r = g->deg[d][5];
                env_release(&v->cur.env[d], P800_REL[r]);
                if (P800_REL_END[r] > rt) rt = P800_REL_END[r];
            }
            v->until = p8.now + (uint32_t)rt;     /* page: v.until=t+rt*1.5+.1; the oscillators stop there */
            v->cur.end = v->until;
        }
    }
    if (!p8.nheld)
        env_release(&p8.e3, P800_REL[g->deg[2][5]]);
}

static void snd_block(p800_snd_t *s, int32_t *vb, int32_t mod12, uint32_t age)
{
    int16_t env[P800_BLK];
    uint32_t d;
    if (!s->on) return;
    if ((int32_t)(s->end - p8.now) <= 0) {
        s->on = 0;
        return;
    }
    for (d = 0; d < s->ndco; d++) {
        p800_osc_t *o = &s->osc[d];
        int32_t wob = age ? (p800_sin(o->wph) * (int32_t)(age * 72 / 10)) >> 15 : 0;   /* page wg.gain=ag*.6 cents */
        o->wph += o->winc * P800_BLK;
        o->inc = inc_of(o->base12 + mod12 + wob);
        env_block(&s->env[d], env);
        osc_block(o, env, vb);
    }
}

/* one block: P800_BLK samples, Q16 stereo */
static void p800_render(int32_t *outl, int32_t *outr)
{
    const p800_prog_t *g;
    int32_t vb[P800_BLK];
    int16_t e3[P800_BLK];
    p800_df1_t c1, c2, chp, cap[6];
    uint32_t i, k, nap = (uint32_t)p8.set[P800_PH_STAGES] + 1u;
    int32_t mg, mod12, fc, fcmg, hc, ph, chd[2], chs[2], nzq, og, wet, pfb, pmix;
    p800_ready();
    g = p8.pg;
    if (nap > 6u) nap = 6u;
    for (i = 0; i < P800_BLK; i++) vb[i] = 0;

    /* the MG: page lfo -> mf (its delay gain) -> md (DCO, up to 100 cents), mv (VCF, up to 1200 cents) */
    mg = p800_sin(p8.mg_ph);
    p8.mg_ph += P800_MG_INC[g->mg[0]] * P800_BLK;
    if (p8.mg_fade >= 0) {
        if (p8.mg_wait > 0) p8.mg_wait--;
        else if (p8.mg_fade < P800_MGFADE_BLKS) p8.mg_gain = 32767 * ++p8.mg_fade / P800_MGFADE_BLKS;
        else p8.mg_fade = -1, p8.mg_gain = 32767;
    }
    mg = (mg * p8.mg_gain) >> 15;
    mod12 = (mg * (g->mg[2] * 1200 / 15)) >> 15;   /* page md.gain=mi/15*100 cents, in twelfths */

    for (k = 0; k < 8u; k++) {
        snd_block(&p8.v[k].cur, vb, mod12, (uint32_t)p8.set[P800_AGE]);
        snd_block(&p8.v[k].ghost, vb, mod12, (uint32_t)p8.set[P800_AGE]);
    }

    /* DEG 3 -> the cutoff (page eg gain ei/15*7200 cents, polarity) and the noise's level */
    env_block(&p8.e3, e3);
    fcmg = p8.base_cents + ((mg * (g->mg[3] * 1200 / 15)) >> 15);
    hc = (p8.set[P800_HP_ON] ? P800_HC_CENTS[p8.set[P800_HP_CUT]] : P800_HP_OFF_CENTS) + ((mg * (g->mg[3] * 1200 / 15)) >> 15);
    df1_set(&chp, DF_HP, hc, P800_K_HP);

    /* the phaser's allpasses: page 350*1.5^k Hz, detune its LFO x depth/10 x 2400 cents */
    ph = p800_sin(p8.ph_ph);
    p8.ph_ph += P800_PH_INC[p8.set[P800_PH_RATE]] * P800_BLK;
    for (k = 0; k < nap; k++)
        df1_set(&cap[k], DF_AP, P800_AP_CENTS[k] + ((ph * (p8.set[P800_PH_DEPTH] * 240)) >> 15), P800_K_AP);

    /* the chorus delays at this block's end: (.013 | .017) + .004 sin, in Q10 samples */
    for (k = 0; k < 2u; k++) {
        int32_t s = p800_sin(p8.ch_ph[k] += k ? P800_CH_INC_R * P800_BLK : P800_CH_INC_L * P800_BLK);
        int32_t d = (k ? P800_CH_D_R : P800_CH_D_L) + ((s * P800_CH_DEPTH8) >> 12);
        chs[k] = (d - p8.ch_d[k]) >> 5;
        chd[k] = p8.ch_d[k];
        p8.ch_d[k] = d;
    }
    nzq = g->nz * 32768 / 30;                    /* page nl.gain=nz/15*.5 */
    og = p8.set[P800_VOL] * 34636833 / 10 * 9;    /* page out.gain=vo/31*.9 (P800_ONE) */
    wet = g->ch ? P800_ONE / 2 : 0;                   /* page wet.gain=ch?.5:0 */
    pfb = p8.set[P800_PH_FB] * (P800_ONE / 10) / 10 * 8;    /* page pf.gain=pf/10*.8 */
    pmix = p8.set[P800_PH_MIX] * (P800_ONE / 10);           /* page pw.gain=pm/10 */

    for (i = 0; i < P800_BLK; i++) {
        int32_t x, y[2], tap[2];
        /* the 2-point polyBLEP smooths each step over two samples: harmonics fall as sinc^2(f / fs) (-2.4 dB at
         * 12.7 kHz, -3.8 dB at 16 kHz) where Chrome's tables are flat. [-a, 1 + 2a, -a], a = 0.14, puts back most of it
         * (+2.5 dB at 12.7 kHz, +3.2 dB at 16 kHz, +0.7 dB at 6 kHz), one sample late; the noise is not touched */
        x = ((vb[i] + p8.hf2) * -9 + p8.hf1 * 82) >> 6;   /* a = 9/64 */
        p8.hf2 = p8.hf1, p8.hf1 = vb[i];
        if (!(i & 7u)) {                         /* the low-passes' cutoff every 8 samples (Chrome: every sample) */
            fc = fcmg + ((e3[i + 4u] * (g->ei * 7200 / 15)) >> 14) * (g->ep ? -1 : 1);
            df1_set(&c1, DF_LP, fc, P800_K_LP1[g->rs]);
            df1_set(&c2, DF_LP, fc, P800_K_LP2);
        }
        if (g->nz) {                             /* white noise, 1.0 peak, x DEG 3 x level */
            int32_t n = (int32_t)p800_rand() >> 15;
            x += (((n * e3[i]) >> 14) * nzq) >> 15;
        }
        x *= 1 << FILT_SH;
        x = df1_run(&c2, &p8.f2, df1_run(&c1, &p8.f1, x));   /* two low-passes */
        x = df1_run(&HSC, &p8.hs, df1_run(&LSC, &p8.ls, x));   /* the low shelf, then the high */
        x = df1_run(&chp, &p8.hp, x);            /* the high-pass (10 Hz when off) */
        x = (x + (1 << (FILT_SH - 1))) >> FILT_SH;
        /* chorus: the delay lines read between samples (linear), hard left and right */
        p800_ch_buf[p8.ch_w & (CH_LEN - 1u)] = x;
        for (k = 0; k < 2u; k++) {
            int32_t d = chd[k] + chs[k] * (int32_t)(i + 1), di = d >> 10, fr = d & 1023;
            int32_t a = p800_ch_buf[(p8.ch_w - (uint32_t)di) & (CH_LEN - 1u)];
            int32_t b = p800_ch_buf[(p8.ch_w - (uint32_t)di - 1u) & (CH_LEN - 1u)];
            tap[k] = a + (((b - a) * fr) >> 10);
        }
        p8.ch_w++;
        for (k = 0; k < 2u; k++) {
            int32_t o = mulq30(og, x + mulq30(wet, tap[k]));
            int32_t a = o + mulq30(pfb, p800_fb[k][p8.fb_i]);   /* the feedback, one Chrome block late */
            uint32_t s;
            for (s = 0; s < nap; s++)            /* the allpasses */
                a = df1_run(&cap[s], &p8.ap[k][s], a);
            p800_fb[k][p8.fb_i] = a;
            y[k] = o + mulq30(pmix, a);
        }
        p8.fb_i = (p8.fb_i + 1u) & (FB_LEN - 1u);
        outl[i] = y[0];
        outr[i] = y[1];
    }
    p8.now += P800_BLK;
}
