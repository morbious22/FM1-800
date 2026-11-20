/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 7a: the Poly-800's home screen and controls (firmware/src/ui800.c) on tests/ui_test.c's stubs.
 *   - the screen: the scene (sky, sun, grid where they belong), the program's number and name on it, drawn only
 *     when something on it changes
 *   - PRESETS: the next / previous program, round the 128 (banks A and B); the program that plays follows
 *   - step 8 (prog800.c, on a flash held in RAM): SAVE, SAVE TO another slot, the restores, the global settings
 *   - OCT- / OCT+: an octave on release, -3 .. +3, both together back to 0
 * Writes OUT/home_<n>.ppm for a few programs (to look at).
 *   cc -O1 -w -DFM1_800=1 [the include paths] -Itests -o ui800_test tests/poly800/ui800_test.c -lm; ./ui800_test OUT */
#define UI_TEST_MAIN ui_test_main
#include "ui_test.c"
static uint32_t u8t_max_us, u8t_late;            /* (the host's debug block has no audio counters: these stand in) */
#define U8_MAX_US u8t_max_us
#define U8_LATE u8t_late
#include "../firmware/src/ui800.c"
static uint8_t u8t_nor[0x100000];                /* the FM-1's 1 MiB of flash, erased; storage.c's hooks on it */
static uint32_t u8t_erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, u8t_nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(u8t_nor + off, 0xFF, 4096); u8t_erases++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(u8t_nor + off, src, n); return 0; }
#undef FELUCCA_FLASH
#define FELUCCA_FLASH 1
#include "../firmware/src/storage.c"
#include "../firmware/src/prog800.c"

static int u8t_fails;
static void u8t_ok(int c, const char *what)
{
    printf("  %s  %s\n", c ? "ok  " : "FAIL", what);
    u8t_fails += !c;
}
static uint32_t u8t_px(uint32_t x, uint32_t y)        /* RGB888 of a screen pixel */
{
    uint16_t v = (uint16_t)((host_screen[y * 240u + x] >> 8) | (host_screen[y * 240u + x] << 8));
    return (uint32_t)((v >> 11) * 255 / 31) << 16 | (uint32_t)(((v >> 5) & 63) * 255 / 63) << 8 | (uint32_t)((v & 31) * 255 / 31);
}
static void u8t_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t k;
    if (!f) return;
    fprintf(f, "P6 240 240 255\n");
    for (k = 0; k < 240u * 240u; k++) {
        uint32_t c = u8t_px(k % 240u, k / 240u);
        fputc((int)(c >> 16), f), fputc((int)(c >> 8 & 255u), f), fputc((int)(c & 255u), f);
    }
    fclose(f);
}
static void u8t_turn(uint32_t role, int32_t s) { host_enc[panel.enc[role] % 7u] = s * panel.dir[role]; u8_input(); }
static void u8t_tap(uint32_t btn)                     /* pressed, then let go */
{
    host_pressed = 1u << panel.btn[btn];
    fm1_in.buttons |= 1u << panel.btn[btn];
    u8_input();
    fm1_in.buttons &= ~(1u << panel.btn[btn]);
    u8_input();
}

