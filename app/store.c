/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* The project in flash: the head, then for each pattern a 64-bit mask of the steps that hold something and those
 * steps, in one of four slots (below). The 16-byte record header goes last, so a save cut short is never read back as
 * a project. A full project of 16 x 8 x 64 steps would be 132 KiB; a slot holds 90 KiB on the FM-1 and 56 KiB on the
 * SMK-37 (about 5,700 and 3,500 steps in use), so a save that does not fit says so and writes nothing. */

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

/* ------------------------------------------------------------------- the four slots --- */
/* Each slot is a fixed run of sectors from the windows plat_data may write. Only a sector's first 0xF00 bytes are
 * used: the update loader looks for its record at 0xF00..0xF07 of sectors in the extra windows (plat.h), so those
 * bytes stay erased everywhere. The SMK-37's runs leave out its autosave pair (0xA1000..0xA2FFF) and the store's
 * owner tag (0xAF000): fm1-base clears every window at boot if that tag is not its own. */
#define NSLOTS 4u
#define SECT_USE 0xF00u
#if PLAT_SMK37
static const uint32_t RUNS[][2] = {{0x9D000u, 0xA1000u}, {0xA3000u, 0xAF000u}, {0xB0000u, 0xBB000u},
                                   {0xC2000u, 0xDC000u}, {0xE0000u, 0xEA000u}};   /* 63 sectors */
#define SLOT_SECTS 15u
#else
static const uint32_t RUNS[][2] = {{0x93000u, 0x97000u}, {0x97000u, 0xDC000u}, {0xE5000u, 0xFC000u}};   /* 96 */
#define SLOT_SECTS 24u
#endif
#define NRUNS (sizeof RUNS / sizeof RUNS[0])
#define SLOT_BYTES (SLOT_SECTS * SECT_USE)       /* FM-1 92,160 B; SMK-37 57,600 B */

static uint32_t slot_sector(uint32_t slot, uint32_t i)   /* the flash offset of a slot's i-th sector */
{
    uint32_t g = slot * SLOT_SECTS + i, r;
    for (r = 0; r < NRUNS; r++) {
        uint32_t n = (RUNS[r][1] - RUNS[r][0]) / 0x1000u;
        if (g < n)
            return RUNS[r][0] + g * 0x1000u;
        g -= n;
    }
    return 0;
}
static uint32_t slot_at(uint32_t slot, uint32_t off)     /* a slot's byte off, in the flash */
{
    return slot_sector(slot, off / SECT_USE) + off % SECT_USE;
}

static void slot_read(uint32_t slot, uint32_t off, void *dst, uint32_t n)   /* (reads stay inside one sector each) */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t room = SECT_USE - off % SECT_USE, k = n < room ? n : room, i;
        const uint8_t *src = plat_data(slot_at(slot, off));
        for (i = 0; i < k; i++)
            d[i] = src ? src[i] : 0xFFu;
        d += k;
        off += k;
        n -= k;
    }
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

/* the writer: a buffer at a time into the slot, each sector erased as it is reached */
static struct {
    uint32_t slot, off, erased;                  /* the next byte's place in the slot; sectors erased so far */
    uint32_t n, crc;
    uint8_t buf[256];
    int err;
} wr;

static void wr_flush(void)
{
    uint32_t done = 0;
    while (done < wr.n && !wr.err) {
        uint32_t room = SECT_USE - wr.off % SECT_USE, k = wr.n - done < room ? wr.n - done : room;
        uint32_t sect = wr.off / SECT_USE;
        while (wr.erased <= sect && !wr.err) {
            if (plat_data_erase(slot_sector(wr.slot, wr.erased)))
                wr.err = 1;
            wr.erased++;
        }
        if (!wr.err && plat_data_write(slot_at(wr.slot, wr.off), wr.buf + done, k))
            wr.err = 1;
        wr.off += k;
        done += k;
    }
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

/* 0 saved; -1 too big for a slot; -2 the flash failed */
static int project_save(uint32_t slot)
{
    uint32_t t, p, s, len = project_size();
    ph_rec_t h;
    if (len + sizeof h > SLOT_BYTES)
        return -1;
    wr.slot = slot;
    wr.off = sizeof h;
    wr.erased = 0;
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
    return plat_data_write(slot_at(slot, 0), &h, sizeof h) ? -2 : 0;
}

/* what a slot holds: its length (0 empty or damaged) and tempo, for the PROJECTS page; the CRC is checked by a load */
static uint32_t slot_info(uint32_t slot, uint32_t *bpm10)
{
    ph_rec_t h;
    uint16_t b = 0;
    slot_read(slot, 0, &h, sizeof h);
    if (h.magic != PH_MAGIC || h.len < sizeof(head_t) || h.len + sizeof h > SLOT_BYTES)
        return 0;
    slot_read(slot, sizeof h + 6u, &b, 2);       /* head_t: magic, version, then bpm10 */
    if (bpm10)
        *bpm10 = b;
    return h.len;
}

static void slot_clear(uint32_t slot)            /* the header's magic written to zeros: no longer a project */
{
    static const uint8_t z[4] = {0, 0, 0, 0};
    plat_data_write(slot_at(slot, 0), z, sizeof z);
}

/* 1 loaded; 0 none (or damaged): the project is left as it was */
static int project_load(uint32_t slot)
{
    ph_rec_t h;
    uint32_t t, p, s, i, off, crc = 0xFFFFFFFFu, len = slot_info(slot, 0);
    uint8_t chunk[64];
    if (!len)
        return 0;
    slot_read(slot, 0, &h, sizeof h);
    for (off = 0; off < len; off += sizeof chunk) {   /* the whole record checked before anything is taken */
        uint32_t k = len - off < sizeof chunk ? len - off : sizeof chunk;
        slot_read(slot, sizeof h + off, chunk, k);
        crc = crc32_step(crc, chunk, k);
    }
    if (~crc != h.crc)
        return 0;
    off = sizeof h;
    slot_read(slot, off, &P, sizeof P);
    off += sizeof P;
    if (!head_sane()) {
        project_new();
        return 0;
    }
    for (t = 0; t < NTRK; t++)
        for (p = 0; p < NPAT; p++) {
            uint64_t m = 0;
            uint8_t mb[8];
            slot_read(slot, off, mb, 8);
            off += 8;
            for (i = 0; i < 8u; i++)
                m |= (uint64_t)mb[i] << (8u * i);
            for (s = 0; s < NSTEP; s++) {
                step_t *st = STEP(t, p, s);
                if (m >> s & 1u) {
                    slot_read(slot, off, st, sizeof(step_t));
                    off += sizeof(step_t);
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
    uint8_t ble_off, trs_off, usb_host, slot;    /* slot: the project slot in use (loaded at power-on) */
} settings_t;
static settings_t set;

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC) {
        set.magic = SET_MAGIC;
        set.ble_off = 0;
        set.trs_off = 0;
        set.usb_host = 0;
        set.slot = 0;
    }
    if (set.slot >= NSLOTS)
        set.slot = 0;
}

static void settings_save(void)
{
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
}

int relay_ble_off(void) { return set.ble_off; }
