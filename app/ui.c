/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* RELAY-16's pages and what the controls do on them. One input layer for both boards:
 *   the grid    16 keys: the FM-1's white keys, the SMK-37's pads (steps, scenes, mutes, tracks)
 *   the side    11 keys: the FM-1's black keys (a held step's groups, drum lanes, mutes); the SMK-37 has none, and
 *               uses SELECT / < > and its keybed instead
 *   notes       the FM-1's keys while they are a keyboard (SEQ held, recording, the pages without steps); the SMK-37's
 *               keybed always, with velocity; MIDI in too
 * The FM-1's names are used below (SEL, HOME ...); the screen shows the SMK-37's own on the SMK (draw.c). */

enum { PG_STEPS, PG_TRACK, PG_LANES, PG_PERF, PG_OVER, PG_GEN, PG_GLOBAL, PG_ARR, PG_SONG, NPG };
static const uint8_t PG_SUBS[NPG] = {1, 2, 2, 1, 1, 2, 6, 1, 1};
enum { GL_CLOCK, GL_KEY, GL_PORTS, GL_TIMING, GL_PROJECTS, GL_ABOUT };
enum { GR_NOTE, GR_TIME, GR_LOGIC, GR_LOCK, NGR };

typedef struct {
    const char *label;
    char val[16];
    int8_t frac;                                 /* the bar under it, 0..100; -1 none */
    uint8_t red;                                 /* a lock (red) */
} knob_t;

static struct {
    uint8_t page, sub[NPG];
    uint8_t bar;                                 /* the 16 steps shown: bar 0..3 of the pattern */
    uint8_t group;                               /* a held step's knobs: GR_* */
    uint32_t grid_held, grid_t[16];              /* grid keys held, and when each went down */
    uint8_t used;                                /* the held step(s) were edited: no toggle on release */
    uint8_t hold_notes;                          /* notes put on the held steps in this hold */
    uint32_t side_held;
    uint32_t btn, keys, btn_t[NB];
    uint64_t keys37;
    uint8_t rec;                                 /* live recording (latched) */
    uint8_t srec, srec_pos, srec_n, srec_notes[NNOTE];   /* step record: on, the cursor, the chord gathering */
    uint8_t menu, menu_n, menu_confirm;
    int8_t menu_i;
    char msg[2][24];
    uint32_t msg_until;
    uint8_t dirty, redraw;
    uint8_t eu_steps, eu_pulses, eu_rot, rnd_density, rnd_range, rnd_seed;
    int8_t smk_oct;
    uint32_t down[4];                            /* notes held now (any source), bit per note */
    uint8_t ndown;
    uint8_t song_sel;
    uint8_t lane_live[NLANE];
    uint8_t clip_ok, scene_clip_ok, undo_ok, undo_t, undo_p;
    uint8_t scene_clip[NTRK];
    uint16_t scene_clip_mute;
    uint8_t rec_pos[128], rec_pat[128];          /* a recorded note: its step, to set its length at the note-off */
    uint32_t rec_tick[128];
    int8_t last_fader[4];
    uint8_t home_used;                           /* HOME was a shift (or closed the menu) while down: no page change */
    uint8_t proj_sel;                            /* GLOBAL > PROJECTS: the slot picked */
    uint8_t save_pending;                        /* SAVE while playing: it saves at the stop */
    uint8_t card;                                /* a button's help card on the screen (B_*), NB none */
    uint8_t hold_b, hold_clean;                  /* the button held alone (NB none), nothing else touched since */
    uint32_t hold_t;
    uint8_t tm_port;                             /* GLOBAL > TIMING: the port the knobs set (PT_*) */
    uint8_t bt_paused;                           /* the Bluetooth device let go by hand: no reconnecting until picked */
} ui;
#define CARD_MS 1000u                            /* a button held alone this long shows its card */

static void input_seen(void) { ui.hold_clean = 0; }   /* another control used: the held button is a shift, no card */
static step_t clip[NSTEP] PLAT_POOL, undo_buf[NSTEP] PLAT_POOL;
static phead_t clip_ph, undo_ph;

/* ------------------------------------------------------------------- text --- */
static void s_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static uint32_t text_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void s_cpy(char *d, const char *s)
{
    *d = 0;
    s_cat(d, s);
}
static void i_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void signed_text(int32_t v, char *b)
{
    if (v > 0) {
        *b++ = '+';
    }
    i_text(v, b);
}
static void note_text(uint32_t n, char *b)
{
    char o[8];
    if (n > 127u) {
        s_cpy(b, "-");
        return;
    }
    s_cpy(b, NOTE_NAME[n % 12u]);
    i_text((int32_t)(n / 12u) - 1, o);
    s_cat(b, o);
}
static void ms_text(int32_t ms, char *t)          /* a port's offset: "0 ms", "-15 ms" (early), "+10 ms" (late) */
{
    signed_text(ms, t);
    s_cat(t, " ms");
}

static void tempo_text(uint32_t b10, char *t)
{
    char f[4];
    i_text((int32_t)(b10 / 10u), t);
    f[0] = '.';
    f[1] = (char)('0' + b10 % 10u);
    f[2] = 0;
    s_cat(t, f);
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[56];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.redraw = 1;
    if (ann_on()) {
        s_cpy(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            s_cat(t, " ");
            s_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* ------------------------------------------------------------------- the project from the UI --- */
static track_t *trk(void) { return &P.trk[sel_track]; }
static phead_t *cur_ph(void) { return &P.ph[sel_track][P.pat[sel_track]]; }
static step_t *grid_step(uint32_t i) { return STEP(sel_track, P.pat[sel_track], ui.bar * 16u + i); }
static void touched(void)
{
    ui.dirty = 1;
    ui.redraw = 1;
}

static void scene_store(void)                    /* the live patterns and mutes are the scene playing */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        P.scene_pat[P.scene][t] = E.t[t].cue != 0xFFu ? E.t[t].cue : P.pat[t];
    P.scene_mute[P.scene] = 0;
    for (t = 0; t < NTRK; t++)
        if (P.trk[t].muted)
            P.scene_mute[P.scene] |= (uint16_t)(1u << t);
    P.scene_used[P.scene] = 1;
}

static void undo_take(void)
{
    uint32_t s;
    for (s = 0; s < NSTEP; s++)
        undo_buf[s] = *STEP(sel_track, P.pat[sel_track], s);
    undo_ph = *cur_ph();
    ui.undo_t = sel_track;
    ui.undo_p = P.pat[sel_track];
    ui.undo_ok = 1;
}

static void undo_do(void)
{
    uint32_t s, f;
    if (!ui.undo_ok) {
        app_say("NOTHING", "TO UNDO");
        return;
    }
    f = plat_lock();
    for (s = 0; s < NSTEP; s++) {
        step_t keep = *STEP(ui.undo_t, ui.undo_p, s);
        *STEP(ui.undo_t, ui.undo_p, s) = undo_buf[s];
        undo_buf[s] = keep;                      /* (undo again: redo) */
    }
    {
        phead_t k = P.ph[ui.undo_t][ui.undo_p];
        P.ph[ui.undo_t][ui.undo_p] = undo_ph;
        undo_ph = k;
    }
    plat_unlock(f);
    touched();
    app_say("UNDONE", "");
}

/* a step press writes this: the remembered notes, or the drum lane's note */
static void step_fill(step_t *s)
{
    const track_t *t = trk();
    uint32_t i;
    step_clear(s, t->defvel);
    if (t->type == TT_DRUM) {
        s->note[0] = t->drum[t->lane_sel % NDLANE];
        return;
    }
    for (i = 0; i < NNOTE; i++)
        s->note[i] = t->rem[i];
    if (s->note[0] == NOTE_NONE)
        s->note[0] = 60;
}

static int step_has(const step_t *s, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NNOTE; i++)
        if (s->note[i] == note)
            return 1;
    return 0;
}

static void step_drop_note(step_t *s, uint32_t note)
{
    uint32_t i, j = 0;
    uint8_t n[NNOTE];
    for (i = 0; i < NNOTE; i++)
        if (s->note[i] != note && s->note[i] != NOTE_NONE)
            n[j++] = s->note[i];
    for (i = 0; i < NNOTE; i++)
        s->note[i] = i < j ? n[i] : NOTE_NONE;
}

static int step_add_note(step_t *s, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NNOTE; i++) {
        if (s->note[i] == note)
            return 1;
        if (s->note[i] == NOTE_NONE) {
            s->note[i] = (uint8_t)note;
            return 1;
        }
    }
    return 0;
}

