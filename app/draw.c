/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* The screen, as LOOM-8 lays it out: the header (22 rows), the page (130), then the tag line, the four knobs and the
 * two hint lines (88). Each band is drawn into fm1-base's canvas and blitted (gfx.c). Every track keeps its colour;
 * cream outlines what you hold; red is only recording and locks. Also the lights: the FM-1's buttons and keys, the
 * SMK-37's buttons and its 16 RGB pads. */

static const uint32_t TC24[NTRK] = {0xF2B84B, 0xE8794A, 0x5FD1C0, 0x6AA8FF, 0xC48CFF, 0xFF7AA8, 0x9BD65C, 0xE6D36A,
                                    0x4FC3E8, 0xFF9A6B, 0x8F9BFF, 0x6FDC9A, 0xD9A066, 0xB0C46A, 0xF0A0E0, 0xA8B8C0};
#define C24(c) RGB((c) >> 16 & 0xFFu, (c) >> 8 & 0xFFu, (c) & 0xFFu)
#define K_BG RGB(12, 15, 16)
#define K_PANEL RGB(26, 31, 33)
#define K_LINE RGB(44, 51, 54)
#define K_FG RGB(236, 231, 216)
#define K_DIM RGB(127, 137, 133)
#define K_HI RGB(255, 251, 234)
#define K_REC RGB(239, 75, 75)
#define K_OK RGB(123, 227, 168)

static uint16_t mix24(uint32_t c, uint32_t pct, uint32_t bg)   /* c over bg at pct % */
{
    uint32_t r = ((c >> 16 & 255u) * pct + (bg >> 16 & 255u) * (100u - pct)) / 100u;
    uint32_t g = ((c >> 8 & 255u) * pct + (bg >> 8 & 255u) * (100u - pct)) / 100u;
    uint32_t b = ((c & 255u) * pct + (bg & 255u) * (100u - pct)) / 100u;
    return RGB(r, g, b);
}
#define PANEL24 0x1A1F21u
static uint16_t tcol(uint32_t t) { return C24(TC24[t % NTRK]); }

#if PLAT_SMK37                                   /* the controls by their printed names */
#define N_SEL "SCALE"
#define N_HOME "PATCH"
#define N_GRID "PADS"
#define N_SIDE "KEYS"
#define N_BAR "< >"
#define N_SELECT "K6"
#define N_ALGO "K7"
#define N_PRESET "K8"
#define N_SAVE "BT"
#else
#define N_SEL "SEL"
#define N_HOME "HOME"
#define N_GRID "WHITE"
#define N_SIDE "BLACK"
#define N_BAR "OCT"
#define N_SELECT "SELECT"
#define N_ALGO "ALGO"
#define N_PRESET "PRESETS"
#define N_SAVE "SAVE"
#endif

static const char *const PG_TITLE[NPG] = {"Steps", "Track", "Lanes", "Perform", "Overview", "Generate", "Global",
                                          "Arrange", "Song"};
static const char *const PG_BTN[NPG] = {"SEQ", PLAT_SMK37 ? "PARA" : "EDIT", PLAT_SMK37 ? "CHORD" : "ENV",
                                        PLAT_SMK37 ? "KBANK" : "FX", PLAT_SMK37 ? "REPEAT" : "LFO", "ARP",
                                        PLAT_SMK37 ? "GLOBE" : "GLO", N_HOME, N_HOME};

static void boxed(int32_t x, int32_t y, const char *s, uint16_t c)   /* a button's name in a box */
{
    int32_t w = text_w(&FONT_T, s) + 6;
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + 11, w, 1, c);
    cv_rect(x, y, 1, 12, c);
    cv_rect(x + w - 1, y, 1, 12, c);
    cv_text(x + 3, y + 1, &FONT_T, s, c);
}

