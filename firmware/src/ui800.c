/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 7a: the Poly-800's own home screen and controls (FM1_800 builds; main.c calls u8_input and u8_draw in
 * place of Felucca's ui_input, ui_leds and ui_draw, whose screens it no longer shows).
 *
 * The screen: a sunset-city scene (a purple sky with stars, a striped sun going yellow to pink, a skyline, a glowing
 * grid to the horizon), drawn from rows, rectangles and points into the canvas (gfx.c) in two strips of 120 rows,
 * nothing stored; over it the program's number and name, WHOLE or DOUBLE, the octave and the phaser. It is drawn when
 * one of those changes, not every frame.
 * The controls: PRESETS turns through the 64 programs (part 1's preset: eng_p800.c p8e_mix loads it), OCT- / OCT+
 * move the keys an octave (-3 .. +3, both together: back to 0). The keys and MIDI play as before (seq.c, the MIDI
 * ISR); every other control does nothing yet (step 7b: SELECT and KNOB 1, GLO; 7c: the FX button). */

#define U8_HZ 150                                /* the horizon */
#define U8_CX 120                                /* the sun's centre and the grid's vanishing point */
#define U8_CY 118
#define U8_R 54

static struct {
    uint8_t ok;                                  /* drawn at least once */
    uint8_t prog, mode, ph, boot;                /* what is on the screen */
    int8_t oct;
    uint32_t boot_ms;                            /* the version shows for 4 s after power-on */
} u8;

static uint16_t u8_rgb(uint32_t r, uint32_t g, uint32_t b) { return RGB(r, g, b); }

/* a colour between stops (t 0..1000): stops are {t, r, g, b} */
static uint16_t u8_grad(const uint16_t (*st)[4], uint32_t n, uint32_t t)
{
    uint32_t i;
    for (i = 0; i + 2u < n && t > st[i + 1u][0]; i++)
        ;
    {
        uint32_t t0 = st[i][0], t1 = st[i + 1u][0], f = t1 > t0 ? (t - t0) * 256u / (t1 - t0) : 0u;
        if (f > 256u) f = 256u;
        return u8_rgb((st[i][1] * (256u - f) + st[i + 1u][1] * f) >> 8, (st[i][2] * (256u - f) + st[i + 1u][2] * f) >> 8,
                      (st[i][3] * (256u - f) + st[i + 1u][3] * f) >> 8);
    }
}

static uint32_t u8_rng;
static uint32_t u8_rand(uint32_t n)
{
    u8_rng = u8_rng * 1103515245u + 12345u;
    return (u8_rng >> 16) % n;
}

static uint32_t u8_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 14;
    while (b) {
        if ((r + b) * (r + b) <= v) r += b;
        b >>= 1;
    }
    return r;
}

/* is row y of the sun one of its gaps: they start above the middle, each wider than the one before */
static int u8_band(int32_t y)
{
    int32_t y0 = U8_CY - 30, gap = 2, step = 10;
    while (y0 < U8_HZ) {
        if (y >= y0 && y < y0 + gap) return 1;
        y0 += step;
        gap++;
        step = step - 1 > gap + 3 ? step - 1 : gap + 3;
    }
    return 0;
}

/* a row of buildings along the horizon */
static void u8_city(uint32_t seed, int32_t hmin, int32_t hmax, int32_t wmin, int32_t wmax, uint16_t col, uint16_t win)
{
    int32_t x, base = U8_HZ - 1;
    u8_rng = seed;
    x = -(int32_t)u8_rand(8);
    while (x < 240) {
        int32_t w = wmin + (int32_t)u8_rand((uint32_t)(wmax - wmin)), h = hmin + (int32_t)u8_rand((uint32_t)(hmax - hmin)), wx, wy;
        cv_rect(x, base - h, w, h + 1, col);
        if (u8_rand(10) < 3u)                    /* an antenna */
            cv_rect(x + w / 2, base - h - 3 - (int32_t)u8_rand(6), 1, 3 + 6, col);
        for (wy = base - h + 3; wy < base - 2; wy += 4)
            for (wx = x + 2; wx < x + w - 2; wx += 3)
                if (u8_rand(100) < 12u)
                    cv_pset(wx, wy, win);
        x += w + (int32_t)u8_rand(3);
    }
}

