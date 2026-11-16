/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 4: play the firmware's Poly-800 sound (firmware/src/p800.c) on the computer, the same phrases at the
 * same sample times as tools/poly800_ref/render.js plays the web page, and write the same files, so
 * tools/poly800_ref/measure.py measures both alike and compare.py compares them.
 *
 *   cc -O2 -Ifirmware/src -o host tests/poly800/host.c
 *   ./host OUT_DIR page|dry|phaser          -> OUT_DIR/<set>/<number>_<phrase>.wav + index.json
 *   ./host pitch                            every key, octave, interval, detune and tune against the page's formula:
 *                                           the largest error in cents (exit 1 over 1 cent) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define P800_NOINLINE
#include "p800.c"

#define Q 128u                                   /* the page's events fall on Web Audio's 128-sample blocks */
#define T0 (8u * Q)
typedef struct { uint32_t t; int on; uint8_t note; } ev_t;
typedef struct { const char *name; uint32_t len; ev_t ev[16]; uint32_t nev; } phrase_t;
static phrase_t PH[3];

static void phrases(void)
{
    static const uint8_t CH[4] = {48, 55, 60, 64}, RUN[8] = {60, 62, 64, 65, 67, 69, 71, 72};
    uint32_t i;
    phrase_t *p = &PH[0];
    p->name = "chord", p->len = 1552u * Q;
    for (i = 0; i < 4u; i++) p->ev[p->nev++] = (ev_t){T0, 1, CH[i]};
    for (i = 0; i < 4u; i++) p->ev[p->nev++] = (ev_t){T0 + 854u * Q, 0, CH[i]};
    p = &PH[1];
    p->name = "note", p->len = 2756u * Q;
    p->ev[p->nev++] = (ev_t){T0, 1, 60};
    p->ev[p->nev++] = (ev_t){T0 + 1026u * Q, 0, 60};
    p = &PH[2];
    p->name = "run", p->len = 862u * Q;
    for (i = 0; i < 8u; i++) {                   /* each note's release, then the next note, at one time */
        p->ev[p->nev++] = (ev_t){T0 + i * 52u * Q, 1, RUN[i]};
        p->ev[p->nev++] = (ev_t){T0 + (i + 1u) * 52u * Q, 0, RUN[i]};
    }
}

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f), fputc(v >> 8 & 255, f), fputc(v >> 16 & 255, f), fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v & 255, f), fputc(v >> 8 & 255, f); }

static void render(const char *path, uint32_t prog, const phrase_t *p, const int16_t *set)
{
    float *buf = malloc(sizeof(float) * 2u * p->len);
    uint32_t n, i, k;
    int32_t l[P800_BLK], r[P800_BLK];
    FILE *f;
    p800_init();
    for (i = 0; i < P800_NSET; i++) p800_set(i, set[i]);
    for (i = 0; i < P800_NPROG; i++)
        if (P800_PROGS[i].num == prog) p800_program(i);
    for (n = 0; n < p->len; n += P800_BLK) {
        for (k = 0; k < p->nev; k++)             /* releases first, then new notes (as render.js) */
            if (p->ev[k].t == n && !p->ev[k].on) p800_note_off(p->ev[k].note);
        for (k = 0; k < p->nev; k++)
            if (p->ev[k].t == n && p->ev[k].on) p800_note_on(p->ev[k].note);
        p800_render(l, r);
        for (i = 0; i < P800_BLK; i++) {
            buf[2u * (n + i)] = l[i] / 65536.0f;
            buf[2u * (n + i) + 1u] = r[i] / 65536.0f;
        }
    }
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f), put32(f, 36u + 8u * p->len), fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16), put16(f, 3), put16(f, 2), put32(f, P800_FS), put32(f, P800_FS * 8u), put16(f, 8), put16(f, 32);
    fwrite("data", 1, 4, f), put32(f, 8u * p->len);
    fwrite(buf, sizeof(float), 2u * p->len, f);
    fclose(f);
    free(buf);
}

