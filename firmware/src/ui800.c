/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 7a: the Poly-800's own home screen and controls (FM1_800 builds; main.c calls u8_input and u8_draw in
 * place of Felucca's ui_input, ui_leds and ui_draw, whose screens it no longer shows).
 *
 * The screen: a sunset-city scene (a purple sky with stars, a striped sun going yellow to pink, a skyline, a glowing
 * grid to the horizon), drawn from rows, rectangles and points into the canvas (gfx.c) in two strips of 120 rows,
 * nothing stored; over it the program's number and name, WHOLE or DOUBLE, the octave and the phaser. It is drawn when
 * one of those changes, not every frame. Between, the stars twinkle: each one's brightness rises and falls on a
 * slow cycle of its own (1.5 .. 4 s), and only a star whose step of brightness changed is redrawn, one pixel; a star
 * the text covers stays as it is (so the text never flickers).
 * The controls: PRESETS turns through the 64 programs (part 1's preset: eng_p800.c p8e_mix loads it), OCT- / OCT+
 * move the keys an octave (-3 .. +3, both together: back to 0). The keys and MIDI play as before (seq.c, the MIDI
 * ISR).
 * Step 7b, editing as on the Poly-800: SELECT steps through the program's parameters (the page's order: DCO 1, DCO 2,
 * noise, VCF, DEG 1..3, MG, chorus, voices; its first turn shows the one it is on), KNOB 1 sets its value and it sounds
 * at once; the lower half shows the section, the parameter and its value, the top "EDITED". The edits go into a copy
 * of the program (u8_ed), which the Poly-800 plays; turning PRESETS leaves them (saving them: step 8). GLO held +
 * KNOB 1: the tuning, -50 .. +50 cents. Each half of the screen is drawn again only when what it shows changes.
 * Every other control does nothing yet (7c: the FX button). */

#define U8_HZ 150                                /* the horizon */
#define U8_CX 120                                /* the sun's centre and the grid's vanishing point */
#define U8_CY 118
#define U8_R 54
#define U8_NSTAR 40

static struct {
    uint8_t ok;                                  /* drawn at least once */
    uint32_t boot_ms;                            /* the version shows for 4 s after power-on */
    struct { uint8_t x, y, lvl, still; uint16_t per, ph; } st[U8_NSTAR];   /* the stars: place, brightness step drawn
                                                  * (0..3), under the text, cycle and phase (ms) */
} u8;
static const uint16_t U8_SKY[4][4] = {{0, 12, 4, 38}, {550, 58, 14, 92}, {850, 150, 30, 120}, {1000, 235, 70, 120}};

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

/* the stars' places and cycles, once (the same every time) */
static void u8_stars(void)
{
    uint32_t i;
    if (u8.st[0].per)
        return;
    u8_rng = 7;
    for (i = 0; i < U8_NSTAR; i++) {
        u8.st[i].x = (uint8_t)u8_rand(240);
        u8.st[i].y = (uint8_t)u8_rand(70);
        u8.st[i].still = (uint8_t)(u8_rand(10) < 7u ? 0u : 2u);   /* (bit 1: a white one; the rest lilac) */
        u8.st[i].per = (uint16_t)(1500u + u8_rand(2500));
        u8.st[i].ph = (uint16_t)u8_rand(u8.st[i].per);
    }
}

/* star i at brightness step l (0 faint .. 3 full): its colour, blended onto the sky behind it */
static uint16_t u8_star(uint32_t i, uint32_t l)
{
    static const uint8_t PCT[4] = {12, 40, 70, 100};
    uint16_t sky = u8_grad(U8_SKY, 4, (uint32_t)u8.st[i].y * 1000u / (U8_HZ - 1)), c;
    uint32_t r, g, b, p = PCT[l & 3u];
    c = (u8.st[i].still & 2u) ? u8_rgb(255, 255, 255) : u8_rgb(200, 180, 255);
    r = ((sky >> 11) * (100u - p) + (c >> 11) * p + 50u) / 100u;
    g = (((sky >> 5) & 63u) * (100u - p) + ((c >> 5) & 63u) * p + 50u) / 100u;
    b = ((sky & 31u) * (100u - p) + (c & 31u) * p + 50u) / 100u;
    return (uint16_t)(r << 11 | g << 5 | b);
}

