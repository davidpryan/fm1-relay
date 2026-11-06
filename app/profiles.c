/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* Device profiles: what a track drives, so the screen names its lanes and drum notes ("CUTOFF", "KICK") instead of
 * numbers. Picking a profile on TRACK sets the track's type, lanes and drum notes; the channel and ports stay the
 * player's. The LOOM-8 kit's notes are the notes of LOOM-8's eleven black keys (F#3 up): to check on a unit, and each
 * lane's note can be changed (TRACK page 2, KNOB 3: the lane picked on Steps). */

typedef struct {
    const char *name;                    /* on the TRACK page */
    const char *tag;                     /* short, in the tag line */
    uint8_t type;
    uint8_t lane_cc[NLANE];
    uint8_t ndrum;
    uint8_t drum[NDLANE];
    const char *const *drum_name;
} profile_t;

static const char *const KIT_LOOM[NDLANE] = {"KEY 1", "KEY 2", "KEY 3", "KEY 4", "KEY 5", "KEY 6", "KEY 7", "KEY 8",
                                             "KEY 9", "KEY 10", "KEY 11"};
static const char *const KIT_GM[NDLANE] = {"KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "LO TOM", "HI TOM", "RIM",
                                           "CRASH", "RIDE", "COWBELL", "TAMB", "CONGA", "SHAKER", "CLAVE", "PERC"};
static const char *const KIT_JIANT[NDLANE] = {"KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "LO TOM", "HI TOM",
                                              "RIM"};
static const char *const KIT_GEN[NDLANE] = {"LANE 1", "LANE 2", "LANE 3", "LANE 4", "LANE 5", "LANE 6", "LANE 7",
                                            "LANE 8", "LANE 9", "LANE 10", "LANE 11", "LANE 12", "LANE 13", "LANE 14",
                                            "LANE 15", "LANE 16"};

/* (new profiles go at the end: a project keeps its tracks' profiles by number) */
enum { PR_GENERIC, PR_LOOM_SYNTH, PR_LOOM_KIT, PR_GM_DRUMS, PR_FM1, PR_DRUMS, PR_JIANT, PR_JIANT_DRUMS, PR_CLIPPER,
       NPROF };
static const profile_t PROFILES[NPROF] = {
    {"GENERIC SYNTH", "SYNTH", TT_NOTE, {74, 71, 1, 7}, 0, {0}, 0},
    {"LOOM-8 SYNTH", "LOOM", TT_NOTE, {74, 71, 73, 72}, 0, {0}, 0},
    {"LOOM-8 KIT", "KIT", TT_DRUM, {7, 10, 91, 93}, 11, {54, 56, 58, 61, 63, 66, 68, 70, 73, 75, 78}, KIT_LOOM},
    {"GM DRUMS", "GM", TT_DRUM, {7, 10, 91, 93}, 16,
     {36, 38, 39, 42, 46, 45, 50, 37, 49, 51, 56, 54, 63, 70, 75, 76}, KIT_GM},
    {"FM-1 STOCK", "FM-1", TT_NOTE, {74, 71, 1, 7}, 0, {0}, 0},
    {"DRUM MACHINE", "DRUMS", TT_DRUM, {7, 10, 1, 74}, 16,
     {36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51}, KIT_GEN},
    /* JIANT FM (juanjiant-bit/JIANT): six tracks on channels 1-6 (ROUTE CH1-6), channel 16 its song's (program changes
     * there jump sections and projects); CC 16-23 the engine's own eight knobs, 102-104 the post filter and drive */
    {"JIANT SYNTH", "JIANT", TT_NOTE, {74, 71, 16, 17}, 0, {0}, 0},
    /* its DRUM-X track: the GM drum map, notes 35-81 (outside it, variations) */
    {"JIANT DRUM-X", "J-DRUM", TT_DRUM, {7, 74, 91, 94}, 8, {36, 38, 39, 42, 46, 45, 50, 37}, KIT_JIANT},
    /* Clipper (isod89/clipper-fm1): one synth (two parts in MULTI), listening on OMNI or GLO > MIDI > IN CH; its four
     * macros on CC 16-19, MORPH on 20 */
    {"CLIPPER", "CLIP", TT_NOTE, {74, 16, 17, 20}, 0, {0}, 0},
};