/* the scene, into the canvas (screen coordinates: cv_oy puts the strip's rows in place; what lies outside is cut) */
static void u8_scene(void)
{
    static const uint16_t SKY[4][4] = {{0, 12, 4, 38}, {550, 58, 14, 92}, {850, 150, 30, 120}, {1000, 235, 70, 120}};
    static const uint16_t SUN[3][4] = {{0, 255, 236, 90}, {500, 255, 150, 40}, {1000, 250, 40, 120}};
    static const uint16_t GND[2][4] = {{0, 30, 6, 46}, {1000, 10, 2, 22}};
    int32_t y, i, n;
    for (y = 0; y < U8_HZ; y++)                  /* the sky */
        cv_rect(0, y, 240, 1, u8_grad(SKY, 4, (uint32_t)y * 1000u / (U8_HZ - 1)));
    u8_rng = 7;                                  /* the stars */
    for (i = 0; i < 40; i++) {
        int32_t x = (int32_t)u8_rand(240), sy = (int32_t)u8_rand(70);
        cv_pset(x, sy, u8_rand(10) < 7u ? u8_rgb(200, 180, 255) : u8_rgb(255, 255, 255));
    }
    for (y = U8_CY - U8_R; y < U8_HZ; y++) {     /* the sun */
        int32_t dy = y - U8_CY, hw;
        if (u8_band(y) || dy * dy > U8_R * U8_R)
            continue;
        hw = (int32_t)u8_isqrt((uint32_t)(U8_R * U8_R - dy * dy));
        cv_rect(U8_CX - hw, y, 2 * hw + 1, 1, u8_grad(SUN, 3, (uint32_t)(y - (U8_CY - U8_R)) * 1000u / (2u * U8_R)));
    }
    u8_city(3, 8, 26, 6, 16, u8_rgb(70, 20, 100), u8_rgb(255, 120, 200));     /* the far blocks, then the near ones */
    u8_city(11, 3, 16, 8, 20, u8_rgb(34, 8, 58), u8_rgb(255, 210, 120));
    for (y = U8_HZ; y < 240; y++)                /* the ground */
        cv_rect(0, y, 240, 1, u8_grad(GND, 2, (uint32_t)(y - U8_HZ) * 1000u / (240 - U8_HZ)));
    for (n = 1;; n++) {                          /* the grid's cross lines, wider apart towards the front */
        int32_t gy = U8_HZ + 2 + n * (n + 3) / 2, f = (gy - U8_HZ) * 1000 / 60 + 300;
        if (gy >= 240) break;
        if (f > 1000) f = 1000;
        cv_rect(0, gy, 240, 1, u8_rgb((90 * (1000 - (uint32_t)f) + 230u * (uint32_t)f) / 1000u,
                                      (20 * (1000 - (uint32_t)f) + 50u * (uint32_t)f) / 1000u,
                                      (110 * (1000 - (uint32_t)f) + 200u * (uint32_t)f) / 1000u));
    }
    for (i = -16; i <= 16; i++) {                /* .. and its lines to the vanishing point, a span a row (no gaps) */
        int32_t xa = U8_CX;
        for (y = U8_HZ; y < 240; y++) {
            int32_t xb = U8_CX + i * 34 * (y + 1 - U8_HZ) / (240 + 40 - U8_HZ), x0 = xa < xb ? xa : xb, x1 = xa < xb ? xb : xa;
            cv_rect(x0, y, x1 - x0 + 1, 1, u8_rgb(202, 44, 182));
            xa = xb;
        }
    }
    cv_rect(0, U8_HZ, 240, 1, u8_rgb(255, 110, 210));   /* the horizon's glow */
}

