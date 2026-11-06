/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* The sequencer: 96 ticks a quarter note, run from app_tick (1 kHz, the timer interrupt) by the 24 MHz clock, or by a
 * MIDI clock coming in. Each track looks half a step ahead: its next step is judged (condition, chance, mute) half a
 * step before the step's line, so a step played early (micro-timing) is still on time. Notes and their note-offs go
 * into a queue, sent on their tick; a stop sends every note-off still owed.
 * The main loop edits the project while this runs: a step's bytes are read as they are; whole-pattern changes (paste,
 * clear, a generator) take plat_lock. */

static head_t P;                                        /* the project, but its steps */
static step_t PAT[NTRK][NPAT][NSTEP] PLAT_POOL;        /* the steps: 128 KiB */
#define STEP(t, p, s) (&PAT[(t)][(p)][(s)])

/* ------------------------------------------------------------------- the event queue --- */
typedef struct {
    uint32_t due;                                       /* the engine tick it goes out on */
    uint32_t pkt;                                       /* USB-MIDI packet */
    uint8_t ports;                                      /* PO_* */
    uint8_t off;                                        /* a note-off (sent even after a stop) */
    uint8_t rsv[2];
} ev_t;
#define NEV 384u
static ev_t evq[NEV];
static uint32_t nev;

typedef struct {
    volatile uint8_t playing;
    volatile uint8_t fill;                              /* FILL held (Perform) */
    uint8_t follow;                                     /* a clock comes in and we follow it */
    uint8_t song_on;                                    /* the song plays (Song page's PLAY) */
    volatile uint32_t tick;                             /* engine ticks since PLAY */
    uint32_t acc24, last24;                             /* 24 MHz time owed to the next tick */
    uint32_t clk_in, clk_t24, clk_per24, clk_ms;        /* the clock in: pulses, the last one's time, its period */
    uint16_t solo;                                      /* soloed tracks (Perform, HOME + a mute key) */
    uint8_t scene_cue;                                  /* a scene waiting for its bar, 0xFF none */
    uint16_t scene_wait;                                /* the tracks it still has to reach */
    uint8_t slot;                                       /* the song's slot playing */
    uint16_t slot_bar;                                  /* bars played in it */
    uint32_t rng;
    struct {
        int16_t pos;                                    /* step playing, -1 before the first */
        int8_t pdir;                                    /* ping-pong's way */
        uint8_t cue;                                    /* a pattern waiting for the bar, 0xFF none */
        uint32_t n, loops;                              /* steps judged, passes of the pattern */
        uint8_t last;                                   /* the last conditional step's outcome (PRE) */
        uint8_t lit;                                    /* 1 while a note of it sounds (the screen's meter) */
        uint16_t lit_until;                             /* ms (low 16 bits) */
    } t[NTRK];
} engine_t;
static engine_t E;

/* the engine's time base, 24 ticks a microsecond: the device's 24 MHz clock; the host simulator's own milliseconds
 * (its plat_ticks24 is the computer's clock, while the script's time runs much faster) */
static uint32_t now24(void)
{
#ifdef PLAT_HOST
    return plat_ms() * 24000u;
#else
    return plat_ticks24();
#endif
}
static uint32_t in24(void)                              /* when the last MIDI message in landed */
{
#ifdef PLAT_HOST
    return now24();
#else
    return plat_midi_in_t24();
#endif
}

static uint32_t rnd(void)
{
    uint32_t x = E.rng ? E.rng : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    E.rng = x;
    return x;
}

static uint32_t ports_to(uint32_t ports)               /* PO_* -> plat.h TO_* */
{
    uint32_t to = 0;
    if (ports & PO_USB)
        to |= TO_USB;
    if (ports & PO_BLE)
        to |= TO_BLE_HOST;
    if (ports & PO_BLEDEV)
        to |= TO_BLE_DEV;
#if PLAT_SMK37
    if ((ports & PO_TRS) && !(ports & PO_USB))         /* (the jack hears everything sent TO_USB already) */
        to |= TO_TRS;
#endif
    return to;
}

