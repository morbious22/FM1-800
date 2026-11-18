/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 7a: the Poly-800's home screen and controls (firmware/src/ui800.c) on tests/ui_test.c's stubs.
 *   - the screen: the scene (sky, sun, grid where they belong), the program's number and name on it, drawn only
 *     when something on it changes
 *   - PRESETS: the next / previous program, round the 64; the program that plays follows (eng_p800.c)
 *   - OCT- / OCT+: an octave on release, -3 .. +3, both together back to 0
 * Writes OUT/home_<n>.ppm for a few programs (to look at).
 *   cc -O1 -w -DFM1_800=1 [the include paths] -Itests -o ui800_test tests/poly800/ui800_test.c -lm; ./ui800_test OUT */
#define UI_TEST_MAIN ui_test_main
#include "ui_test.c"
#include "../firmware/src/ui800.c"

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
    p800_init();
    trk[0].engine = trk[0].eng_req = ENGI_P800;
    trk[0].preset = 0;
    fm1_ms = 100000;
    u8_input();
    u8.boot_ms = 1;                               /* (long after power-on: no version shown) */
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
    u8t_ok(trk[0].preset == P800_NPROG - 1u, "PRESETS anticlockwise from the first: the last (round the 64)");
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        u8t_ok(p8.pg == &P800_PROGS[P800_NPROG - 1u], "the program shown is the program that plays");
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
        trk[0].preset = 0;
        song.master_q12 = 4096;                   /* (MASTER up: ui_power_on cleared it) */
        p800_program(0);
        p8e_prog = 0;
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
        u8t_ok(p8.pg == &P800_PROGS[1] && !u8_edited() && ue.view == UV_NONE, "PRESETS: the next program as stored, the edits gone");
        u8t_turn(EN_PRESET, -1);
        u8t_ok(p8.pg == &P800_PROGS[0] && P800_PROGS[0].co == before_co, "and back: STRINGS as it was");
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
        u8t_ok(ec.delay == 40 && ec.repeat == 6 && ec.balance == 5 && ec.drive == 2,
               "KNOB 1..4: DELAY 40, REPEAT 6, BALANCE 5, INPUT 40 dB (held at its last)");
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
        fm1_in.buttons |= g;
        u8_input();
        u8t_tap(B_SCL);                           /* (u8t_tap leaves GLO held: it only lets go of SEL) */
#if FM1_HP
        u8t_ok(p8.set[P800_HP_ON] && ue.view == UV_HP, "GLO + SEL: the high-pass on, shown");
        u8t_turn(EN_K2, 50);
        u8t_ok(p8.set[P800_HP_CUT] == 75, "GLO + KNOB 2: its cutoff 25 -> 75");
        u8t_tap(B_LFO);
        u8t_ok(!p8.set[P800_HP_MG], "GLO + LFO: the MG no longer sweeps it");
        u8_draw();
        snprintf(path, sizeof path, "%s/hpf.ppm", out);
        u8t_dump(path);
        u8t_tap(B_LFO);
        u8t_ok(p8.set[P800_HP_MG], "GLO + LFO again: it does");
        {                                         /* and it is heard: a low note thinner with it on */
            int32_t o[2 * CTL];
            uint32_t b, i, on;
            double e[2];
            fm1_in.buttons &= ~g;
            u8_input();
            ue.view = UV_NONE;
            trk[0].preset = 0;                    /* (STRINGS as stored: the walk through every parameter above left
                                                   * it edited, its noise at full) */
            p800_program(0);
            p8e_prog = 0;
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
        u8t_ok(!p8.set[P800_HP_ON], "FM1_HP=0: GLO + SEL does nothing (no high-pass in this build)");
        fm1_in.buttons &= ~g;
        u8_input();
#endif
    }
    for (i = 0; i < 4u; i++) {
        trk[0].preset = SHOW[i];
        song.octave = 0;
        u8_draw();
        snprintf(path, sizeof path, "%s/home_%u.ppm", out, P800_PROGS[SHOW[i]].num);
        u8t_dump(path);
    }
    printf(u8t_fails ? "%d FAILED\n" : "all passed\n", u8t_fails);
    return u8t_fails != 0;
}