static void step_tap(uint32_t i)                 /* a step pressed and let go: on, off, or a drum lane in or out */
{
    step_t *s = grid_step(i);
    const track_t *t = trk();
    if (ui.bar * 16u + i >= cur_ph()->len)
        return;
    if (t->type == TT_DRUM) {
        uint32_t note = t->drum[t->lane_sel % NDLANE];
        if (s->note[0] == NOTE_NONE) {
            uint32_t k, keep[NLANE];
            for (k = 0; k < NLANE; k++)
                keep[k] = s->lock[k];
            step_fill(s);
            for (k = 0; k < NLANE; k++)
                s->lock[k] = (uint8_t)keep[k];
        } else if (step_has(s, note)) {
            step_drop_note(s, note);
        } else if (!step_add_note(s, note)) {
            app_say("STEP FULL", "4 NOTES");
        }
    } else if (s->note[0] == NOTE_NONE) {
        step_fill(s);
    } else {
        step_clear(s, t->defvel);
    }
    touched();
}

/* ------------------------------------------------------------------- notes in --- */
static int note_down(uint32_t n) { return (int)(ui.down[n >> 5] >> (n & 31u) & 1u); }

static void held_steps_note(uint32_t note)       /* a note played while steps are held: their notes */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        step_t *s;
        if (!(ui.grid_held >> i & 1u) || ui.bar * 16u + i >= cur_ph()->len)
            continue;
        s = grid_step(i);
        if (!ui.hold_notes) {
            uint32_t k;
            if (s->note[0] == NOTE_NONE) {
                step_fill(s);
            }
            for (k = 0; k < NNOTE; k++)
                s->note[k] = NOTE_NONE;
        }
        step_add_note(s, note);
    }
    ui.hold_notes = 1;
    ui.used = 1;
    touched();
}

static void srec_commit(void)                    /* step record: the chord gathered goes on the cursor's step */
{
    step_t *s = STEP(sel_track, P.pat[sel_track], ui.srec_pos);
    uint32_t i;
    step_clear(s, trk()->defvel);
    for (i = 0; i < NNOTE; i++)
        s->note[i] = i < ui.srec_n ? ui.srec_notes[i] : NOTE_NONE;
    ui.srec_n = 0;
    ui.srec_pos = (uint8_t)((ui.srec_pos + 1u) % cur_ph()->len);
    ui.bar = (uint8_t)(ui.srec_pos / 16u);
    touched();
}

static void rec_note_on(uint32_t note, uint32_t vel, uint32_t tick)
{
    uint32_t tr = sel_track, p = P.pat[tr];
    int32_t pos = E.t[tr].pos;
    step_t *s;
    (void)tick;
    if (pos < 0)
        pos = 0;
    s = STEP(tr, p, (uint32_t)pos);
    if (s->note[0] == NOTE_NONE) {
        step_clear(s, vel);
        s->note[0] = (uint8_t)note;
    } else if (!step_add_note(s, note)) {
        return;
    }
    s->vel = (uint8_t)vel;
    ui.rec_pos[note] = (uint8_t)pos;
    ui.rec_pat[note] = (uint8_t)(p | 0x80u);
    ui.rec_tick[note] = E.tick;
    touched();
}

static void rec_note_off(uint32_t note)
{
    uint32_t tr = sel_track, div, q;
    step_t *s;
    if (!(ui.rec_pat[note] & 0x80u))
        return;
    s = STEP(tr, ui.rec_pat[note] & 7u, ui.rec_pos[note]);
    div = DIV_TICKS[P.ph[tr][ui.rec_pat[note] & 7u].div % NDIV];
    q = (E.tick - ui.rec_tick[note]) * 4u / div;
    s->len = (uint8_t)clamp((int32_t)q, 1, 255);
    ui.rec_pat[note] = 0;
}

/* a note from the keys, the keybed or MIDI in; play: send it (MIDI in has been played by thru already) */
static void note_event(uint32_t note, uint32_t vel, int on, int play)
{
    track_t *t = trk();
    if (note > 127u)
        return;
    if (on) {
        input_seen();
        if (play)
            live_on(sel_track, note, vel);
        if (!ui.ndown) {                         /* the first of a chord: the remembered notes start again */
            uint32_t k;
            for (k = 0; k < NNOTE; k++)
                t->rem[k] = NOTE_NONE;
        }
        if (t->type == TT_NOTE) {
            uint32_t k;
            for (k = 0; k < NNOTE; k++)
                if (t->rem[k] == NOTE_NONE || t->rem[k] == note) {
                    t->rem[k] = (uint8_t)note;
                    break;
                }
        } else {
            uint32_t k;
            for (k = 0; k < NDLANE; k++)
                if (t->drum[k] == note) {
                    t->lane_sel = (uint8_t)k;
                    ui.redraw = 1;
                    break;
                }
        }
        if (!note_down(note)) {
            ui.down[note >> 5] |= 1u << (note & 31u);
            ui.ndown++;
        }
        if (ui.grid_held && ui.page == PG_STEPS)
            held_steps_note(note);
        else if (ui.srec && ui.srec_n < NNOTE)
            ui.srec_notes[ui.srec_n++] = (uint8_t)note;
        else if (ui.rec && E.playing)
            rec_note_on(note, vel, E.tick);
        ui.redraw = 1;
    } else {
        if (play)
            live_off(note);
        if (note_down(note)) {
            ui.down[note >> 5] &= ~(1u << (note & 31u));
            if (ui.ndown)
                ui.ndown--;
        }
        rec_note_off(note);
        if (ui.srec && !ui.ndown && ui.srec_n)
            srec_commit();
    }
}

/* ------------------------------------------------------------------- generators --- */
static int euclid_hit(uint32_t i, uint32_t steps, uint32_t pulses, uint32_t rot)
{
    uint32_t j;
    if (!steps || !pulses)
        return 0;
    j = (i + steps - rot % steps) % steps;
    return (j * pulses) % steps < pulses;
}

static void gen_print(void)
{
    uint32_t s, f, len;
    track_t *t = trk();
    undo_take();
    f = plat_lock();
    if (ui.sub[PG_GEN] == 0) {
        len = ui.eu_steps;
        cur_ph()->len = (uint8_t)len;
        for (s = 0; s < len; s++) {
            step_t *st = STEP(sel_track, P.pat[sel_track], s);
            int hit = euclid_hit(s, len, ui.eu_pulses, ui.eu_rot);
            if (t->type == TT_DRUM) {
                uint32_t note = t->drum[t->lane_sel % NDLANE];
                if (hit && !step_has(st, note)) {
                    if (st->note[0] == NOTE_NONE)
                        step_fill(st);
                    else
                        step_add_note(st, note);
                } else if (!hit && step_has(st, note)) {
                    step_drop_note(st, note);
                }
            } else if (hit) {
                step_fill(st);
            } else {
                step_clear(st, t->defvel);
            }
        }
    } else {
        uint32_t seed = ui.rnd_seed * 2654435761u + 1u, base = t->rem[0] != NOTE_NONE ? t->rem[0] : 60u;
        len = cur_ph()->len;
        for (s = 0; s < len; s++) {
            step_t *st = STEP(sel_track, P.pat[sel_track], s);
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            if (seed % 100u >= ui.rnd_density) {
                if (t->type == TT_DRUM)
                    step_drop_note(st, t->drum[t->lane_sel % NDLANE]);
                else
                    step_clear(st, t->defvel);
                continue;
            }
            if (t->type == TT_DRUM) {
                if (st->note[0] == NOTE_NONE)
                    step_fill(st);
                else
                    step_add_note(st, t->drum[t->lane_sel % NDLANE]);
            } else {
                uint32_t n = base + (ui.rnd_range ? (seed >> 8) % (ui.rnd_range + 1u) : 0u);
                if (P.scale != SC_CHROM)
                    n = quantize(n > 127u ? 127u : n);
                step_clear(st, 70u + (seed >> 16) % 58u);
                st->note[0] = (uint8_t)(n > 127u ? 127u : n);
            }
        }
    }
    plat_unlock(f);
    touched();
    app_say("PRINTED", ui.sub[PG_GEN] ? "RANDOM" : "EUCLID");
}

/* ------------------------------------------------------------------- pattern actions --- */
static void pat_copy(void)
{
    uint32_t s;
    for (s = 0; s < NSTEP; s++)
        clip[s] = *STEP(sel_track, P.pat[sel_track], s);
    clip_ph = *cur_ph();
    ui.clip_ok = 1;
    app_say("COPIED", "PATTERN");
}

static void pat_paste(void)
{
    uint32_t s, f;
    if (!ui.clip_ok) {
        app_say("NOTHING", "COPIED");
        return;
    }
    undo_take();
    f = plat_lock();
    for (s = 0; s < NSTEP; s++)
        *STEP(sel_track, P.pat[sel_track], s) = clip[s];
    *cur_ph() = clip_ph;
    plat_unlock(f);
    touched();
    app_say("PASTED", "PATTERN");
}

static void pat_clear(void)
{
    uint32_t s, f;
    undo_take();
    f = plat_lock();
    for (s = 0; s < NSTEP; s++)
        step_clear(STEP(sel_track, P.pat[sel_track], s), trk()->defvel);
    plat_unlock(f);
    touched();
    app_say("CLEARED", "PATTERN");
}