static uint32_t midi_sent;                              /* messages sent (the host's checks) */
static void midi_send(uint32_t pkt, uint32_t ports)
{
    uint32_t to = ports_to(ports);
    if (to) {
        plat_midi_send(pkt, to);
        midi_sent++;
    }
}

static uint32_t pkt3(uint32_t st, uint32_t d1, uint32_t d2)   /* a channel message as a USB-MIDI packet */
{
    return (st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
}
static uint32_t pkt2(uint32_t st, uint32_t d1)
{
    return (st >> 4) | st << 8 | (d1 & 0x7Fu) << 16;
}
#define PKT_RT(b) (0x0Fu | (uint32_t)(b) << 8)

static void ev_put(uint32_t due, uint32_t pkt, uint32_t ports, int off)
{
    ev_t *e;
    if (nev >= NEV)
        return;
    e = &evq[nev++];
    e->due = due;
    e->pkt = pkt;
    e->ports = (uint8_t)ports;
    e->off = (uint8_t)off;
}

/* a note and its note-off. A note-off owed for the same note, due after this note-on, is moved up to it: the synth
 * hears the old note end and the new one start, never a note-on over a held note */
static void note_sched(uint32_t on, uint32_t gate, uint32_t chan, uint32_t note, uint32_t vel, uint32_t ports)
{
    uint32_t i, off_pkt = pkt3(0x80u | chan, note, 0);
    if (nev + 2u > NEV - 8u)                            /* room for note-offs is kept */
        return;
    for (i = 0; i < nev; i++)
        if (evq[i].off && evq[i].pkt == off_pkt && evq[i].ports == ports && evq[i].due > on)
            evq[i].due = on;
    ev_put(on, pkt3(0x90u | chan, note, vel ? vel : 1u), ports, 0);
    ev_put(on + (gate ? gate : 1u), off_pkt, ports, 1);
}

static void ev_run(uint32_t now)                        /* what is due: note-offs first, then the rest */
{
    uint32_t i, pass;
    for (pass = 0; pass < 2u; pass++) {
        for (i = 0; i < nev;) {
            ev_t *e = &evq[i];
            if ((int32_t)(e->due - now) <= 0 && e->off == (pass == 0u)) {
                uint32_t room = e->off ? 0u : plat_midi_out_room(ports_to(e->ports));
                if (e->off || room == 0u || room >= 8u)  /* (0: nobody listens there; a few: keep them for note-offs) */
                    midi_send(e->pkt, e->ports);
                *e = evq[--nev];
            } else {
                i++;
            }
        }
    }
}

static void ev_flush_offs(void)                         /* a stop: every note-off owed goes now, the rest is dropped */
{
    uint32_t i;
    for (i = 0; i < nev; i++)
        if (evq[i].off)
            midi_send(evq[i].pkt, evq[i].ports);
    nev = 0;
}

/* ------------------------------------------------------------------- notes and keys --- */
static uint32_t quantize(uint32_t note)                 /* the nearest note of the key, at or below */
{
    uint32_t mask = SCALE_MASK[P.scale < NSCALE ? P.scale : 0], i;
    for (i = 0; i < 12u; i++) {
        uint32_t deg = (note + 120u - P.root - i) % 12u;
        if (mask >> deg & 1u)
            return note - i;
        if (note - i == 0u)
            break;
    }
    return note;
}

static int32_t track_note(uint32_t tr, uint32_t note)   /* a step's note as it goes out: -1 none */
{
    const track_t *t = &P.trk[tr];
    int32_t n;
    if (note == NOTE_NONE)
        return -1;
    if (t->type == TT_DRUM)
        return (int32_t)note;
    n = (int32_t)note + t->transpose;
    if (n < 0 || n > 127)
        return -1;
    if (t->quant && P.scale != SC_CHROM)
        n = (int32_t)quantize((uint32_t)n);
    return n;
}

static void lane_send(uint32_t tr, uint32_t lane, uint32_t v, uint32_t due, int now)
{
    const track_t *t = &P.trk[tr];
    uint32_t cc = t->lane_cc[lane], pkt, ch = t->chan & 15u;
    if (cc == LANE_NONE)
        return;
    if (cc == LANE_PB)
        pkt = pkt3(0xE0u | ch, (v & 1u) ? 0x7Fu : 0, v);   /* 64 = the middle; 127 the top */
    else if (cc == LANE_AT)
        pkt = pkt2(0xD0u | ch, v);
    else
        pkt = pkt3(0xB0u | ch, cc, v);
    if (now)
        midi_send(pkt, t->ports);
    else
        ev_put(due, pkt, t->ports, 0);
}

/* ------------------------------------------------------------------- conditions --- */
static void ab_of(uint32_t c, uint32_t *a, uint32_t *b) /* CO 1..35 -> A:B, b 2..8, a 1..b */
{
    uint32_t bb = 2, k = c - 1u;
    while (k >= bb) {
        k -= bb;
        bb++;
    }
    *a = k + 1u;
    *b = bb;
}

static void cond_text(uint32_t c, char *t)
{
    static const char *const W[] = {"FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST"};
    if (c == CO_ALWAYS) {
        t[0] = '-';
        t[1] = 0;
    } else if (c <= CO_AB_LAST) {
        uint32_t a, b;
        ab_of(c, &a, &b);
        t[0] = (char)('0' + a);
        t[1] = ':';
        t[2] = (char)('0' + b);
        t[3] = 0;
    } else {
        const char *s = W[(c - CO_FILL) % 8u];
        while ((*t++ = *s++) != 0) {
        }
    }
}

static int cond_ok(uint32_t tr, uint32_t c)
{
    uint32_t loops = E.t[tr].loops;
    if (c == CO_ALWAYS)
        return 1;
    if (c <= CO_AB_LAST) {
        uint32_t a, b;
        ab_of(c, &a, &b);
        return loops % b == a - 1u;
    }
    switch (c) {
    case CO_FILL: return E.fill;
    case CO_NFILL: return !E.fill;
    case CO_PRE: return E.t[tr].last;
    case CO_NPRE: return !E.t[tr].last;
    case CO_NEI: return tr ? E.t[tr - 1u].last : 0;
    case CO_NNEI: return tr ? !E.t[tr - 1u].last : 1;
    case CO_FIRST: return loops == 0u;
    case CO_NFIRST: return loops != 0u;
    default: return 1;
    }
}

/* ------------------------------------------------------------------- a step --- */
static int track_heard(uint32_t tr)
{
    if (E.solo)
        return E.solo >> tr & 1u;
    return !P.trk[tr].muted;
}

static void step_play(uint32_t tr, const step_t *st, uint32_t line, uint32_t div, uint32_t odd)
{
    const track_t *t = &P.trk[tr];
    const phead_t *ph = &P.ph[tr][P.pat[tr]];
    int32_t at = (int32_t)line, ok = 1;
    uint32_t i, k, hits, gate, sub, vel, ch = t->chan & 15u, on = st->note[0] != NOTE_NONE;
    if (odd && ph->swing > 50u)
        at += (int32_t)((ph->swing - 50u) * div / 50u);
    at += (int32_t)st->micro * (int32_t)div / 24;
    if (at < (int32_t)E.tick)
        at = (int32_t)E.tick;
    if (st->cond != CO_ALWAYS || (on && st->chance < 100u)) {
        ok = cond_ok(tr, st->cond);
        if (ok && on && st->chance < 100u)
            ok = rnd() % 100u < st->chance;
        E.t[tr].last = (uint8_t)ok;
    }
    if (!ok || !track_heard(tr))
        return;
    for (i = 0; i < NLANE; i++)                         /* the locks first (a step without notes too) */
        if (st->lock[i] != LOCK_NONE)
            lane_send(tr, i, st->lock[i], (uint32_t)at, 0);
    if (!on)
        return;
    hits = (st->rat & 7u) + 1u;
    gate = (uint32_t)st->len * div / 4u;
    if (st->flags & SF_GLIDE && gate < div + div / 4u)
        gate = div + div / 4u;                          /* legato: into the next step */
    sub = div / hits;
    if (hits > 1u && gate > sub * 3u / 4u)
        gate = sub * 3u / 4u;
    if (!gate)
        gate = 1;
    for (k = 0; k < hits; k++) {
        vel = st->flags & SF_ACCENT ? 127u : st->vel;
        if (hits > 1u) {                                /* a ratchet's velocity ramp */
            uint32_t r = (st->rat >> 4) & 3u;
            if (r == RR_UP)
                vel = vel * (k + 1u) / hits;
            else if (r == RR_DOWN)
                vel = vel * (hits - k) / hits;
        }
        for (i = 0; i < NNOTE; i++) {
            int32_t n = track_note(tr, st->note[i]);
            if (n >= 0)
                note_sched((uint32_t)at + k * sub, gate, ch, (uint32_t)n, vel, t->ports);
        }
    }
    E.t[tr].lit = 1;
    E.t[tr].lit_until = (uint16_t)(plat_ms() + 90u);
}

static void scene_apply_track(uint32_t tr, uint32_t sc)
{
    track_t *t = &P.trk[tr];
    P.pat[tr] = P.scene_pat[sc][tr] < NPAT ? P.scene_pat[sc][tr] : 0;
    t->muted = (uint8_t)(P.scene_mute[sc] >> tr & 1u);
    if (t->prog != PROG_NONE)
        midi_send(pkt2(0xC0u | (t->chan & 15u), t->prog), t->ports);
}

/* the track's step at the line `line` (an engine tick) */
static void track_step(uint32_t tr, uint32_t line)
{
    phead_t *ph;
    uint32_t div, len;
    int32_t pos;
    if (line % (4u * TPQ) == 0u) {                      /* a bar line: what was waiting for it */
        if (E.scene_cue != 0xFFu && (E.scene_wait >> tr & 1u)) {
            scene_apply_track(tr, E.scene_cue);
            E.scene_wait &= (uint16_t)~(1u << tr);
            E.t[tr].pos = -1;
            E.t[tr].loops = 0;
            if (!E.scene_wait) {
                P.scene = E.scene_cue;
                E.scene_cue = 0xFFu;
            }
        }
        if (E.t[tr].cue != 0xFFu) {
            P.pat[tr] = E.t[tr].cue;
            E.t[tr].cue = 0xFFu;
            E.t[tr].pos = -1;
            E.t[tr].loops = 0;
        }
    }
    ph = &P.ph[tr][P.pat[tr]];
    div = DIV_TICKS[ph->div < NDIV ? ph->div : DIV_16];
    len = ph->len ? ph->len : 16u;
    pos = E.t[tr].pos;
    if (pos < 0) {
        pos = ph->dir == DIR_REV ? (int32_t)len - 1 : 0;
        E.t[tr].pdir = 1;
    } else {
        switch (ph->dir) {
        case DIR_REV:
            if (--pos < 0) {
                pos = (int32_t)len - 1;
                E.t[tr].loops++;
            }
            break;
        case DIR_PING:
            pos += E.t[tr].pdir;
            if (pos >= (int32_t)len) {
                E.t[tr].pdir = -1;
                pos = len > 1u ? (int32_t)len - 2 : 0;
            } else if (pos < 0) {
                E.t[tr].pdir = 1;
                pos = len > 1u ? 1 : 0;
                E.t[tr].loops++;
            }
            break;
        case DIR_RAND:
            pos = (int32_t)(rnd() % len);
            if ((E.t[tr].n + 1u) % len == 0u)
                E.t[tr].loops++;
            break;
        default:
            if (++pos >= (int32_t)len) {
                pos = 0;
                E.t[tr].loops++;
            }
            break;
        }
    }
    E.t[tr].pos = (int16_t)pos;
    step_play(tr, STEP(tr, P.pat[tr], pos), line, div, E.t[tr].n & 1u);
    E.t[tr].n++;
}

/* the song: half a bar ahead, so every track hears a scene change at the same bar line */
static void song_bar(void)
{
    if (!E.song_on || !P.song_n)
        return;
    if (++E.slot_bar >= (P.song_bars[E.slot] ? P.song_bars[E.slot] : 1u)) {
        E.slot_bar = 0;
        if (++E.slot >= P.song_n) {
            if (!P.song_loop) {
                E.song_on = 0;
                return;
            }
            E.slot = 0;
        }
        E.scene_cue = P.song_scene[E.slot] % NSCENE;
        E.scene_wait = 0xFFFFu;
    }
}

static void engine_tick(void)                           /* one engine tick */
{
    uint32_t tr, now = E.tick;
    if (P.clock == CK_SEND && now % 4u == 0u)
        midi_send(PKT_RT(0xF8), PO_ALL);
    if ((now + 2u * TPQ) % (4u * TPQ) == 0u)
        song_bar();
    for (tr = 0; tr < NTRK; tr++) {
        const phead_t *ph = &P.ph[tr][P.pat[tr]];
        uint32_t div = DIV_TICKS[ph->div < NDIV ? ph->div : DIV_16];
        if ((now + div / 2u) % div == 0u)
            track_step(tr, now + div / 2u);
    }
    ev_run(now);
    E.tick = now + 1u;
}

/* ------------------------------------------------------------------- transport --- */
static void seq_start(int from_clock)
{
    uint32_t tr, f = plat_lock();
    nev = 0;
    E.tick = 0;
    E.acc24 = 0;
    E.last24 = now24();
    E.clk_in = 0;
    for (tr = 0; tr < NTRK; tr++) {
        E.t[tr].pos = -1;
        E.t[tr].n = 0;
        E.t[tr].loops = 0;
        E.t[tr].last = 0;
        if (E.t[tr].cue != 0xFFu) {
            P.pat[tr] = E.t[tr].cue;
            E.t[tr].cue = 0xFFu;
        }
        if (P.trk[tr].prog != PROG_NONE)
            midi_send(pkt2(0xC0u | (P.trk[tr].chan & 15u), P.trk[tr].prog), P.trk[tr].ports);
    }
    if (E.scene_cue != 0xFFu) {                         /* a scene cued while stopped starts now */
        for (tr = 0; tr < NTRK; tr++)
            scene_apply_track(tr, E.scene_cue);
        P.scene = E.scene_cue;
        E.scene_cue = 0xFFu;
    }
    if (E.song_on) {
        E.slot_bar = 0;
        for (tr = 0; tr < NTRK; tr++)
            scene_apply_track(tr, P.song_scene[E.slot] % NSCENE);
        P.scene = P.song_scene[E.slot] % NSCENE;
    }
    if (P.clock == CK_SEND && !from_clock)
        midi_send(PKT_RT(0xFA), PO_ALL);
    for (tr = 0; tr < NTRK; tr++)                       /* the steps on the first line, judged now */
        track_step(tr, 0);
    E.playing = 1;
    plat_unlock(f);
}

static void seq_stop(void)
{
    uint32_t f = plat_lock();
    if (E.playing && P.clock == CK_SEND)
        midi_send(PKT_RT(0xFC), PO_ALL);
    E.playing = 0;
    E.song_on = 0;
    ev_flush_offs();
    plat_unlock(f);
}

static void seq_cue_pattern(uint32_t tr, uint32_t p)
{
    if (!E.playing || P.launch == LA_NOW) {
        uint32_t f = plat_lock();
        P.pat[tr] = (uint8_t)p;
        E.t[tr].cue = 0xFFu;
        if (E.playing && E.t[tr].pos >= (int32_t)P.ph[tr][p].len)
            E.t[tr].pos = -1;
        plat_unlock(f);
    } else {
        E.t[tr].cue = (uint8_t)(p == P.pat[tr] ? 0xFFu : p);
    }
}

static void seq_cue_scene(uint32_t sc, int now)
{
    uint32_t tr, f = plat_lock();
    if (!E.playing || now) {
        for (tr = 0; tr < NTRK; tr++)
            scene_apply_track(tr, sc);
        P.scene = (uint8_t)sc;
        E.scene_cue = 0xFFu;
        if (E.playing)
            for (tr = 0; tr < NTRK; tr++)
                E.t[tr].pos = -1, E.t[tr].loops = 0;
    } else {
        E.scene_cue = (uint8_t)sc;
        E.scene_wait = 0xFFFFu;
    }
    plat_unlock(f);
}

/* ------------------------------------------------------------------- MIDI in --- */
/* what came in for the main loop: notes (thru and recording are the main loop's), CCs */
typedef struct {
    uint8_t st, d1, d2, port;
    uint32_t tick;                                      /* the engine's tick when it came */
} in_t;
#define NIN 64u
static in_t inq[NIN];
static volatile uint32_t in_w, in_r;
static uint8_t thru_note[128];                          /* what a thru note-on sent, to end it (0xFF none) */
static uint8_t thru_chan[128], thru_ports[128];
static volatile uint8_t sel_track;                      /* the UI's track: thru plays it (seq.c reads it) */

static void thru(uint32_t st, uint32_t d1, uint32_t d2)  /* MIDI in plays the selected track (tick) */
{
    uint32_t ty = st & 0xF0u, tr = sel_track % NTRK;
    const track_t *t = &P.trk[tr];
    if (ty == 0x90u && d2) {
        int32_t n = t->type == TT_DRUM ? (int32_t)d1 : track_note(tr, d1);
        if (n < 0)
            return;
        if (thru_note[d1] != 0xFFu)
            midi_send(pkt3(0x80u | thru_chan[d1], thru_note[d1], 0), thru_ports[d1]);
        thru_note[d1] = (uint8_t)n;
        thru_chan[d1] = (uint8_t)(t->chan & 15u);
        thru_ports[d1] = t->ports;
        midi_send(pkt3(0x90u | thru_chan[d1], (uint32_t)n, d2), t->ports);
    } else if (ty == 0x80u || ty == 0x90u) {
        if (thru_note[d1] != 0xFFu) {
            midi_send(pkt3(0x80u | thru_chan[d1], thru_note[d1], 0), thru_ports[d1]);
            thru_note[d1] = 0xFFu;
        }
    } else if (ty == 0xB0u || ty == 0xE0u || ty == 0xD0u) {   /* wheels, pedals, pressure: to the track's channel */
        midi_send(ty == 0xD0u ? pkt2(ty | (t->chan & 15u), d1) : pkt3(ty | (t->chan & 15u), d1, d2), t->ports);
    }
}

static void clock_in(uint32_t st)
{
    uint32_t at24 = in24();
    if (P.clock != CK_RECEIVE)
        return;
    if (st == 0xF8u) {
        uint32_t d = at24 - E.clk_t24;
        if (E.clk_t24 && d > 2000u * PLAT_TICKS_PER_US && d < 250000u * PLAT_TICKS_PER_US)
            E.clk_per24 = E.clk_per24 ? (E.clk_per24 * 3u + d) / 4u : d;
        E.clk_t24 = at24;
        E.clk_ms = plat_ms();
        E.follow = 1;
        if (E.playing)
            E.clk_in++;
    } else if (st == 0xFAu) {
        seq_start(1);
    } else if (st == 0xFBu) {
        if (!E.playing) {
            E.playing = 1;
            E.last24 = now24();
        }
    } else if (st == 0xFCu) {
        seq_stop();
    }
}

static void midi_read(void)                             /* tick: everything that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st >= 0xF8u) {
            clock_in(st);
            continue;
        }
        if (st < 0x80u || st >= 0xF0u)
            continue;
        if (P.thru)
            thru(st, d1, d2);
        if (in_w - in_r < NIN) {
            in_t *m = &inq[in_w % NIN];
            m->st = (uint8_t)st;
            m->d1 = (uint8_t)d1;
            m->d2 = (uint8_t)d2;
            m->port = (uint8_t)port;
            m->tick = E.tick;
            in_w++;
        }
    }
}

/* ------------------------------------------------------------------- the clock --- */
static uint32_t tick_per24(void)                        /* 24 MHz ticks an engine tick */
{
    if (P.clock == CK_RECEIVE && E.follow && E.clk_per24)
        return E.clk_per24 / 4u;
    return 150000000u / (P.bpm10 ? P.bpm10 : 1200u);
}

static uint32_t tempo10_now(void)                       /* the tempo playing, x 10 */
{
    if (P.clock == CK_RECEIVE && E.follow && E.clk_per24)
        return 25000000u / (E.clk_per24 / PLAT_TICKS_PER_US);   /* 600e6 / (24 clocks x the clock's us) */
    return P.bpm10;
}

void app_tick(void)
{
    uint32_t t24 = now24(), per, n = 0;
    midi_read();
    if (E.follow && plat_ms() - E.clk_ms > 500u)        /* the clock stopped: our own tempo again */
        E.follow = 0;
    if (!E.playing) {
        E.last24 = t24;
        return;
    }
    per = tick_per24();
    E.acc24 += t24 - E.last24;
    E.last24 = t24;
    if (P.clock == CK_RECEIVE && E.follow) {
        uint32_t target = E.clk_in * 4u;                /* never ahead of the clock's next pulse, never behind it */
        while (E.tick + 4u <= target && n < 16u) {
            engine_tick();
            n++;
            E.acc24 = 0;
        }
        while (E.acc24 >= per && E.tick < target + 3u && n < 8u) {
            E.acc24 -= per;
            engine_tick();
            n++;
        }
        if (E.tick >= target + 3u)
            E.acc24 = 0;
        return;
    }
    while (E.acc24 >= per && n < 8u) {
        E.acc24 -= per;
        engine_tick();
        n++;
    }
    if (E.acc24 > per * 8u)                             /* (a long stall: catch up no further) */
        E.acc24 = 0;
}

/* ------------------------------------------------------------------- live notes --- */
/* the keys (the FM-1's keys, the SMK-37's keybed and pads) playing a track now: what each sent, to end it */
static uint8_t key_note[128], key_chan[128], key_ports[128];

static void live_on(uint32_t tr, uint32_t note, uint32_t vel)
{
    const track_t *t = &P.trk[tr];
    int32_t n = t->type == TT_DRUM ? (int32_t)note : track_note(tr, note);
    if (n < 0 || note > 127u)
        return;
    if (key_note[note] != 0xFFu)
        midi_send(pkt3(0x80u | key_chan[note], key_note[note], 0), key_ports[note]);
    key_note[note] = (uint8_t)n;
    key_chan[note] = (uint8_t)(t->chan & 15u);
    key_ports[note] = t->ports;
    midi_send(pkt3(0x90u | key_chan[note], (uint32_t)n, vel ? vel : 1u), t->ports);
}

static void live_off(uint32_t note)
{
    if (note > 127u || key_note[note] == 0xFFu)
        return;
    midi_send(pkt3(0x80u | key_chan[note], key_note[note], 0), key_ports[note]);
    key_note[note] = 0xFFu;
}

static void panic_all(void)                             /* all notes off, on every channel and port */
{
    uint32_t ch, i;
    seq_stop();
    for (i = 0; i < 128u; i++)
        key_note[i] = thru_note[i] = 0xFFu;
    for (ch = 0; ch < 16u; ch++) {
        midi_send(pkt3(0xB0u | ch, 123, 0), PO_ALL);
        midi_send(pkt3(0xB0u | ch, 120, 0), PO_ALL);
    }
}

static void seq_init(void)
{
    uint32_t i;
    E.scene_cue = 0xFFu;
    for (i = 0; i < NTRK; i++) {
        E.t[i].cue = 0xFFu;
        E.t[i].pos = -1;
    }
    for (i = 0; i < 128u; i++)
        key_note[i] = thru_note[i] = 0xFFu;
    E.rng = 0x9E3779B9u ^ plat_ticks24();
}