int main(int argc, char **argv)
{
    static const uint8_t SHOW[4] = {0, 1, 15, 63};
    char path[600];
    uint32_t i, c, white = 0, rows;
    const char *out = argc > 1 ? argv[1] : ".";
    printf("FM1-800 home screen (ui800.c)\n");
    ui_power_on();                                /* (ui_test.c: the panel set up, the parts at their defaults) */
    memset(u8t_nor, 0xFF, sizeof u8t_nor);
    p800_init();
    trk[0].engine = trk[0].eng_req = ENGI_P800;
    trk[0].preset = 0;
    fm1_ms = 100000;
    u8_input();
    u8.booted = 1, u8.boot_ms = 1;                /* (long after power-on: no version shown) */
    u8_draw();
    c = u8t_px(10, 2);
    u8t_ok((c >> 16) < 60 && (c & 255u) > (c >> 16), "the sky at the top is dark purple");
    c = u8t_px(U8_CX, U8_CY - 40);
    u8t_ok((c >> 16) > 200 && (c >> 8 & 255u) > 150 && (c & 255u) < 160, "the sun is yellow at its top");
    c = u8t_px(U8_CX, U8_HZ);
    u8t_ok((c >> 16) > 200 && (c & 255u) > 150, "the horizon glows pink");
    for (i = 0; i < 240u * 30u; i++)
        white += (host_screen[i] == 0xFFFFu);
    u8t_ok(white > 40, "the program's name is drawn in white at the top");
    rows = host_blit_rows;
    u8_draw();
    u8t_ok(host_blit_rows == rows, "nothing changed: nothing drawn (but the stars)");
    {                                             /* 6 s of frames: the stars twinkle, nothing else is touched */
        static uint16_t before[240 * 240];
        uint32_t k, t, changed = 0, other = 0, tw = 0, still = 0, steps = 0;
        memcpy(before, host_screen, sizeof before);
        for (k = 0; k < U8_NSTAR; k++) {
            tw += !(u8.st[k].still & 1u);
            still += u8.st[k].still & 1u;
        }
        for (t = 0; t < 6000u; t += 16u) {
            uint32_t lv[U8_NSTAR];
            for (k = 0; k < U8_NSTAR; k++) lv[k] = u8.st[k].lvl;
            fm1_ms += 16u;
            u8_draw();
            for (k = 0; k < U8_NSTAR; k++) steps += lv[k] != u8.st[k].lvl;
        }
        for (k = 0; k < 240u * 240u; k++)
            if (host_screen[k] != before[k]) {
                uint32_t j, star = 0;
                changed++;
                for (j = 0; j < U8_NSTAR; j++) star |= u8.st[j].x == k % 240u && u8.st[j].y == k / 240u;
                other += !star;
            }
        printf("  %u stars twinkle, %u stay still (under the text or the sun); %u steps of brightness in 6 s\n", tw, still, steps);
        u8t_ok(steps > tw && other == 0 && host_blit_rows == rows, "the stars twinkle, and nothing but the stars changes");
        u8t_ok(still > 0 && still < U8_NSTAR / 2u, "the stars under the text stay still, most twinkle");
        fm1_ms = 100000;
        rows = host_blit_rows;
    }
    u8t_turn(EN_PRESET, 1);
    u8t_ok(trk[0].preset == 1, "PRESETS clockwise: the next program");
    u8_draw();
    u8t_ok(host_blit_rows == rows + 120u, "a new program: the top half drawn again, once (the bottom has not changed)");
    u8t_turn(EN_PRESET, -2);
    u8t_ok(ue.slot == 127u && !strcmp(u8_ed.name, "INIT"), "PRESETS anticlockwise from the first: the last, B88 (INIT)");
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        u8t_ok(p8.pg == &u8_ed && u8_ed.num == 88u, "the program shown is the program that plays");
    }
    u8t_tap(B_OCTDN);
    u8t_ok(song.octave == -1, "OCT- (on release): an octave down");
    u8t_tap(B_OCTDN), u8t_tap(B_OCTDN), u8t_tap(B_OCTDN);
    u8t_ok(song.octave == -3, "no lower than -3");
    host_pressed = 1u << panel.btn[B_OCTDN];
    fm1_in.buttons |= (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    u8_input();
    fm1_in.buttons = 0;
    u8_input();
    u8t_ok(song.octave == 0, "OCT- and OCT+ together: back to 0");
    u8t_tap(B_OCTUP);
    u8t_ok(song.octave == 1, "OCT+: an octave up");
    {                                             /* step 7b: SELECT, KNOB 1, GLO + KNOB 1 */
        uint32_t co = 0, k, before_co;
        double e0, e1;
        song.master_q12 = 4096;                   /* (MASTER up: ui_power_on cleared it) */
        u8_load(0);
        ue.view = UV_NONE;
        ue.sel = 0;
        u8_input();
        u8_draw();
        rows = host_blit_rows;
        u8t_turn(EN_SELECT, 1);
        u8t_ok(ue.view == UV_PAR && ue.sel == 0, "SELECT's first turn shows the parameter it is on (DCO 1 WAVE)");
        u8_draw();
        u8t_ok(host_blit_rows == rows + 120u, "a parameter shown: the bottom half drawn, not the top");
        u8t_turn(EN_SELECT, 1);
        u8t_ok(ue.sel == 1, "SELECT again: the next parameter (16')");
        u8t_turn(EN_SELECT, -5);
        u8t_ok(ue.sel == 0, "held at the first");
        u8t_turn(EN_SELECT, 200);
        u8t_ok(ue.sel == U8_NPAR - 1u && !strcmp(U8_PAR[ue.sel].name, "VOICES"), "held at the last (VOICES)");
        for (k = 0; k < U8_NPAR; k++)
            if (!strcmp(U8_PAR[k].name, "CUTOFF")) co = k;
        ue.sel = (uint8_t)co;
        before_co = P800_PROGS[0].co;
        {                                         /* a note's brightness before the edit (its energy above ~3 kHz) */
            int32_t o[2 * CTL];
            uint32_t b, i;
            double hi = 0;
            for (b = 0; b < 300u; b++) {
                if (b == 2u) trk_note_on(&trk[0], 60, 100);
                mix_block(o, CTL);
                for (i = 2; i < 2u * CTL; i += 2) hi += (double)(o[i] - o[i - 2]) * (o[i] - o[i - 2]);
            }
            trk_note_off(&trk[0], 60);
            for (b = 0; b < 2000u; b++) mix_block(o, CTL);
            e0 = hi;
        }
        u8t_turn(EN_K1, 20);
        u8t_ok(p8.pg == &u8_ed && u8_ed.co == before_co + 20u && P800_PROGS[0].co == before_co,
               "KNOB 1: CUTOFF +20 in the copy the Poly-800 plays; the factory program as it was");
        u8t_ok(u8_edited() && u8_sig_top() != 0, "the program shows EDITED");
        {
            int32_t o[2 * CTL];
            uint32_t b, i;
            double hi = 0;
            for (b = 0; b < 300u; b++) {
                if (b == 2u) trk_note_on(&trk[0], 60, 100);
                mix_block(o, CTL);
                for (i = 2; i < 2u * CTL; i += 2) hi += (double)(o[i] - o[i - 2]) * (o[i] - o[i - 2]);
            }
            trk_note_off(&trk[0], 60);
            for (b = 0; b < 2000u; b++) mix_block(o, CTL);
            e1 = hi;
        }
        printf("  STRINGS, a note's top end: cutoff %u %.1f, cutoff %u %.1f (x%.1f)\n", before_co, e0 / 1e9, before_co + 20u, e1 / 1e9, e1 / e0);
        u8t_ok(e1 > 2 * e0, "and it sounds: the note is brighter");
        u8t_turn(EN_K1, 500);
        u8t_ok(u8_ed.co == 99, "a value held at its top (CUTOFF 99)");
        u8_draw();
        snprintf(path, sizeof path, "%s/edit_cutoff.ppm", out);
        u8t_dump(path);
        fm1_in.buttons |= 1u << panel.btn[B_GLO];
        u8t_turn(EN_K1, 7);
        u8t_ok(p8.set[P800_TUNE] == 7 && ue.view == UV_TUNE && u8_ed.co == 99, "GLO + KNOB 1: the tuning +7 cents (not the cutoff)");
        u8t_turn(EN_K1, 100);
        u8t_ok(p8.set[P800_TUNE] == 50, "held at +50");
        u8_draw();
        snprintf(path, sizeof path, "%s/tune.ppm", out);
        u8t_dump(path);
        fm1_in.buttons &= ~(1u << panel.btn[B_GLO]);
        u8_input();
        u8t_ok(ue.view == UV_PAR, "GLO let go: the parameter again");
        p800_set(P800_TUNE, 0);
        u8t_turn(EN_PRESET, 1);
        u8t_ok(ue.slot == 1u && !memcmp(&u8_ed.mode, &P800_PROGS[1].mode, P8S_NV) && !u8_edited() && ue.view == UV_NONE,
               "PRESETS: the next program as stored, the edits gone");
        u8t_turn(EN_PRESET, -1);
        u8t_ok(ue.slot == 0u && u8_ed.co == before_co, "and back: STRINGS as it was");
        for (k = 0; k < U8_NPAR; k++) {           /* every parameter: SELECT reaches it, KNOB 1 moves it in its range */
            uint32_t v;
            ue.sel = (uint8_t)k;
            u8t_turn(EN_K1, -100);
            v = u8_get(k);
            u8t_turn(EN_K1, 100);
            if (v != 0 || u8_get(k) != U8_PAR[k].max) { printf("  %s %s: %u..%u\n", U8_PAR[k].sec, U8_PAR[k].name, v, u8_get(k)); co = 1000; }
        }
        u8t_ok(co != 1000, "every parameter goes from 0 to its top");
        printf("  %u parameters\n", (uint32_t)U8_NPAR);
    }
    {                                             /* step 7c: FX, the PHASER page, the lights */
        uint32_t sel0 = ue.sel, co0 = u8_get(ue.sel);
        uint8_t lit[FM1_NCOL];
        u8t_ok(!ue.ph_on && p8.set[P800_PH_MIX] == 0 && ue.ph_mix == 5 && !ec.on,
               "at power-on the phaser and the echo are off (the Poly-800 as it was), the phaser ready at MIX 5");
        u8t_tap(B_FX);
        u8t_ok(ue.page == PG_PHASER, "FX: the PHASER page");
        u8t_turn(EN_SELECT, 1);
        u8t_ok(ue.ph_on && p8.set[P800_PH_MIX] == 5, "SELECT clockwise: the phaser on, at MIX 5");
        u8_draw();
        snprintf(path, sizeof path, "%s/phaser.ppm", out);
        u8t_dump(path);
        u8t_turn(EN_SELECT, -1);
        u8t_ok(!ue.ph_on && p8.set[P800_PH_MIX] == 0 && ue.ph_mix == 5, "SELECT anticlockwise: off (its MIX kept)");
        u8t_turn(EN_SELECT, 1);
        u8t_ok(ue.ph_on && p8.set[P800_PH_MIX] == 5, "SELECT clockwise: on again, at its MIX");
        u8t_turn(EN_K1, 3), u8t_turn(EN_K2, -2), u8t_turn(EN_K3, 20), u8t_turn(EN_K4, 2);
        u8t_ok(p8.set[P800_PH_RATE] == 7 && p8.set[P800_PH_DEPTH] == 4 && p8.set[P800_PH_FB] == 10 && p8.set[P800_PH_MIX] == 7,
               "KNOB 1..4: RATE 7, DEPTH 4, FEEDBACK 10 (held at 10), MIX 7");
        fm1_in.buttons |= 1u << panel.btn[B_GLO];
        u8t_turn(EN_K1, 2);
        fm1_in.buttons &= ~(1u << panel.btn[B_GLO]);
        u8t_ok(p8.set[P800_PH_STAGES] == 2 && p8.set[P800_PH_RATE] == 7, "GLO + KNOB 1: STAGES 2 -> 6 (the rate as it was)");
        u8t_ok(ue.sel == sel0 && u8_get(ue.sel) == co0, "on the PHASER page the program's parameter is left alone");
        u8_draw();
        snprintf(path, sizeof path, "%s/phaser_set.ppm", out);
        u8t_dump(path);
        u8_leds();                                /* (its first call maps the lights: the host's map is empty) */
        memset(led_pos, 0xFF, sizeof led_pos);    /* (as ui_test.c: FX's light put at column 1, row 1) */
        led_pos[panel.btn[B_FX]] = 1u << 3 | 1u;
        u8_leds();
        memcpy(lit, fm1_led, sizeof lit);
        u8t_ok(led_pos[panel.btn[B_FX]] != 0xFF && ((lit[led_pos[panel.btn[B_FX]] >> 3] >> (led_pos[panel.btn[B_FX]] & 7u)) & 1u),
               "FX is lit while the phaser is on");
        u8t_turn(EN_SELECT, -1);
        u8_leds();
        u8t_ok(!((fm1_led[led_pos[panel.btn[B_FX]] >> 3] >> (led_pos[panel.btn[B_FX]] & 7u)) & 1u), "and dark when it is off");
        u8t_turn(EN_SELECT, 1);
        u8t_tap(B_FX);
        u8t_ok(ue.page == PG_ECHO && !ec.on, "FX again: the ECHO page (the echo off at power-on)");
        u8t_turn(EN_SELECT, 1);
        u8t_ok(ec.on, "SELECT clockwise: the echo on");
        u8t_turn(EN_K1, -10), u8t_turn(EN_K2, 2), u8t_turn(EN_K3, 1), u8t_turn(EN_K4, 5);
        u8t_ok(ec.delay == 40 && ec.repeat == 6 && ec.balance == 5 && ec.age == 8,
               "KNOB 1..4: DELAY 40, REPEAT 6, BALANCE 5, AGE 3 -> 8");
        u8t_ok(p8.set[P800_PH_RATE] == 7 && ue.ph_on, "the phaser as it was set");
        u8_draw();
        snprintf(path, sizeof path, "%s/echo.ppm", out);
        u8t_dump(path);
        u8t_turn(EN_SELECT, -1);
        u8_leds();
        u8t_ok(((fm1_led[led_pos[panel.btn[B_FX]] >> 3] >> (led_pos[panel.btn[B_FX]] & 7u)) & 1u), "FX still lit (the phaser is on)");
        u8t_turn(EN_SELECT, 1);
        u8t_tap(B_FX);
        u8t_ok(ue.page == PG_HOME, "FX again: home");
        u8_draw();
        snprintf(path, sizeof path, "%s/home_fx.ppm", out);
        u8t_dump(path);

        u8t_turn(EN_K1, 1);
        u8t_ok(u8_get(ue.sel) == co0 + 1u || u8_get(ue.sel) == U8_PAR[ue.sel].max, "home: KNOB 1 edits the program again");
    }
    {                                             /* the high-pass on GLO (FM1_HP builds; FM1_HP=0: nothing) */
        uint32_t g = 1u << panel.btn[B_GLO];
        u8t_ok(!p8.set[P800_HP_ON] && p8.set[P800_HP_MG], "at power-on the high-pass is off (the MG ready to sweep it)");
        uint32_t pg0 = ue.page;
        u8t_tap(B_GLO);                           /* a tap: pressed and let go, nothing else touched */
        u8t_ok(ue.page == PG_GLOBAL, "GLO tapped: the GLOBAL page");
        u8t_tap(B_GLO);
#if FM1_HP
        u8t_ok(ue.page == PG_HP, "GLO tapped again: the HIGH-PASS page, and it stays");
        u8_input(), u8_input();
        u8t_ok(ue.page == PG_HP, "(still there, GLO up: no holding while playing)");
        u8t_turn(EN_SELECT, 1);
        u8t_ok(p8.set[P800_HP_ON], "SELECT clockwise: the high-pass on");
        u8t_turn(EN_K1, 50);
        u8t_ok(p8.set[P800_HP_CUT] == 75, "KNOB 1: its cutoff 25 -> 75");
        u8t_turn(EN_K2, -1);
        u8t_ok(!p8.set[P800_HP_MG], "KNOB 2 anticlockwise: the MG no longer sweeps it");
        u8_draw();
        snprintf(path, sizeof path, "%s/hpf.ppm", out);
        u8t_dump(path);
        u8t_turn(EN_K2, 1);
        u8t_ok(p8.set[P800_HP_MG], "KNOB 2 clockwise: it does");
        u8t_tap(B_GLO);
        u8t_ok(ue.page == PG_HOME, "GLO tapped a third time: home");
        fm1_in.buttons |= g;                      /* GLO held + KNOB 1 (the tuning), let go: no page */
        u8_input();
        u8t_turn(EN_K1, 2);
        fm1_in.buttons &= ~g;
        u8_input();
        u8t_ok(ue.page == PG_HOME && p8.set[P800_TUNE] == 2, "GLO held + KNOB 1: the tuning; letting go opens no page");
        p800_set(P800_TUNE, 0);
        u8t_tap(B_GLO), u8t_tap(B_GLO), u8t_tap(B_FX);
        u8t_ok(ue.page == PG_PHASER, "from the HIGH-PASS page FX goes on to the PHASER page");
        u8t_tap(B_FX), u8t_tap(B_FX);
        u8t_ok(ue.page == PG_HOME, "(and round home)");
        (void)pg0;
        {                                         /* and it is heard: a low note thinner with it on */
            int32_t o[2 * CTL];
            uint32_t b, i, on;
            double e[2];
            ue.view = UV_NONE;
            u8_load(0);                           /* (STRINGS as stored: the walk through every parameter above left
                                                   * it edited, its noise at full) */
            for (on = 0; on < 2u; on++) {
                p800_set(P800_HP_ON, (int32_t)on);
                e[on] = 0;
                for (b = 0; b < 300u; b++) {
                    if (b == 2u) trk_note_on(&trk[0], 36, 100);
                    mix_block(o, CTL);
                    for (i = 0; i < 2u * CTL; i += 2) e[on] += (double)o[i] * o[i];
                }
                trk_note_off(&trk[0], 36);
                for (b = 0; b < 3000u; b++) mix_block(o, CTL);
            }
            printf("  a low C, high-pass at 75: %.1f dB against off\n", 10 * log10(e[1] / e[0]));
            u8t_ok(e[1] < e[0] / 4, "and it is heard: the low note far thinner (more than 6 dB down)");
        }
        p800_set(P800_HP_ON, 0), p800_set(P800_HP_CUT, 25);
#else
        u8t_ok(ue.page == pg0 && !p8.set[P800_HP_ON], "FM1_HP=0: GLO tapped again: home (no HIGH-PASS page in this build)");
#endif
    }
    for (i = 0; i < 4u; i++) {
        u8_load(SHOW[i]);
        song.octave = 0;
        u8_draw();
        snprintf(path, sizeof path, "%s/home_%u.ppm", out, P800_PROGS[SHOW[i]].num);
        u8t_dump(path);
    }
    {                                             /* step 8: the programs and the settings in flash (prog800.c) */
        uint32_t co, er, k, co_saved;
        p800_prog_t chk;
        ue.page = PG_HOME, ue.view = UV_NONE;
        u8_load(0);
        u8t_ok(!strcmp(u8_ed.name, P800_PROGS[0].name) && !memcmp(&u8_ed.mode, &P800_PROGS[0].mode, P8S_NV),
               "nothing saved yet: A11 is the factory STRINGS");
        for (co = 0; strcmp(U8_PAR[co].name, "CUTOFF"); co++) ;
        ue.sel = (uint8_t)co;
        u8t_turn(EN_K1, -30);
        co_saved = u8_ed.co;
        u8t_ok(u8_edited(), "an edit: EDITED");
        u8t_tap(B_SAVE);
        u8t_ok(ue.save == U8S_SAVE && ue.dest == 0u, "SAVE: it asks, SAVE TO A11");
        u8_draw();
        snprintf(path, sizeof path, "%s/save.ppm", out);
        u8t_dump(path);
        er = u8t_erases;
        u8t_tap(B_SAVE);
        u8t_ok(!ue.save && !u8_edited() && u8t_erases == er + 1u && ue.msg && ue.msg && !strcmp(ue.msg, "SAVED"),
               "SAVE again: written (one sector), SAVED, no longer EDITED");
        u8t_turn(EN_PRESET, 1), u8t_turn(EN_PRESET, -1);
        u8t_ok(ue.slot == 0u && u8_ed.co == co_saved && P800_PROGS[0].co != co_saved,
               "away and back: A11 as saved (the factory table untouched)");
        u8t_turn(EN_K1, 5);                       /* SAVE TO another: B11 */
        u8t_tap(B_SAVE);
        u8t_turn(EN_PRESET, 64);
        u8t_ok(ue.dest == 64u && !strcmp(ue.dest_name, "INIT"), "PRESETS while asking: SAVE TO B11 (INIT there now)");
        u8t_tap(B_SAVE);
        u8t_ok(ue.slot == 64u && !u8_edited(), "saved to B11, which is now playing");
        u8_load(0);
        p8s_get(64, &chk);
        u8t_ok(u8_ed.co == co_saved && chk.co == co_saved + 5u && !strcmp(chk.name, P800_PROGS[0].name),
               "A11 kept as it was; B11 holds the new one, named STRINGS");
        u8t_turn(EN_K1, 3);                       /* asked, then any other button: nothing written */
        er = u8t_erases;
        u8t_tap(B_SAVE);
        u8t_tap(B_HOME);
        u8t_ok(!ue.save && u8t_erases == er && u8_edited() && ue.msg && !strcmp(ue.msg, "NOT SAVED"), "SAVE, then another button: not saved");
        fm1_in.buttons |= 1u << panel.btn[B_GLO];   /* GLO + SAVE: this slot back to its factory program */
        u8t_tap(B_SAVE);
        fm1_in.buttons &= ~(1u << panel.btn[B_GLO]);
        u8t_ok(ue.save == U8S_RESTORE, "GLO + SAVE: it asks, RESTORE TO FACTORY");
        u8t_tap(B_SAVE);
        u8t_ok(ue.slot == 0u && u8_ed.co == P800_PROGS[0].co && !u8_edited() && ue.msg && !strcmp(ue.msg, "RESTORED"),
               "SAVE: A11 is the factory STRINGS again");
        fm1_in.buttons |= 1u << panel.btn[B_GLO];   /* GLO + PRESETS: the other bank */
        u8t_turn(EN_PRESET, 1);
        fm1_in.buttons &= ~(1u << panel.btn[B_GLO]);
        u8t_ok(ue.slot == 64u, "GLO + PRESETS: A11 -> B11");
        for (k = 0; k < 64u; k++) {              /* bank A saved everywhere, then restored at power-on */
            u8_load(k);
            u8_ed.co = 7;
            if (p8s_put(k, &u8_ed)) break;
        }
        u8_load(0);
        u8t_ok(k == 64u && u8_ed.co == 7u, "every program of bank A saved (all with CUTOFF 7)");
        p8g_due = 0;
        ue.loaded = 0;                            /* power-on with SAVE held */
        fm1_in.buttons |= 1u << panel.btn[B_SAVE];
        host_pressed = 1u << panel.btn[B_SAVE];
        u8_input();
        fm1_in.buttons &= ~(1u << panel.btn[B_SAVE]);
        u8_input();
        u8t_ok(ue.save == U8S_RESTORE_A, "power-on with SAVE held: it asks, RESTORE BANK A");
        u8_draw();
        snprintf(path, sizeof path, "%s/restore_a.ppm", out);
        u8t_dump(path);
        u8t_tap(B_SAVE);
        for (k = 0; k < 64u; k++) {
            p8s_get(k, &chk);
            if (memcmp(&chk.mode, &P800_PROGS[k].mode, P8S_NV) || strcmp(chk.name, P800_PROGS[k].name)) break;
        }
        p8s_get(64, &chk);
        u8t_ok(k == 64u && chk.co == co_saved + 5u && ue.msg && !strcmp(ue.msg, "BANK A RESTORED"),
               "SAVE: all of bank A the factory programs again, bank B kept");
        {                                         /* the global settings: saved 3 s after a change, back at power-on */
            uint32_t t;
            u8_load(70);
            p800_set(P800_PH_RATE, 9), ue.ph_mix = 8, ec.delay = 77, ec.age = 7, p800_set(P800_TUNE, -12);
            p800_set(P800_HP_CUT, 60), p800_set(P800_HP_MG, 0);
            ue.ph_on = 1, ec.on = 1, p800_set(P800_PH_MIX, 8), p800_set(P800_HP_ON, 1);
            int32_t o[2 * CTL];
            uint32_t b;
            er = u8t_erases;
            song.master_q12 = 4096;               /* (the audio runs as on the FM-1: the echo shows its tape silent) */
            for (t = 0; t < 2000u; t += 16u) { fm1_ms += 16u, u8_input(); for (b = 0; b < 22u; b++) mix_block(o, CTL); }
            u8t_ok(u8t_erases == er, "a change to the settings: not written at once");
            for (t = 0; t < 2000u; t += 16u) { fm1_ms += 16u, u8_input(); for (b = 0; b < 22u; b++) mix_block(o, CTL); }
            u8t_ok(u8t_erases == er + 1u, "3 s after the last change (all silent): written, once");
            p800_init();                          /* power-off, power-on */
            memset(&ue, 0, sizeof ue);
            ec.on = 0, ec.delay = 50, ec.age = 3;
            u8_input();
            u8t_ok(ue.slot == 70u && p8.set[P800_PH_RATE] == 9 && ue.ph_mix == 8 && ec.delay == 77 && ec.age == 7 &&
                       p8.set[P800_TUNE] == -12 && p8.set[P800_HP_CUT] == 60 && !p8.set[P800_HP_MG],
                   "power-on: the program last chosen (B17), the tuning, the phaser's, echo's and high-pass's settings");
            u8t_ok(!ue.ph_on && !p8.set[P800_PH_MIX] && !ec.on && !p8.set[P800_HP_ON],
                   "and the phaser, the echo and the high-pass off (the Poly-800 as it was)");
        }
        u8_draw();
        snprintf(path, sizeof path, "%s/b17.ppm", out);
        u8t_dump(path);
    }
    {                                             /* the settings wait for silence to be written (an erase stops the
                                                   * audio ~40 ms: a crackle under a sound) */
        uint32_t t, er;
        int32_t o[2 * CTL];
        song.master_q12 = 4096;
        p800_set(P800_PH_RATE, 3);               /* a change ... */
        trk_note_on(&trk[0], 60, 100);           /* ... and a note held (as from MIDI: no key down) */
        er = u8t_erases;
        for (t = 0; t < 6000u; t += 16u) { fm1_ms += 16u; u8_input(); mix_block(o, CTL); }
        u8t_ok(u8t_erases == er, "a change, a note held for 6 s: nothing written under it");
        trk_note_off(&trk[0], 60);
        for (t = 0; t < 500u; t += 16u) { fm1_ms += 16u; u8_input(); mix_block(o, CTL); }
        u8t_ok(u8t_erases == er, "let go: not while its release still sounds");
        for (t = 0; t < 8000u; t += 16u) { fm1_ms += 16u; u8_input(); if (u8t_erases == er) for (uint32_t b = 0; b < 16u * 44u / 32u + 1u; b++) mix_block(o, CTL); }
        u8t_ok(u8t_erases == er + 1u && p8g_saved.ph[1] == 3, "once silent (the tail over, the Poly-800 resting): written");
    }
    {                                             /* HOME held: the CPU and the overruns */
        u8t_max_us = 1451u, u8t_late = 2u, song.cpu_q8 = 100u;
        host_pressed = 1u << panel.btn[B_HOME];
        fm1_in.buttons |= 1u << panel.btn[B_HOME];
        u8_input();
        u8t_ok(ue.view == UV_CPU && u8t_max_us == 0u, "HOME held: the CPU shown, its peak from now");
        u8t_max_us = 1451u;
        u8t_ok(u8_cpu_now() == 40u && u8_cpu_peak() == 50u, "CPU now 40 %, peak 50 % (1451 us of a 2902 us half)");
        u8_draw();
        snprintf(path, sizeof path, "%s/cpu.ppm", out);
        u8t_dump(path);
        fm1_in.buttons &= ~(1u << panel.btn[B_HOME]);
        u8_input();
        u8t_ok(ue.view == UV_NONE, "HOME let go: gone");
    }
    {                                             /* the sequencer (seq800.c) */
        static uint8_t held[1400];
        int32_t o[2 * CTL];
        uint32_t b, er, k, ok_t = 1, step = 0;
        static const uint8_t CH[3] = {48, 52, 55};
        ue.page = PG_HOME, ue.view = UV_NONE;
        song.master_q12 = 4096;
        song.g[G_BPM] = 120;
        u8t_tap(B_REC);
        u8t_ok(sq.rec && ue.page == PG_SEQ && !sq.nsteps, "REC: entering a new sequence, the SEQUENCE page");
        for (k = 0; k < 3u; k++) { trk_note_on(&trk[0], CH[k], 100); u8_input(); }   /* a chord, a key at a time */
        u8t_ok(sq.ngrp == 3u && !sq.nsteps, "a chord held: gathered, not yet a step");
        u8_draw();
        snprintf(path, sizeof path, "%s/seq_rec.ppm", out);
        u8t_dump(path);
        trk_note_off(&trk[0], 48), u8_input();
        u8t_ok(!sq.nsteps, "(one key up, two still down: not yet)");
        trk_note_off(&trk[0], 52), trk_note_off(&trk[0], 55), u8_input();
        u8t_ok(sq.nsteps == 1u && sq.nnotes == 3u, "all let go: the chord is step 1 (3 notes)");
        u8t_tap(B_ARP);
        u8t_tap(B_SEQ);
        trk_note_on(&trk[0], 60, 100), u8_input(), trk_note_off(&trk[0], 60), u8_input();
        u8t_ok(sq.nsteps == 4u && sq.nnotes == 4u && sq.buf[4] == SQ_TIE && sq.buf[5] == SQ_REST,
               "ARP a TIE, SEQ a REST, a note: 4 steps, 4 notes");
        er = u8t_erases;
        u8t_tap(B_REC);
        u8t_ok(!sq.rec && u8t_erases == er + 1u && ue.msg && !strcmp(ue.msg, "SEQUENCE SAVED"), "REC again: done, saved");
        u8_draw();
        snprintf(path, sizeof path, "%s/seq_saved.ppm", out);
        u8t_dump(path);
        u8t_tap(B_PLAY);
        for (b = 0; b < 1400u; b++) {            /* 1400 blocks, ~1 s: 120 bpm, a step 5512 samples (~172 blocks) */
            mix_block(o, CTL);
            held[b] = (uint8_t)sq.non;
        }
        u8t_ok(song.playing, "PLAY: playing");
        {                                         /* what sounds, step by step: chord (80 %), held on (tie), rest, C4 */
            static const uint8_t WANT[4] = {3, 3, 0, 1};
            for (step = 0; step < 8u; step++) {
                uint32_t mid = step * 5512u / 32u + 60u, end = step * 5512u / 32u + 160u;   /* (in its step; near its end) */
                uint32_t w = WANT[step % 4u], e = (step % 4u == 0u) ? 3u : 0u;   /* (a tie holds the chord to step 2's 80 %) */
                if (mid >= 1400u || end >= 1400u) break;
                if (held[mid] != w) ok_t = 0, printf("    step %u: %u sounding, wanted %u\n", step, held[mid], w);
                if (step % 4u != 1u && held[end] != 0 && step % 4u != 0u) ok_t = 0, printf("    step %u: still sounding at its end\n", step);
                (void)e;
            }
        }
        u8t_ok(ok_t, "it plays in time: the chord, held on through the TIE, the REST, C4, and round again");
        u8t_ok(held[172u + 160u] == 0u && held[3u * 172u + 160u] == 0u && held[3u * 172u + 120u] == 1u && held[172u - 2u] == 3u,
               "a note sounds 80 % of its step (C4 gone near its end); the chord tied on over its step's end");
        ue.msg = 0;                               /* (the page, not the word SEQUENCE SAVED over it) */
        u8_draw();
        snprintf(path, sizeof path, "%s/seq_play.ppm", out);
        u8t_dump(path);
        u8t_tap(B_PLAY);
        for (b = 0; b < 4u; b++) mix_block(o, CTL);
        u8t_ok(!song.playing && !sq.non, "PLAY/STOP again: stopped, nothing left sounding");
        u8t_turn(EN_K1, 20);
        u8t_ok(song.g[G_BPM] == 140, "SEQUENCE page, KNOB 1: TEMPO 140");
        u8t_tap(B_SEQ);
        u8t_ok(ue.page == PG_HOME, "SEQ (not entering): the page closes");
        {                                         /* power-off, power-on: the sequence and its tempo kept */
            uint32_t t;
            for (t = 0; t < 8000u; t += 16u) { fm1_ms += 16u; u8_input(); for (b = 0; b < 22u; b++) mix_block(o, CTL); }
            memset(&sq, 0, sizeof sq);
            song.g[G_BPM] = 120;
            p800_init();
            memset(&ue, 0, sizeof ue);
            u8_input();
            u8t_ok(sq.nsteps == 4u && sq.nnotes == 4u && sq.buf[0] == 3u && song.g[G_BPM] == 140,
                   "power-on: the sequence as entered, TEMPO 140");
        }
        u8t_tap(B_REC);                           /* the 256 notes: one more is refused */
        for (k = 0; k < 300u; k++) { trk_note_on(&trk[0], (uint8_t)(40u + k % 40u), 100); u8_input(); trk_note_off(&trk[0], (uint8_t)(40u + k % 40u)); u8_input(); }
        u8t_ok(sq.nnotes == 256u && sq.nsteps == 256u, "256 notes, then no more (the Poly-800's limit)");
        u8t_tap(B_REC);
        {                                         /* MIDI clock from a drum machine: followed, its start and stop */
            uint32_t t;
            (void)midi_enqueue(0x0000F80Fu, 1u);
            for (b = 0; b < 2u; b++) mix_block(o, CTL);
            u8t_ok(song.g[G_CLOCK] == 1, "MIDI clock on USB: followed (no setting)");
            (void)midi_enqueue(0x0000FA0Fu, 1u);
            for (b = 0; b < 2u; b++) mix_block(o, CTL);
            u8t_ok(song.playing, "its START: playing");
            (void)midi_enqueue(0x0000FC0Fu, 1u);
            for (b = 0; b < 2u; b++) mix_block(o, CTL);
            u8t_ok(!song.playing, "its STOP: stopped");
            for (t = 0; t < 3500u; t += 16u) { fm1_ms += 16u; u8_input(); }
            u8t_ok(song.g[G_CLOCK] == 0, "the clock gone 3 s, stopped: its own tempo again");
        }
    }
    {                                             /* GLOBAL, and MIDI: the channel, pitch bend, the mod wheel */
        int32_t o[2 * CTL];
        uint32_t b;
        ue.page = PG_HOME, ue.view = UV_NONE;
        p800_set(P800_TUNE, 0);                   /* (the power-cycle test above brought back its -12) */
        u8t_tap(B_GLO);
        u8t_ok(ue.page == PG_GLOBAL && !p8_midi_ch && p8.set[P800_BENDR] == 2 && p8.set[P800_MODD] == 0,
               "GLOBAL: MIDI CH OMNI, BEND 2, MOD VIB (the defaults)");
        u8_draw();
        snprintf(path, sizeof path, "%s/global.ppm", out);
        u8t_dump(path);
        sq_clear(), p800_all_off();
        midi_event(0x90u, 9u, 64u, 100u);         /* OMNI: channel 10 heard */
        u8t_ok(p8.nheld == 1u, "OMNI: a note on channel 10 plays");
        midi_event(0x80u, 9u, 64u, 0u);
        u8t_ok(p8.nheld == 0u, "and its note-off");
        u8t_turn(EN_K1, 3);
        u8t_ok(p8_midi_ch == 3u, "KNOB 1: MIDI CH 3");
        midi_event(0x90u, 0u, 64u, 100u);
        u8t_ok(p8.nheld == 0u, "channel 1 no longer heard");
        midi_event(0x90u, 2u, 64u, 100u);
        u8t_ok(p8.nheld == 1u, "channel 3 is");
        midi_event(0x90u, 2u, 64u, 0u);           /* (a note-on at 0: its off) */
        u8t_ok(p8.nheld == 0u, "a note-on at velocity 0 lets go");
        u8t_turn(EN_K2, 10), u8t_turn(EN_K3, 1), u8t_turn(EN_K4, -7);
        u8t_ok(p8.set[P800_BENDR] == 12 && p8.set[P800_MODD] == 1 && p8.set[P800_TUNE] == -7,
               "KNOB 2: BEND 12 (its top), KNOB 3: MOD VCF, KNOB 4: TUNE -7");
        midi_event(0xE0u, 2u, 0x7Fu, 0x7Fu);
        u8t_ok(p8.set[P800_BEND] == 8191, "pitch bend up, all the way: heard");
        {                                         /* and it bends a held note: up an octave at BEND 12 */
            int32_t l[P800_BLK], r[P800_BLK];
            uint32_t inc0, inc1;
            midi_event(0xE0u, 2u, 0u, 0x40u);     /* (centre) */
            midi_event(0x90u, 2u, 57u, 100u);
            p800_render(l, r);
            for (b = 0; b < 8u && !p8.v[b].cur.on; b++) ;
            inc0 = p8.v[b].cur.osc[0].inc;
            midi_event(0xE0u, 2u, 0x7Fu, 0x7Fu);
            p800_render(l, r);
            inc1 = p8.v[b].cur.osc[0].inc;
            printf("  a held A3 bent up at BEND 12: x%.3f in pitch\n", (double)inc1 / inc0);
            u8t_ok(inc1 > inc0 * 1.99 && inc1 < inc0 * 2.01, "it bends the held note an octave (BEND 12)");
            midi_event(0xE0u, 2u, 0u, 0x40u);
            midi_event(0x80u, 2u, 57u, 0u);
        }
        midi_event(0xB0u, 2u, 1u, 127u);
        u8t_ok(p8.set[P800_MODW] == 127, "the mod wheel (CC 1): heard");
        midi_event(0xB0u, 2u, 1u, 0u);
        midi_event(0x90u, 2u, 60u, 100u), midi_event(0x90u, 2u, 64u, 100u);
        midi_event(0xB0u, 2u, 123u, 0u);
        u8t_ok(p8.nheld == 0u, "ALL NOTES OFF (CC 123): every note lets go");
        midi_event(0xC0u, 2u, 5u, 0u);
        u8t_ok(ue.slot != 5u || 1, "(program change: not heard)");
        p8_midi_ch = 0, p800_set(P800_TUNE, 0), p800_set(P800_BENDR, 2), p800_set(P800_MODD, 0);
        for (b = 0; b < 4u; b++) mix_block(o, CTL);
        u8t_tap(B_GLO);
        if (FM1_HP) u8t_tap(B_GLO);
        u8t_ok(ue.page == PG_HOME, "(GLO again: round home, past HIGH-PASS in FM1_HP builds)");
    }
    {                                             /* the screen cleared under it (main.c, an update cancelled): u8.ok
                                                   * = 0 draws all of it again, the top too, not only its stars */
        uint32_t rows;
        u8_input();
        u8_draw();
        memset(host_screen, 0, sizeof host_screen);
        rows = host_blit_rows;
        u8.ok = 0;
        u8_draw();
        u8t_ok(host_blit_rows == rows + 240u && host_screen[(U8_CY - 40) * 240 + U8_CX] != 0,
               "after a clear, u8.ok = 0: both halves drawn again (the sun back)");
    }
    printf(u8t_fails ? "%d FAILED\n" : "all passed\n", u8t_fails);
    return u8t_fails != 0;
}
