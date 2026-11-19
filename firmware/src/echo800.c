/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800: the tape echo, after the Poly-800 (fx.c mix_block, the FM-1's build). Its controls are those of a small
 * 1970s tape echo, DELAY, REPEAT, BALANCE (its VOLUME is MASTER), and in place of its input switch (20 and 40 dB were
 * noisy: v0.9.2) AGE, the tape from new to worn. Tuned by ear (it is not on the page). Mono, as such a unit, into
 * both sides.
 *
 *   the input (both sides' mean) and the repeats go onto the tape through its saturation (the soft clip's tanh, at most
 *   the tape's full level: a loud chord just warms it),
 *   the tape is read DELAY later (40 ms .. 1 s, round the curve of the knob), wavering (wow ~0.6 Hz, flutter ~7 Hz)
 *   and read between samples; played back through a low-pass (the first echo a little duller), each repeat goes
 *   round again through it and a ~110 Hz high-pass, so they darken and thin as they go. AGE (0..10) wears the tape: the waver from +-0.05 % to +-1.2 %, the low-pass from ~2.6 kHz
 *   to ~0.9 kHz, and from AGE 4 dropouts, the level dipping now and then (to -3 dB at 10, ~20 a second). No hiss. REPEAT sets how much comes back (0 .. 1.15: at the top it
 *   runs away slowly, held by the saturation, as a tape echo does). A turn of DELAY slides the tape's speed, so the
 *   repeats bend in pitch. Off: no more input, the repeats die away; once the tape is quiet it costs nothing.
 *
 * The tape is Felucca's delay line (fx.c dly_buf, 1.49 s), which the FM-1's build never runs. */

#define EC_LEN DLY_LEN
static const uint16_t EC_DELAY[100] = {   /* DELAY 0..99: samples, 40 ms x 25^(v / 99) */
    1764, 1822, 1883, 1945, 2009, 2075, 2144, 2215, 2288, 2364,
    2442, 2522, 2606, 2692, 2781, 2873, 2968, 3066, 3167, 3272,
    3380, 3492, 3607, 3726, 3849, 3977, 4108, 4244, 4384, 4529,
    4679, 4833, 4993, 5158, 5328, 5505, 5686, 5874, 6069, 6269,
    6476, 6690, 6911, 7140, 7376, 7619, 7871, 8131, 8400, 8678,
    8965, 9261, 9567, 9883, 10210, 10547, 10896, 11256, 11628, 12012,
    12409, 12819, 13243, 13680, 14132, 14599, 15082, 15580, 16095, 16627,
    17177, 17744, 18331, 18937, 19562, 20209, 20877, 21567, 22280, 23016,
    23776, 24562, 25374, 26213, 27079, 27974, 28898, 29853, 30840, 31859,
    32912, 34000, 35123, 36284, 37483, 38722, 40002, 41324, 42689, 44100
};

static struct {
    uint8_t on, delay, repeat, balance, age;     /* the controls: DELAY 0..99, REPEAT 0..10, BALANCE 0..10, AGE 0..10 */
    uint32_t w, wow, flt, quiet;                 /* the tape's write place; wow and flutter phases; quiet samples */
    int32_t d;                                   /* the tape's delay now, Q8 samples (it slides to DELAY's) */
    uint32_t rng;                                /* the dropouts' chance */
    int32_t dg, dt;                              /* .. their gain now and where it heads (Q15) */
    int32_t lp, hpx, hpy;                        /* the repeats' low-pass and high-pass, 8 bits finer than the tape
                                                  * (so what is left of a repeat dies to exactly 0: no hum) */
} ec = {0, 50, 4, 4, 3, 0, 0, 0, EC_LEN, 0, 1, 32768, 32768, 0, 0, 0};

/* onto the tape: x (Q15, 32768 = the tape's full level) through its saturation, small-signal gain 1, never past */
static int32_t ec_tape(int32_t x)
{
    return softclip(x > 2000000 ? 2000000 : x < -2000000 ? -2000000 : x);
}

/* a block: l, r are the mix (the page's full scale 16384); the echo added to both */
static void echo_block(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    int32_t fb = ec.repeat * 3768,               /* Q15: REPEAT 10 = 1.15 (the playback's filter takes some back) */
        dry = ec.balance >= 5u ? (10 - (int32_t)ec.balance) * 205 : 1024,     /* Q10: BALANCE 5 both full, */
        wet = ec.balance <= 5u ? (int32_t)ec.balance * 6554 : 32768,          /* (Q15) 0 dry alone, 10 echo alone */
        target = (int32_t)EC_DELAY[ec.delay < 100u ? ec.delay : 99u] << 8,
        age = ec.age > 10u ? 10 : (int32_t)ec.age,
        wd = 10 + 23 * age,                      /* the waver: +-(0.05 + 0.115 x AGE) % */
        lpa = 80 - 5 * age,                      /* the low-pass, Q8: ~2.6 kHz .. ~0.9 kHz */
        dd = age > 3 ? (age - 3) * 1400 : 0;     /* the dropouts' depth, Q15: to -3 dB */
    if (!ec.on && ec.quiet > EC_LEN)             /* off and the tape silent: nothing to do */
        return;
    for (i = 0; i < n; i++) {
        int32_t in = ec.on ? (l[i] + r[i]) : 0;  /* (mean x 2: the page's full scale to Q15's) */
        int32_t wv = (p800_sin(ec.wow) * 3 + p800_sin(ec.flt)) >> 2;          /* the waver, Q15 */
        int32_t d = ec.d + (((((ec.d >> 8) * wv) >> 15) * wd) * 32 / 2500);   /* (x wd / 20000 x 256; 32-bit only) */
        int32_t di = d >> 8, fr = d & 255, a, b, rd, x, t;
        ec.d += (target - ec.d) >> 12;           /* the tape's speed slides to DELAY's (~90 ms) */
        ec.wow += 58000u, ec.flt += 680000u;     /* ~0.6 Hz, ~7 Hz */
        a = dly_buf[(ec.w - (uint32_t)di) & (EC_LEN - 1u)];
        b = dly_buf[(ec.w - (uint32_t)di - 1u) & (EC_LEN - 1u)];
        rd = a + (((b - a) * fr) >> 8);          /* the tape, between samples */
        if (!(ec.w & 2047u)) {                   /* a worn tape: now and then the level dips (~20 a second) */
            ec.rng = ec.rng * 1664525u + 1013904223u;
            ec.dt = 32768 - (int32_t)(((ec.rng >> 17) * (uint32_t)dd) >> 15);
        }
        ec.dg += (ec.dt - ec.dg) >> 8;
        rd = (rd * (ec.dg >> 1)) >> 14;          /* (Q15 gain halved: the product under 2^31) */
        ec.lp += (((rd << 8) - ec.lp) * lpa) >> 8;   /* the playback, duller with AGE (one-pole; under 2^31: 8.4e6 x 80):
                                                  * the first echo a little, each repeat more */
        rd = (ec.lp + 128) >> 8;
        x = mulq30(ec.lp, fb << 15);             /* the repeat (8 bits finer), thinner each time round: */
        ec.hpy = ec.hpy - (ec.hpy >> 6) + x - ec.hpx;   /* (~110 Hz one-pole high-pass) */
        ec.hpx = x;
        t = ec_tape(in + ((ec.hpy + 128) >> 8));
        dly_buf[ec.w & (EC_LEN - 1u)] = (int16_t)(t > 32767 ? 32767 : t < -32768 ? -32768 : t);
        ec.w++;
        ec.quiet = (t || in) ? 0u : ec.quiet + 1u;
        x = (rd * wet) >> 16;                    /* (Q15 back to the mix's scale: / 2) */
        l[i] = ((l[i] * dry) >> 10) + x;         /* (the mix under 2^20: no 64-bit product) */
        r[i] = ((r[i] * dry) >> 10) + x;
    }
}