/* star i's brightness step now: a triangle over its cycle, mostly bright, dipping to faint */
static uint32_t u8_star_lvl(uint32_t i, uint32_t ms)
{
    uint32_t per = u8.st[i].per, t = (ms + u8.st[i].ph) % per, tri = t * 2000u / per;   /* 0..2000 */
    tri = tri > 1000u ? 2000u - tri : tri;                                             /* 0..1000..0 */
    return tri < 150u ? 0u : tri < 350u ? 1u : tri < 600u ? 2u : 3u;
}

/* between redraws: the stars whose step changed, a pixel each (not those under the text) */
static void u8_twinkle(void)
{
    uint32_t i, l;
    for (i = 0; i < U8_NSTAR; i++) {
        if (u8.st[i].still & 1u)
            continue;
        if ((l = u8_star_lvl(i, fm1_ms)) != u8.st[i].lvl) {
            u8.st[i].lvl = (uint8_t)l;
            lcd_fill(u8.st[i].x, u8.st[i].y, 1, 1, u8_star(i, l));
        }
    }
}

/* the scene, into the canvas (screen coordinates: cv_oy puts the strip's rows in place; what lies outside is cut) */
static void u8_scene(void)
{
    static const uint16_t SUN[3][4] = {{0, 255, 236, 90}, {500, 255, 150, 40}, {1000, 250, 40, 120}};
    static const uint16_t GND[2][4] = {{0, 30, 6, 46}, {1000, 10, 2, 22}};
    int32_t y, i, n;
    for (y = 0; y < U8_HZ; y++)                  /* the sky */
        cv_rect(0, y, 240, 1, u8_grad(U8_SKY, 4, (uint32_t)y * 1000u / (U8_HZ - 1)));
    for (i = 0; i < U8_NSTAR; i++)               /* the stars (u8_stars), at full brightness */
        cv_pset(u8.st[i].x, u8.st[i].y, u8_star(i, 3u));
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

/* ------------------------------------------------------------------ editing (step 7b) --- */
/* the program's parameters in the order SELECT steps through them: the page's sections, DCO 1 to the voices */
enum { PK_NUM, PK_ONOFF, PK_WAVE, PK_OCT, PK_KT, PK_POL, PK_TRIG, PK_VOICES };
typedef struct { const char *sec, *name; uint8_t kind, max; uint16_t off; } u8_par_t;
#define U8_PO(f) ((uint16_t)__builtin_offsetof(p800_prog_t, f))
#define U8_DCO(n, d) {n, "WAVE", PK_WAVE, 1, U8_PO(dco[d].saw)}, {n, "16'", PK_ONOFF, 1, U8_PO(dco[d].foot[0])}, \
    {n, "8'", PK_ONOFF, 1, U8_PO(dco[d].foot[1])}, {n, "4'", PK_ONOFF, 1, U8_PO(dco[d].foot[2])},              \
    {n, "2'", PK_ONOFF, 1, U8_PO(dco[d].foot[3])}, {n, "OCTAVE", PK_OCT, 2, U8_PO(dco[d].oct)},               \
    {n, "LEVEL", PK_NUM, 31, U8_PO(dco[d].level)}
#define U8_DEG(n, e) {n, "ATTACK", PK_NUM, 31, U8_PO(deg[e][0])}, {n, "DECAY", PK_NUM, 31, U8_PO(deg[e][1])}, \
    {n, "BREAK POINT", PK_NUM, 31, U8_PO(deg[e][2])}, {n, "SLOPE", PK_NUM, 31, U8_PO(deg[e][3])},            \
    {n, "SUSTAIN", PK_NUM, 31, U8_PO(deg[e][4])}, {n, "RELEASE", PK_NUM, 31, U8_PO(deg[e][5])}
static const u8_par_t U8_PAR[] = {
    U8_DCO("DCO 1", 0),
    U8_DCO("DCO 2", 1),
    {"DCO 2", "INTERVAL", PK_NUM, 12, U8_PO(iv)}, {"DCO 2", "DETUNE", PK_NUM, 3, U8_PO(dt)},
    {"NOISE", "LEVEL", PK_NUM, 15, U8_PO(nz)},
    {"VCF", "CUTOFF", PK_NUM, 99, U8_PO(co)}, {"VCF", "RESONANCE", PK_NUM, 15, U8_PO(rs)},
    {"VCF", "KEY TRACK", PK_KT, 2, U8_PO(kt)}, {"VCF", "EG INTENSITY", PK_NUM, 15, U8_PO(ei)},
    {"VCF", "EG POLARITY", PK_POL, 1, U8_PO(ep)}, {"VCF", "TRIGGER", PK_TRIG, 1, U8_PO(tr)},
    U8_DEG("DEG 1", 0), U8_DEG("DEG 2", 1), U8_DEG("DEG 3", 2),
    {"MG", "FREQUENCY", PK_NUM, 15, U8_PO(mg[0])}, {"MG", "DELAY", PK_NUM, 15, U8_PO(mg[1])},
    {"MG", "DCO INTENSITY", PK_NUM, 15, U8_PO(mg[2])}, {"MG", "VCF INTENSITY", PK_NUM, 15, U8_PO(mg[3])},
    {"CHORUS", "CHORUS", PK_ONOFF, 1, U8_PO(ch)},
    {"VOICES", "VOICES", PK_VOICES, 1, U8_PO(mode)},
};
#define U8_NPAR (sizeof U8_PAR / sizeof U8_PAR[0])

static p800_prog_t u8_ed;                        /* the program being edited (a copy: the 64 stay as they are); the
                                                  * Poly-800 plays it while p8.pg points here. A new program: gone */
enum { UV_NONE, UV_PAR, UV_TUNE };
static struct { uint8_t sel, view, prog; } ue;   /* the parameter SELECT is on, what the lower half shows, the program
                                                  * the edits belong to */

static int u8_edited(void) { return p8.pg == &u8_ed; }
static uint32_t u8_get(uint32_t k) { return ((const uint8_t *)p8.pg)[U8_PAR[k].off]; }

/* the value's words: a number, or what the setting is called */
static void u8_val(char *b, uint32_t k, uint32_t v)
{
    static const char *const W[][3] = {{"", "", ""}, {"OFF", "ON", ""}, {"SQUARE", "SAW", ""}, {"LOW", "MID", "HIGH"},
                                       {"OFF", "HALF", "FULL"}, {"NORMAL", "INVERT", ""}, {"SINGLE", "MULTI", ""},
                                       {"WHOLE", "DOUBLE", ""}};
    uint32_t kind = U8_PAR[k].kind;
    if (kind == PK_NUM)
        fmt_int(b, (int32_t)v);
    else
        str_cpy(b, W[kind][v < 3u ? v : 0u], 8);
}

/* KNOB 1: parameter k by s steps (held at its ends); the first edit of a program makes the copy the Poly-800 plays */
static void u8_edit(uint32_t k, int32_t s)
{
    int32_t v = (int32_t)u8_get(k) + s;
    if (v < 0) v = 0;
    if (v > U8_PAR[k].max) v = U8_PAR[k].max;
    if (!u8_edited()) {
        u8_ed = *p8.pg;
        p8.pg = &u8_ed;                          /* (one store: the audio reads the copy from the next block) */
    }
    ((uint8_t *)&u8_ed)[U8_PAR[k].off] = (uint8_t)v;
    p800_apply();                                /* (the cutoff's base, key tracking) */
}

/* what the screen shows: the top half (the program) and the bottom half (a parameter, the octave, the phaser),
 * each a signature; a half is drawn again only when its signature changes */
static uint32_t u8_sig_top(void)
{
    const p800_prog_t *g = p8.pg && ue.prog == trk[0].preset ? p8.pg : &P800_PROGS[trk[0].preset % P800_NPROG];
    return 1u + trk[0].preset + (uint32_t)g->mode * 101u + (uint32_t)u8_edited() * 211u;
}
static uint32_t u8_sig_bot(void)
{
    uint32_t sig = 7u + (uint32_t)(song.octave + 3) * 3u + (uint32_t)(p8.set[P800_PH_MIX] > 0) * 31u +
                   (uint32_t)(fm1_ms - u8.boot_ms < 4000u) * 61u + ue.view * 131u;
    if (ue.view == UV_PAR)
        sig += ue.sel * 1009u + u8_get(ue.sel) * 100003u;
    if (ue.view == UV_TUNE)
        sig += (uint32_t)(p8.set[P800_TUNE] + 50) * 7919u;
    return sig;
}

static void u8_draw(void)
{
    static uint32_t top, bot;
    const p800_prog_t *g = u8_edited() ? p8.pg : &P800_PROGS[trk[0].preset % P800_NPROG];
    uint32_t st = u8_sig_top(), sb = u8_sig_bot(), y0;
    char num[4], o[8], v[12], tn[8];
    const aafont_t *nf;
    int32_t nx;
    if (u8.ok && st == top && sb == bot) {
        u8_twinkle();
        return;
    }
    u8_stars();
    num[0] = (char)('0' + g->num / 10u), num[1] = (char)('0' + g->num % 10u), num[2] = 0;
    str_cpy(o, "OCT ", sizeof o);
    if (song.octave > 0) str_cpy(o + 4, "+", sizeof o - 4);
    fmt_int(o + str_len(o), song.octave);
    nx = 10 + text_w(&AF_L, num) + 12;
    nf = text_w(&AF_M, g->name) <= 232 - nx ? &AF_M : &AF_S;   /* (a long name in the small face) */
    for (y0 = 0; y0 < 240u; y0 += 120u) {
        if (u8.ok && (y0 ? sb == bot : st == top))
            continue;                            /* (this half has not changed) */
        cv_begin(240, 120, 0);
        cv_oy = -(int32_t)y0;
        cv_cy0 = 0, cv_cy1 = 120;
        u8_scene();
        u8_text(10, 6, &AF_L, num, u8_rgb(120, 240, 255));
        u8_text(nx, 9, nf, g->name, u8_rgb(255, 255, 255));
        u8_text(nx, 29, &AF_S, g->mode ? "DOUBLE" : "WHOLE", u8_rgb(255, 170, 230));
        if (u8_edited())                         /* (not what is stored: SAVE comes with step 8) */
            u8_text(nx + text_w(&AF_S, g->mode ? "DOUBLE" : "WHOLE") + 10, 29, &AF_S, "EDITED", u8_rgb(255, 230, 120));
        if (ue.view == UV_PAR) {                 /* the parameter: its section, its name, its value */
            u8_val(v, ue.sel, u8_get(ue.sel));
            u8_text(10, 164, &AF_S, U8_PAR[ue.sel].sec, u8_rgb(255, 170, 230));
            u8_text(10, 180, &AF_M, U8_PAR[ue.sel].name, u8_rgb(255, 255, 255));
            u8_text(230 - text_w(&AF_L, v), 168, &AF_L, v, u8_rgb(120, 240, 255));
        } else if (ue.view == UV_TUNE) {         /* GLO + KNOB 1 */
            tn[0] = 0;
            if (p8.set[P800_TUNE] > 0) str_cpy(tn, "+", sizeof tn);
            fmt_int(tn + str_len(tn), p8.set[P800_TUNE]);
            u8_text(10, 164, &AF_S, "GLOBAL", u8_rgb(255, 170, 230));
            u8_text(10, 180, &AF_M, "TUNE (CENTS)", u8_rgb(255, 255, 255));
            u8_text(230 - text_w(&AF_L, tn), 168, &AF_L, tn, u8_rgb(120, 240, 255));
        } else if (fm1_ms - u8.boot_ms < 4000u) {
            u8_text(120 - text_w(&AF_S, "FM1-800 " FELUCCA_VERSION) / 2, 200, &AF_S, "FM1-800 " FELUCCA_VERSION,
                    u8_rgb(255, 255, 255));
        }
        u8_text(10, 220, &AF_S, o, u8_rgb(120, 240, 255));
        if (p8.set[P800_PH_MIX] > 0)
            u8_text(230 - text_w(&AF_S, "PHASER"), 220, &AF_S, "PHASER", u8_rgb(255, 170, 230));
        if (!y0) {                               /* the stars the text covers (all lie in this strip) stay still */
            uint32_t i;
            for (i = 0; i < U8_NSTAR; i++) {
                u8.st[i].still = (uint8_t)((u8.st[i].still & 2u) |
                                           (cv_px[(uint32_t)u8.st[i].y * 240u + u8.st[i].x] != swap16(u8_star(i, 3u))));
                u8.st[i].lvl = 3;
            }
        }
        cv_oy = 0;
        cv_blit(0, y0);
    }
    u8.ok = 1, top = st, bot = sb;
}

static void u8_input(void)
{
    uint32_t pressed = fm1_input_edges(0), both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]), tap, k;
    uint32_t glo = (fm1_in.buttons >> panel.btn[B_GLO]) & 1u;
    int32_t s;
    (void)fm1_input_note_edges();                /* (the keys play from seq.c: their edges are nobody's here) */
    if (!u8.boot_ms)
        u8.boot_ms = fm1_ms | 1u;
    song.grid = 0;                               /* the keys play */
    if ((s = panel_enc(EN_PRESET)) != 0) {       /* PRESETS: the program, round the 64 (its edits gone) */
        int32_t p = ((int32_t)trk[0].preset + s) % (int32_t)P800_NPROG;
        trk[0].preset = (uint8_t)(p < 0 ? p + (int32_t)P800_NPROG : p);
        p800_program(trk[0].preset);             /* (now, not at the next block: the screen shows it at once) */
        p8e_prog = trk[0].preset;
        ue.view = UV_NONE;                       /* (the program alone; SELECT keeps its place) */
    }
    ue.prog = trk[0].preset;
    if ((s = panel_enc(EN_SELECT)) != 0) {       /* SELECT: the parameter (held at the first and last) */
        int32_t p = (int32_t)ue.sel + (ue.view == UV_PAR ? s : 0);   /* (the first turn shows where it is) */
        ue.sel = (uint8_t)(p < 0 ? 0 : p >= (int32_t)U8_NPAR ? (int32_t)U8_NPAR - 1 : p);
        ue.view = UV_PAR;
    }
    if ((s = panel_enc(EN_K1)) != 0) {           /* KNOB 1: its value; with GLO held, the tuning */
        if (glo) {
            int32_t t = p8.set[P800_TUNE] + s;
            p800_set(P800_TUNE, t < -50 ? -50 : t > 50 ? 50 : t);
            ue.view = UV_TUNE;
        } else {
            u8_edit(ue.sel, s);
            ue.view = UV_PAR;
        }
    }
    if (!glo && ue.view == UV_TUNE)              /* GLO let go: the parameter again */
        ue.view = UV_PAR;
    for (k = EN_SELECT; k < NE; k++)             /* the other knobs: not yet (their turns dropped) */
        if (k != EN_PRESET && k != EN_SELECT && k != EN_K1)
            (void)panel_enc(k);
    tap = oct_taps(pressed, 1);                  /* OCT- / OCT+ (on release; both together: 0) */
    if ((fm1_in.buttons & both) == both)
        song.octave = 0;
    else if (tap & 1u)
        song.octave -= song.octave > -3 ? 1 : 0;
    else if (tap & 2u)
        song.octave += song.octave < 3 ? 1 : 0;
}
