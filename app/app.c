/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* RELAY-16: the app's unity build, compiled after fm1-base's platform (base/device/unity.c, base/host/plat_host.c):
 * gfx.c, announce.c and lib/btdev.c are in scope.
 *   model.h     the project's shape
 *   seq.c       the sequencer: the clock, the steps, MIDI out and in
 *   profiles.c  the instruments a track can drive, by name
 *   store.c     the project and the settings in flash
 *   ui.c        the pages and the controls
 *   draw.c      the screen and the lights */
#include "model.h"
#include "seq.c"
#include "profiles.c"
#include "store.c"
#include "ui.c"
#include "draw.c"

void app_init(void)
{
    uint32_t i;
    seq_init();
    settings_load();
    project_new();
    project_load(set.slot);
    ui.proj_sel = set.slot;
    ui.page = PG_STEPS;
    ui.card = ui.hold_b = NB;
    ui.eu_steps = 16;
    ui.eu_pulses = 5;
    ui.eu_rot = 0;
    ui.rnd_density = 50;
    ui.rnd_range = 12;
    ui.rnd_seed = 1;
    for (i = 0; i < 4u; i++)
        ui.last_fader[i] = -1;
    for (i = 0; i < NLANE; i++)
        ui.lane_live[i] = 64;
    sel_track = 0;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)                 /* RELAY-16 makes no sound of its own */
        lr[i] = 0;
}

void app_panic(void)
{
    if (E.playing)
        seq_stop();
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, K_BG);
#if PLAT_SMK37
    plat_trs_out(!set.trs_off);
#endif
    if (set.usb_host)
        plat_usb_host(1);
    ui.redraw = 1;
}

static void speak(void)                          /* a screen reader that starts listening hears where it is */
{
    char t[64];
    if (ann_on() && ann_where()) {
        s_cpy(t, APP_NAME ". ");
        s_cat(t, PG_TITLE[ui.page]);
        s_cat(t, ". TRACK ");
        i_text(sel_track + 1, t + text_len(t));
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void events(void)                         /* Bluetooth devices and the USB host: what happened */
{
    int ev;
    btdev_page(ui.page == PG_GLOBAL && ui.sub[PG_GLOBAL] == 2u && !set.ble_off);
    while ((ev = btdev_event()) != BTDEV_NONE)
        if (ev != BTDEV_CONNECTING)
            app_say(btdev_word(ev), btdev_name());
    switch (plat_usb_host_msg()) {
    case USBH_MSG_MIDI: app_say("USB", "DEVICE CONNECTED"); break;
    case USBH_MSG_NOMIDI: app_say("USB DEVICE", "HAS NO MIDI"); break;
    case USBH_MSG_LOW: app_say("USB DEVICE", "NEEDS POWER"); break;
    case USBH_MSG_LOST: app_say("USB DEVICE", "LOST"); break;
    default: break;
    }
    if (plat_usb_lost()) {                       /* what the lost device held: let go */
        uint32_t i;
        for (i = 0; i < 128u; i++)
            key_note[i] = thru_note[i] = 0xFFu;
    }
}

void app_frame(void)
{
    static uint32_t last_draw, last_msg;
    uint32_t now = plat_ms();
    input();
    speak();
    events();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if ((E.playing || now - last_msg < 300u) && now - last_draw >= 40u)
        ui.redraw = 1;                           /* the playheads move: 25 frames a second */
    if (plat_midi_ago(PORT_USB, 1) < 200u || plat_midi_ago(PORT_BLE, 1) < 200u)
        last_msg = now;
    if (ui.hold_b < NB && ui.hold_clean && ui.card == NB && now - ui.hold_t >= CARD_MS &&
        (ui.btn >> ui.hold_b & 1u) && ui.hold_b != (PLAT_SMK37 ? NB - 3u : NB)) {   /* (not FADER BANK: fill) */
        ui.card = ui.hold_b;                     /* a button held alone a second: its card */
        ui.redraw = 1;
        if (ann_on())
            ann_say(CARDS[ui.card].text, 0, ANN_NOW);
    }
    if (ui.save_pending && !E.playing) {        /* stopped (here, or by a clock's stop): the save waiting */
        project_save_say();
        ui.redraw = 1;
    }
    lights();
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = E.playing;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "track"))
        *got = sel_track;
    else if (!strcmp(what, "pattern"))
        *got = P.pat[sel_track];
    else if (!strcmp(what, "scene"))
        *got = P.scene;
    else if (!strcmp(what, "len"))
        *got = cur_ph()->len;
    else if (!strcmp(what, "dirty"))
        *got = ui.dirty;
    else if (!strcmp(what, "rec"))
        *got = ui.rec;
    else if (!strcmp(what, "srec"))
        *got = ui.srec;
    else if (!strcmp(what, "sent"))
        *got = (int)midi_sent;
    else if (!strcmp(what, "tick"))
        *got = (int)E.tick;
    else if (!strcmp(what, "pos"))
        *got = E.t[sel_track].pos;
    else if (!strcmp(what, "muted"))
        *got = P.trk[sel_track].muted;
    else if (!strcmp(what, "group"))
        *got = ui.group;
    else if (!strcmp(what, "card"))           /* the button whose help card shows, -1 none */
        *got = ui.card == NB ? -1 : ui.card;
    else if (!strcmp(what, "slot"))
        *got = set.slot;
    else if (!strcmp(what, "save_pending"))
        *got = ui.save_pending;
    else if (!strncmp(what, "slotlen", 7))     /* slotlenN: the bytes slot N holds (0 empty) */
        *got = (int)slot_info((uint32_t)atoi(what + 7) % NSLOTS, 0);
    else if (!strcmp(what, "song_n"))
        *got = P.song_n;
    else if (!strcmp(what, "tempo10"))
        *got = (int)tempo10_now();
    else if (!strncmp(what, "note", 4))         /* noteN: step N's first note on the selected track (-1 none) */
    {
        const step_t *s = STEP(sel_track, P.pat[sel_track], (uint32_t)atoi(what + 4) % NSTEP);
        *got = s->note[0] == NOTE_NONE ? -1 : s->note[0];
    } else if (!strncmp(what, "nnotes", 6)) {
        const step_t *s = STEP(sel_track, P.pat[sel_track], (uint32_t)atoi(what + 6) % NSTEP);
        uint32_t i, n = 0;
        for (i = 0; i < NNOTE; i++)
            n += s->note[i] != NOTE_NONE;
        *got = (int)n;
    } else if (!strncmp(what, "cond", 4)) {
        *got = STEP(sel_track, P.pat[sel_track], (uint32_t)atoi(what + 4) % NSTEP)->cond;
    } else if (!strncmp(what, "lock", 4)) {     /* lockN: step N's first lane */
        *got = STEP(sel_track, P.pat[sel_track], (uint32_t)atoi(what + 4) % NSTEP)->lock[0];
    } else if (!strcmp(what, "used")) {
        uint32_t s, n = 0;
        for (s = 0; s < NSTEP; s++)
            n += step_used(STEP(sel_track, P.pat[sel_track], s));
        *got = (int)n;
    } else
        return 0;
    return 1;
}
#endif
