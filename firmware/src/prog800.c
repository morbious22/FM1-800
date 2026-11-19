/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 morbious22 (FM1-800) */
/* FM1-800 step 8: the programs and the global settings in flash (storage.c; the FM-1's build only).
 *
 * 128 program slots: bank A, 11..88, the 64 factory programs, each one rewritable; bank B, 11..88, 64 more, INIT until
 * something is saved there. A slot never saved holds its factory program (A) or INIT (B), so nothing has to be
 * written before the first SAVE, and a factory restore is the factory programs saved again (bank B is never touched).
 * Each 32 slots are one storage object (OBJ_P8A0, A1, B0, B1: 32 records of 72 bytes, 2304 B; the name in full, then
 * the program's 47 settings as p800_prog_t holds them from `mode` on). Reading a program reads its object (main loop
 * only: st_buf); writing one rewrites its object, its records first gathered in p8s_bank.
 *
 * The global settings (tuning, the phaser's and the echo's settings, the high-pass's cutoff and MG sweep, the program
 * last chosen) are one small object (OBJ_P8GLOB), saved by themselves 3 s after the last change. The phaser, the echo
 * and the high-pass always start off: the Poly-800 as it was; their settings wait for them. */

#if !FELUCCA_FLASH
#error "FM1-800's programs need the flash (FELUCCA_FLASH)"
#endif

#define P8S_NV (sizeof(p800_prog_t) - __builtin_offsetof(p800_prog_t, mode))   /* the settings after the name */
typedef struct {
    char name[24];
    uint8_t v[P8S_NV];
    uint8_t mark;                                /* P8S_MARK: a record that holds a program */
} p8s_rec_t;
#define P8S_MARK 0xA5u
#define P8S_PER 32u                              /* records an object */
#define P8S_NSLOT 128u
_Static_assert(sizeof(p8s_rec_t) == 72u, "a stored program is 72 bytes");
_Static_assert(P8S_PER * sizeof(p8s_rec_t) <= ST_PAYLOAD_MAX, "32 programs fit one storage object");
_Static_assert(sizeof rev_comb >= P8S_PER * sizeof(p8s_rec_t), "the bank's scratch fits Felucca's reverb buffer");

/* a bank's 32 records while one is written: Felucca's reverb comb buffer (fx.c, pool RAM), which the FM-1's build
 * never runs (fx.c mix_block: the Poly-800 alone) */
#define p8s_bank ((p8s_rec_t *)(void *)rev_comb)

/* INIT: one DCO, a saw at 8', the filter open, the envelopes an organ's gate, no MG, no chorus */
static const p800_prog_t P8S_INIT = {
    0, "INIT", 0,
    {{1, 1, {0, 1, 0, 0}, 31}, {1, 1, {0, 1, 0, 0}, 31}},
    0, 0, 0,
    99, 0, 0, 0, 0, 1,
    0,
    {{0, 0, 31, 0, 31, 4}, {0, 0, 31, 0, 31, 4}, {0, 0, 31, 0, 31, 4}},
    {8, 0, 0, 0},
};

static uint32_t p8s_num(uint32_t slot) { return (slot % 64u / 8u + 1u) * 10u + slot % 8u + 1u; }   /* 11..88 */
static uint32_t p8s_obj(uint32_t slot) { return OBJ_P8A0 + slot / P8S_PER; }

/* the factory program of a slot: bank A's own, bank B's INIT */
static void p8s_factory(uint32_t slot, p800_prog_t *g)
{
    *g = slot < 64u ? P800_PROGS[slot] : P8S_INIT;
    g->num = (uint8_t)p8s_num(slot);
}

static void p8s_pack(p8s_rec_t *r, const p800_prog_t *g)
{
    memset(r, 0, sizeof *r);
    str_cpy(r->name, g->name, sizeof r->name);
    memcpy(r->v, &g->mode, P8S_NV);
    r->mark = P8S_MARK;
}

/* slot's program into g: as stored, or its factory program */
static void p8s_get(uint32_t slot, p800_prog_t *g)
{
    const p8s_rec_t *r = (const p8s_rec_t *)(const void *)st_buf;
    st_hdr_t h;
    slot %= P8S_NSLOT;
    p8s_factory(slot, g);
    if (st_current(p8s_obj(slot), &h) < 0 || h.len != P8S_PER * sizeof(p8s_rec_t))
        return;
    r += slot % P8S_PER;
    if (r->mark != P8S_MARK)
        return;
    memcpy(&g->mode, r->v, P8S_NV);
    str_cpy(g->name, r->name, sizeof g->name);
}

/* g into slot (all 32 of its object written again: those not stored yet as their factory programs); 0 when done */
static int p8s_put(uint32_t slot, const p800_prog_t *g)
{
    uint32_t base, k;
    st_hdr_t h;
    slot %= P8S_NSLOT;
    base = slot / P8S_PER * P8S_PER;
    if (st_current(p8s_obj(slot), &h) >= 0 && h.len == P8S_PER * sizeof(p8s_rec_t))
        memcpy(p8s_bank, st_buf, P8S_PER * sizeof(p8s_rec_t));
    else
        for (k = 0; k < P8S_PER; k++) {
            p800_prog_t f;
            p8s_factory(base + k, &f);
            p8s_pack(&p8s_bank[k], &f);
        }
    p8s_pack(&p8s_bank[slot - base], g);
    return st_save(p8s_obj(slot), p8s_bank, P8S_PER * sizeof(p8s_rec_t));
}