static void pat_double(void)
{
    uint32_t s, len = cur_ph()->len, f;
    if (len * 2u > NSTEP) {
        app_say("TOO LONG", "64 STEPS MOST");
        return;
    }
    undo_take();
    f = plat_lock();
    for (s = 0; s < len; s++)
        *STEP(sel_track, P.pat[sel_track], len + s) = *STEP(sel_track, P.pat[sel_track], s);
    cur_ph()->len = (uint8_t)(len * 2u);
    plat_unlock(f);
    touched();
    app_say("DOUBLED", "");
}

static void pat_shift(int right)
{
    uint32_t s, len = cur_ph()->len, f;
    step_t keep;
    undo_take();
    f = plat_lock();
    if (right) {
        keep = *STEP(sel_track, P.pat[sel_track], len - 1u);
        for (s = len - 1u; s > 0u; s--)
            *STEP(sel_track, P.pat[sel_track], s) = *STEP(sel_track, P.pat[sel_track], s - 1u);
        *STEP(sel_track, P.pat[sel_track], 0) = keep;
    } else {
        keep = *STEP(sel_track, P.pat[sel_track], 0);
        for (s = 0; s + 1u < len; s++)
            *STEP(sel_track, P.pat[sel_track], s) = *STEP(sel_track, P.pat[sel_track], s + 1u);
        *STEP(sel_track, P.pat[sel_track], len - 1u) = keep;
    }
    plat_unlock(f);
    touched();
}

static void lanes_clear(void)
{
    uint32_t s, k;
    undo_take();
    for (s = 0; s < NSTEP; s++)
        for (k = 0; k < NLANE; k++)
            STEP(sel_track, P.pat[sel_track], s)->lock[k] = LOCK_NONE;
    touched();
    app_say("LOCKS", "CLEARED");
}

/* ------------------------------------------------------------------- the menu (SEL) --- */
enum { MA_COPY, MA_PASTE, MA_CLEAR, MA_DOUBLE, MA_SHL, MA_SHR, MA_UNDO, MA_LOCKS_CLEAR, MA_PRINT, MA_SC_COPY,
       MA_SC_PASTE, MA_SC_CLEAR, MA_SONG_PLAY, MA_SONG_DEL, MA_SONG_CLEAR, MA_UNMUTE, MA_SAVE, MA_NEW, MA_PANIC,
       MA_BT_CONNECT, MA_TRACK_RESET, MA_LOAD, MA_SAVE_TO, MA_SLOT_CLEAR, NMA };
static const char *const MA_NAME[NMA] = {"COPY PATTERN", "PASTE PATTERN", "CLEAR PATTERN", "DOUBLE PATTERN",
                                         "SHIFT LEFT", "SHIFT RIGHT", "UNDO", "CLEAR LOCKS", "PRINT", "COPY SCENE",
                                         "PASTE SCENE", "CLEAR SCENE", "PLAY SONG", "DELETE SLOT", "CLEAR SONG",
                                         "UNMUTE ALL", "SAVE PROJECT", "NEW PROJECT", "ALL NOTES OFF",
                                         "BT CONNECT / LET GO", "RESET TRACK", "LOAD PROJECT", "SAVE HERE",
                                         "CLEAR SLOT"};
static uint8_t menu_items[12];

static uint32_t menu_build(void)
{
    uint32_t n = 0;
#define ADD(a) menu_items[n++] = (a)
    switch (ui.page) {
    case PG_STEPS: ADD(MA_COPY); ADD(MA_PASTE); ADD(MA_CLEAR); ADD(MA_DOUBLE); ADD(MA_SHL); ADD(MA_SHR); ADD(MA_UNDO);
        break;
    case PG_LANES: ADD(MA_LOCKS_CLEAR); ADD(MA_UNDO); break;
    case PG_GEN: ADD(MA_PRINT); ADD(MA_UNDO); break;
    case PG_ARR: ADD(MA_SC_COPY); ADD(MA_SC_PASTE); ADD(MA_SC_CLEAR); break;
    case PG_SONG: ADD(MA_SONG_PLAY); ADD(MA_SONG_DEL); ADD(MA_SONG_CLEAR); break;
    case PG_PERF: case PG_OVER: ADD(MA_UNMUTE); break;
    case PG_TRACK: ADD(MA_TRACK_RESET); ADD(MA_COPY); ADD(MA_PASTE); break;
    default: break;
    }
    if (ui.page == PG_GLOBAL && ui.sub[PG_GLOBAL] == GL_PORTS && FM1B_BLE_CENTRAL)
        ADD(MA_BT_CONNECT);
    if (ui.page == PG_GLOBAL && ui.sub[PG_GLOBAL] == GL_PROJECTS) {
        ADD(MA_LOAD);
        ADD(MA_SAVE_TO);
        ADD(MA_SLOT_CLEAR);
    }
    ADD(MA_SAVE);
    ADD(MA_NEW);
    ADD(MA_PANIC);
#undef ADD
    return n;
}

static int project_save_say(void);
static void slot_load_now(uint32_t slot);

static void menu_do(uint32_t a)
{
    switch (a) {
    case MA_COPY: pat_copy(); break;
    case MA_PASTE: pat_paste(); break;
    case MA_CLEAR: pat_clear(); break;
    case MA_DOUBLE: pat_double(); break;
    case MA_SHL: pat_shift(0); app_say("SHIFTED", "LEFT"); break;
    case MA_SHR: pat_shift(1); app_say("SHIFTED", "RIGHT"); break;
    case MA_UNDO: undo_do(); break;
    case MA_LOCKS_CLEAR: lanes_clear(); break;
    case MA_PRINT: gen_print(); break;
    case MA_SC_COPY: {
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            ui.scene_clip[t] = P.scene_pat[P.scene][t];
        ui.scene_clip_mute = P.scene_mute[P.scene];
        ui.scene_clip_ok = 1;
        app_say("COPIED", "SCENE");
        break;
    }
    case MA_SC_PASTE: {
        uint32_t t;
        if (!ui.scene_clip_ok) {
            app_say("NOTHING", "COPIED");
            break;
        }
        for (t = 0; t < NTRK; t++)
            P.scene_pat[P.scene][t] = ui.scene_clip[t];
        P.scene_mute[P.scene] = ui.scene_clip_mute;
        seq_cue_scene(P.scene, 0);
        touched();
        app_say("PASTED", "SCENE");
        break;
    }
    case MA_SC_CLEAR: {
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            P.scene_pat[P.scene][t] = 0;
        P.scene_mute[P.scene] = 0;
        P.scene_used[P.scene] = P.scene == 0u;
        touched();
        app_say("CLEARED", "SCENE");
        break;
    }
    case MA_SONG_PLAY:
        if (!P.song_n) {
            app_say("SONG", "EMPTY");
            break;
        }
        seq_stop();
        E.slot = ui.song_sel < P.song_n ? ui.song_sel : 0;
        E.song_on = 1;
        seq_start(0);
        break;
    case MA_SONG_DEL: {
        uint32_t i;
        if (!P.song_n)
            break;
        for (i = ui.song_sel; i + 1u < P.song_n; i++) {
            P.song_scene[i] = P.song_scene[i + 1u];
            P.song_bars[i] = P.song_bars[i + 1u];
        }
        P.song_n--;
        if (ui.song_sel && ui.song_sel >= P.song_n)
            ui.song_sel--;
        touched();
        break;
    }
    case MA_SONG_CLEAR: P.song_n = 0; ui.song_sel = 0; touched(); app_say("SONG", "CLEARED"); break;
    case MA_UNMUTE: {
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            P.trk[t].muted = 0;
        E.solo = 0;
        scene_store();
        touched();
        app_say("ALL", "UNMUTED");
        break;
    }
    case MA_SAVE: project_save_say(); break;
    case MA_NEW:
        if (!ui.menu_confirm) {
            ui.menu_confirm = 1;
            app_say("NEW PROJECT?", "SEL YES");
            return;
        }
        seq_stop();
        project_new();
        ui.undo_ok = ui.clip_ok = 0;
        touched();
        app_say("NEW", "PROJECT");
        break;
    case MA_PANIC: panic_all(); app_say("ALL NOTES", "OFF"); break;
    case MA_BT_CONNECT: {
        int ev = btdev_save();
        ui.bt_paused = ev == BTDEV_LET_GO;       /* let go by hand: auto-connect waits until a device is picked */
        app_say(btdev_word(ev), btdev_name());
        break;
    }
    case MA_LOAD:
        if (ui.dirty && !ui.menu_confirm) {
            ui.menu_confirm = 1;
            app_say("NOT SAVED: LOSE IT?", "SEL YES");
            return;
        }
        slot_load_now(ui.proj_sel);
        break;
    case MA_SAVE_TO:
        if (slot_info(ui.proj_sel, 0) && ui.proj_sel != set.slot && !ui.menu_confirm) {
            ui.menu_confirm = 1;
            app_say("REPLACE PROJECT?", "SEL YES");
            return;
        }
        set.slot = ui.proj_sel;
        settings_save();
        project_save_say();
        break;
    case MA_SLOT_CLEAR:
        if (!ui.menu_confirm) {
            ui.menu_confirm = 1;
            app_say("CLEAR THE SLOT?", "SEL YES");
            return;
        }
        slot_clear(ui.proj_sel);
        if (ui.proj_sel == set.slot)
            ui.dirty = 1;                        /* what is in RAM is no longer saved anywhere */
        app_say("SLOT", "CLEARED");
        break;
    case MA_TRACK_RESET: {
        uint32_t ch = trk()->chan, po = trk()->ports;
        track_defaults(sel_track);
        trk()->chan = (uint8_t)ch;
        trk()->ports = (uint8_t)po;
        touched();
        app_say("TRACK", "RESET");
        break;
    }
    default: break;
    }
    ui.menu = 0;
    ui.menu_confirm = 0;
    ui.redraw = 1;
}