static void outline(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

/* ------------------------------------------------------------------- the header --- */
static void draw_header(void)
{
    char t[24];
    uint32_t now = plat_ms();
    int32_t x = 5;
    cv_begin(240, 22, K_BG);
    if (now < ui.msg_until) {
        x = cv_text(6, 3, &FONT_B, ui.msg[0], K_HI);
        cv_text(x + 8, 3, &FONT_B, ui.msg[1], tcol(sel_track));
        cv_blit(0, 0);
        return;
    }
    if (ui.page != PG_ARR && ui.page != PG_SONG && ui.page != PG_GLOBAL) {
        s_cpy(t, "T");
        i_text(sel_track + 1, t + 1);
        cv_rect(x, 4, text_w(&FONT_XS, t) + 8, 14, tcol(sel_track));
        x = cv_text(x + 4, 4, &FONT_XS, t, K_BG) + 8;
    }
    boxed(x, 5, PG_BTN[ui.page], K_FG);
    x += text_w(&FONT_T, PG_BTN[ui.page]) + 11;
    cv_text(x, 3, &FONT_B, ui.menu ? "Menu" : step_edit_mode() ? "Step" : PG_TITLE[ui.page], K_FG);
    /* the right: recording, unsaved, the scene, the tempo */
    tempo_text(tempo10_now(), t);
    x = 235 - text_w(&FONT_B, t);
    cv_text(x, 3, &FONT_B, t, P.clock == CK_RECEIVE && E.follow ? K_OK : K_FG);
    s_cpy(t, "S");
    i_text(P.scene + 1, t + 1);
    x -= text_w(&FONT_XS, t) + 6;
    cv_text(x, 6, &FONT_XS, t, K_DIM);
    if (E.playing) {                             /* a play triangle */
        int32_t i;
        x -= 11;
        for (i = 0; i < 5; i++)
            cv_rect(x + i, 7 + i / 2, 1, 9 - (i / 2) * 2, ui.rec ? K_REC : K_OK);
    }
    if (ui.rec || ui.srec) {
        x -= 9;
        cv_rect(x, 8, 6, 6, K_REC);
    }
    if (ui.save_pending) {                       /* SAVE pressed while playing: it saves at the stop */
        x -= text_w(&FONT_T, "SAVE AT STOP") + 6;
        cv_text(x, 6, &FONT_T, "SAVE AT STOP", K_HI);
    } else if (ui.dirty) {
        x -= 8;
        cv_text(x, 3, &FONT_B, "*", K_DIM);
    }
    {                                            /* MIDI going out: USB, Bluetooth */
        int32_t px = x - 10, p;
        for (p = 0; p < 2; p++)
            cv_rect(px + p * 5, 10, 3, 3, plat_midi_ago(p == 0 ? PORT_USB : PORT_BLE, 1) < 150u ? K_OK : K_LINE);
    }
    cv_blit(0, 0);
}

/* ------------------------------------------------------------------- the steps --- */
static void step_marks(const step_t *s, int32_t x, int32_t y, int32_t w, uint16_t c)
{
    char t[8];
    int32_t mx = x + 2;
    uint32_t i, r = (s->rat & 7u) + 1u;
    if (r > 1u) {
        for (i = 0; i < r && i < 6u; i++)
            cv_rect(mx + (int32_t)i * 2, y, 1, 5, c);
        mx += (int32_t)(r < 6u ? r : 6u) * 2 + 2;
    }
    if (s->cond != CO_ALWAYS) {
        cond_text(s->cond, t);
        cv_text(mx, y - 4, &FONT_T, t, c);
    } else if (s->chance < 100u) {
        i_text(s->chance, t);
        cv_text(mx, y - 4, &FONT_T, t, c);
    }
    if (s->flags & SF_GLIDE)
        cv_rect(x + w - 9, y + 1, 7, 2, c);           /* glide: a dash */
    for (i = 0; i < NLANE; i++)
        if (s->lock[i] != LOCK_NONE) {
            cv_rect(x + w - 5, 3 + (y - 39), 3, 3, K_REC);
            break;
        }
    if (s->micro)
        cv_rect(x + w / 2 - 3 + s->micro / 3, y - 38, 6, 1, K_HI);
}

static void draw_bars_tabs(int32_t y)            /* the pattern's bars: the one shown, the one playing */
{
    uint32_t bars = (cur_ph()->len + 15u) / 16u, b, play_bar = 0xFF;
    int32_t x = 235 - (int32_t)bars * 13;
    char t[2] = {0, 0};
    if (E.playing && E.t[sel_track].pos >= 0)
        play_bar = (uint32_t)E.t[sel_track].pos / 16u;
    for (b = 0; b < bars; b++) {
        uint16_t bg = b == ui.bar ? tcol(sel_track) : K_PANEL;
        cv_rect(x, y, 11, 11, bg);
        t[0] = (char)('1' + b);
        cv_text(x + 3, y, &FONT_T, t, b == ui.bar ? K_BG : K_DIM);
        if (b == play_bar)
            cv_rect(x, y + 12, 11, 2, K_HI);
        x += 13;
    }
}

static void draw_note_steps(void)
{
    uint32_t i, len = cur_ph()->len;
    int32_t play = E.playing ? E.t[sel_track].pos : -1;
    char t[16];
    const track_t *tr = trk();
    s_cpy(t, PROFILES[tr->prof % NPROF].tag);
    s_cat(t, " ch");
    i_text(tr->chan + 1, t + text_len(t));
    cv_text(5, 1, &FONT_XS, t, K_FG);
    if (tr->rem[0] != NOTE_NONE) {
        char n[8];
        s_cpy(t, "notes ");
        note_text(tr->rem[0], n);
        s_cat(t, n);
        if (tr->rem[1] != NOTE_NONE)
            s_cat(t, "+");
        cv_text(80, 1, &FONT_XS, t, K_DIM);
    }
    draw_bars_tabs(2);
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bar * 16u + i;
        int32_t x = 5 + (int32_t)(i % 8u) * 29, y = 18 + (int32_t)(i / 8u) * 54;
        const step_t *s = grid_step(i);
        uint16_t bg = K_PANEL, fg = K_DIM;
        int on = s->note[0] != NOTE_NONE;
        if (si >= len) {
            cv_rect(x, y, 27, 50, RGB(18, 21, 22));
            continue;
        }
        if (on) {
            bg = mix24(TC24[sel_track], 38, PANEL24);
            fg = K_FG;
        }
        if ((int32_t)si == play && on) {
            bg = tcol(sel_track);
            fg = K_BG;
        } else if ((int32_t)si == play) {
            bg = K_LINE;
        }
        cv_rect(x, y, 27, 50, bg);
        i_text((int32_t)si + 1, t);
        cv_text(x + 2, y + 1, &FONT_T, t, (int32_t)si == play && on ? K_BG : K_DIM);
        if (on) {
            note_text(s->note[0], t);
            cv_text(x + (27 - text_w(&FONT_XS, t)) / 2, y + 16, &FONT_XS, t, fg);
            if (s->note[1] != NOTE_NONE) {
                uint32_t n = 1;
                while (n < NNOTE && s->note[n] != NOTE_NONE)
                    n++;
                t[0] = '+';
                i_text((int32_t)n - 1, t + 1);
                cv_text(x + (27 - text_w(&FONT_T, t)) / 2, y + 28, &FONT_T, t, fg);
            }
        }
        step_marks(s, x, y + 44, 27, fg);
        if (ui.grid_held >> i & 1u)
            outline(x, y, 27, 50, K_HI);
        if (ui.srec && ui.srec_pos == si)
            outline(x, y, 27, 50, K_REC);
    }
}

static void draw_drum_steps(void)
{
    const track_t *tr = trk();
    uint32_t nl = drum_lanes(tr), first, l, i, len = cur_ph()->len;
    int32_t play = E.playing ? E.t[sel_track].pos : -1;
    char t[16];
    s_cpy(t, PROFILES[tr->prof % NPROF].tag);
    s_cat(t, " ch");
    i_text(tr->chan + 1, t + text_len(t));
    cv_text(5, 1, &FONT_XS, t, K_FG);
    cv_text(80, 1, &FONT_XS, drum_lane_name(tr, tr->lane_sel), K_HI);
    draw_bars_tabs(2);
    first = tr->lane_sel > 3u ? tr->lane_sel - 3u : 0u;
    if (nl > 8u && first > nl - 8u)
        first = nl - 8u;
    if (nl <= 8u)
        first = 0;
    for (i = 0; i < 16u; i += 4u) {
        i_text((int32_t)(ui.bar * 16u + i + 1u), t);
        cv_text(42 + (int32_t)i * 12, 16, &FONT_T, t, K_DIM);
    }
    for (l = first; l < first + 8u && l < nl; l++) {
        int32_t y = 28 + (int32_t)(l - first) * 13;
        uint32_t note = tr->drum[l];
        cv_text(3, y, &FONT_T, drum_lane_name(tr, l), l == tr->lane_sel ? K_HI : K_FG);
        if (l == tr->lane_sel)
            cv_rect(3, y + 11, 34, 1, K_HI);
        for (i = 0; i < 16u; i++) {
            uint32_t si = ui.bar * 16u + i;
            const step_t *s = grid_step(i);
            int32_t x = 40 + (int32_t)i * 12;
            uint16_t c = si >= len ? RGB(18, 21, 22) : K_PANEL;
            if (si < len && s->note[0] != NOTE_NONE && step_has(s, note))
                c = s->vel >= 100u || (s->flags & SF_ACCENT) ? tcol(sel_track) : mix24(TC24[sel_track], 55, PANEL24);
            cv_rect(x, y, 11, 11, c);
            if ((int32_t)si == play)
                outline(x, y, 11, 11, K_HI);
            if (ui.grid_held >> i & 1u)
                outline(x, y, 11, 11, K_FG);
        }
    }
}

