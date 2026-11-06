/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* The project in flash: the head, then for each pattern a 64-bit mask of the steps that hold something and those
 * steps, written into the data area from RELAY_PROJ_LO (app.h). The 16-byte record header goes last, so a save cut
 * short is never read back as a project. A full project of 16 x 8 x 64 steps would be 132 KiB; the SMK-37's window is
 * 104 KiB, so a save that does not fit says so and writes nothing. */

#define PH_MAGIC 0x50594C52u                     /* "RLYP" */
typedef struct {
    uint32_t magic, len, crc, rsv;               /* len: the bytes after this header */
} ph_rec_t;

static uint32_t crc32_step(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}

static void step_clear(step_t *s, uint32_t vel)
{
    uint32_t i;
    for (i = 0; i < NNOTE; i++)
        s->note[i] = NOTE_NONE;
    s->vel = (uint8_t)(vel ? vel : 100u);
    s->len = 3;
    s->micro = 0;
    s->chance = 100;
    s->cond = CO_ALWAYS;
    s->rat = 0;
    s->flags = 0;
    s->rsv = 0;
    for (i = 0; i < NLANE; i++)
        s->lock[i] = LOCK_NONE;
}

static int step_used(const step_t *s)
{
    uint32_t i;
    if (s->note[0] != NOTE_NONE)
        return 1;
    for (i = 0; i < NLANE; i++)
        if (s->lock[i] != LOCK_NONE)
            return 1;
    return 0;
}

static uint64_t pat_mask(uint32_t t, uint32_t p)
{
    uint64_t m = 0;
    uint32_t s;
    for (s = 0; s < NSTEP; s++)
        if (step_used(STEP(t, p, s)))
            m |= (uint64_t)1 << s;
    return m;
}

static uint32_t popcount64(uint64_t m)
{
    uint32_t n = 0;
    while (m) {
        m &= m - 1u;
        n++;
    }
    return n;
}

static void track_defaults(uint32_t i)
{
    track_t *t = &P.trk[i];
    uint32_t p, k;
    profile_apply(t, i == 0 ? PR_LOOM_KIT : i < 8u ? PR_LOOM_SYNTH : PR_GENERIC);
    t->chan = (uint8_t)i;
    t->ports = PO_USB | PO_BLE | PO_BLEDEV | (PLAT_SMK37 ? PO_TRS : 0u);
    t->transpose = 0;
    t->quant = 0;
    t->prog = PROG_NONE;
    t->defvel = 100;
    for (k = 0; k < NNOTE; k++)
        t->rem[k] = NOTE_NONE;
    t->rem[0] = 60;
    t->lane_sel = 0;
    t->muted = 0;
    for (p = 0; p < NPAT; p++) {
        P.ph[i][p].len = 16;
        P.ph[i][p].div = DIV_16;
        P.ph[i][p].dir = DIR_FWD;
        P.ph[i][p].swing = 50;
    }
}

static void project_new(void)
{
    uint32_t t, p, s;
    uint8_t *b = (uint8_t *)&P;
    for (s = 0; s < sizeof P; s++)
        b[s] = 0;
    P.magic = PROJ_MAGIC;
    P.version = PROJ_VERSION;
    P.bpm10 = 1200;
    P.clock = CK_SEND;
    P.launch = LA_BAR;
    P.root = 0;
    P.scale = SC_MAJOR;
    P.thru = 1;
    P.keyoct = 3;
    P.song_loop = 1;
    for (t = 0; t < NTRK; t++) {
        track_defaults(t);
        for (p = 0; p < NPAT; p++)
            for (s = 0; s < NSTEP; s++)
                step_clear(STEP(t, p, s), 100);
    }
    P.scene_used[0] = 1;
}

static int head_sane(void)                       /* a loaded head with nothing out of range */
{
    uint32_t t, p;
    if (P.magic != PROJ_MAGIC || P.version != PROJ_VERSION || P.bpm10 < 300u || P.bpm10 > 3000u)
        return 0;
    for (t = 0; t < NTRK; t++) {
        if (P.trk[t].type >= NTT || P.trk[t].prof >= NPROF || P.pat[t] >= NPAT)
            return 0;
        for (p = 0; p < NPAT; p++)
            if (!P.ph[t][p].len || P.ph[t][p].len > NSTEP || P.ph[t][p].div >= NDIV || P.ph[t][p].dir >= NDIR)
                return 0;
    }
    return P.clock < NCK && P.scale < NSCALE && P.root < 12u && P.scene < NSCENE && P.song_n <= NSLOT;
}