static void slot_load_now(uint32_t slot)
{
    char t[4] = {(char)('1' + slot), 0, 0, 0};
    if (E.playing)
        seq_stop();
    ui.save_pending = 0;
    if (!slot_info(slot, 0)) {
        app_say("SLOT EMPTY", t);
        return;
    }
    if (!project_load(slot)) {
        app_say("SLOT DAMAGED", t);
        return;
    }
    set.slot = (uint8_t)slot;
    settings_save();
    sel_track = 0;
    ui.bar = 0;
    ui.dirty = 0;
    ui.undo_ok = 0;
    app_say("LOADED PROJECT", t);
}

static int project_save_say(void)
{
    char t[16];
    int rc;
    if (E.playing) {                             /* erasing the flash stops everything for a moment: at the stop */
        ui.save_pending = 1;
        app_say("SAVES AT", "THE STOP");
        return 0;
    }
    ui.save_pending = 0;
    rc = project_save(set.slot);
    if (!rc) {
        ui.dirty = 0;
        s_cpy(t, "PROJECT ");
        t[8] = (char)('1' + set.slot);
        t[9] = 0;
        app_say("SAVED", t);
    } else {
        app_say("NOT SAVED", rc == -1 ? "TOO BIG FOR A SLOT" : "FLASH ERROR");
    }
    ui.redraw = 1;
    return rc;
}

/* ------------------------------------------------------------------- the knobs --- */
static const uint8_t PORT_SETS[] = {
#if PLAT_SMK37
    PO_USB | PO_BLE | PO_BLEDEV | PO_TRS, PO_USB, PO_TRS, PO_BLE | PO_BLEDEV, PO_BLEDEV, PO_BLE};
static const char *const PORT_NAME[] = {"ALL", "USB+TRS", "TRS", "BLUETOOTH", "BT DEVICE", "BT HOST"};
#else
    PO_USB | PO_BLE | PO_BLEDEV, PO_USB, PO_BLE | PO_BLEDEV, PO_BLEDEV, PO_BLE};
static const char *const PORT_NAME[] = {"ALL", "USB", "BLUETOOTH", "BT DEVICE", "BT HOST"};
#endif
#define NPORTSET ((int)sizeof PORT_SETS)
static int port_index(uint32_t ports)
{
    int i;
    for (i = 0; i < NPORTSET; i++)
        if (PORT_SETS[i] == ports)
            return i;
    return 0;
}

static void lane_text(uint32_t cc, char *b)      /* a lane's name: its CC's, else "CC 21" */
{
    const char *n = cc_name(trk()->prof, cc);
    if (cc == LANE_NONE)
        s_cpy(b, "-");
    else if (n)
        s_cpy(b, n);
    else {
        s_cpy(b, "CC ");
        i_text((int32_t)cc, b + 3);
    }
}

static int held_first(void)                      /* the first held step, -1 none */
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (ui.grid_held >> i & 1u)
            return (int)i;
    return -1;
}

static int step_edit_mode(void) { return ui.page == PG_STEPS && ui.grid_held && held_first() >= 0; }

static void knob_set(knob_t *k, const char *label, const char *val, int frac)
{
    k->label = label;
    s_cpy(k->val, val);
    k->frac = (int8_t)frac;
    k->red = 0;
}