static void draw_inspector(void)
{
    const step_t *s = grid_step((uint32_t)held_first());
    static const char *const GN[NGR] = {"NOTE", "TIME", "LOGIC", "LOCKS"};
    static const char *const LBL[NGR][4] = {{"PITCH", "VEL", "LENGTH", "GLIDE"}, {"MICRO", "RATCHET", "RAMP", "ACCENT"},
                                            {"COND", "CHANCE", "", ""}, {"L1", "L2", "L3", "L4"}};
    char t[24], n[8];
    uint32_t i, g;
    int32_t x = 5;
    if (s->note[0] == NOTE_NONE) {
        cv_text(5, 0, &FONT_M, "EMPTY", K_DIM);
    } else {
        for (i = 0; i < NNOTE && s->note[i] != NOTE_NONE; i++) {
            note_text(s->note[i], n);
            x = cv_text(x, 0, i ? &FONT_B : &FONT_M, n, i ? K_FG : K_HI) + 6;
        }
    }
    s_cpy(t, "step ");
    i_text((int32_t)(ui.bar * 16u + (uint32_t)held_first() + 1u), t + 5);
    cv_text(235 - text_w(&FONT_XS, t), 6, &FONT_XS, t, K_DIM);
    for (g = 0; g < NGR; g++) {
        knob_t k[4];
        uint8_t keep = ui.group;
        int32_t y = 28 + (int32_t)g * 22;
        ui.group = (uint8_t)g;
        knobs_get(k);
        ui.group = keep;
        for (i = 0; i < 4u; i++) {
            int32_t cx = 5 + (int32_t)i * 58;
            if (!LBL[g][i][0])
                continue;
            cv_rect(cx, y, 56, 20, K_PANEL);
            cv_text(cx + 3, y, &FONT_T, LBL[g][i], K_DIM);
            cv_text(cx + 3, y + 8, &FONT_XS, k[i].val, k[i].red ? K_REC : K_FG);
            if (g == ui.group)
                outline(cx, y, 56, 20, K_HI);
        }
    }
    for (g = 0; g < NGR; g++) {                  /* the groups: black keys 1-4 (SMK: < >) */
        int32_t gx = 5 + (int32_t)g * 58;
        s_cpy(t, PLAT_SMK37 ? "" : g == 0 ? "1 " : g == 1 ? "2 " : g == 2 ? "3 " : "4 ");
        s_cat(t, GN[g]);
        cv_rect(gx, 118, 56, 12, g == ui.group ? K_HI : K_BG);
        cv_text(gx + 3, 118, &FONT_T, t, g == ui.group ? K_BG : K_DIM);
    }
}

/* ------------------------------------------------------------------- the other pages --- */
static void draw_track_page(void)
{
    const track_t *tr = trk();
    const profile_t *pr = &PROFILES[tr->prof % NPROF];
    char t[24];
    uint32_t i;
    cv_text(5, 2, &FONT_M, pr->name, tcol(sel_track));
    s_cpy(t, "channel ");
    i_text(tr->chan + 1, t + 8);
    cv_text(5, 34, &FONT_S, t, K_FG);
    cv_text(120, 34, &FONT_S, PORT_NAME[port_index(tr->ports)], K_FG);
    cv_text(5, 54, &FONT_XS, tr->type == TT_DRUM ? "drum lanes" : "notes", K_DIM);
    if (tr->type == TT_DRUM) {
        for (i = 0; i < drum_lanes(tr) && i < 12u; i++) {
            int32_t x = 5 + (int32_t)(i % 4u) * 58, y = 70 + (int32_t)(i / 4u) * 20;
            char n[8];
            cv_rect(x, y, 56, 18, K_PANEL);
            cv_text(x + 3, y + 3, &FONT_T, drum_lane_name(tr, i), K_FG);
            note_text(tr->drum[i], n);
            cv_text(x + 52 - text_w(&FONT_T, n), y + 3, &FONT_T, n, K_DIM);
        }
    } else {
        for (i = 0; i < NLANE; i++) {
            int32_t x = 5 + (int32_t)i * 58;
            lane_text(tr->lane_cc[i], t);
            cv_rect(x, 70, 56, 18, K_PANEL);
            cv_text(x + 3, 73, &FONT_T, t, K_FG);
        }
        cv_text(5, 96, &FONT_XS, tr->quant ? "notes snap to the key" : "notes as played", K_DIM);
    }
}

static void draw_lanes_page(void)
{
    const track_t *tr = trk();
    uint32_t l, i;
    char t[16];
    for (l = 0; l < NLANE; l++) {
        int32_t y = 4 + (int32_t)l * 31;
        lane_text(tr->lane_cc[l], t);
        cv_text(5, y, &FONT_XS, t, K_FG);
        if (tr->lane_cc[l] < 128u) {
            char n[8] = "CC";
            i_text(tr->lane_cc[l], n + 2);
            cv_text(5, y + 13, &FONT_T, n, K_DIM);
        }
        for (i = 0; i < 16u; i++) {               /* this bar's locks, as bars */
            const step_t *s = grid_step(i);
            int32_t x = 64 + (int32_t)i * 11, h = s->lock[l] == LOCK_NONE ? 0 : 1 + s->lock[l] * 22 / 127;
            cv_rect(x, y, 9, 24, K_PANEL);
            if (h)
                cv_rect(x, y + 24 - h, 9, h, K_REC);
            if (E.playing && E.t[sel_track].pos == (int32_t)(ui.bar * 16u + i))
                cv_rect(x, y + 25, 9, 2, K_HI);
        }
    }
}

