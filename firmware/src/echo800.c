/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800: the tape echo, after the Poly-800 (fx.c mix_block, the FM-1's build). Its controls are those of a small
 * 1970s tape echo: DELAY, REPEAT, BALANCE and an input switch, 0 / 20 / 40 dB, read here as how hard the tape is
 * driven; its VOLUME is MASTER. Tuned by ear (it is not on the page). Mono, as such a unit, into both sides.
 *
 *   the input (both sides' mean), raised by the switch (x1, x3, x9: as a real unit's input stage, before the tape),
 *   and the repeats go onto the tape through its saturation (the soft clip's tanh, at most the tape's full level): at
 *   0 dB a loud chord just warms it, at 20 dB it squashes, at 40 dB it is gritty and the echo louder; the repeats go
 *   round without the switch's gain, so it does not make the loop run away sooner,
 *   the tape is read DELAY later (40 ms .. 1 s, round the curve of the knob), wavering a little (wow ~0.6 Hz,
 *   flutter ~7 Hz) and read between samples; each repeat goes back through a low-pass (~2 kHz) and a high-pass
 *   (~120 Hz), so they darken and thin as they go round. REPEAT sets how much comes back (0 .. 1.08: at the top it
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
    uint8_t on, delay, repeat, balance, drive;   /* the controls: DELAY 0..99, REPEAT 0..10, BALANCE 0..10, INPUT 0..2 */
    uint32_t w, wow, flt, quiet;                 /* the tape's write place; wow and flutter phases; quiet samples */
    int32_t d;                                   /* the tape's delay now, Q8 samples (it slides to DELAY's) */
    int32_t lp, hpx, hpy;                        /* the repeats' low-pass and high-pass, 8 bits finer than the tape
                                                  * (so what is left of a repeat dies to exactly 0: no hum) */
} ec = {0, 50, 4, 4, 0, 0, 0, 0, EC_LEN, 0, 0, 0, 0};

/* onto the tape: x (Q15, 32768 = the tape's full level) through its saturation, small-signal gain 1, never past */
static int32_t ec_tape(int32_t x)
{
    return softclip(x > 2000000 ? 2000000 : x < -2000000 ? -2000000 : x);
}

/* a block: l, r are the mix (the page's full scale 16384); the echo added to both */
static void echo_block(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    int32_t fb = ec.repeat * 3539,               /* Q15: REPEAT 10 = 1.08 */
        dry = ec.balance >= 5u ? (10 - (int32_t)ec.balance) * 205 : 1024,     /* Q10: BALANCE 5 both full, */
        wet = ec.balance <= 5u ? (int32_t)ec.balance * 6554 : 32768,          /* (Q15) 0 dry alone, 10 echo alone */
        target = (int32_t)EC_DELAY[ec.delay < 100u ? ec.delay : 99u] << 8;
    if (!ec.on && ec.quiet > EC_LEN)             /* off and the tape silent: nothing to do */
        return;
    for (i = 0; i < n; i++) {
        int32_t in = ec.on ? (l[i] + r[i]) * (ec.drive == 2u ? 9 : ec.drive == 1u ? 3 : 1) : 0;   /* (mean x 2: the
                                                  * page's full scale to Q15's; the switch x1 / x3 / x9) */
        int32_t wv = (p800_sin(ec.wow) * 3 + p800_sin(ec.flt)) >> 2;          /* the waver, Q15 */
        int32_t d = ec.d + ((((ec.d >> 8) * wv) >> 15) * 256 / 500);   /* +-0.2 % (32-bit only: CPU.md) */
        int32_t di = d >> 8, fr = d & 255, a, b, rd, x, t;
        ec.d += (target - ec.d) >> 12;           /* the tape's speed slides to DELAY's (~90 ms) */
        ec.wow += 58000u, ec.flt += 680000u;     /* ~0.6 Hz, ~7 Hz */
        a = dly_buf[(ec.w - (uint32_t)di) & (EC_LEN - 1u)];
        b = dly_buf[(ec.w - (uint32_t)di - 1u) & (EC_LEN - 1u)];
        rd = a + (((b - a) * fr) >> 8);          /* the tape, between samples */
        x = (rd * fb) >> 15;                     /* the repeat, darker and thinner each time round */
        ec.lp += ((x << 8) - ec.lp) >> 2;        /* (~2 kHz one-pole) */
        ec.hpy = ec.hpy - (ec.hpy >> 6) + ec.lp - ec.hpx;   /* (~110 Hz one-pole high-pass) */
        ec.hpx = ec.lp;
        t = ec_tape(in + ((ec.hpy + 128) >> 8));
        dly_buf[ec.w & (EC_LEN - 1u)] = (int16_t)(t > 32767 ? 32767 : t < -32768 ? -32768 : t);
        ec.w++;
        ec.quiet = (t || in) ? 0u : ec.quiet + 1u;
        x = (rd * wet) >> 16;                    /* (Q15 back to the mix's scale: / 2) */
        l[i] = ((l[i] * dry) >> 10) + x;         /* (the mix under 2^20: no 64-bit product) */
        r[i] = ((r[i] * dry) >> 10) + x;
    }
}