static void knobs_get(knob_t k[4])
{
    char t[16];
    uint32_t i;
    const track_t *tr = trk();
    phead_t *ph = cur_ph();
    for (i = 0; i < 4u; i++)
        knob_set(&k[i], "", "", -1);
    if (step_edit_mode()) {
        const step_t *s = grid_step((uint32_t)held_first());
        switch (ui.group) {
        case GR_NOTE:
            note_text(s->note[0], t);
            knob_set(&k[0], tr->type == TT_DRUM ? "NOTE" : "PITCH", s->note[0] == NOTE_NONE ? "-" : t, -1);
            i_text(s->vel, t);
            knob_set(&k[1], "VELOCITY", t, s->vel * 100 / 127);
            i_text(s->len / 4u, t);
            if (s->len % 4u) {
                static const char *const Q[] = {"", ".25", ".5", ".75"};
                s_cat(t, Q[s->len % 4u]);
            }
            knob_set(&k[2], "LENGTH", t, s->len * 100 / 255);
            knob_set(&k[3], "GLIDE", s->flags & SF_GLIDE ? "ON" : "OFF", -1);
            break;
        case GR_TIME:
            signed_text(s->micro, t);
            s_cat(t, "/24");
            knob_set(&k[0], "MICRO", t, (s->micro + 11) * 100 / 22);
            i_text((s->rat & 7u) + 1u, t);
            knob_set(&k[1], "RATCHET", t, (int)((s->rat & 7u) * 100u / 7u));
            {
                static const char *const R[] = {"FLAT", "UP", "DOWN"};
                knob_set(&k[2], "RAMP", R[((s->rat >> 4) & 3u) % NRR], -1);
            }
            knob_set(&k[3], "ACCENT", s->flags & SF_ACCENT ? "ON" : "OFF", -1);
            break;
        case GR_LOGIC:
            cond_text(s->cond, t);
            knob_set(&k[0], "CONDITION", t, -1);
            i_text(s->chance, t);
            s_cat(t, "%");
            knob_set(&k[1], "CHANCE", t, s->chance);
            break;
        default:
            for (i = 0; i < NLANE; i++) {
                static char lab[NLANE][12];
                lane_text(tr->lane_cc[i], lab[i]);
                if (s->lock[i] == LOCK_NONE)
                    s_cpy(t, "-");
                else
                    i_text(s->lock[i], t);
                knob_set(&k[i], lab[i], t, s->lock[i] == LOCK_NONE ? -1 : s->lock[i] * 100 / 127);
                k[i].red = s->lock[i] != LOCK_NONE;
            }
            break;
        }
        return;
    }
    switch (ui.page) {
    case PG_STEPS: case PG_OVER:
        i_text(ph->len, t);
        knob_set(&k[0], "LENGTH", t, ph->len * 100 / 64);
        knob_set(&k[1], "DIV", DIV_NAME[ph->div % NDIV], ph->div * 100 / (NDIV - 1));
        {
            static const char *const D[] = {"FWD", "REV", "PING", "RAND"};
            knob_set(&k[2], "DIRECTION", D[ph->dir % NDIR], -1);
        }
        i_text(ph->swing, t);
        s_cat(t, "%");
        knob_set(&k[3], "SWING", t, (ph->swing - 50) * 4);
        break;
    case PG_TRACK:
        if (ui.sub[PG_TRACK] == 0) {
            knob_set(&k[0], "DEVICE", PROFILES[tr->prof % NPROF].tag, -1);
            i_text(tr->chan + 1, t);
            knob_set(&k[1], "CHANNEL", t, tr->chan * 100 / 15);
            knob_set(&k[2], "PORTS", PORT_NAME[port_index(tr->ports)], -1);
            signed_text(tr->transpose, t);
            knob_set(&k[3], "TRANSPOSE", t, (tr->transpose + 24) * 100 / 48);
        } else {
            knob_set(&k[0], "TYPE", tr->type == TT_DRUM ? "DRUMS" : "NOTES", -1);
            if (tr->prog == PROG_NONE)
                s_cpy(t, "-");
            else
                i_text(tr->prog + 1, t);
            knob_set(&k[1], "PROGRAM", t, tr->prog == PROG_NONE ? -1 : tr->prog * 100 / 127);
            if (tr->type == TT_DRUM) {
                note_text(tr->drum[tr->lane_sel % NDLANE], t);
                knob_set(&k[2], "LANE NOTE", t, -1);
            } else {
                knob_set(&k[2], "IN KEY", tr->quant ? "ON" : "OFF", -1);
            }
            i_text(tr->defvel, t);
            knob_set(&k[3], "VELOCITY", t, tr->defvel * 100 / 127);
        }
        break;
    case PG_LANES:
        for (i = 0; i < NLANE; i++) {
            static char lab[NLANE][12];
            if (ui.sub[PG_LANES] == 0) {
                static const char *const LBL[NLANE] = {"LANE 1", "LANE 2", "LANE 3", "LANE 4"};
                if (tr->lane_cc[i] == LANE_NONE)
                    s_cpy(t, "OFF");
                else if (tr->lane_cc[i] == LANE_PB || tr->lane_cc[i] == LANE_AT)
                    s_cpy(t, tr->lane_cc[i] == LANE_PB ? "BEND" : "PRESS");
                else {
                    s_cpy(t, "CC ");
                    i_text(tr->lane_cc[i], t + 3);
                }
                knob_set(&k[i], LBL[i], t, -1);
            } else {
                lane_text(tr->lane_cc[i], lab[i]);
                i_text(ui.lane_live[i], t);
                knob_set(&k[i], lab[i], t, ui.lane_live[i] * 100 / 127);
            }
        }
        break;
    case PG_GEN:
        if (ui.sub[PG_GEN] == 0) {
            i_text(ui.eu_steps, t);
            knob_set(&k[0], "STEPS", t, ui.eu_steps * 100 / 64);
            i_text(ui.eu_pulses, t);
            knob_set(&k[1], "PULSES", t, ui.eu_steps ? ui.eu_pulses * 100 / ui.eu_steps : 0);
            i_text(ui.eu_rot, t);
            knob_set(&k[2], "ROTATE", t, ui.eu_steps ? ui.eu_rot * 100 / ui.eu_steps : 0);
        } else {
            i_text(ui.rnd_density, t);
            s_cat(t, "%");
            knob_set(&k[0], "DENSITY", t, ui.rnd_density);
            i_text(ui.rnd_range, t);
            knob_set(&k[1], "RANGE", t, ui.rnd_range * 100 / 24);
            i_text(ui.rnd_seed, t);
            knob_set(&k[2], "SEED", t, ui.rnd_seed * 100 / 99);
        }
        if (tr->type == TT_DRUM)
            knob_set(&k[3], "LANE", drum_lane_name(tr, tr->lane_sel), -1);
        else {
            note_text(tr->rem[0] != NOTE_NONE ? tr->rem[0] : 60u, t);
            knob_set(&k[3], "NOTE", t, -1);
        }
        break;
    case PG_GLOBAL:
        switch (ui.sub[PG_GLOBAL]) {
        case 0: {
            static const char *const C[] = {"INTERNAL", "SEND", "RECEIVE"};
            tempo_text(P.bpm10, t);
            knob_set(&k[0], "TEMPO", t, (P.bpm10 - 300) * 100 / 2700);
            knob_set(&k[1], "CLOCK", C[P.clock % NCK], -1);
            knob_set(&k[2], "LAUNCH", P.launch == LA_NOW ? "NOW" : "BAR", -1);
            knob_set(&k[3], "THRU", P.thru ? "ON" : "OFF", -1);
            break;
        }
        case 1:
            knob_set(&k[0], "ROOT", NOTE_NAME[P.root % 12u], -1);
            knob_set(&k[1], "SCALE", SCALE_NAME[P.scale % NSCALE], -1);
            signed_text((int32_t)P.keyoct - 3, t);
            knob_set(&k[2], "KEYS OCT", t, -1);
            break;
        case 2:
            knob_set(&k[0], "USB", set.usb_host ? "HOST" : "DEVICE", -1);
            knob_set(&k[1], "BLUETOOTH", set.ble_off ? "OFF" : set.bt_auto ? "AUTO" : "ON", -1);
            if (FM1B_BLE_CENTRAL) {
                char name[21];
                if (btdev_found(btdev_pick(), name, 0)) {
                    name[10] = 0;
                    knob_set(&k[2], "BT DEVICE", name, -1);
                } else {
                    knob_set(&k[2], "BT DEVICE", "-", -1);
                }
            }
#if PLAT_SMK37
            knob_set(&k[3], "TRS OUT", set.trs_off ? "OFF" : "ON", -1);
#endif
            break;
        case GL_TIMING: {
            static const char *const PN[NPT] = {PLAT_SMK37 ? "USB+TRS" : "USB", "BT HOST", "BT DEVICE"};
            static const char *const CN[NPC] = {"OFF", "CLOCK", "+START"};
            uint32_t p = ui.tm_port % NPT;
            knob_set(&k[0], "PORT", PN[p], -1);
            ms_text(set.port_ms[p][0], t);
            knob_set(&k[1], "NOTES", t, (set.port_ms[p][0] + 120) * 100 / 240);
            ms_text(set.port_ms[p][1], t);
            knob_set(&k[2], "CLOCK AT", t, (set.port_ms[p][1] + 120) * 100 / 240);
            knob_set(&k[3], "CLOCK", CN[set.port_clock[p] % NPC], -1);
            break;
        }
        case GL_PROJECTS:
            i_text(ui.proj_sel + 1, t);
            knob_set(&k[0], "SLOT", t, ui.proj_sel * 100 / (NSLOTS - 1));
            break;
        default:
            break;
        }
        break;
    case PG_ARR: case PG_PERF:
        tempo_text(P.bpm10, t);
        knob_set(&k[0], "TEMPO", t, (P.bpm10 - 300) * 100 / 2700);
        knob_set(&k[1], "LAUNCH", P.launch == LA_NOW ? "NOW" : "BAR", -1);
        if (ui.page == PG_ARR) {
            static char lab[8];
            s_cpy(lab, "T");
            i_text(sel_track + 1, lab + 1);
            s_cat(lab, " PAT");
            i_text((E.t[sel_track].cue != 0xFFu ? E.t[sel_track].cue : P.pat[sel_track]) + 1, t);
            knob_set(&k[2], lab, t, -1);
            knob_set(&k[3], "MUTE", tr->muted ? "ON" : "OFF", -1);
        } else {
            knob_set(&k[2], "FILL", E.fill ? "ON" : "OFF", -1);
        }
        break;
    case PG_SONG:
        i_text(ui.song_sel + 1, t);
        knob_set(&k[0], "SLOT", P.song_n ? t : "-", -1);
        if (P.song_n) {
            i_text(P.song_scene[ui.song_sel] + 1, t);
            knob_set(&k[1], "SCENE", t, -1);
            i_text(P.song_bars[ui.song_sel], t);
            knob_set(&k[2], "BARS", t, P.song_bars[ui.song_sel] * 100 / 64);
        }
        knob_set(&k[3], "AT THE END", P.song_loop ? "LOOP" : "STOP", -1);
        break;
    default:
        break;
    }
}

static int32_t step_clamp(int32_t v, int32_t d, int32_t lo, int32_t hi) { return clamp(v + d, lo, hi); }

static void step_knob(uint32_t k, int32_t d)     /* a held step's knob: every held step moves */
{
    uint32_t i, j;
    for (i = 0; i < 16u; i++) {
        step_t *s;
        if (!(ui.grid_held >> i & 1u) || ui.bar * 16u + i >= cur_ph()->len)
            continue;
        s = grid_step(i);
        switch (ui.group) {
        case GR_NOTE:
            if (k == 0) {
                for (j = 0; j < NNOTE; j++)
                    if (s->note[j] != NOTE_NONE)
                        s->note[j] = (uint8_t)step_clamp(s->note[j], d, 0, 127);
            } else if (k == 1) {
                s->vel = (uint8_t)step_clamp(s->vel, d, 1, 127);
            } else if (k == 2) {
                s->len = (uint8_t)step_clamp(s->len, d, 1, 255);
            } else {
                s->flags = (uint8_t)(d > 0 ? s->flags | SF_GLIDE : s->flags & ~SF_GLIDE);
            }
            break;
        case GR_TIME:
            if (k == 0)
                s->micro = (int8_t)step_clamp(s->micro, d, -11, 11);
            else if (k == 1)
                s->rat = (uint8_t)((s->rat & 0xF0u) | (uint32_t)step_clamp(s->rat & 7u, d, 0, 7));
            else if (k == 2)
                s->rat = (uint8_t)((s->rat & 7u) | (uint32_t)step_clamp((s->rat >> 4) & 3u, d, 0, NRR - 1) << 4);
            else
                s->flags = (uint8_t)(d > 0 ? s->flags | SF_ACCENT : s->flags & ~SF_ACCENT);
            break;
        case GR_LOGIC:
            if (k == 0)
                s->cond = (uint8_t)step_clamp(s->cond, d, 0, NCO - 1);
            else if (k == 1)
                s->chance = (uint8_t)step_clamp(s->chance, d, 1, 100);
            break;
        default:
            if (s->lock[k] == LOCK_NONE) {
                if (d > 0)
                    s->lock[k] = 64;
            } else if (s->lock[k] == 0 && d < 0) {
                s->lock[k] = LOCK_NONE;          /* turned below 0: no lock */
            } else {
                s->lock[k] = (uint8_t)step_clamp(s->lock[k], d, 0, 127);
            }
            break;
        }
    }
    ui.used = 1;
    touched();
}