static void draw_perf_page(void)
{
    uint32_t i;
    char t[8];
    for (i = 0; i < NTRK; i++) {
        int32_t x = 5 + (int32_t)(i % 8u) * 29, y = 6 + (int32_t)(i / 8u) * 52;
        int heard = track_heard(i), lit = E.t[i].lit && (int16_t)(E.t[i].lit_until - (uint16_t)plat_ms()) > 0;
        cv_rect(x, y, 27, 48, heard ? (lit ? tcol(i) : mix24(TC24[i], 45, PANEL24)) : RGB(18, 21, 22));
        s_cpy(t, "T");
        i_text((int32_t)i + 1, t + 1);
        cv_text(x + 3, y + 2, &FONT_XS, t, heard ? K_BG : K_DIM);
        if (!heard)
            cv_text(x + 3, y + 30, &FONT_T, E.solo ? "-" : "MUTE", K_REC);
        if (E.solo >> i & 1u)
            outline(x, y, 27, 48, K_HI);
    }
    cv_text(5, 112, &FONT_XS, E.fill ? "FILL" : "", K_HI);
    cv_text(60, 112, &FONT_XS, PLAT_SMK37 ? "hold FADER BANK: fill" : "black key 1 held: fill", K_DIM);
}

static void draw_over_page(void)
{
    uint32_t first = sel_track < 8u ? 0u : 8u, i;
    char t[16];
    for (i = first; i < first + 8u; i++) {
        int32_t y = 3 + (int32_t)(i - first) * 16;
        const phead_t *ph = &P.ph[i][P.pat[i]];
        int heard = track_heard(i);
        uint16_t fg = heard ? K_FG : K_LINE;
        int32_t w = ph->len * 132 / 64;
        i_text((int32_t)i + 1, t);
        cv_text(3, y, &FONT_XS, t, i == sel_track ? K_HI : fg);
        cv_rect(20, y + 3, 8, 8, heard ? tcol(i) : K_LINE);
        cv_text(32, y, &FONT_XS, PROFILES[P.trk[i].prof % NPROF].tag, fg);
        s_cpy(t, "P");
        i_text(P.pat[i] + 1, t + 1);
        cv_text(74, y, &FONT_XS, t, K_DIM);
        cv_rect(94, y + 3, 132, 9, K_PANEL);
        cv_rect(94, y + 3, w, 9, mix24(TC24[i], heard ? 35 : 15, PANEL24));
        if (E.playing && E.t[i].pos >= 0)
            cv_rect(94 + E.t[i].pos * 132 / 64, y + 3, 2, 9, heard ? tcol(i) : K_DIM);
        if (i == sel_track)
            outline(0, y - 1, 240, 15, K_LINE);
    }
}

static void draw_gen_page(void)
{
    const track_t *tr = trk();
    uint32_t s, len = ui.sub[PG_GEN] == 0 ? ui.eu_steps : cur_ph()->len;
    uint32_t seed = ui.rnd_seed * 2654435761u + 1u;
    cv_text(5, 0, &FONT_B, ui.sub[PG_GEN] == 0 ? "Euclid" : "Random", K_FG);
    cv_text(90, 3, &FONT_XS, tr->type == TT_DRUM ? drum_lane_name(tr, tr->lane_sel) : "notes", K_DIM);
    for (s = 0; s < 64u; s++) {
        int32_t x = 5 + (int32_t)(s % 16u) * 14, y = 24 + (int32_t)(s / 16u) * 18;
        int hit = 0;
        if (ui.sub[PG_GEN] == 0) {
            hit = s < len && euclid_hit(s, len, ui.eu_pulses, ui.eu_rot);
        } else if (s < len) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            hit = seed % 100u < ui.rnd_density;
        }
        cv_rect(x, y, 12, 14, s >= len ? RGB(18, 21, 22) : hit ? tcol(sel_track) : K_PANEL);
    }
    cv_text(5, 96, &FONT_XS, "a preview: printing writes it in", K_DIM);
    cv_text(5, 112, &FONT_XS, "ARP prints", K_HI);
}