/* the bytes a save writes after its header */
static uint32_t project_size(void)
{
    uint32_t t, p, n = sizeof(head_t);
    for (t = 0; t < NTRK; t++)
        for (p = 0; p < NPAT; p++)
            n += 8u + 16u * popcount64(pat_mask(t, p));
    return n;
}

/* the writer: bytes to the flash a buffer at a time, each sector erased as it is reached */
static struct {
    uint32_t off, at_erased;                     /* the next byte's flash offset; the end of what is erased */
    uint32_t n, crc;
    uint8_t buf[256];
    int err;
} wr;

static void wr_flush(void)
{
    if (!wr.n || wr.err)
        return;
    while (wr.at_erased < wr.off + wr.n) {
        if (plat_data_erase(wr.at_erased))
            wr.err = 1;
        wr.at_erased += 0x1000u;
    }
    if (!wr.err && plat_data_write(wr.off, wr.buf, wr.n))
        wr.err = 1;
    wr.off += wr.n;
    wr.n = 0;
}

static void wr_put(const void *src, uint32_t n)
{
    const uint8_t *s = src;
    wr.crc = crc32_step(wr.crc, s, n);
    while (n--) {
        wr.buf[wr.n++] = *s++;
        if (wr.n == sizeof wr.buf)
            wr_flush();
    }
}

/* 0 saved; -1 too big for the flash; -2 the flash failed */
static int project_save(void)
{
    uint32_t t, p, s, len = project_size();
    ph_rec_t h;
    if (len + sizeof h > RELAY_PROJ_HI - RELAY_PROJ_LO)
        return -1;
    wr.off = RELAY_PROJ_LO + sizeof h;
    wr.at_erased = RELAY_PROJ_LO;
    wr.n = 0;
    wr.crc = 0xFFFFFFFFu;
    wr.err = 0;
    wr_put(&P, sizeof P);
    for (t = 0; t < NTRK; t++)
        for (p = 0; p < NPAT; p++) {
            uint64_t m = pat_mask(t, p);
            wr_put(&m, 8);
            for (s = 0; s < NSTEP; s++)
                if (m >> s & 1u)
                    wr_put(STEP(t, p, s), sizeof(step_t));
        }
    wr_flush();
    if (wr.err)
        return -2;
    h.magic = PH_MAGIC;
    h.len = len;
    h.crc = ~wr.crc;
    h.rsv = 0;
    return plat_data_write(RELAY_PROJ_LO, &h, sizeof h) ? -2 : 0;
}

/* 1 loaded; 0 none (or damaged): the project is left as it was */
static int project_load(void)
{
    const uint8_t *base = plat_data(RELAY_PROJ_LO), *q;
    const ph_rec_t *h = (const ph_rec_t *)base;
    uint32_t t, p, s, i;
    if (!base || h->magic != PH_MAGIC || h->len < sizeof(head_t) ||
        h->len + sizeof *h > RELAY_PROJ_HI - RELAY_PROJ_LO)
        return 0;
    if (~crc32_step(0xFFFFFFFFu, base + sizeof *h, h->len) != h->crc)
        return 0;
    q = base + sizeof *h;
    for (i = 0; i < sizeof P; i++)
        ((uint8_t *)&P)[i] = q[i];
    if (!head_sane()) {
        project_new();
        return 0;
    }
    q += sizeof P;
    for (t = 0; t < NTRK; t++)
        for (p = 0; p < NPAT; p++) {
            uint64_t m = 0;
            for (i = 0; i < 8u; i++)
                m |= (uint64_t)q[i] << (8u * i);
            q += 8;
            for (s = 0; s < NSTEP; s++) {
                step_t *st = STEP(t, p, s);
                if (m >> s & 1u) {
                    for (i = 0; i < sizeof(step_t); i++)
                        ((uint8_t *)st)[i] = q[i];
                    q += sizeof(step_t);
                } else {
                    step_clear(st, P.trk[t].defvel);
                }
            }
        }
    for (t = 0; t < NTRK; t++)                   /* (a scene's mute is the live one at the start) */
        P.trk[t].muted = (uint8_t)(P.scene_mute[P.scene] >> t & 1u);
    return 1;
}

/* the settings: the unit's, not the project's */
#define SET_MAGIC 0x53594C52u                    /* "RLYS" */
typedef struct {
    uint32_t magic;
    uint8_t ble_off, trs_off, usb_host, rsv;
} settings_t;
static settings_t set;

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC) {
        set.magic = SET_MAGIC;
        set.ble_off = 0;
        set.trs_off = 0;
        set.usb_host = 0;
        set.rsv = 0;
    }
}

static void settings_save(void)
{
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
}

int relay_ble_off(void) { return set.ble_off; }