/* bank A as it came from the factory (bank B untouched); 0 when done */
static int p8s_restore_a(void)
{
    uint32_t o, k;
    int rc = 0;
    for (o = 0; o < 2u; o++) {
        for (k = 0; k < P8S_PER; k++) {
            p800_prog_t f;
            p8s_factory(o * P8S_PER + k, &f);
            p8s_pack(&p8s_bank[k], &f);
        }
        rc |= st_save(OBJ_P8A0 + o, p8s_bank, P8S_PER * sizeof(p8s_rec_t));
    }
    return rc;
}

/* ------------------------------------------------------------------ the global settings --- */
#define P8G_MAGIC 0x31473850u                    /* "P8G1" */
typedef struct {
    uint32_t magic;
    uint8_t slot;                                /* the program last chosen */
    int8_t tune;                                 /* -50 .. +50 cents */
    uint8_t ph[5];                               /* the phaser: STAGES, RATE, DEPTH, FEEDBACK, MIX */
    uint8_t ec[4];                               /* the echo: DELAY, REPEAT, BALANCE, INPUT */
    uint8_t hp_cut, hp_mg;                       /* the high-pass: CUTOFF, the MG sweeps it */
    uint8_t rsv[3];
} p8g_t;
_Static_assert(sizeof(p8g_t) == 20u, "the global settings' layout");
static p8g_t p8g_saved;                          /* as in flash (or as at power-on, none there) */
static uint32_t p8g_due;                         /* fm1_ms to save at (0: nothing to save) */

/* the settings now (ui800.c ue, echo800.c ec, p800.c p8.set) */
static void p8g_now(p8g_t *g)
{
    memset(g, 0, sizeof *g);
    g->magic = P8G_MAGIC;
    g->slot = ue.slot;
    g->tune = (int8_t)p8.set[P800_TUNE];
    g->ph[0] = (uint8_t)p8.set[P800_PH_STAGES], g->ph[1] = (uint8_t)p8.set[P800_PH_RATE];
    g->ph[2] = (uint8_t)p8.set[P800_PH_DEPTH], g->ph[3] = (uint8_t)p8.set[P800_PH_FB], g->ph[4] = ue.ph_mix;
    g->ec[0] = ec.delay, g->ec[1] = ec.repeat, g->ec[2] = ec.balance, g->ec[3] = ec.drive;
    g->hp_cut = (uint8_t)p8.set[P800_HP_CUT], g->hp_mg = (uint8_t)p8.set[P800_HP_MG];
}

/* at power-on: the settings as last saved (each held to its range), the program last chosen; none: as they are */
static uint32_t p8g_boot(void)
{
    p8g_t g;
    p8g_now(&p8g_saved);
    if (st_load(OBJ_P8GLOB, &g, sizeof g) != (int)sizeof g || g.magic != P8G_MAGIC)
        return 0;
    p800_set(P800_TUNE, g.tune < -50 ? -50 : g.tune > 50 ? 50 : g.tune);
    p800_set(P800_PH_STAGES, g.ph[0] > 5u ? 5 : g.ph[0]);
    p800_set(P800_PH_RATE, g.ph[1] > 10u ? 10 : g.ph[1]);
    p800_set(P800_PH_DEPTH, g.ph[2] > 10u ? 10 : g.ph[2]);
    p800_set(P800_PH_FB, g.ph[3] > 10u ? 10 : g.ph[3]);
    ue.ph_mix = (uint8_t)(g.ph[4] > 10u ? 10u : g.ph[4]);
    ec.delay = (uint8_t)(g.ec[0] > 99u ? 99u : g.ec[0]), ec.repeat = (uint8_t)(g.ec[1] > 10u ? 10u : g.ec[1]);
    ec.balance = (uint8_t)(g.ec[2] > 10u ? 10u : g.ec[2]), ec.drive = (uint8_t)(g.ec[3] > 2u ? 2u : g.ec[3]);
    p800_set(P800_HP_CUT, g.hp_cut > 99u ? 99 : g.hp_cut);
    p800_set(P800_HP_MG, g.hp_mg != 0);
    p8g_now(&p8g_saved);
    p8g_saved.slot = g.slot;
    return g.slot % P8S_NSLOT;
}

/* silence, as a flash erase needs (it stops the audio ~40 ms: heard as a crackle, the lights dim): no key held, no
 * note held (keys or MIDI), the Poly-800 silent and resting (its tails, its noise, eng_p800.c's last second), the
 * echo's tape quiet */
static int p8g_quiet(void)
{
    return !fm1_in.notes && !fm1_in.buttons && !p8.nheld && !p800_busy() && !p8e_live && ec.quiet > EC_LEN;
}

/* once a frame: a change to the settings is saved 3 s after the last one (so turning a knob writes once), and then
 * only in silence: never under a sound */
static void p8g_poll(void)
{
    p8g_t g;
    p8g_now(&g);
    if (!memcmp(&g, &p8g_saved, sizeof g)) {
        p8g_due = 0;
        return;
    }
    if (!p8g_due) {
        p8g_due = fm1_ms + 3000u;
        return;
    }
    if ((int32_t)(fm1_ms - p8g_due) < 0 || !p8g_quiet())
        return;
    p8g_due = 0;
    if (!st_save(OBJ_P8GLOB, &g, sizeof g))
        p8g_saved = g;
}