static void draw_global_page(void)
{
    char t[32];
    uint32_t i;
    switch (ui.sub[PG_GLOBAL]) {
    case 0:
        tempo_text(tempo10_now(), t);
        cv_text(5, 4, &FONT_L, t, K_HI);
        cv_text(5, 44, &FONT_XS, P.clock == CK_SEND ? "sends MIDI clock, start and stop" :
                                 P.clock == CK_RECEIVE ? (E.follow ? "following the clock coming in" :
                                                          "waiting for a clock (own tempo meanwhile)") :
                                 "keeps time, sends no clock", K_FG);
        cv_text(5, 64, &FONT_XS, P.launch == LA_NOW ? "scenes and patterns change at once" :
                                 "scenes and patterns change at the bar", K_DIM);
        cv_text(5, 80, &FONT_XS, P.thru ? "MIDI in plays the selected track" : "MIDI in is only recorded", K_DIM);
        break;
    case 1:
        s_cpy(t, NOTE_NAME[P.root % 12u]);
        s_cat(t, " ");
        s_cat(t, SCALE_NAME[P.scale % NSCALE]);
        cv_text(5, 4, &FONT_M, t, K_HI);
        for (i = 0; i < 12u; i++) {
            int in = SCALE_MASK[P.scale % NSCALE] >> ((i + 12u - P.root) % 12u) & 1u;
            int32_t x = 5 + (int32_t)i * 19;
            cv_rect(x, 40, 17, 30, in ? tcol(sel_track) : K_PANEL);
            cv_text(x + 2, 74, &FONT_T, NOTE_NAME[i], in ? K_FG : K_DIM);
        }
        cv_text(5, 94, &FONT_XS, "tracks with IN KEY on (Track, page 2) snap to it", K_DIM);
        break;
    case GL_PORTS: {
        int32_t y = 2;
        int pk = FM1B_BLE ? plat_ble_passkey() : -1;
        cv_text(5, y, &FONT_XS, "USB", K_DIM);
        if (set.usb_host) {
            const char *nm = plat_usb_host_name();
            cv_text(68, y, &FONT_XS, plat_usb_host_state() == USBH_CONNECTED ? (nm[0] ? nm : "device") :
                                     "host: waiting (powered hub)", K_FG);
        } else {
            cv_text(68, y, &FONT_XS, "computer", K_FG);
        }
        y += 15;
        cv_text(5, y, &FONT_XS, "BT HOST", K_DIM);
        cv_text(68, y, &FONT_XS, set.ble_off ? "off" : plat_ble_connected() ? "connected" : "advertising", K_FG);
        y += 15;
        cv_text(5, y, &FONT_XS, "BT DEVICE", K_DIM);
        if (FM1B_BLE_CENTRAL) {
            btdev_title(t);
            cv_text(68, y, &FONT_XS, t, btdev_ready() ? K_OK : K_FG);
            y += 15;
            if (pk >= 0) {                       /* a device asks for a code: type this one on it */
                char n[12];
                i_text(pk, n);
                cv_text(5, y, &FONT_XS, "PASSKEY", K_HI);
                cv_text(68, y - 3, &FONT_M, n, K_HI);
                y += 22;
            }
            for (i = 0; i < 5u && y < 112; i++) {
                char nm[21], r[8];
                int rssi = 0;
                if (!btdev_found((int)i, nm, &rssi))
                    break;
                nm[16] = 0;
                cv_text(78, y, &FONT_XS, nm, (int)i == btdev_pick() ? K_HI : K_DIM);
                if ((int)i == btdev_pick())
                    cv_text(68, y, &FONT_XS, ">", K_HI);
                i_text(rssi, r);
                cv_text(210, y, &FONT_T, r, K_DIM);
                y += 14;
            }
            if (set.bt_auto && set.bt_name[0] && !btdev_busy()) {
                s_cpy(t, ui.bt_paused ? "auto: paused (pick one)" : "auto: looking for ");
                if (!ui.bt_paused)
                    s_cat(t, set.bt_name);
                t[30] = 0;
                cv_text(5, 103, &FONT_T, t, K_DIM);
            }
        }
#if PLAT_SMK37
        cv_text(5, 116, &FONT_XS, "TRS OUT", K_DIM);
        cv_text(68, 116, &FONT_XS, set.trs_off ? "off" : "on: the jack sends", K_FG);
#endif
        break;
    }
    case GL_TIMING: {
        static const char *const PN[NPT] = {PLAT_SMK37 ? "USB+TRS" : "USB", "BT HOST", "BT DEVICE"};
        static const char *const CN[NPC] = {"off", "clock", "+start"};
        cv_text(5, 2, &FONT_T, "PORT", K_DIM);
        cv_text(84, 2, &FONT_T, "NOTES", K_DIM);
        cv_text(134, 2, &FONT_T, "CLOCK AT", K_DIM);
        cv_text(186, 2, &FONT_T, "SENDS", K_DIM);
        for (i = 0; i < NPT; i++) {
            int32_t y = 16 + (int32_t)i * 20;
            if (i == ui.tm_port)
                cv_rect(2, y - 2, 236, 18, K_LINE);
            cv_text(5, y, &FONT_XS, PN[i], i == ui.tm_port ? K_HI : K_FG);
            ms_text(set.port_ms[i][0], t);
            cv_text(84, y, &FONT_XS, t, set.port_ms[i][0] ? K_HI : K_DIM);
            ms_text(set.port_ms[i][1], t);
            cv_text(134, y, &FONT_XS, t, set.port_clock[i] && set.port_ms[i][1] ? K_HI : K_DIM);
            cv_text(186, y, &FONT_XS, CN[set.port_clock[i] % NPC], set.port_clock[i] ? K_FG : K_DIM);
        }
        cv_text(5, 80, &FONT_XS, "- early, + late. A LOOM-8 on Bluetooth plays", K_DIM);
        cv_text(5, 94, &FONT_XS, "its clock ~75 ms late: CLOCK AT -75 there.", K_DIM);
        if (out_base) {
            s_cpy(t, "everything waits ");
            i_text((int32_t)out_base, t + text_len(t));
            s_cat(t, " ms for the earliest");
            cv_text(5, 112, &FONT_XS, t, K_FG);
        }
        break;
    }
    case GL_PROJECTS:
        for (i = 0; i < NSLOTS; i++) {
            uint32_t bpm = 0, len = slot_info(i, &bpm);
            int32_t y = 2 + (int32_t)i * 26;
            char n[16];
            cv_rect(3, y, 234, 24, i == ui.proj_sel ? K_LINE : K_PANEL);
            n[0] = (char)('1' + i);
            n[1] = 0;
            cv_text(9, y + 2, &FONT_B, n, i == set.slot ? tcol(sel_track) : K_DIM);
            if (!len) {
                cv_text(30, y + 4, &FONT_XS, "empty", K_DIM);
            } else {
                tempo_text(bpm, t);
                s_cat(t, " BPM");
                cv_text(30, y + 4, &FONT_XS, t, K_FG);
                i_text((int32_t)((len + 1023u) / 1024u), n);
                s_cat(n, " of ");
                i_text((int32_t)(SLOT_BYTES / 1024u), n + text_len(n));
                s_cat(n, " KB");
                cv_text(110, y + 4, &FONT_XS, n, K_DIM);
            }
            if (i == set.slot)
                cv_text(188, y + 4, &FONT_XS, ui.dirty ? "in use *" : "in use", K_HI);
            if (i == ui.proj_sel)
                outline(3, y, 234, 24, K_HI);
        }
        cv_text(5, 108, &FONT_XS, N_SEL ": load, save here, clear", K_DIM);
        break;
    default: {
        static const char *const PN[NPORTS] = {"USB", "BLE", "TRS"};
        cv_text(5, 2, &FONT_M, APP_NAME, K_HI);
        cv_text(5, 30, &FONT_XS, APP_VERSION, K_DIM);
        cv_text(5, 46, &FONT_XS, PLAT_SMK37 ? "M-VAVE SMK-37" : "M-VAVE FM-1", K_DIM);
        for (i = 0; i < NPORTS; i++) {
            int32_t y = 66 + (int32_t)i * 16;
            cv_text(5, y, &FONT_XS, PN[i], K_DIM);
            s_cpy(t, "in ");
            i_text((int32_t)plat_midi_count((int)i, 0), t + 3);
            cv_text(50, y, &FONT_XS, t, K_FG);
            s_cpy(t, "out ");
            i_text((int32_t)plat_midi_count((int)i, 1), t + 4);
            cv_text(140, y, &FONT_XS, t, K_FG);
        }
        break;
    }
    }
}

static void draw_arr_page(void)
{
    uint32_t i;
    char t[16];
    for (i = 0; i < NTRK; i++) {
        int32_t x = 4 + (int32_t)(i / 8u) * 74, y = 2 + (int32_t)(i % 8u) * 15;
        int heard = track_heard(i);
        i_text((int32_t)i + 1, t);
        cv_text(x, y, &FONT_XS, t, i == sel_track ? K_HI : heard ? K_FG : K_LINE);
        cv_rect(x + 17, y + 3, 8, 8, heard ? tcol(i) : K_LINE);
        s_cpy(t, "P");
        i_text(P.pat[i] + 1, t + 1);
        if (E.t[i].cue != 0xFFu) {
            s_cat(t, ">");
            i_text(E.t[i].cue + 1, t + text_len(t));
        }
        cv_text(x + 29, y, &FONT_XS, t, heard ? K_FG : K_LINE);
    }
    cv_text(178, 0, &FONT_XS, "scene", K_DIM);
    i_text(P.scene + 1, t);
    cv_text(196 - text_w(&FONT_L, t) / 2 + 4, 14, &FONT_L, t, K_HI);
    if (E.scene_cue != 0xFFu) {
        s_cpy(t, "next ");
        i_text(E.scene_cue + 1, t + 5);
        cv_text(170, 50, &FONT_XS, t, K_OK);
    }
    for (i = 0; i < NSCENE; i++) {
        int32_t x = 166 + (int32_t)(i % 8u) * 9, y = 72 + (int32_t)(i / 8u) * 9;
        uint16_t c = i == P.scene ? K_HI : P.scene_used[i] ? K_DIM : K_PANEL;
        cv_rect(x, y, 7, 7, c);
        if (i == E.scene_cue)
            outline(x - 1, y - 1, 9, 9, K_OK);
    }
}

