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
 * of the program (u8_ed), which the Poly-800 plays; turning PRESETS leaves them (unless saved). GLO held +
 * KNOB 1: the tuning, -50 .. +50 cents. Each half of the screen is drawn again only when what it shows changes.
 * Step 7c, the effects: FX goes to the PHASER page and back (the echo's page will sit between). The phaser is one
 * setting for every program, as a pedal on the floor: SELECT clockwise switches it on, anticlockwise off; KNOB 1..4
 * RATE, DEPTH, FEEDBACK, MIX (0..10); GLO + KNOB 1 its STAGES (2..12); off at power-on. Then the ECHO page (echo800.c, the tape
 * echo, off at power-on): SELECT on / off, KNOB 1..4 DELAY (shown in ms), REPEAT, BALANCE, AGE (0..10).
 * FX is lit while either is on.
 * FM1_HP builds (the owner's own; FM1_HP=0 leaves it out): the high-pass add-on, one setting for every program, on its
 * own page: GLO tapped (pressed and let go, nothing else touched meanwhile) opens it and stays (v0.9.3: no holding
 * while playing), tapped again (or FX) leaves. On it SELECT clockwise on, anticlockwise off; KNOB 1 its CUTOFF
 * (0..99); KNOB 2 clockwise the MG sweeps it (with the low-pass, as the page), anticlockwise not. HPF at the bottom of
 * the home screen while it is on. GLO held with another control is as before (tuning, STAGES, banks, restore).
 * The sequencer (seq800.c, step 11): REC starts entering a new sequence (the SEQUENCE page shows it) and, again, ends
 * it (saved); while entering every note or chord played is a step, ARP a TIE, SEQ a REST. Otherwise SEQ opens the
 * SEQUENCE page (and again, home): KNOB 1 its TEMPO (40..240; following MIDI clock it shows EXT). PLAY/STOP starts and
 * stops it (with MIDI clock, the drum machine's start and stop too). REC lit while entering, PLAY while playing.
 * GLO tapped (pressed and let go, nothing else touched): the GLOBAL page (v0.11.0): KNOB 1 MIDI CH (OMNI, 1..16),
 * KNOB 2 BEND (0..12 semitones), KNOB 3 MOD (the wheel: VIB, VCF or BOTH), KNOB 4 TUNE (-50..+50). Tapped again:
 * the HIGH-PASS page (FM1_HP builds), then home.
 * HOME held: the CPU, now and its peak since HOME went down, and the overruns since power-on (audio.c: a half of the
 * audio not ready in time, heard as a crackle; a crackle with none is something else, such as a flash erase).
 * The lights (u8_leds, in place of Felucca's ui_leds): every button and key glows dim, FX lit with the phaser on, OCT-
 * / OCT+ lit when the keys are moved down / up, a key lit while it is held. Every other control does nothing. */

#define U8_HZ 150                                /* the horizon */
#define U8_CX 120                                /* the sun's centre and the grid's vanishing point */
#define U8_CY 118
#define U8_R 54
#define U8_NSTAR 40

static struct {
    uint8_t ok, booted;                          /* drawn at least once; boot_ms set */
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

static p800_prog_t u8_ed;                        /* the program playing: its slot's, read from flash (prog800.c), edited
                                                  * here; the
                                                  * Poly-800 plays it while p8.pg points here. A new program: gone */
enum { UV_NONE, UV_PAR, UV_TUNE, UV_HP, UV_CPU };
enum { PG_HOME, PG_PHASER, PG_ECHO, PG_HP, PG_SEQ, PG_GLOBAL };
static struct {
    uint8_t sel, view, prog, page, ph_on, ph_set, ph_mix;
    uint8_t slot, edited, loaded;                /* step 8: the slot playing (0..63 bank A, 64..127 bank B), edited
                                                  * since it was loaded or saved, the first load done */
    uint8_t save, dest;                          /* SAVE: what it is asking (U8S_*), the slot it would write */
    uint8_t glo_prev, glo_used;                  /* GLO down last frame; something else used while it was down (then
                                                  * letting go is no tap: the high-pass page does not open) */
    char dest_name[24];                          /* .. the name of what is in it now */
    const char *msg;                             /* a word for a moment (SAVED, RESTORED, ..) */
    uint32_t msg_ms;
} ue;
enum { U8S_NONE, U8S_SAVE, U8S_RESTORE, U8S_RESTORE_A };
/* prog800.c (after storage.c): the programs and the global settings in flash */
static void p8s_get(uint32_t slot, p800_prog_t *g);
static int p8s_put(uint32_t slot, const p800_prog_t *g);
static int p8s_restore_a(void);
static void p8s_factory(uint32_t slot, p800_prog_t *g);
static uint32_t p8g_boot(void);
static void p8g_poll(void);
static int p8q_save(void);
static void p8q_load(void);   /* .. the page; the phaser on, its MIX
                                                  * (p8.set's is 0 while it is off), read once from the defaults */   /* the parameter SELECT is on, what the lower half shows, the program
                                                  * the edits belong to */

static int u8_edited(void) { return ue.edited; }

/* the slot's program, playing: from flash (or its factory program), into u8_ed; the Poly-800 points at it. Part 1's
 * preset follows (bank A's index, p8e_prog first: eng_p800.c p8e_mix never loads over it) */
static void u8_load(uint32_t slot)
{
    slot %= 128u;
    p8s_get(slot, &u8_ed);
    ue.slot = (uint8_t)slot, ue.edited = 0, ue.loaded = 1;
    p8.pg = &u8_ed;
    p800_apply();
    p8e_prog = (uint8_t)(slot & 63u);
    trk[0].preset = (uint8_t)(slot & 63u);
}

static void u8_say(const char *m) { ue.msg = m, ue.msg_ms = fm1_ms; }   /* (ue.msg marks it set: the time as it is,
                                                  * never one ahead, or the 1.5 s would wrap round to gone) */

/* a note's name, "C#4" (MIDI 60 = C4) */
static void u8_note_txt(char *b, uint32_t nt)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t oct = (int32_t)(nt / 12u) - 1;
    str_cpy(b, N[nt % 12u], 4);
    if (oct < 0)
        str_cpy(b + str_len(b), "-1", 4);
    else
        fmt_int(b + str_len(b), oct);
}

/* "A 11": the slot's bank and number */
static void u8_slot_txt(char *b, uint32_t slot)
{
    uint32_t n = (slot % 64u / 8u + 1u) * 10u + slot % 8u + 1u;
    b[0] = slot < 64u ? 'A' : 'B', b[1] = (char)('0' + n / 10u), b[2] = (char)('0' + n % 10u), b[3] = 0;
}
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
    ((uint8_t *)&u8_ed)[U8_PAR[k].off] = (uint8_t)v;
    ue.edited = 1;
    p800_apply();                                /* (the cutoff's base, key tracking) */
}

#ifndef U8_MAX_US                               /* (audio.c's: the longest half since cleared, the halves late) */
#define U8_MAX_US felucca_dbg.max_us
#define U8_LATE felucca_dbg.late
#endif

/* HOME held: the load now (song.cpu_q8, 256 = all) and a half's time in us, as % to the nearest 5 (the screen is not
 * redrawn for every flicker of the load) */
static uint32_t u8_cpu_now(void) { return (song.cpu_q8 * 100u / 256u + 2u) / 5u * 5u; }
static uint32_t u8_cpu_peak(void) { return (U8_MAX_US * 100u / (HALF_FRAMES * 1000000u / FS) + 2u) / 5u * 5u; }

/* what the screen shows: the top half (the program) and the bottom half (a parameter, the octave, the phaser),
 * each a signature; a half is drawn again only when its signature changes */
static uint32_t u8_sig_top(void)
{
    return 1u + ue.slot + (uint32_t)u8_ed.mode * 131u + (uint32_t)u8_edited() * 211u + str_len(u8_ed.name) * 7001u +
           (uint32_t)(uint8_t)u8_ed.name[0] * 60013u;
}
static uint32_t u8_sig_bot(void)
{
    uint32_t sig = 7u + ue.save * 900007u + ue.dest * 7907u + (uint32_t)(ue.msg != 0 && !ue.save) * 5003u +
                   (uint32_t)(ue.msg ? (uintptr_t)ue.msg & 0xFFFFu : 0u) * 17u + (uint32_t)ue.edited * 13u +
                   (uint32_t)(song.octave + 3) * 3u + (uint32_t)ue.ph_on * 31u +
                   (uint32_t)(fm1_ms - u8.boot_ms < 4000u) * 61u + ue.view * 131u + ue.page * 977u;
    sig += (uint32_t)ec.on * 49999u;
    if (ue.page == PG_ECHO)
        sig += ec.delay * 11u + ec.repeat * 1201u + ec.balance * 13001u + ec.age * 150001u;
    else if (ue.page == PG_PHASER)
        sig += (uint32_t)p8.set[P800_PH_STAGES] * 11u + (uint32_t)p8.set[P800_PH_RATE] * 101u +
               (uint32_t)p8.set[P800_PH_DEPTH] * 1103u + (uint32_t)p8.set[P800_PH_FB] * 12011u + ue.ph_mix * 130003u;
    else
    if (ue.view == UV_PAR)
        sig += ue.sel * 1009u + u8_get(ue.sel) * 100003u;
    else if (ue.view == UV_TUNE)
        sig += (uint32_t)(p8.set[P800_TUNE] + 50) * 7919u;
    sig += (uint32_t)p8.set[P800_HP_ON] * 7u + (uint32_t)p8.set[P800_HP_MG] * 70001u;
    if (ue.page == PG_HP)
        sig += (uint32_t)p8.set[P800_HP_CUT] * 311u + 4999u;
    if (ue.page == PG_GLOBAL)
        sig += p8_midi_ch * 101u + (uint32_t)p8.set[P800_BENDR] * 10007u + (uint32_t)p8.set[P800_MODD] * 3001u +
               (uint32_t)(p8.set[P800_TUNE] + 50) * 7919u;
    sig += (uint32_t)sq.rec * 8191u + (uint32_t)song.playing * 524287u;
    if (ue.page == PG_SEQ)
        sig += sq.nsteps * 37u + sq.nnotes * 1031u + (uint32_t)song.g[G_BPM] * 65537u + (uint32_t)song.g[G_CLOCK] * 999983u +
               (uint32_t)sq.ngrp * 251u + (song.playing ? sq.at * 17u : 0u);
    if (ue.view == UV_CPU)
        sig += u8_cpu_now() * 1000003u + u8_cpu_peak() * 3001u +
               U8_LATE * 7u;
    return sig;
}

static void u8_draw(void)
{
    static uint32_t top, bot;
    const p800_prog_t *g = &u8_ed;
    if (!ue.loaded && !u8_ed.name[0])            /* (the first frame, at power-on before the flash is read: A11's
                                                  * factory name, not a blank; u8_input loads the real one) */
        p8s_factory(0, &u8_ed);
    uint32_t st = u8_sig_top(), sb = u8_sig_bot(), y0;
    char num[6], o[8], v[12], tn[8];
    const aafont_t *nf;
    int32_t nx;
    if (u8.ok && st == top && sb == bot) {
        u8_twinkle();
        return;
    }
    u8_stars();
    u8_slot_txt(num, ue.slot);                   /* "A11" */
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
        if (u8_edited())                         /* (not what is stored: SAVE keeps it) */
            u8_text(nx + text_w(&AF_S, g->mode ? "DOUBLE" : "WHOLE") + 10, 29, &AF_S, "EDITED", u8_rgb(255, 230, 120));
        if (ue.save || ue.msg) {                 /* SAVE asking, or a word after it */
            char d[6];
            if (!ue.save) {                      /* (a question always over a word left from before; a long word in
                                                  * the middle face, the screen 240 wide) */
                const aafont_t *mf = text_w(&AF_L, ue.msg) <= 224 ? &AF_L : &AF_M;
                u8_text(120 - text_w(mf, ue.msg) / 2, 172, mf, ue.msg, u8_rgb(255, 230, 120));
            } else {
                u8_text(10, 152, &AF_S, ue.save == U8S_SAVE ? "SAVE TO" : ue.save == U8S_RESTORE ? "RESTORE TO FACTORY"
                                                                                                   : "RESTORE BANK A",
                        u8_rgb(255, 170, 230));
                if (ue.save == U8S_RESTORE_A) {
                    u8_text(10, 168, &AF_M, "ALL 64 FACTORY PROGRAMS", u8_rgb(255, 255, 255));
                    u8_text(10, 186, &AF_S, "(BANK B IS KEPT)", u8_rgb(255, 170, 230));
                } else {
                    u8_slot_txt(d, ue.dest);
                    u8_text(10, 166, &AF_L, d, u8_rgb(120, 240, 255));
                    u8_text(14 + text_w(&AF_L, d) + 6, 172, text_w(&AF_M, ue.dest_name) <= 150 ? &AF_M : &AF_S,
                            ue.dest_name, u8_rgb(255, 255, 255));
                }
                u8_text(10, 202, &AF_S, ue.save == U8S_SAVE ? "SAVE: YES   PRESETS: WHERE   OTHER: NO"
                                                            : "SAVE: YES   OTHER: NO", u8_rgb(255, 230, 120));
            }
        } else if (ue.view == UV_CPU) {          /* HOME held: the CPU and the overruns */
            static const char *const L[3] = {"CPU NOW", "PEAK", "OVERRUNS"};
            uint32_t c;
            u8_text(10, 152, &AF_S, "HOME: THE CPU", u8_rgb(255, 170, 230));
            for (c = 0; c < 3u; c++) {
                int32_t cx = 40 + 80 * (int32_t)c;
                fmt_int(v, (int32_t)(c == 0u ? u8_cpu_now() : c == 1u ? u8_cpu_peak() : U8_LATE));
                if (c < 2u) str_cpy(v + str_len(v), "%", sizeof v - str_len(v));
                u8_text(cx - text_w(&AF_S, L[c]) / 2, 172, &AF_S, L[c], u8_rgb(255, 170, 230));
                u8_text(cx - text_w(&AF_L, v) / 2, 188, &AF_L, v,
                        c == 2u && U8_LATE ? u8_rgb(255, 230, 120) : u8_rgb(120, 240, 255));
            }
        } else if (ue.page == PG_GLOBAL) {       /* GLOBAL: KNOB 1..4 in four columns */
            static const char *const L[4] = {"MIDI CH", "BEND", "MOD", "TUNE"};
            static const char *const MD[3] = {"VIB", "VCF", "BOTH"};
            uint32_t c;
            u8_text(10, 152, &AF_S, "GLOBAL", u8_rgb(255, 170, 230));
            for (c = 0; c < 4u; c++) {
                int32_t cx = 30 + 60 * (int32_t)c;
                if (c == 0u) {
                    if (p8_midi_ch) fmt_int(v, p8_midi_ch);
                    else str_cpy(v, "OMNI", sizeof v);
                } else if (c == 1u) {
                    fmt_int(v, p8.set[P800_BENDR]);
                } else if (c == 2u) {
                    str_cpy(v, MD[p8.set[P800_MODD] % 3], sizeof v);
                } else {
                    v[0] = 0;
                    if (p8.set[P800_TUNE] > 0) str_cpy(v, "+", sizeof v);
                    fmt_int(v + str_len(v), p8.set[P800_TUNE]);
                }
                u8_text(cx - text_w(&AF_S, L[c]) / 2, 172, &AF_S, L[c], u8_rgb(255, 170, 230));
                u8_text(cx - text_w(&AF_M, v) / 2, 185, &AF_M, v, u8_rgb(255, 255, 255));
            }
            u8_text(10, 206, &AF_S, "BEND IN SEMITONES  TUNE IN CENTS", u8_rgb(150, 120, 170));
        } else if (ue.page == PG_SEQ) {          /* the sequence: entering or playing, TEMPO, steps, notes */
            static const char *const L[3] = {"TEMPO", "STEPS", "NOTES"};
            uint32_t c;
            const char *st = sq.rec ? "RECORDING" : song.playing ? "PLAYING" : "STOPPED";
            u8_text(10, 152, &AF_S, "SEQUENCE", u8_rgb(255, 170, 230));
            u8_text(230 - text_w(&AF_M, st), 150, &AF_M, st, sq.rec ? u8_rgb(255, 230, 120) : u8_rgb(120, 240, 255));
            for (c = 0; c < 3u; c++) {
                int32_t cx = c == 0u ? 30 : c == 1u ? 110 : 190;
                if (c == 0u) {
                    if (song.g[G_CLOCK]) str_cpy(v, "EXT", sizeof v);
                    else fmt_int(v, song.g[G_BPM]);
                } else if (c == 1u) {
                    fmt_int(v, sq.nsteps);
                    if (song.playing && !sq.rec && sq.nsteps) {
                        str_cpy(v, "", sizeof v);
                        fmt_int(v, sq.at ? sq.at : sq.nsteps);
                        str_cpy(v + str_len(v), "/", sizeof v - str_len(v));
                        fmt_int(v + str_len(v), sq.nsteps);
                    }
                } else {
                    fmt_int(v, sq.nnotes);
                    str_cpy(v + str_len(v), "/256", sizeof v - str_len(v));
                }
                u8_text(cx - text_w(&AF_S, L[c]) / 2, 172, &AF_S, L[c], u8_rgb(255, 170, 230));
                u8_text(cx - text_w(&AF_M, v) / 2, 185, &AF_M, v, u8_rgb(255, 255, 255));
            }
            if (sq.rec) {                        /* entering: the chord held now, else the step entered last */
                char ln[40], nn[6];
                uint32_t k2, cnt = sq.ngrp ? sq.ngrp : sq.nlast;
                const uint8_t *ns = sq.ngrp ? sq.grp : sq.last;
                str_cpy(ln, sq.ngrp ? "NOW: " : "LAST: ", sizeof ln);
                if (!sq.ngrp && sq.last_kind == 2u) str_cpy(ln + str_len(ln), "TIE", sizeof ln - str_len(ln));
                else if (!sq.ngrp && sq.last_kind == 3u) str_cpy(ln + str_len(ln), "REST", sizeof ln - str_len(ln));
                else if (!cnt) str_cpy(ln, "PLAY A NOTE  ARP: TIE  SEQ: REST", sizeof ln);
                for (k2 = 0; k2 < cnt && (sq.ngrp || sq.last_kind == 1u); k2++) {
                    u8_note_txt(nn, ns[k2]);
                    str_cpy(ln + str_len(ln), nn, sizeof ln - str_len(ln));
                    if (k2 + 1u < cnt) str_cpy(ln + str_len(ln), " ", sizeof ln - str_len(ln));
                }
                u8_text(10, 206, &AF_S, ln, u8_rgb(255, 230, 120));
            }
        } else if (ue.page == PG_HP) {           /* the high-pass's page (FM1_HP): on / off, KNOB 1 and 2 */
            u8_text(10, 152, &AF_S, "HIGH-PASS", u8_rgb(255, 170, 230));
            u8_text(230 - text_w(&AF_L, p8.set[P800_HP_ON] ? "ON" : "OFF"), 148, &AF_L, p8.set[P800_HP_ON] ? "ON" : "OFF",
                    p8.set[P800_HP_ON] ? u8_rgb(120, 240, 255) : u8_rgb(150, 120, 170));
            fmt_int(v, p8.set[P800_HP_CUT]);
            u8_text(30 - text_w(&AF_S, "CUTOFF") / 2, 186, &AF_S, "CUTOFF", u8_rgb(255, 170, 230));
            u8_text(30 - text_w(&AF_M, v) / 2, 199, &AF_M, v, u8_rgb(255, 255, 255));
            u8_text(90 - text_w(&AF_S, "MG SWEEP") / 2, 186, &AF_S, "MG SWEEP", u8_rgb(255, 170, 230));
            u8_text(90 - text_w(&AF_M, p8.set[P800_HP_MG] ? "ON" : "OFF") / 2, 199, &AF_M,
                    p8.set[P800_HP_MG] ? "ON" : "OFF", u8_rgb(255, 255, 255));
        } else if (ue.page == PG_PHASER) {       /* the phaser: on / off, STAGES, and KNOB 1..4's values in four columns */
            static const char *const L[4] = {"RATE", "DEPTH", "FEEDBACK", "MIX"};
            static const uint8_t ID[4] = {P800_PH_RATE, P800_PH_DEPTH, P800_PH_FB, 0};
            uint32_t c;
            u8_text(10, 152, &AF_S, "PHASER", u8_rgb(255, 170, 230));
            str_cpy(tn, "STAGES ", sizeof tn);
            u8_text(10, 168, &AF_S, tn, u8_rgb(255, 255, 255));
            fmt_int(v, 2 * (p8.set[P800_PH_STAGES] + 1));
            u8_text(10 + text_w(&AF_S, tn), 168, &AF_S, v, u8_rgb(120, 240, 255));
            u8_text(230 - text_w(&AF_L, ue.ph_on ? "ON" : "OFF"), 148, &AF_L, ue.ph_on ? "ON" : "OFF",
                    ue.ph_on ? u8_rgb(120, 240, 255) : u8_rgb(150, 120, 170));
            for (c = 0; c < 4u; c++) {
                int32_t cx = 30 + 60 * (int32_t)c;
                fmt_int(v, c == 3u ? ue.ph_mix : p8.set[ID[c]]);
                u8_text(cx - text_w(&AF_S, L[c]) / 2, 186, &AF_S, L[c], u8_rgb(255, 170, 230));
                u8_text(cx - text_w(&AF_M, v) / 2, 199, &AF_M, v, u8_rgb(255, 255, 255));
            }
        } else if (ue.page == PG_ECHO) {         /* the echo: on / off, KNOB 1..4's values in four columns */
            static const char *const L[4] = {"DELAY", "REPEAT", "BALANCE", "AGE"};
            uint32_t c;
            u8_text(10, 152, &AF_S, "TAPE ECHO", u8_rgb(255, 170, 230));
            u8_text(230 - text_w(&AF_L, ec.on ? "ON" : "OFF"), 148, &AF_L, ec.on ? "ON" : "OFF",
                    ec.on ? u8_rgb(120, 240, 255) : u8_rgb(150, 120, 170));
            for (c = 0; c < 4u; c++) {
                int32_t cx = 30 + 60 * (int32_t)c;
                if (c == 0u) {
                    fmt_int(v, (int32_t)((EC_DELAY[ec.delay] * 10u + 220u) / 441u));
                    str_cpy(v + str_len(v), " ms", sizeof v - str_len(v));
                } else {
                    fmt_int(v, c == 1u ? ec.repeat : c == 2u ? ec.balance : ec.age);
                }
                u8_text(cx - text_w(&AF_S, L[c]) / 2, 186, &AF_S, L[c], u8_rgb(255, 170, 230));
                u8_text(cx - text_w(c == 0u ? &AF_S : &AF_M, v) / 2, c == 0u ? 202 : 199, c == 0u ? &AF_S : &AF_M, v,
                        u8_rgb(255, 255, 255));
            }
        } else if (ue.view == UV_PAR) {          /* the parameter: its section, its name, its value */
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
        } else if (fm1_ms - u8.boot_ms < 4000u) {   /* (just after power-on: the version) */
            u8_text(120 - text_w(&AF_S, "FM1-800 " FELUCCA_VERSION) / 2, 200, &AF_S, "FM1-800 " FELUCCA_VERSION,
                    u8_rgb(255, 255, 255));
        }
        u8_text(10, 220, &AF_S, o, u8_rgb(120, 240, 255));
        if (ue.page == PG_HOME) {                /* the effects that are on */
            char fx[24];
            fx[0] = 0;
            if (p8.set[P800_HP_ON]) str_cpy(fx, "HPF  ", sizeof fx);
            if (ue.ph_on) str_cpy(fx + str_len(fx), "PHASER  ", sizeof fx - str_len(fx));
            if (ec.on) str_cpy(fx + str_len(fx), "ECHO  ", sizeof fx - str_len(fx));
            if (*fx) {
                fx[str_len(fx) - 2u] = 0;        /* (the last gap) */
                u8_text(230 - text_w(&AF_S, fx), 220, &AF_S, fx, u8_rgb(255, 170, 230));
            }
        }
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
    if (!u8.booted)                              /* (the time as it is: one ahead, the 4 s would wrap round) */
        u8.booted = 1, u8.boot_ms = fm1_ms;
    if (glo && !ue.glo_prev)                     /* GLO down: a tap, unless something else is used before it lets go */
        ue.glo_used = 0;
    if (glo && (pressed & ~(1u << panel.btn[B_GLO])))
        ue.glo_used = 1;
    if (!ue.ph_set) {                            /* the phaser: off at power-on (the Poly-800 as it was: no phaser,
                                                  * no echo, only each program's own chorus), ready at the page's MIX */
        ue.ph_set = 1;
        ue.ph_on = 0;
        ue.ph_mix = (uint8_t)(p8.set[P800_PH_MIX] > 0 ? p8.set[P800_PH_MIX] : P800_SET_DEF[P800_PH_MIX]);
        p800_set(P800_PH_MIX, 0);
    }
    if (!ue.loaded) {                            /* power-on: the settings and the program last chosen (prog800.c);
                                                  * SAVE held: the factory restore of bank A is offered */
        uint32_t t, k2;
        for (t = 0; t < NTRK; t++) {             /* Felucca's own patterns (an old autosave's): gone, so PLAY plays
                                                  * only the Poly-800's sequence; part 1 steps in 16ths */
            for (k2 = 0; k2 < NSTEP; k2++) trk[t].step[k2].time = ST_REST;
            trk[t].p[P_AMODE] = 0;
        }
        trk[0].p[P_SDIV] = 2, trk[0].p[P_SLEN] = 16;
        song.g[G_CLOCK] = 0;
        u8_load(p8g_boot());
        p8q_load();
        if ((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u)
            ue.save = U8S_RESTORE_A;
        pressed &= ~(1u << panel.btn[B_SAVE]);   /* (that press is the one held through power-on) */
    }
    if (ue.msg && fm1_ms - ue.msg_ms > 1500u)    /* a word stays 1.5 s */
        ue.msg = 0;
    p8g_poll();
    song.grid = 0;                               /* the keys play */
    if (ue.save) {                               /* SAVE asking: SAVE again does it, PRESETS picks where (SAVE TO),
                                                  * any other button: no. The knobs do nothing meanwhile */
        uint32_t other = pressed & ~((1u << panel.btn[B_SAVE]) | (1u << panel.btn[B_GLO]));
        if (ue.save == U8S_SAVE && (s = panel_enc(EN_PRESET)) != 0) {
            int32_t d = ((int32_t)ue.dest + s) % 128;
            p800_prog_t there;
            ue.dest = (uint8_t)(d < 0 ? d + 128 : d);
            p8s_get(ue.dest, &there);
            str_cpy(ue.dest_name, there.name, sizeof ue.dest_name);
        }
        for (k = 0; k < NE; k++)
            (void)panel_enc(k);
        if ((pressed >> panel.btn[B_SAVE]) & 1u) {
            int rc;
            if (ue.save == U8S_SAVE) {
                rc = p8s_put(ue.dest, &u8_ed);
                if (!rc) ue.slot = ue.dest, ue.edited = 0, trk[0].preset = p8e_prog = (uint8_t)(ue.dest & 63u);
                u8_say(rc ? "SAVE FAILED" : "SAVED");
            } else if (ue.save == U8S_RESTORE) {
                p800_prog_t f;
                p8s_factory(ue.dest, &f);
                rc = p8s_put(ue.dest, &f);
                if (!rc) u8_load(ue.dest);
                u8_say(rc ? "RESTORE FAILED" : "RESTORED");
            } else {
                rc = p8s_restore_a();
                if (!rc) u8_load(ue.slot);
                u8_say(rc ? "RESTORE FAILED" : "BANK A RESTORED");
            }
            ue.save = U8S_NONE;
        } else if (other) {
            ue.save = U8S_NONE;
            u8_say("NOT SAVED");
        }
        (void)fm1_input_note_edges();
        return;
    }
    /* the sequencer: REC, ARP / SEQ (entering: TIE / REST), SEQ (its page), PLAY/STOP */
    if ((pressed >> panel.btn[B_REC]) & 1u) {
        if (!sq.rec) {                           /* a new sequence: the old one goes (as the Poly-800) */
            if (song.playing) transport_req = 2;
            sq_clear();
            sq.rec = 1;
            ue.page = PG_SEQ;
        } else {                                 /* done: kept */
            sq_rec_poll();
            if (sq.ngrp) (void)sq_add(1, sq.grp, sq.ngrp), sq.ngrp = 0;
            sq.rec = 0;
            u8_say(p8q_save() ? "NOT SAVED" : "SEQUENCE SAVED");
        }
    }
    if (sq.rec) {
        if ((pressed >> panel.btn[B_ARP]) & 1u) (void)sq_add(SQ_TIE, 0, 0);
        if ((pressed >> panel.btn[B_SEQ]) & 1u) (void)sq_add(SQ_REST, 0, 0);
        sq_rec_poll();
    } else if ((pressed >> panel.btn[B_SEQ]) & 1u) {
        ue.page = ue.page == PG_SEQ ? PG_HOME : PG_SEQ;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {   /* PLAY/STOP (entering: it ends first, kept) */
        if (sq.rec) {
            if (sq.ngrp) (void)sq_add(1, sq.grp, sq.ngrp), sq.ngrp = 0;
            sq.rec = 0;
            (void)p8q_save();
        }
        transport_req = song.playing ? 2u : 1u;
    }
    if (song.g[G_CLOCK] && !song.playing && fm1_ms - sq.ext_ms > 3000u)
        song.g[G_CLOCK] = 0;                     /* the clock gone 3 s, stopped: its own tempo again */
    if ((pressed >> panel.btn[B_HOME]) & 1u)     /* HOME held: the CPU (its peak from now) */
        U8_MAX_US = 0, ue.view = UV_CPU;
    if (ue.view == UV_CPU && !((fm1_in.buttons >> panel.btn[B_HOME]) & 1u))
        ue.view = UV_NONE;
    if ((pressed >> panel.btn[B_SAVE]) & 1u) {   /* SAVE: to this slot, or (PRESETS) another; GLO + SAVE: this slot
                                                  * back to its factory program */
        p800_prog_t there;
        ue.save = glo ? U8S_RESTORE : U8S_SAVE;
        ue.dest = ue.slot;
        p8s_get(ue.dest, &there);
        str_cpy(ue.dest_name, glo ? there.name : u8_ed.name, sizeof ue.dest_name);
        if (glo) {
            p8s_factory(ue.dest, &there);
            str_cpy(ue.dest_name, there.name, sizeof ue.dest_name);
        }
        return;
    }
    if ((pressed >> panel.btn[B_FX]) & 1u) {     /* FX: the PHASER page, the ECHO page, and back */
        ue.page = ue.page == PG_PHASER ? PG_ECHO : ue.page == PG_ECHO ? PG_HOME : PG_PHASER;
        if (ue.view == UV_TUNE) ue.view = UV_PAR;
    }
    if ((s = panel_enc(EN_PRESET)) != 0) {       /* PRESETS: the program, round the 128 (A11..A88, B11..B88; its
                                                  * edits gone); GLO + PRESETS: the other bank's same number */
        int32_t p = ((int32_t)ue.slot + (glo ? s * 64 : s)) % 128;
        ue.glo_used |= (uint8_t)glo;
        u8_load((uint32_t)(p < 0 ? p + 128 : p));
        ue.view = UV_NONE;                       /* (the program alone; SELECT keeps its place) */
    }
    ue.prog = trk[0].preset;
    if (!glo && ue.glo_prev && !ue.glo_used)     /* GLO tapped: GLOBAL, then (FM1_HP) HIGH-PASS, then home */
        ue.page = ue.page != PG_GLOBAL && ue.page != PG_HP ? PG_GLOBAL
                : ue.page == PG_GLOBAL && FM1_HP ? PG_HP : PG_HOME;
    ue.glo_prev = (uint8_t)glo;
    if (ue.page == PG_GLOBAL) {                  /* GLOBAL: KNOB 1 MIDI CH, 2 BEND, 3 MOD, 4 TUNE */
        if ((s = panel_enc(EN_K1)) != 0) {
            int32_t c = (int32_t)p8_midi_ch + s;
            p8_midi_ch = (uint8_t)(c < 0 ? 0 : c > 16 ? 16 : c);
        }
        if ((s = panel_enc(EN_K2)) != 0) {
            int32_t c = p8.set[P800_BENDR] + s;
            p800_set(P800_BENDR, c < 0 ? 0 : c > 12 ? 12 : c);
        }
        if ((s = panel_enc(EN_K3)) != 0) {
            int32_t c = p8.set[P800_MODD] + s;
            p800_set(P800_MODD, c < 0 ? 0 : c > 2 ? 2 : c);
        }
        if ((s = panel_enc(EN_K4)) != 0) {
            int32_t c = p8.set[P800_TUNE] + s;
            p800_set(P800_TUNE, c < -50 ? -50 : c > 50 ? 50 : c);
        }
        (void)panel_enc(EN_SELECT), (void)panel_enc(EN_ALGO);
    } else if (ue.page == PG_SEQ) {              /* the sequence's page: KNOB 1 its TEMPO (its own clock) */
        if ((s = panel_enc(EN_K1)) != 0 && !song.g[G_CLOCK]) {
            int32_t b = song.g[G_BPM] + s;
            song.g[G_BPM] = (int16_t)(b < 40 ? 40 : b > 240 ? 240 : b);
        }
        for (k = 0; k < NE; k++)
            if (k != EN_PRESET && k != EN_K1)
                (void)panel_enc(k);
    } else if (ue.page == PG_HP) {               /* the high-pass's page: SELECT on / off, KNOB 1 CUTOFF, KNOB 2 MG */
        if ((s = panel_enc(EN_SELECT)) != 0)
            p800_set(P800_HP_ON, s > 0);
        if ((s = panel_enc(EN_K1)) != 0) {
            int32_t c = p8.set[P800_HP_CUT] + s;
            p800_set(P800_HP_CUT, c < 0 ? 0 : c > 99 ? 99 : c);
        }
        if ((s = panel_enc(EN_K2)) != 0)
            p800_set(P800_HP_MG, s > 0);
        for (k = EN_K3; k < NE; k++)
            (void)panel_enc(k);
        (void)panel_enc(EN_ALGO);
    } else if (ue.page == PG_PHASER) {                  /* the phaser's page: SELECT on / off, KNOB 1..4, GLO + KNOB 1 */
        static const uint8_t ID[4] = {P800_PH_RATE, P800_PH_DEPTH, P800_PH_FB, P800_PH_MIX};
        if ((s = panel_enc(EN_SELECT)) != 0)
            ue.ph_on = s > 0;
        for (k = 0; k < 4u; k++)
            if ((s = panel_enc(EN_K1 + k)) != 0) {
                uint32_t id = glo && !k ? P800_PH_STAGES : ID[k], mx = id == P800_PH_STAGES ? 5u : 10u;
                ue.glo_used |= (uint8_t)(glo && !k);
                int32_t v = (id == P800_PH_MIX ? ue.ph_mix : p8.set[id]) + s;
                v = v < 0 ? 0 : v > (int32_t)mx ? (int32_t)mx : v;
                if (id == P800_PH_MIX)
                    ue.ph_mix = (uint8_t)v;
                else
                    p800_set(id, v);
            }
        p800_set(P800_PH_MIX, ue.ph_on ? ue.ph_mix : 0);
        (void)panel_enc(EN_ALGO);
    } else if (ue.page == PG_ECHO) {             /* the echo's page: SELECT on / off, KNOB 1..4 */
        if ((s = panel_enc(EN_SELECT)) != 0)
            ec.on = s > 0;
        for (k = 0; k < 4u; k++)
            if ((s = panel_enc(EN_K1 + k)) != 0) {
                uint8_t *c = k == 0u ? &ec.delay : k == 1u ? &ec.repeat : k == 2u ? &ec.balance : &ec.age;
                int32_t mx = k == 0u ? 99 : 10, v = (int32_t)*c + s;
                *c = (uint8_t)(v < 0 ? 0 : v > mx ? mx : v);
            }
        (void)panel_enc(EN_ALGO);
    } else {
    if ((s = panel_enc(EN_SELECT)) != 0) {       /* SELECT: the parameter (held at the first and last) */
        int32_t p = (int32_t)ue.sel + (ue.view == UV_PAR ? s : 0);   /* (the first turn shows where it is) */
        ue.sel = (uint8_t)(p < 0 ? 0 : p >= (int32_t)U8_NPAR ? (int32_t)U8_NPAR - 1 : p);
        ue.view = UV_PAR;
    }
    if ((s = panel_enc(EN_K1)) != 0) {           /* KNOB 1: its value; with GLO held, the tuning */
        if (glo) {
            int32_t t = p8.set[P800_TUNE] + s;
            ue.glo_used = 1;
            p800_set(P800_TUNE, t < -50 ? -50 : t > 50 ? 50 : t);
            ue.view = UV_TUNE;
        } else {
            u8_edit(ue.sel, s);
            ue.view = UV_PAR;
        }
    }
    if (!glo && ue.view == UV_TUNE)              /* GLO let go: the parameter again */
        ue.view = UV_PAR;
    for (k = EN_SELECT; k < NE; k++)             /* the other knobs: nothing here (their turns dropped) */
        if (k != EN_PRESET && k != EN_SELECT && k != EN_K1)
            (void)panel_enc(k);
    }
    tap = oct_taps(pressed, 1);                  /* OCT- / OCT+ (on release; both together: 0) */
    if ((fm1_in.buttons & both) == both)
        song.octave = 0;
    else if (tap & 1u)
        song.octave -= song.octave > -3 ? 1 : 0;
    else if (tap & 2u)
        song.octave += song.octave < 3 ? 1 : 0;
}

/* the lights: every button and key glows dim (found in the dark), FX lit with the phaser on, OCT- / OCT+ lit when the
 * keys are moved, a key lit while it is held; not over the power-on sweep */
static void u8_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0};
    uint32_t k, c, held = fm1_in.notes;
    static uint8_t ready;
    if (fm1_led_anim_on())
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    for (k = 0; k < 27u; k++) {
        led_put(nd, 14u + k, 1);
        led_put(nl, 14u + k, (int)((held >> k) & 1u));
    }
    led_put(nl, panel.btn[B_FX], ue.ph_on || ec.on);
    led_put(nl, panel.btn[B_REC], sq.rec);
    led_put(nl, panel.btn[B_PLAY], song.playing);
    led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
    led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    fm1_led_dim_level(settings_leds == LEDS_DIM_LO);
    for (c = 0; c < FM1_NCOL; c++) {
        nd[c] = settings_leds == LEDS_DIM || settings_leds == LEDS_DIM_LO ? (uint8_t)(nd[c] & ~nl[c]) : 0u;
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = 0;
        fm1_led_mid[c] = 0;
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}