/* a CC's name for the screen: LOOM-8's where the track plays LOOM-8, else the common ones; 0 = only its number */
static const char *cc_name(uint32_t prof, uint32_t cc)
{
    if (cc == LANE_PB)
        return "BEND";
    if (cc == LANE_AT)
        return "PRESSURE";
    if (prof == PR_JIANT || prof == PR_JIANT_DRUMS) {
        static const char *const E[8] = {"E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"};
        if (cc >= 16u && cc <= 23u)
            return E[cc - 16u];          /* the engine's own knobs, EDIT 1 then EDIT 2 */
        switch (cc) {
        case 94: return "DELAY";
        case 102: return "POST CUT";
        case 103: return "POST RES";
        case 104: return "DRIVE";
        case 105: return "SUSTAIN";
        default: break;
        }
    }
    if (prof == PR_CLIPPER) {
        switch (cc) {
        case 16: return "BRITE";
        case 17: return "TIMBR";
        case 18: return "MOTN";
        case 19: return "SPACE";
        case 20: return "MORPH";
        case 74: return "CUTOFF";
        case 76: return "LFO RATE";
        case 77: return "VIBRATO";
        case 78: return "LFO FADE";
        case 94: return "DELAY";
        default: break;
        }
    }
    if (prof == PR_LOOM_SYNTH || prof == PR_LOOM_KIT) {
        switch (cc) {
        case 1: return "LFO AMT";
        case 5: return "GLIDE";
        case 7: return "LEVEL";
        case 9: return "MUTE";
        case 10: return "PAN";
        case 71: return prof == PR_LOOM_KIT ? "LOW CUT" : "RESO";
        case 72: return prof == PR_LOOM_KIT ? "LENGTH" : "RELEASE";
        case 73: return "ATTACK";
        case 74: return prof == PR_LOOM_KIT ? "HI CUT" : "CUTOFF";
        case 75: return "DECAY";
        case 91: return "SEND I";
        case 93: return "SEND II";
        default: break;
        }
    }
    switch (cc) {
    case 1: return "MOD";
    case 2: return "BREATH";
    case 5: return "PORTA";
    case 7: return "VOLUME";
    case 10: return "PAN";
    case 11: return "EXPR";
    case 64: return "SUSTAIN";
    case 71: return "RESO";
    case 72: return "RELEASE";
    case 73: return "ATTACK";
    case 74: return "CUTOFF";
    case 75: return "DECAY";
    case 91: return "REVERB";
    case 93: return "CHORUS";
    default: return 0;
    }
}

/* a drum lane's name: the profile's, or the note's */
static const char *drum_lane_name(const track_t *t, uint32_t lane)
{
    const profile_t *p = &PROFILES[t->prof < NPROF ? t->prof : 0];
    if (p->drum_name && lane < p->ndrum)
        return p->drum_name[lane];
    return KIT_GEN[lane & 15u];
}

static uint32_t drum_lanes(const track_t *t)   /* how many lanes the drum track shows */
{
    const profile_t *p = &PROFILES[t->prof < NPROF ? t->prof : 0];
    return p->ndrum ? p->ndrum : NDLANE;
}

static void profile_apply(track_t *t, uint32_t prof)
{
    const profile_t *p = &PROFILES[prof];
    uint32_t i;
    t->prof = (uint8_t)prof;
    t->type = p->type;
    for (i = 0; i < NLANE; i++)
        t->lane_cc[i] = p->lane_cc[i];
    for (i = 0; i < NDLANE; i++)
        t->drum[i] = p->type == TT_DRUM ? (i < p->ndrum ? p->drum[i] : (uint8_t)(36 + i)) : (uint8_t)(36 + i);
    if (t->lane_sel >= drum_lanes(t))
        t->lane_sel = 0;
}