static void draw_song_page(void)
{
    uint32_t i, first = ui.song_sel > 5u ? ui.song_sel - 5u : 0u;
    char t[24];
    if (!P.song_n) {
        cv_text(5, 4, &FONT_B, "The song is empty", K_FG);
        cv_text(5, 26, &FONT_XS, "press a scene's " N_GRID " key to add it", K_DIM);
        return;
    }
    for (i = first; i < P.song_n && i < first + 7u; i++) {
        int32_t y = 2 + (int32_t)(i - first) * 18;
        int playing = E.song_on && E.slot == i;
        i_text((int32_t)i + 1, t);
        cv_text(5, y, &FONT_XS, t, K_DIM);
        s_cpy(t, "scene ");
        i_text(P.song_scene[i] + 1, t + 6);
        cv_text(30, y, &FONT_B, t, playing ? K_OK : K_FG);
        i_text(P.song_bars[i], t);
        s_cat(t, P.song_bars[i] == 1u ? " bar" : " bars");
        cv_text(130, y + 2, &FONT_XS, t, K_DIM);
        if (playing)
            cv_rect(200, y + 6, (int32_t)E.slot_bar * 30 / (P.song_bars[i] ? P.song_bars[i] : 1), 4, K_OK);
        if (i == ui.song_sel)
            outline(1, y - 1, 238, 17, K_LINE);
    }
}

static void draw_menu(void)
{
    uint32_t i, first = ui.menu_i > 6 ? (uint32_t)ui.menu_i - 6u : 0u;
    cv_rect(10, 0, 220, 130, K_PANEL);
    outline(10, 0, 220, 130, K_LINE);
    for (i = first; i < ui.menu_n && i < first + 7u; i++) {
        int32_t y = 4 + (int32_t)(i - first) * 18;
        int sel = (int)i == ui.menu_i;
        if (sel)
            cv_rect(12, y - 1, 216, 17, ui.menu_confirm ? K_REC : K_HI);
        cv_text(20, y, &FONT_S, MA_NAME[menu_items[i]], sel ? K_BG : K_FG);
    }
}

/* ------------------------------------------------------------------- help cards --- */
/* a button held alone for a second: what it does, on the page band, until it is let go */
#if PLAT_SMK37
#define N_NAMEOF(fm1, smk) smk
#else
#define N_NAMEOF(fm1, smk) fm1
#endif
typedef struct {
    const char *name, *text;
} card_t;
static const card_t CARDS[NB] = {
    [B_FX] = {N_NAMEOF("FX", "FX"), "Perform: the " N_GRID " keys mute the 16 tracks. Hold " N_HOME " and press one to solo it. "
              N_NAMEOF("Black key 1 held: fill.", "FADER BANK held: fill, on any page.")},
    [B_SEL] = {N_SEL, "The page's menu: copy, paste, clear, double, shift, undo; scenes; the song; save, new project, "
               "all notes off. " N_SELECT " picks, " N_SEL " does it, " N_HOME " closes. " N_HOME " + " N_SEL ": undo."},
    [B_ENV] = {N_NAMEOF("ENV", "CHORD"), "Lanes: the track's four CCs. Page 1 picks each lane's CC; page 2 plays "
               "them live, and records them while recording. A step locks them (hold it, LOCKS)."},
    [B_LFO] = {N_NAMEOF("LFO", "NOTE REPEAT"), "Overview: eight tracks, their pattern lengths and playheads. The "
               N_GRID " keys pick a track. The knobs edit its pattern."},
    [B_EDIT] = {N_NAMEOF("EDIT", "PARA"), "Track: the device it plays, its MIDI channel, ports and transpose. Page 2: "
                "notes or drums, program change, in key (a drum lane's note), velocity. " N_HOME " + "
                N_NAMEOF("EDIT", "PARA") ": paste the pattern."},
    [B_GLO] = {N_NAMEOF("GLO", "GLOBE"), "Global: clock and tempo, key and scale, ports (USB host, Bluetooth"
               N_NAMEOF("", ", TRS out") "), the four projects, about. " N_HOME " + " N_NAMEOF("GLO", "GLOBE")
               ": all notes off."},
    [B_HOME] = {N_HOME, "Arrange: the " N_GRID " keys are scenes, at the bar (again: now). Tap " N_HOME
                " again: the song. Held, " N_HOME " is a shift: undo, copy, paste, solo, the next scene, fine tempo."},
    [B_SAVE] = {N_SAVE, "Saves the project into its slot (GLOBAL page 4 has all four). While playing, it saves "
                "when you stop: the flash would stall the sequencer."},
    [B_ARP] = {"ARP", "Generate: Euclid (steps, pulses, rotate) and Random (density, range, seed), previewed. ARP "
               "again prints it into the pattern; " N_HOME " + " N_SEL " undoes it."},
    [B_SEQ] = {"SEQ", "Steps: a bar of the pattern on the " N_GRID " keys. Tap a step: on or off. Hold a step: all of "
               "it on the knobs. " N_NAMEOF("Hold SEQ: the keys play the track and set the notes a tap writes.",
               "The keybed plays the track and sets the notes a tap writes.")},
    [B_PLAY] = {N_NAMEOF("PLAY", "SEQ PLAY"), "Starts and stops. With the clock on SEND, the other instruments start "
                "and stop too. REC held + PLAY: play and record. On the Song page: the song."},
    [B_REC] = {N_NAMEOF("REC", "SEQ REC"), "Playing: recording on or off; notes and lane moves go onto the steps. "
               "Stopped: step record, a note or chord a step; " N_NAMEOF("OCT+ a rest, OCT- back.",
               "> a rest, < back.")},
    [B_OCTDN] = {N_NAMEOF("OCT-", "OCTAVE-"), N_NAMEOF("The bar before on Steps; the keys' octave while they play "
                 "notes. Step record: back a step.", "The keybed's octave down.")},
    [B_OCTUP] = {N_NAMEOF("OCT+", "OCTAVE+"), N_NAMEOF("The next bar on Steps; the keys' octave while they play "
                 "notes. Step record: a rest.", "The keybed's octave up.")},
#if PLAT_SMK37
    [B_KBANK] = {"KNOB BANK", "Perform: the pads mute the 16 tracks. Hold PATCH and press one to solo it."},
    [B_FBANK] = {"FADER BANK", "Held: fill, on any page (steps with FILL play, !FILL rest)."},
    [B_LEFT] = {"<", "The bar before on Steps; with a step held, its group; step record: back a step."},
    [B_RIGHT] = {">", "The next bar on Steps; with a step held, its group; step record: a rest."},
#endif
};