static void lane_live(uint32_t k, uint32_t v)    /* a lane's value now: sent, and recorded while recording */
{
    ui.lane_live[k] = (uint8_t)v;
    lane_send(sel_track, k, v, 0, 1);
    if (ui.rec && E.playing && E.t[sel_track].pos >= 0) {
        STEP(sel_track, P.pat[sel_track], (uint32_t)E.t[sel_track].pos)->lock[k] = (uint8_t)v;
        ui.dirty = 1;
    }
    ui.redraw = 1;
}

static void knob_turn(uint32_t k, int32_t d)
{
    track_t *tr = trk();
    phead_t *ph = cur_ph();
    if (step_edit_mode()) {
        step_knob(k, d);
        return;
    }
    switch (ui.page) {
    case PG_STEPS: case PG_OVER:
        if (k == 0)
            ph->len = (uint8_t)step_clamp(ph->len, d, 1, NSTEP);
        else if (k == 1)
            ph->div = (uint8_t)step_clamp(ph->div, d, 0, NDIV - 1);
        else if (k == 2)
            ph->dir = (uint8_t)step_clamp(ph->dir, d, 0, NDIR - 1);
        else
            ph->swing = (uint8_t)step_clamp(ph->swing, d, 50, 75);
        if (ui.bar * 16u >= ph->len)
            ui.bar = (uint8_t)((ph->len - 1u) / 16u);
        break;
    case PG_TRACK:
        if (ui.sub[PG_TRACK] == 0) {
            if (k == 0) {
                profile_apply(tr, (uint32_t)step_clamp(tr->prof, d, 0, NPROF - 1));
                app_say("DEVICE", PROFILES[tr->prof].name);
            } else if (k == 1) {
                tr->chan = (uint8_t)step_clamp(tr->chan, d, 0, 15);
            } else if (k == 2) {
                tr->ports = PORT_SETS[step_clamp(port_index(tr->ports), d, 0, NPORTSET - 1)];
            } else {
                tr->transpose = (int8_t)step_clamp(tr->transpose, d, -24, 24);
            }
        } else {
            if (k == 0)
                tr->type = (uint8_t)step_clamp(tr->type, d, 0, NTT - 1);
            else if (k == 1)
                tr->prog = (uint8_t)(tr->prog == PROG_NONE ? (d > 0 ? 0 : PROG_NONE)
                                     : (tr->prog == 0 && d < 0) ? PROG_NONE : step_clamp(tr->prog, d, 0, 127));
            else if (k == 2 && tr->type == TT_DRUM)
                tr->drum[tr->lane_sel % NDLANE] = (uint8_t)step_clamp(tr->drum[tr->lane_sel % NDLANE], d, 0, 127);
            else if (k == 2)
                tr->quant = d > 0;
            else
                tr->defvel = (uint8_t)step_clamp(tr->defvel, d, 1, 127);
        }
        break;
    case PG_LANES:
        if (ui.sub[PG_LANES] == 0) {
            int32_t v = tr->lane_cc[k] == LANE_NONE ? -1 : tr->lane_cc[k];
            v = step_clamp(v, d, -1, 129);       /* off, CC 0..127, bend, pressure */
            tr->lane_cc[k] = (uint8_t)(v < 0 ? LANE_NONE : v);
        } else {
            lane_live(k, (uint32_t)step_clamp(ui.lane_live[k], d, 0, 127));
        }
        break;
    case PG_GEN:
        if (k == 3) {
            if (tr->type == TT_DRUM)
                tr->lane_sel = (uint8_t)step_clamp(tr->lane_sel, d, 0, (int32_t)drum_lanes(tr) - 1);
            else
                tr->rem[0] = (uint8_t)step_clamp(tr->rem[0] != NOTE_NONE ? tr->rem[0] : 60, d, 0, 127);
        } else if (ui.sub[PG_GEN] == 0) {
            if (k == 0)
                ui.eu_steps = (uint8_t)step_clamp(ui.eu_steps, d, 1, NSTEP);
            else if (k == 1)
                ui.eu_pulses = (uint8_t)step_clamp(ui.eu_pulses, d, 0, ui.eu_steps);
            else
                ui.eu_rot = (uint8_t)step_clamp(ui.eu_rot, d, 0, ui.eu_steps - 1);
            if (ui.eu_pulses > ui.eu_steps)
                ui.eu_pulses = ui.eu_steps;
            if (ui.eu_rot >= ui.eu_steps)
                ui.eu_rot = (uint8_t)(ui.eu_steps - 1u);
        } else {
            if (k == 0)
                ui.rnd_density = (uint8_t)step_clamp(ui.rnd_density, d * 5, 0, 100);
            else if (k == 1)
                ui.rnd_range = (uint8_t)step_clamp(ui.rnd_range, d, 0, 24);
            else
                ui.rnd_seed = (uint8_t)step_clamp(ui.rnd_seed, d, 0, 99);
        }
        ui.redraw = 1;
        return;                                  /* (the generator's settings are not the project's) */
    case PG_GLOBAL:
        switch (ui.sub[PG_GLOBAL]) {
        case GL_CLOCK:
            if (k == 0)
                P.bpm10 = (uint16_t)step_clamp(P.bpm10, d * ((ui.btn >> B_HOME & 1u) ? 1 : 10), 300, 3000);
            else if (k == 1)
                P.clock = (uint8_t)step_clamp(P.clock, d, 0, NCK - 1);
            else if (k == 2)
                P.launch = (uint8_t)step_clamp(P.launch, d, 0, NLA - 1);
            else
                P.thru = d > 0;
            break;
        case 1:
            if (k == 0)
                P.root = (uint8_t)((P.root + 12 + (d % 12)) % 12);
            else if (k == 1)
                P.scale = (uint8_t)step_clamp(P.scale, d, 0, NSCALE - 1);
            else if (k == 2)
                P.keyoct = (uint8_t)step_clamp(P.keyoct, d, 0, 6);
            break;
        case 2:
            if (k == 0) {
                set.usb_host = d > 0;
                plat_usb_host(set.usb_host);
                settings_save();
                app_say("USB", set.usb_host ? "HOST: POWERED HUB" : "COMPUTER");
            } else if (k == 1) {                 /* OFF, ON, AUTO (on, and back to the last device by itself) */
                int32_t v = set.ble_off ? 0 : set.bt_auto ? 2 : 1;
                v = step_clamp(v, d, 0, 2);
                set.ble_off = v == 0;
                set.bt_auto = v == 2;
                plat_ble_enable(!set.ble_off);
                settings_save();
            } else if (k == 2 && FM1B_BLE_CENTRAL) {
                btdev_choose(btdev_pick() + d);
                ui.bt_paused = 0;
            }
#if PLAT_SMK37
            else if (k == 3) {
                set.trs_off = d < 0;
                plat_trs_out(!set.trs_off);
                settings_save();
            }
#endif
            ui.redraw = 1;
            return;
        case GL_TIMING: {
            uint32_t p = ui.tm_port % NPT;
            int32_t step = (ui.btn >> B_HOME & 1u) ? 1 : 5;   /* 5 ms a detent; HOME held: 1 */
            if (k == 0)
                ui.tm_port = (uint8_t)step_clamp(ui.tm_port, d, 0, NPT - 1);
            else if (k == 1 || k == 2)
                set.port_ms[p][k - 1u] = (int8_t)step_clamp(set.port_ms[p][k - 1u], d * step, -120, 120);
            else
                set.port_clock[p] = (uint8_t)step_clamp(set.port_clock[p], d, 0, NPC - 1);
            if (k)
                settings_save(), settings_apply();
            ui.redraw = 1;
            return;
        }
        case GL_PROJECTS:
            if (k == 0)
                ui.proj_sel = (uint8_t)step_clamp(ui.proj_sel, d, 0, NSLOTS - 1);
            ui.redraw = 1;
            return;
        default:
            return;
        }
        break;
    case PG_ARR: case PG_PERF:
        if (k == 0) {
            P.bpm10 = (uint16_t)step_clamp(P.bpm10, d * ((ui.btn >> B_HOME & 1u) ? 1 : 10), 300, 3000);
        } else if (k == 1) {
            P.launch = (uint8_t)step_clamp(P.launch, d, 0, NLA - 1);
        } else if (k == 2 && ui.page == PG_ARR) {
            uint32_t cur = E.t[sel_track].cue != 0xFFu ? E.t[sel_track].cue : P.pat[sel_track];
            seq_cue_pattern(sel_track, (uint32_t)step_clamp((int32_t)cur, d, 0, NPAT - 1));
            scene_store();
        } else if (k == 2) {
            E.fill = d > 0;
        } else if (k == 3 && ui.page == PG_ARR) {
            tr->muted = d > 0;
            scene_store();
        }
        break;
    case PG_SONG:
        if (k == 0 && P.song_n)
            ui.song_sel = (uint8_t)step_clamp(ui.song_sel, d, 0, P.song_n - 1);
        else if (k == 1 && P.song_n)
            P.song_scene[ui.song_sel] = (uint8_t)step_clamp(P.song_scene[ui.song_sel], d, 0, NSCENE - 1);
        else if (k == 2 && P.song_n)
            P.song_bars[ui.song_sel] = (uint8_t)step_clamp(P.song_bars[ui.song_sel], d, 1, 64);
        else if (k == 3)
            P.song_loop = d > 0;
        break;
    default:
        return;
    }
    touched();
}

