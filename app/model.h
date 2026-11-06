/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* RELAY-16's project: 16 tracks, each with 8 patterns of up to 64 steps; 16 scenes; a song of 32 slots. The head
 * (everything but the steps) is small and lives in .bss; the steps (128 KiB) live in the pool. */
#pragma once
#include <stdint.h>

#define NTRK 16
#define NPAT 8
#define NSTEP 64
#define NNOTE 4                          /* notes a step holds (a chord, or drum lanes hit together) */
#define NLANE 4                          /* CC lanes a track has: a step locks each */
#define NDLANE 16                        /* drum lanes a drum track names */
#define NSCENE 16
#define NSLOT 32
#define NOTE_NONE 0xFFu
#define LOCK_NONE 0xFFu
#define LANE_NONE 0xFFu
#define LANE_PB 128u                     /* a lane that sends pitch bend (its 0..127 as the bend's top seven bits) */
#define LANE_AT 129u                     /* channel aftertouch */
#define PROG_NONE 0xFFu

enum { TT_NOTE, TT_DRUM, NTT };
enum { DIR_FWD, DIR_REV, DIR_PING, DIR_RAND, NDIR };
enum { CK_INTERNAL, CK_SEND, CK_RECEIVE, NCK };
enum { LA_BAR, LA_NOW, NLA };            /* scene and pattern changes: at the next bar, or at once */
/* where a track's notes go */
#define PO_USB 1u                        /* USB: the computer, or the device on the USB host */
#define PO_BLE 2u                        /* the computer or phone connected over Bluetooth */
#define PO_BLEDEV 4u                     /* the Bluetooth MIDI device RELAY-16 connected to */
#define PO_TRS 8u                        /* the SMK-37's TRS jack (it also hears USB: fm1-base sends USB's there) */
#define PO_ALL (PO_USB | PO_BLE | PO_BLEDEV | PO_TRS)

/* conditions: 0 always; 1..36 A:B (b = 2..8, a = 1..b); then the rest */
enum { CO_ALWAYS = 0, CO_AB_LAST = 35, CO_FILL, CO_NFILL, CO_PRE, CO_NPRE, CO_NEI, CO_NNEI, CO_FIRST, CO_NFIRST,
       NCO };

/* a step: 16 bytes */
typedef struct {
    uint8_t note[NNOTE];                 /* MIDI notes, NOTE_NONE for none; the step is on when note[0] is set */
    uint8_t vel;                         /* 1..127 */
    uint8_t len;                         /* gate, quarter steps: 1..255 (4 = one step) */
    int8_t micro;                        /* -11..11: twenty-fourths of a step, early or late */
    uint8_t chance;                      /* 1..100 % */
    uint8_t cond;                        /* CO_* */
    uint8_t rat;                         /* bits 0..2: hits - 1 (1..8 hits); bits 4..5: velocity ramp RR_* */
    uint8_t flags;                       /* SF_* */
    uint8_t rsv;
    uint8_t lock[NLANE];                 /* a lane's value at this step, 0..127, or LOCK_NONE */
} step_t;
#define SF_GLIDE 1u                      /* legato: this step's notes overlap the next */
#define SF_ACCENT 2u                     /* velocity raised to 127 */
enum { RR_FLAT, RR_UP, RR_DOWN, NRR };

typedef struct {                         /* a pattern's own settings */
    uint8_t len;                         /* 1..64 steps */
    uint8_t div;                         /* DIV_* index: a step's note value */
    uint8_t dir;                         /* DIR_* */
    uint8_t swing;                       /* 50..75 %: the late half of each pair of steps */
} phead_t;

typedef struct {                         /* 44 bytes */
    uint8_t type;                        /* TT_* */
    uint8_t chan;                        /* 0..15 */
    uint8_t ports;                       /* PO_* */
    uint8_t prof;                        /* profile (profiles.c): the names of its lanes and drum notes */
    uint8_t lane_cc[NLANE];              /* CC number, LANE_PB, LANE_AT or LANE_NONE */
    int8_t transpose;                    /* -24..24 */
    uint8_t quant;                       /* 1: notes snap to the project's key and scale */
    uint8_t prog;                        /* program change sent when a scene starts it, or PROG_NONE */
    uint8_t defvel;                      /* a new step's velocity */
    uint8_t rem[NNOTE];                  /* the notes a step press writes (the last keys held together) */
    uint8_t drum[NDLANE];                /* a drum track's lanes: their notes */
    uint8_t lane_sel;                    /* the drum lane a step press writes */
    uint8_t muted;                       /* the live mute (a scene sets it) */
    uint8_t rsv[10];
} track_t;

typedef struct {
    uint32_t magic;
    uint16_t version, bpm10;             /* tempo x 10: 300..3000 */
    uint8_t clock;                       /* CK_* */
    uint8_t launch;                      /* LA_* */
    uint8_t root, scale;                 /* the key: root 0..11 (C..B), SCALE_* */
    uint8_t thru;                        /* 1: MIDI in plays the selected track */
    uint8_t keyoct;                      /* the keys' octave, -3..3 stored + 3 */
    uint8_t song_n, song_loop;
    uint8_t scene;                       /* the scene playing (or last launched) */
    uint8_t rsv[7];
    track_t trk[NTRK];
    phead_t ph[NTRK][NPAT];
    uint8_t pat[NTRK];                   /* each track's pattern now */
    uint8_t scene_pat[NSCENE][NTRK];
    uint16_t scene_mute[NSCENE];
    uint8_t scene_used[NSCENE];
    uint8_t song_scene[NSLOT], song_bars[NSLOT];
} head_t;

#define PROJ_MAGIC 0x31594C52u           /* "RLY1" */
#define PROJ_VERSION 1u

/* a step's note value, in the engine's ticks (96 a quarter note) */
#define TPQ 96u
static const uint16_t DIV_TICKS[] = {12, 16, 24, 32, 48, 64, 96, 192, 384};
static const char *const DIV_NAME[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 BAR"};
#define NDIV ((int)(sizeof DIV_TICKS / sizeof DIV_TICKS[0]))
#define DIV_16 2

enum { SC_CHROM, SC_MAJOR, SC_MINOR, SC_DORIAN, SC_PHRYG, SC_LYDIAN, SC_MIXO, SC_LOCRIAN, SC_HMINOR, SC_MAJPENT,
       SC_MINPENT, SC_BLUES, NSCALE };
static const uint16_t SCALE_MASK[NSCALE] = {   /* bit i: semitone i above the root is in the scale */
    0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x9AD, 0x295, 0x4A9, 0x4E9};
static const char *const SCALE_NAME[NSCALE] = {"CHROMATIC", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN",
                                               "MIXOLYDIAN", "LOCRIAN", "HARM MINOR", "MAJ PENTA", "MIN PENTA",
                                               "BLUES"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