static void draw_card(uint32_t b)                /* the card over the page band, the words wrapped to it */
{
    const card_t *c = &CARDS[b % NB];
    const char *p = c->text;
    char line[64];
    int32_t y = 30;
    cv_rect(6, 2, 228, 126, K_PANEL);
    outline(6, 2, 228, 126, K_HI);
    boxed(14, 10, c->name ? c->name : "", K_HI);
    while (*p && y < 118) {                      /* as many words as fit 212 pixels, a line at a time */
        uint32_t n = 0, fit = 0;
        while (p[n] && n < sizeof line - 1u) {
            line[n] = p[n];
            line[n + 1u] = 0;
            if (text_w(&FONT_XS, line) > 212)
                break;
            n++;
            if (!p[n] || p[n] == ' ')
                fit = n;
        }
        if (!p[n])
            fit = n;
        if (!fit)
            fit = n ? n : 1u;
        line[fit] = 0;
        cv_text(14, y, &FONT_XS, line, K_FG);
        p += fit;
        while (*p == ' ')
            p++;
        y += 15;
    }
}

/* ------------------------------------------------------------------- the bottom: tag, knobs, hints --- */
static void hint(int32_t x, int32_t y, const char *key, const char *what)
{
    int32_t w = text_w(&FONT_T, key) + 6;
    outline(x, y + 1, w, 12, K_FG);
    cv_text(x + 3, y + 1, &FONT_T, key, K_FG);
    cv_text(x + w + 4, y, &FONT_XS, what, K_FG);
}

static void draw_hints(int32_t y)
{
    const char *a = "", *b = "", *c = "", *d = "", *e = "", *f = "", *g = "", *h = "";
    if (ui.menu) {
        a = N_SELECT; b = "pick"; c = N_SEL; d = "do it"; e = N_HOME; f = "close";
    } else if (step_edit_mode()) {
        a = PLAT_SMK37 ? "< >" : N_SIDE; b = "group"; c = PLAT_SMK37 ? "KEYS" : "SEQ+KEYS"; d = "notes";
        e = N_GRID; f = "more steps"; g = "K1-4"; h = "edit";
    } else {
        switch (ui.page) {
        case PG_STEPS:
            if (ui.srec) {
                a = PLAT_SMK37 ? "KEYS" : "KEYS"; b = "write"; c = PLAT_SMK37 ? "< >" : "OCT"; d = "rest / back";
                e = "REC"; f = "done";
            } else if (trk()->type == TT_DRUM) {
                a = N_GRID; b = "hits"; c = PLAT_SMK37 ? "KEYS" : N_SIDE; d = "lane"; e = N_BAR; f = "bar";
                g = "hold"; h = "step";
            } else {
                a = N_GRID; b = "steps"; c = PLAT_SMK37 ? "KEYS" : "SEQ held"; d = PLAT_SMK37 ? "play" : "keys play";
                e = N_BAR; f = "bar"; g = "hold"; h = "step";
            }
            break;
        case PG_ARR: a = N_GRID; b = "scene"; c = PLAT_SMK37 ? N_HOME : N_SIDE;
            d = PLAT_SMK37 ? "again: song" : "mute"; e = N_PRESET; f = "pattern"; g = N_ALGO; h = "track"; break;
        case PG_SONG: a = N_GRID; b = "add scene"; c = "PLAY"; d = "from slot"; e = N_SEL; f = "menu"; break;
        case PG_PERF: a = N_GRID; b = "mute"; c = N_HOME "+"; d = "solo"; e = N_SEL; f = "unmute all"; break;
        case PG_GEN: a = "ARP"; b = "print"; c = N_SELECT; d = "kind"; e = N_HOME "+" N_SEL; f = "undo"; break;
        default:
            a = PLAT_SMK37 ? N_GRID : "KEYS"; b = PLAT_SMK37 ? "track" : "play"; c = N_SELECT; d = "page";
            e = N_ALGO; f = "track"; g = N_SEL; h = "menu";
            break;
        }
    }
    if (a[0])
        hint(5, y, a, b);
    if (c[0])
        hint(124, y, c, d);
    if (e[0])
        hint(5, y + 16, e, f);
    if (g[0])
        hint(124, y + 16, g, h);
}

static void draw_bottom(void)
{
    knob_t k[4];
    char t[32];
    uint32_t i, subs = PG_SUBS[ui.page];
    int32_t x;
    cv_begin(240, 88, K_BG);
    cv_rect(5, 0, 230, 1, K_LINE);
    /* the tag line: where you are */
    s_cpy(t, "T");
    i_text(sel_track + 1, t + 1);
    s_cat(t, " ");
    s_cat(t, PROFILES[trk()->prof % NPROF].tag);
    s_cat(t, " > P");
    i_text(P.pat[sel_track] + 1, t + text_len(t));
    s_cat(t, " > S");
    i_text(P.scene + 1, t + text_len(t));
    s_cat(t, "   PROJECT ");
    i_text(set.slot + 1, t + text_len(t));
    x = cv_text(6, 1, &FONT_T, t, K_FG);
    (void)x;
    for (i = 0; i < subs && subs > 1u; i++)
        cv_rect(234 - (int32_t)(subs - i) * 7, 5, 4, 4, i == ui.sub[ui.page] ? K_FG : K_LINE);
    /* the four knobs */
    knobs_get(k);
    cv_rect(4, 14, 232, 40, K_PANEL);
    for (i = 0; i < 4u; i++) {
        int32_t cx = 4 + (int32_t)i * 58;
        if (i)
            cv_rect(cx, 16, 1, 36, K_LINE);
        if (!k[i].label[0])
            continue;
        cv_text(cx + (58 - text_w(&FONT_T, k[i].label)) / 2, 15, &FONT_T, k[i].label, K_DIM);
        cv_text(cx + (58 - text_w(&FONT_B, k[i].val)) / 2, 25, &FONT_B, k[i].val, k[i].red ? K_REC : K_FG);
        if (k[i].frac >= 0) {
            cv_rect(cx + 8, 47, 42, 3, K_LINE);
            cv_rect(cx + 8, 47, 42 * k[i].frac / 100, 3, tcol(sel_track));
        }
    }
    draw_hints(56);
    cv_blit(0, 152);
}