/* page: base=440*2^((m-69+(d==2?iv:0))/12)*[.5,1,2][o]; o.detune = tune + (d==2? dt*20/3 : 0); footage x .5 1 2 4 */
static int pitch(void)
{
    double worst = 0;
    int note, oct, iv, dt, tune, f;
    for (note = 17; note <= 108; note++)
        for (oct = 0; oct < 3; oct++)
            for (iv = 0; iv <= 12; iv++)
                for (dt = 0; dt < 4; dt++)
                    for (tune = -50; tune <= 50; tune += 25) {
                        double hz = 440.0 * pow(2, (note - 69 + iv) / 12.0) * (0.5 * (1 << oct)) * pow(2, (tune + dt * 20.0 / 3) / 1200);
                        int32_t p12 = 12 * (100 * note + 100 * iv + 1200 * (oct - 1) - 1200 + tune) + dt * 80;
                        uint32_t inc = inc_of(p12);
                        for (f = 0; f < 4; f++) {           /* 16' .. 2' */
                            double want = hz * 0.5 * (1 << f), got = (double)inc * (1u << f) * P800_FS / 4294967296.0;
                            double err = fabs(1200 * log2(got / want));
                            if (want >= P800_FS / 2) continue;   /* silent, as Chrome */
                            if (err > worst) worst = err;
                        }
                    }
    printf("pitch: largest error %.4f cents (keys 17..108, octaves, intervals 0..12, detunes 0..3, tune +-50)\n", worst);
    return worst > 1.0;
}

int main(int argc, char **argv)
{
    int16_t set[P800_NSET];
    char dir[512], path[600];
    uint32_t i, j, v, nv = 1;
    const char *vname[3] = {""};
    static const uint8_t PHPROG[4] = {11, 17, 15, 33};
    FILE *idx;
    int first = 1;
    (void)p800_busy;                              /* (Felucca's side asks it: tests/regress.c) */
    if (argc == 2 && !strcmp(argv[1], "pitch")) return pitch();
    if (argc != 3 || (strcmp(argv[2], "page") && strcmp(argv[2], "dry") && strcmp(argv[2], "phaser"))) {
        fprintf(stderr, "usage: host OUT_DIR page|dry|phaser\n");
        return 2;
    }
    phrases();
    snprintf(dir, sizeof dir, "%s/%s", argv[1], argv[2]);
    mkdir(argv[1], 0777), mkdir(dir, 0777);
    snprintf(path, sizeof path, "%s/index.json", dir);
    idx = fopen(path, "w");
    fprintf(idx, "{\"set\": \"%s\", \"renders\": [\n", argv[2]);
    if (!strcmp(argv[2], "phaser")) nv = 3, vname[0] = "fb0", vname[1] = "fb10", vname[2] = "off";
    for (i = 0; i < P800_NPROG; i++) {
        const p800_prog_t *g = &P800_PROGS[i];
        int phaser = !strcmp(argv[2], "phaser");
        if (phaser && g->num != PHPROG[0] && g->num != PHPROG[1] && g->num != PHPROG[2] && g->num != PHPROG[3]) continue;
        for (v = 0; v < nv; v++) {
            memcpy(set, P800_SET_DEF, sizeof set);
            if (strcmp(argv[2], "page")) set[P800_AGE] = 0, set[P800_PH_MIX] = 0;   /* dry: Age 0, phaser mix 0 */
            if (phaser) {
                set[P800_PH_MIX] = 10;
                if (v == 0) set[P800_PH_FB] = 0;
                if (v == 1) set[P800_PH_FB] = 10;
                if (v == 2) set[P800_PH_MIX] = 0;
            }
            for (j = 0; j < 3u; j++) {
                char file[64];
                snprintf(file, sizeof file, "%u%s%s_%s.wav", g->num, *vname[v] ? "_" : "", vname[v], PH[j].name);
                snprintf(path, sizeof path, "%s/%s", dir, file);
                render(path, g->num, &PH[j], set);
                fprintf(idx, "%s {\"prog\": %u, \"name\": \"%s\", \"phrase\": \"%s\", \"vary\": \"%s\", \"file\": \"%s\"}",
                        first ? "" : ",\n", g->num, g->name, PH[j].name, vname[v], file);
                first = 0;
            }
        }
    }
    fprintf(idx, "\n]}\n");
    fclose(idx);
    return 0;
}