/* text over the scene: a dark edge, then the text, each blended onto what is there (gfx.c cv_over) */
static void u8_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg)
{
    static const int8_t D[6][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}, {2, 2}};
    uint32_t k;
    cv_over = 1;
    cv_over_fg = u8_rgb(20, 0, 30);
    for (k = 0; k < 6u; k++)
        cv_text_on(x + D[k][0], y + D[k][1], f, s, cv_over_fg, cv_over_fg);
    cv_over_fg = fg;
    cv_text_on(x, y, f, s, fg, fg);
    cv_over = 0;
}

static void u8_draw(void)
{
    const p800_prog_t *g = &P800_PROGS[trk[0].preset % P800_NPROG];
    uint32_t ph = p8.set[P800_PH_MIX] > 0, boot = fm1_ms - u8.boot_ms < 4000u, y0;
    char num[4], o[8];
    const aafont_t *nf;
    int32_t nx;
    if (u8.ok && u8.prog == trk[0].preset && u8.mode == g->mode && u8.oct == song.octave && u8.ph == ph && u8.boot == boot)
        return;
    u8.ok = 1, u8.prog = trk[0].preset, u8.mode = g->mode, u8.oct = (int8_t)song.octave, u8.ph = (uint8_t)ph;
    u8.boot = (uint8_t)boot;
    num[0] = (char)('0' + g->num / 10u), num[1] = (char)('0' + g->num % 10u), num[2] = 0;
    str_cpy(o, "OCT ", sizeof o);
    if (song.octave > 0) str_cpy(o + 4, "+", sizeof o - 4);
    fmt_int(o + str_len(o), song.octave);
    nx = 10 + text_w(&AF_L, num) + 12;
    nf = text_w(&AF_M, g->name) <= 232 - nx ? &AF_M : &AF_S;   /* (a long name in the small face) */
    for (y0 = 0; y0 < 240u; y0 += 120u) {
        cv_begin(240, 120, 0);
        cv_oy = -(int32_t)y0;
        cv_cy0 = 0, cv_cy1 = 120;
        u8_scene();
        u8_text(10, 6, &AF_L, num, u8_rgb(120, 240, 255));
        u8_text(nx, 9, nf, g->name, u8_rgb(255, 255, 255));
        u8_text(nx, 29, &AF_S, g->mode ? "DOUBLE" : "WHOLE", u8_rgb(255, 170, 230));
        u8_text(10, 220, &AF_S, o, u8_rgb(120, 240, 255));
        if (ph)
            u8_text(230 - text_w(&AF_S, "PHASER"), 220, &AF_S, "PHASER", u8_rgb(255, 170, 230));
        if (boot)
            u8_text(120 - text_w(&AF_S, "FM1-800 " FELUCCA_VERSION) / 2, 200, &AF_S, "FM1-800 " FELUCCA_VERSION,
                    u8_rgb(255, 255, 255));
        cv_oy = 0;
        cv_blit(0, y0);
    }
}

static void u8_input(void)
{
    uint32_t pressed = fm1_input_edges(0), both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]), tap, k;
    int32_t s;
    (void)fm1_input_note_edges();                /* (the keys play from seq.c: their edges are nobody's here) */
    if (!u8.boot_ms)
        u8.boot_ms = fm1_ms | 1u;
    song.grid = 0;                               /* the keys play */
    if ((s = panel_enc(EN_PRESET)) != 0) {       /* PRESETS: the program, round the 64 */
        int32_t p = ((int32_t)trk[0].preset + s) % (int32_t)P800_NPROG;
        trk[0].preset = (uint8_t)(p < 0 ? p + (int32_t)P800_NPROG : p);
    }
    for (k = EN_SELECT; k < NE; k++)             /* the other knobs: not yet (their turns dropped) */
        if (k != EN_PRESET)
            (void)panel_enc(k);
    tap = oct_taps(pressed, 1);                  /* OCT- / OCT+ (on release; both together: 0) */
    if ((fm1_in.buttons & both) == both)
        song.octave = 0;
    else if (tap & 1u)
        song.octave -= song.octave > -3 ? 1 : 0;
    else if (tap & 2u)
        song.octave += song.octave < 3 ? 1 : 0;
}