static void draw(void)
{
    draw_header();
    cv_begin(240, 130, K_BG);
    if (ui.card != NB)
        draw_card(ui.card);
    else if (ui.menu)
        draw_menu();
    else if (step_edit_mode())
        draw_inspector();
    else
        switch (ui.page) {
        case PG_STEPS:
            if (trk()->type == TT_DRUM)
                draw_drum_steps();
            else
                draw_note_steps();
            break;
        case PG_TRACK: draw_track_page(); break;
        case PG_LANES: draw_lanes_page(); break;
        case PG_PERF: draw_perf_page(); break;
        case PG_OVER: draw_over_page(); break;
        case PG_GEN: draw_gen_page(); break;
        case PG_GLOBAL: draw_global_page(); break;
        case PG_ARR: draw_arr_page(); break;
        case PG_SONG: draw_song_page(); break;
        default: break;
        }
    cv_blit(0, 22);
    draw_bottom();
}

/* ------------------------------------------------------------------- the lights --- */
static uint32_t step_lit_grid(uint32_t *dim)     /* the steps page's 16 keys: bright and dim */
{
    uint32_t i, lit = 0, len = cur_ph()->len;
    int32_t play = E.playing ? E.t[sel_track].pos : -1;
    const track_t *tr = trk();
    *dim = 0;
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bar * 16u + i;
        const step_t *s = grid_step(i);
        if (si >= len)
            continue;
        if (s->note[0] != NOTE_NONE && (tr->type != TT_DRUM || step_has(s, tr->drum[tr->lane_sel % NDLANE])))
            *dim |= 1u << i;
        if ((int32_t)si == play || (ui.grid_held >> i & 1u) || (ui.srec && ui.srec_pos == si))
            lit |= 1u << i;
    }
    return lit;
}

static void grid_lights(uint32_t *lit, uint32_t *dim)
{
    uint32_t i;
    *lit = *dim = 0;
    switch (ui.page) {
    case PG_STEPS: *lit = step_lit_grid(dim); break;
    case PG_ARR: case PG_SONG:
        for (i = 0; i < NSCENE; i++) {
            if (P.scene_used[i])
                *dim |= 1u << i;
            if (i == P.scene || (i == E.scene_cue && (plat_ms() & 256u)))
                *lit |= 1u << i;
        }
        break;
    case PG_PERF:
        for (i = 0; i < NTRK; i++)
            if (track_heard(i))
                *lit |= 1u << i;
        break;
    default:
        for (i = 0; i < NTRK; i++)
            *dim |= 1u << i;
        *lit = 1u << sel_track;
        break;
    }
}

#if PLAT_SMK37
static uint32_t pad_shown[16];
static void pad_set(uint32_t p, uint32_t rgb)
{
    if (pad_shown[p] != rgb + 1u) {
        pad_shown[p] = rgb + 1u;
        plat_pad_rgb((int)p, rgb);
    }
}
static uint32_t dim24(uint32_t c, uint32_t pct)
{
    return ((c >> 16 & 255u) * pct / 100u) << 16 | ((c >> 8 & 255u) * pct / 100u) << 8 | (c & 255u) * pct / 100u;
}
static void pads_show(uint32_t lit, uint32_t dim)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        uint32_t c = 0;
        switch (ui.page) {
        case PG_STEPS:
            c = lit >> i & 1u ? ((ui.grid_held >> i & 1u) ? 0xFFF6E0u : ui.srec ? 0xFF2020u : 0xFFFFFFu)
                : dim >> i & 1u ? dim24(TC24[sel_track], 45) : ui.bar * 16u + i < cur_ph()->len ? 0x060808u : 0;
            break;
        case PG_ARR: case PG_SONG:
            c = lit >> i & 1u ? 0xFFF0C0u : dim >> i & 1u ? 0x403820u : 0;
            break;
        case PG_PERF:
            c = lit >> i & 1u ? dim24(TC24[i], 70) : 0x300000u;
            break;
        default:
            c = i == sel_track ? TC24[i] : dim24(TC24[i], 18);
            break;
        }
        pad_set(i, c);
    }
}
#endif

static void lights(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    static const uint8_t PAGE_BTN[NPG] = {B_SEQ, B_EDIT, B_ENV, B_FX, B_LFO, B_ARP, B_GLO, B_HOME, B_HOME};
    uint32_t lit, dim, keys = 0, dkeys = 0, i, btn = 0, dbtn = 0;
    grid_lights(&lit, &dim);
    for (i = 0; i < 16u; i++) {
        if (lit >> i & 1u)
            keys |= 1u << WK[i];
        else if (dim >> i & 1u)
            dkeys |= 1u << WK[i];
    }
    if (!PLAT_SMK37) {                           /* the black keys */
        const track_t *tr = trk();
        if (ui.page == PG_STEPS && ui.grid_held) {
            for (i = 0; i < NGR; i++) {
                if (i == ui.group)
                    keys |= 1u << BK[i];
                else
                    dkeys |= 1u << BK[i];
            }
        } else if (ui.page == PG_STEPS && tr->type == TT_DRUM) {
            for (i = 0; i < 11u && i < drum_lanes(tr); i++) {
                if (i == tr->lane_sel)
                    keys |= 1u << BK[i];
                else
                    dkeys |= 1u << BK[i];
            }
        } else if (ui.page == PG_ARR || ui.page == PG_OVER || ui.page == PG_PERF) {
            for (i = 0; i < 11u; i++)
                if (track_heard(i))
                    dkeys |= 1u << BK[i];
        }
        if (fm1_keyboard()) {
            keys = ui.keys;
            dkeys = 0;
        }
    }
    btn |= 1u << PAGE_BTN[ui.page];
    if (E.playing)
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    if (ui.rec || ui.srec)
        btn |= 1u << B_REC;
    if (ui.dirty)
        btn |= 1u << B_SAVE;
    if (ui.menu)
        btn |= 1u << B_SEL;
    plat_play_red(ui.rec && E.playing);
    plat_lights(btn, keys, dbtn, dkeys, 1);
#if PLAT_SMK37
    pads_show(lit, dim);
#endif
}