/* ------------------------------------------------------------------- the grid and the side --- */
static void grid_press(uint32_t i)
{
    input_seen();
    uint32_t now = plat_ms();
    switch (ui.page) {
    case PG_STEPS:
        if (ui.srec) {                           /* step record: the cursor goes there */
            ui.srec_pos = (uint8_t)(ui.bar * 16u + i);
            ui.redraw = 1;
            return;
        }
        if (!ui.grid_held) {
            ui.used = 0;
            ui.hold_notes = 0;
        } else {
            ui.used = 1;                         /* a second step: editing them together */
        }
        ui.grid_held |= 1u << i;
        ui.grid_t[i] = now;
        ui.redraw = 1;
        break;
    case PG_ARR: {
        if (!P.scene_used[i]) {                  /* an empty scene: a copy of what plays now */
            uint32_t from = P.scene;
            P.scene = (uint8_t)i;
            scene_store();
            P.scene = (uint8_t)from;
            P.scene_used[i] = 1;
        }
        seq_cue_scene(i, E.scene_cue == i || P.launch == LA_NOW);
        app_say("SCENE", E.scene_cue == i ? "AT THE BAR" : "");
        touched();
        break;
    }
    case PG_SONG:
        if (P.song_n < NSLOT && P.scene_used[i]) {
            uint32_t at = P.song_n ? ui.song_sel + 1u : 0u, k;
            for (k = P.song_n; k > at; k--) {
                P.song_scene[k] = P.song_scene[k - 1u];
                P.song_bars[k] = P.song_bars[k - 1u];
            }
            P.song_scene[at] = (uint8_t)i;
            P.song_bars[at] = (uint8_t)(at ? P.song_bars[at - 1u] : 4u);
            P.song_n++;
            ui.song_sel = (uint8_t)at;
            touched();
        } else if (!P.scene_used[i]) {
            app_say("SCENE", "EMPTY");
        }
        break;
    case PG_PERF:
        if (ui.btn >> B_HOME & 1u)
            E.solo ^= (uint16_t)(1u << i);
        else {
            P.trk[i].muted ^= 1u;
            scene_store();
        }
        touched();
        break;
    default:                                     /* the other pages: the grid picks the track */
        sel_track = (uint8_t)i;
        ui.bar = 0;
        ui.redraw = 1;
        break;
    }
}

static void grid_release(uint32_t i)
{
    if (!(ui.grid_held >> i & 1u))
        return;
    ui.grid_held &= ~(1u << i);
    if (ui.page == PG_STEPS && !ui.used && plat_ms() - ui.grid_t[i] < 450u)
        step_tap(i);
    if (!ui.grid_held)
        ui.used = 0;
    ui.redraw = 1;
}

static void side_press(uint32_t i)               /* the FM-1's black keys */
{
    input_seen();
    track_t *t = trk();
    if (ui.page == PG_STEPS && ui.grid_held) {
        if (i < NGR) {
            ui.group = (uint8_t)i;
            ui.used = 1;
        }
    } else if (ui.page == PG_STEPS && t->type == TT_DRUM) {
        if (i < drum_lanes(t)) {
            t->lane_sel = (uint8_t)i;
            live_on(sel_track, t->drum[i], t->defvel);
            live_off(t->drum[i]);
        }
    } else if (ui.page == PG_PERF && i == 0) {
        E.fill = 1;
    } else if (ui.page == PG_ARR || ui.page == PG_OVER || ui.page == PG_PERF) {
        P.trk[i].muted ^= 1u;
        scene_store();
        touched();
    }
    ui.redraw = 1;
}

static void side_release(uint32_t i)
{
    if (ui.page == PG_PERF && i == 0)
        E.fill = 0;
    ui.redraw = 1;
}

/* ------------------------------------------------------------------- buttons --- */
static void page_go(uint32_t pg)
{
    if (ui.page == pg) {
        ui.sub[pg] = (uint8_t)((ui.sub[pg] + 1u) % PG_SUBS[pg]);
    } else {
        ui.page = (uint8_t)pg;
    }
    ui.grid_held = 0;
    ui.menu = 0;
    ui.redraw = 1;
}

static void transport_play(void)
{
    if (E.playing) {
        seq_stop();
        ui.rec = 0;
    } else {
        if (ui.page == PG_SONG && P.song_n) {
            E.slot = ui.song_sel;
            E.song_on = 1;
        }
        seq_start(0);
    }
    ui.redraw = 1;
}

static void button_press(uint32_t b)
{
    int home = (int)(ui.btn >> B_HOME & 1u) && b != B_HOME;
    ui.hold_b = (uint8_t)b;                      /* a card if it stays held alone (app_frame) */
    ui.hold_t = plat_ms();
    ui.hold_clean = ui.btn == 1u << b;
    if (home)
        ui.home_used = 1;
    if (b == B_HOME) {
        ui.btn_t[B_HOME] = plat_ms();
        ui.home_used = ui.menu;
    }
    if (ui.menu) {                               /* the menu: SEL picks, HOME leaves, the rest closes it */
        if (b == B_SEL)
            menu_do(menu_items[ui.menu_i % ui.menu_n]);
        else {
            ui.menu = 0;
            ui.menu_confirm = 0;
        }
        ui.redraw = 1;
        if (b == B_SEL || b == B_HOME)
            return;
    }
    switch (b) {
    case B_PLAY:
        if ((ui.btn >> B_REC & 1u) && !E.playing)
            ui.rec = 1;                          /* REC held + PLAY: play and record */
        transport_play();
        break;
    case B_REC:
        if (E.playing) {
            ui.rec ^= 1u;
            app_say("RECORD", ui.rec ? "ON" : "OFF");
        } else {
            ui.srec ^= 1u;
            ui.srec_n = 0;
            ui.srec_pos = (uint8_t)(ui.bar * 16u);
            if (ui.srec)
                ui.page = PG_STEPS;
            app_say("STEP RECORD", ui.srec ? "ON" : "OFF");
        }
        break;
    case B_SEQ: if (!home) page_go(PG_STEPS); else pat_copy(); break;
    case B_EDIT: if (!home) page_go(PG_TRACK); else pat_paste(); break;
    case B_ENV: page_go(PG_LANES); break;
    case B_FX: page_go(PG_PERF); break;
    case B_LFO: page_go(PG_OVER); break;
    case B_ARP:
        if (home || (ui.page == PG_GEN))
            gen_print();
        else
            page_go(PG_GEN);
        break;
    case B_GLO:
        if (home) {
            panic_all();
            app_say("ALL NOTES", "OFF");
        } else {
            page_go(PG_GLOBAL);
        }
        break;
    case B_SAVE: project_save_say(); break;
    case B_SEL:
        if (home) {
            undo_do();
            break;
        }
        ui.menu_n = (uint8_t)menu_build();
        ui.menu = 1;
        ui.menu_i = 0;
        ui.menu_confirm = 0;
        break;
    case B_OCTDN: case B_OCTUP: {
        int d = b == B_OCTUP ? 1 : -1;
        if (ui.srec && !PLAT_SMK37) {           /* step record: OCT+ a rest, OCT- back */
            ui.srec_pos = (uint8_t)((ui.srec_pos + cur_ph()->len + (uint32_t)d) % cur_ph()->len);
            ui.bar = (uint8_t)(ui.srec_pos / 16u);
        } else if (PLAT_SMK37) {
            ui.smk_oct = (int8_t)clamp(ui.smk_oct + d, -3, 3);
            app_say("KEYBED OCTAVE", ui.smk_oct > 0 ? "+" : ui.smk_oct < 0 ? "-" : "0");
        } else if (ui.btn >> B_SEQ & 1u || ui.page != PG_STEPS) {
            P.keyoct = (uint8_t)clamp((int32_t)P.keyoct + d, 0, 6);
        } else {
            uint32_t bars = (cur_ph()->len + 15u) / 16u;
            ui.bar = (uint8_t)clamp((int32_t)ui.bar + d, 0, (int32_t)bars - 1);
        }
        break;
    }
#if PLAT_SMK37
    case B_LEFT: case B_RIGHT: {
        int d = b == B_RIGHT ? 1 : -1;
        if (ui.srec) {
            ui.srec_pos = (uint8_t)((ui.srec_pos + cur_ph()->len + (uint32_t)d) % cur_ph()->len);
            ui.bar = (uint8_t)(ui.srec_pos / 16u);
        } else if (ui.page == PG_STEPS && ui.grid_held) {
            ui.group = (uint8_t)((ui.group + NGR + d) % NGR);
            ui.used = 1;
        } else {
            uint32_t bars = (cur_ph()->len + 15u) / 16u;
            ui.bar = (uint8_t)clamp((int32_t)ui.bar + d, 0, (int32_t)bars - 1);
        }
        break;
    }
    case B_KBANK: page_go(PG_PERF); break;
    case B_FBANK: E.fill = 1; break;             /* FADER BANK held: fill */
#endif
    default:
        break;
    }
    ui.redraw = 1;
}

static void button_release(uint32_t b)
{
    if (b == ui.hold_b) {
        ui.hold_b = NB;
        if (ui.card != NB) {                     /* the card goes; the button does nothing more */
            ui.card = NB;
            ui.redraw = 1;
            if (b == B_HOME)
                ui.home_used = 1;
            return;
        }
    }
    if (b == B_HOME && plat_ms() - ui.btn_t[B_HOME] < 400u && !ui.home_used && !ui.menu) {
        page_go(ui.page == PG_ARR ? PG_SONG : PG_ARR);   /* a tap: Arrange, again: Song */
        ui.sub[PG_ARR] = ui.sub[PG_SONG] = 0;
    }
#if PLAT_SMK37
    if (b == B_FBANK)
        E.fill = 0;
#endif
    ui.redraw = 1;
}

/* ------------------------------------------------------------------- the encoders --- */
static void encoders(void)
{
    int32_t d;
    uint32_t k;
    if ((d = plat_enc(EN_SELECT)) != 0) {
        input_seen();
        if (ui.menu) {
            ui.menu_i = (int8_t)clamp(ui.menu_i + d, 0, ui.menu_n - 1);
            ui.menu_confirm = 0;
        } else if (step_edit_mode()) {
            ui.group = (uint8_t)clamp((int32_t)ui.group + d, 0, NGR - 1);
            ui.used = 1;
        } else if (PG_SUBS[ui.page] > 1u) {
            ui.sub[ui.page] = (uint8_t)clamp((int32_t)ui.sub[ui.page] + d, 0, PG_SUBS[ui.page] - 1);
        }
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_ALGO)) != 0) {
        input_seen();
        sel_track = (uint8_t)clamp((int32_t)sel_track + d, 0, NTRK - 1);
        ui.grid_held = 0;
        if (ui.bar * 16u >= cur_ph()->len)
            ui.bar = 0;
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_PRESET)) != 0) {
        input_seen();
        if (ui.btn >> B_HOME & 1u) {             /* HOME + PRESETS: the next scene */
            int32_t sc;
            ui.home_used = 1;
            sc = clamp((int32_t)(E.scene_cue != 0xFFu ? E.scene_cue : P.scene) + d, 0, NSCENE - 1);
            seq_cue_scene((uint32_t)sc, 0);
        } else if (ui.page == PG_STEPS && trk()->type == TT_DRUM && !ui.grid_held) {
            trk()->lane_sel = (uint8_t)clamp(trk()->lane_sel + d, 0, (int32_t)drum_lanes(trk()) - 1);
        } else {
            uint32_t cur = E.t[sel_track].cue != 0xFFu ? E.t[sel_track].cue : P.pat[sel_track];
            seq_cue_pattern(sel_track, (uint32_t)clamp((int32_t)cur + d, 0, NPAT - 1));
            scene_store();
            ui.dirty = 1;
        }
        ui.redraw = 1;
    }
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) {
            input_seen();
            if (ui.btn >> B_HOME & 1u)
                ui.home_used = 1;
            knob_turn(k, d);
        }
}

/* ------------------------------------------------------------------- the keys --- */
static int fm1_keyboard(void)                    /* the FM-1's keys play notes now */
{
    if (PLAT_SMK37)
        return 0;
    if (ui.srec || (ui.rec && E.playing) || (ui.btn >> B_SEQ & 1u))
        return 1;
    return ui.page == PG_TRACK || ui.page == PG_LANES || ui.page == PG_GEN || ui.page == PG_GLOBAL;
}

static void keys_fm1(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    static uint8_t key_was_note[NKEYS];          /* a key that went down as a note ends as a note */
    uint32_t keys = plat_keys(), i, kbd = (uint32_t)fm1_keyboard();
    uint32_t pressed = keys & ~ui.keys, released = ui.keys & ~keys;
    for (i = 0; i < NKEYS; i++) {
        uint32_t bit = 1u << i, note = (uint32_t)clamp(53 + (int32_t)i + 12 * ((int32_t)P.keyoct - 3), 0, 127);
        if (pressed & bit) {
            key_was_note[i] = (uint8_t)kbd;
            if (kbd) {
                note_event(note, trk()->defvel, 1, 1);
                continue;
            }
        } else if (released & bit) {
            if (key_was_note[i]) {
                note_event(note, 0, 0, 1);
                continue;
            }
        } else {
            continue;
        }
        {
            uint32_t w, b;
            for (w = 0; w < 16u; w++)
                if (WK[w] == i) {
                    if (pressed & bit)
                        grid_press(w);
                    else
                        grid_release(w);
                }
            for (b = 0; b < 11u; b++)
                if (BK[b] == i) {
                    if (pressed & bit) {
                        ui.side_held |= 1u << b;
                        side_press(b);
                    } else {
                        ui.side_held &= ~(1u << b);
                        side_release(b);
                    }
                }
        }
    }
    ui.keys = keys;
}

#if PLAT_SMK37
static void keys_smk(void)
{
    uint64_t now = plat_keys37(), hit = plat_keys37_pressed(), re = plat_keys37_retrig(), up = ui.keys37 & ~now;
    uint32_t i, pads = plat_pads(), pressed = plat_pads_pressed();
    static uint32_t pads_was;
    for (i = 0; i < NKEYS37; i++) {
        uint32_t note = (uint32_t)clamp(48 + (int32_t)i + 12 * ui.smk_oct, 0, 127);
        if (((re >> i) & 1u) && !((hit >> i) & 1u)) {  /* struck again while down: end it, start it again */
            note_event(note, 0, 0, 1);
            note_event(note, (uint32_t)plat_key_vel((int)i), 1, 1);
        } else if ((hit >> i) & 1u)
            note_event(note, (uint32_t)plat_key_vel((int)i), 1, 1);
        else if ((up >> i) & 1u)
            note_event(note, 0, 0, 1);
    }
    ui.keys37 = now;
    for (i = 0; i < 16u; i++) {                  /* pads: 0..7 the top row (steps 1-8), 8..15 the bottom (9-16) */
        uint32_t bit = 1u << i;
        if (pressed & bit)
            grid_press(i);
        else if ((pads_was & bit) && !(pads & bit))
            grid_release(i);
    }
    pads_was = pads;
    for (i = 0; i < 4u; i++) {                   /* faders: lanes 1-4, live */
        int v = plat_fader((int)i);
        if (v >= 0 && v != ui.last_fader[i]) {
            if (ui.last_fader[i] >= 0)
                lane_live(i, (uint32_t)v);
            ui.last_fader[i] = (int8_t)v;
        }
    }
    {                                            /* the wheels: to the selected track */
        static int pb_was = 0, mod_was = -1;
        int pb = plat_wheel(PLAT_WHEEL_PITCH), md = plat_wheel(PLAT_WHEEL_MOD);
        const track_t *t = trk();
        if (mod_was < 0)
            mod_was = md;                        /* (the first reading: where the wheel rests, nothing sent) */
        if (pb != pb_was) {
            uint32_t v = (uint32_t)(pb + 8192);
            midi_send(pkt3(0xE0u | (t->chan & 15u), v & 0x7Fu, v >> 7), t->ports);
            pb_was = pb;
        }
        if (md != mod_was) {
            midi_send(pkt3(0xB0u | (t->chan & 15u), 1, (uint32_t)md), t->ports);
            mod_was = md;
        }
    }
}
#endif

static void midi_in_ui(void)                     /* what came in over MIDI, for the UI (thru has played it) */
{
    while (in_r != in_w) {
        in_t m = inq[in_r % NIN];
        uint32_t ty = m.st & 0xF0u;
        in_r++;
        if (ty == 0x90u && m.d2)
            note_event(m.d1, m.d2, 1, 0);
        else if (ty == 0x80u || ty == 0x90u)
            note_event(m.d1, 0, 0, 0);
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), b;
    uint32_t pressed = btn & ~ui.btn, released = ui.btn & ~btn;
    ui.btn = btn;
    for (b = 0; b < NB; b++) {
        if (pressed >> b & 1u)
            button_press(b);
        else if (released >> b & 1u)
            button_release(b);
    }
    encoders();
#if PLAT_SMK37
    keys_smk();
#else
    keys_fm1();
#endif
    midi_in_ui();
}

void app_input_only(void) { input(); }
