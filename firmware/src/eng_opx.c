/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX (OPX fork): altered FM after the Korg opsix. Milestone M1: the six operators in FM mode, the 40 preset
 * algorithms and the user algorithm, ratio / fixed pitch, per-operator feedback, the loop feedback of algorithms
 * 4, 6 and 34 (ALG FB), per-operator level envelopes (ATK DEC SUS REL, CURVE LIN..EXP), level velocity. M2 (so far):
 * the 23 waveforms and WIDTH (opx_wt_build, opx_table). The other operator modes, key tracking, EG1-3, LFOs, the filter, effects and
 * the virtual patch come later
 * (docs/OPX_PLAN.md); so does the patch's storage (docs/OPX_STORAGE.md: M3). Until then a track plays one of the
 * factory patches below (SLOT).
 *
 * One renderer for every algorithm: an algorithm is a modulation matrix (op j -> op k amounts, per operator self
 * feedback, which operators are heard). The 40 preset ones are the opsix's charts (Owner's Manual p.83: 1..32 the
 * DX7's, 33..40 the opsix's own) at 100 % per connection ("100 % = the level of a preset algorithm's connection",
 * USER ALG p.74); USER is the patch's own matrix. Each sample the operators run from 6 down to 1: an input from a
 * higher operator is this sample's, from a lower one (a loop: algorithms 4, 6, 34, or a USER connection upwards)
 * the previous sample's. Self feedback is the mean of the operator's last two outputs (the DX7's), linear in FB:
 * 100 % = 2 rad ("near 75 % sawtooth-like, above it noise", p.43).
 *
 * Assumptions to check by ear against an opsix (the manual gives no curve): an operator at LEVEL 100 and a full
 * envelope modulates by 4 pi rad (2 cycles, the DX7's level 99); LEVEL is (L / 100)^3 in amplitude (-7.5 dB at 75,
 * -18 dB at 50); envelope times on the patch format's TIME10 code (tools/opx_patch_spec.py); CURVE 10 is an
 * exponential approach (2^-10 at the end of the stage), 1..9 between it and linear.
 *
 * The device's EDIT values over the patch: the opsix's HOME page (p.42), and SLOT:
 *   ALG   PAT = the patch's algorithm, 1..40 another one, USR the patch's user matrix
 *   ATK   all operators' attack times, longer (+) or shorter (-) (the opsix's HOME ATK)
 *   DEC   all operators' decay and release times (HOME DECAY)
 *   SLOT  F1..F7 load a factory patch into the track; OWN the track's own (edited on the operator pages, ui_fm6op.c,
 *         or loaded with a project, opx_store.c). The patch is opx_patch.c's packed record per track
 * Felucca's FM6-style macros (E3..E6: MLVL, FB, MRAT, DTUN) are not the opsix's: off (OPX_FELUCCA_MACROS 0; the track
 * keeps their values, nothing reads them, no page shows them: ui.c page_visible)
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done). The track's FLT moves MLVL,
 * SHP the feedback, PIT the pitch.
 *
 * State: per voice an opx_note_t in the part's engine memory (engines.c eng_mem), per part the patch through the
 * macros (opx_rt, rebuilt once a block in opx_block). */
#ifndef OPX_FELUCCA_MACROS
#define OPX_FELUCCA_MACROS 0     /* MLVL FB MRAT DTUN (E3..E6): Felucca's, not the opsix's; off */
#endif
#define OPX_DLY 784u             /* M2b: a voice's delay line (int16, SHORT DELAY / COMB), after the six notes in its part */
#define OPX_POLY 6               /* engine_t.poly: as FM6, until the target's cost is measured */
#define OPX_NOP 6
#define OPX_NALG 40              /* preset algorithms; 40 = USER */
#define OPX_USER 40u
/* M2: the waveforms (opx_table, below) */
enum { OPX_W_SIN, OPX_W_SIN12, OPX_W_SIN8, OPX_W_TRI, OPX_W_SAW, OPX_W_SAWHD, OPX_W_SQR, OPX_W_SQRHD, OPX_W_ASAW3,
       OPX_W_ASQR3, OPX_W_ATRI3, OPX_W_A12345, OPX_W_A12, OPX_W_A13, OPX_W_A14, OPX_W_A15, OPX_W_A16, OPX_W_A17,
       OPX_W_A18, OPX_W_NOISE_SH, OPX_W_WHITE, OPX_W_PINK, OPX_W_BLUE, OPX_NWAVE };
#define OPX_W_NARROW 0x80u
/* M2b: the effects' E and F values (p.34): lo, hi of each, in the units the patch stores them (fx_e / fx_f = value -
 * lo): PEAK EQ FREQ (0.1 semitone) GAIN (0.1 dB); SHELV EQ LOW HIGH (0.1 dB); PHASER FREQ, FB (%); SHORT DELAY TIME
 * (ms) FB (%); COMB FREQ, FB (%); DIST DIST (%) LOW (0.1 dB); DRIVE DRIVE (%) LOW; DEC FREQ BIT (0.01); WAVESHAPER TYPE
 * (0..59) DAMP (%); PUNCH PUNCH (%) HIGH (0.1 dB) */
static const int16_t OPX_FXV[10][4] = {
    {-500, 500, -180, 180}, {-180, 180, -180, 180}, {-500, 500, -100, 100}, {1, 300, 0, 100}, {-500, 500, -100, 100},
    {0, 100, -180, 180}, {0, 100, -180, 180}, {200, 25600, 200, 2400}, {0, 59, 0, 100}, {0, 100, -180, 180}};
/* M2b: the operator modes (MODE p.43, the patch format's order) */
enum { OPX_M_FM, OPX_M_RING, OPX_M_FILTER, OPX_M_FILTER_FM, OPX_M_FOLD, OPX_M_EFFECT, OPX_M_BYPASS, OPX_M_MUTE, OPX_NMODE };
static const char *const N_OPX_WAVE[OPX_NWAVE + 1] = {
    "SIN", "SIN 12BIT", "SIN 8BIT", "TRI", "SAW", "SAW HD", "SQR", "SQR HD", "ADTV SAW3", "ADTV SQR3", "ADTV TRI3",
    "ADTV 12345", "ADTV 1+2", "ADTV 1+3", "ADTV 1+4", "ADTV 1+5", "ADTV 1+6", "ADTV 1+7", "ADTV 1+8", "NOISE S/H",
    "NOISE WHITE", "NOISE PINK", "NOISE BLUE", 0};

/* ------------------------------------------------------------- the patch --- */
typedef struct {                 /* one operator: the patch format's fields (tools/opx_patch_spec.py) M1 plays */
    uint8_t level;               /* 0..100 % */
    uint8_t coarse;              /* 0..38: 1/128 .. 1/2, 1 .. 32 (index 7 = 1) */
    uint16_t fine;               /* x0.001, 500..2000 */
    int8_t detune;               /* cents, -50..50 */
    int8_t trans;                /* semitones, -12..12 */
    uint8_t fixed;               /* 0 RATIO, 1 FIXED */
    uint32_t fixed_freq;         /* x0.01 Hz, 1..999999 */
    uint16_t atk, dec, rel;      /* TIME10: 0 = 0 ms, 1..1023 = 1 ms x 90000^((c-1)/1022) */
    uint8_t sus;                 /* 0..100 */
    uint8_t curve;               /* 0 LIN .. 10 EXP */
    uint8_t fb;                  /* FM FB, 0..100 % */
    uint8_t vel;                 /* LMOD VEL: level velocity sensitivity, 0..100 % */
    uint8_t wave;                /* M2: OPX_W_* (Waveform List p.84), 0 = SIN */
    uint8_t width;               /* M2: FM WIDTH, 0 = OFF, 1..99 % */
    uint8_t mode;                /* M2b: OPX_M_* (MODE p.43) */
    uint8_t ring_depth, ring_shape;               /* RING MOD: DEPTH SHAPE, % */
    uint8_t fold_gain, fold_oscmix; int8_t fold_bias;   /* WAVE FOLDER: GAIN OSC MIX %, BIAS -100..100 % */
    uint8_t flt_type, flt_reso, flt_oscmix; int16_t flt_cutoff;   /* FILTER (FM): TYPE, RESO OSC MIX %, CUTOFF x0.1 st */
    uint8_t fx_type, fx_oscmix; uint16_t fx_e, fx_f;   /* EFFECT: FX, OSC MIX %, E F (OPX_FXV's, less lo) */
    int8_t ktrk_low, ktrk_high;  /* KTRK (p.53): the slopes below / above CENTER, -100..100 % */
    uint8_t ktrk_center, ktrk_lcrv, ktrk_hcrv;   /* CENTER (MIDI note 21..108), the curves (0 LIN, 1 EXP) */
} opx_op_t;

typedef struct {
    const char *name;
    uint8_t algo;                /* 0..39 = algorithm 1..40, 40 = USER */
    uint8_t alg_fb;              /* 0..100 %: the loop of algorithms 4, 6, 34 */
    uint8_t ua[OPX_NOP][OPX_NOP];   /* USER: ua[x][y] op x+1 -> op y+1 (%), ua[x][x] op x+1's self feedback */
    uint8_t ua_out;              /* USER: bit k: op k+1 is heard (DIRECT OUT) */
    opx_op_t op[OPX_NOP];        /* op[0] = OP1 */
} opx_patch_t;

/* ---------------------------------------------------------- algorithms --- */
/* algorithm a (0..39): mod[k] bit j: op j+1 modulates op k+1; out: the heard operators; fb: the operator whose
 * self loop the chart draws (1..6, 0 none: informational, every operator has its own FB); loop: a feedback
 * connection that ALG FB scales (src << 4 | dst, operators 1..6; 0 none) */
#define B(n) (1u << ((n) - 1))
typedef struct { uint8_t mod[OPX_NOP], out, fb, loop; } opx_alg_t;
static const opx_alg_t OPX_ALG[OPX_NALG] = {
    /*        op1               op2      op3              op4              op5     op6        out                      fb loop */
    {{B(2), 0, B(4), B(5), B(6), 0}, B(1) | B(3), 6, 0},                                          /* 1 */
    {{B(2), 0, B(4), B(5), B(6), 0}, B(1) | B(3), 2, 0},                                          /* 2 */
    {{B(2), B(3), 0, B(5), B(6), 0}, B(1) | B(4), 6, 0},                                          /* 3 */
    {{B(2), B(3), 0, B(5), B(6), 0}, B(1) | B(4), 0, 4 << 4 | 6},                                 /* 4: 4 -> 6 */
    {{B(2), 0, B(4), 0, B(6), 0}, B(1) | B(3) | B(5), 6, 0},                                      /* 5 */
    {{B(2), 0, B(4), 0, B(6), 0}, B(1) | B(3) | B(5), 0, 5 << 4 | 6},                             /* 6: 5 -> 6 */
    {{B(2), 0, B(4) | B(5), 0, B(6), 0}, B(1) | B(3), 6, 0},                                      /* 7 */
    {{B(2), 0, B(4) | B(5), 0, B(6), 0}, B(1) | B(3), 4, 0},                                      /* 8 */
    {{B(2), 0, B(4) | B(5), 0, B(6), 0}, B(1) | B(3), 2, 0},                                      /* 9 */
    {{B(2), B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(4), 3, 0},                                      /* 10 */
    {{B(2), B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(4), 6, 0},                                      /* 11 */
    {{B(2), 0, B(4) | B(5) | B(6), 0, 0, 0}, B(1) | B(3), 2, 0},                                  /* 12 */
    {{B(2), 0, B(4) | B(5) | B(6), 0, 0, 0}, B(1) | B(3), 6, 0},                                  /* 13 */
    {{B(2), 0, B(4), B(5) | B(6), 0, 0}, B(1) | B(3), 6, 0},                                      /* 14 */
    {{B(2), 0, B(4), B(5) | B(6), 0, 0}, B(1) | B(3), 2, 0},                                      /* 15 */
    {{B(2) | B(3) | B(5), 0, B(4), 0, B(6), 0}, B(1), 6, 0},                                      /* 16 */
    {{B(2) | B(3) | B(5), 0, B(4), 0, B(6), 0}, B(1), 2, 0},                                      /* 17 */
    {{B(2) | B(3) | B(4), 0, 0, B(5), B(6), 0}, B(1), 3, 0},                                      /* 18 */
    {{B(2), B(3), 0, B(6), B(6), 0}, B(1) | B(4) | B(5), 6, 0},                                   /* 19 */
    {{B(3), B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(2) | B(4), 3, 0},                               /* 20 */
    {{B(3), B(3), 0, B(6), B(6), 0}, B(1) | B(2) | B(4) | B(5), 3, 0},                            /* 21 */
    {{B(2), 0, B(6), B(6), B(6), 0}, B(1) | B(3) | B(4) | B(5), 6, 0},                            /* 22 */
    {{0, B(3), 0, B(6), B(6), 0}, B(1) | B(2) | B(4) | B(5), 6, 0},                               /* 23 */
    {{0, 0, B(6), B(6), B(6), 0}, B(1) | B(2) | B(3) | B(4) | B(5), 6, 0},                        /* 24 */
    {{0, 0, 0, B(6), B(6), 0}, B(1) | B(2) | B(3) | B(4) | B(5), 6, 0},                           /* 25 */
    {{0, B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(2) | B(4), 6, 0},                                  /* 26 */
    {{0, B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(2) | B(4), 3, 0},                                  /* 27 */
    {{B(2), 0, B(4), B(5), 0, 0}, B(1) | B(3) | B(6), 5, 0},                                      /* 28 */
    {{0, 0, B(4), 0, B(6), 0}, B(1) | B(2) | B(3) | B(5), 6, 0},                                  /* 29 */
    {{0, 0, B(4), B(5), 0, 0}, B(1) | B(2) | B(3) | B(6), 5, 0},                                  /* 30 */
    {{0, 0, 0, 0, B(6), 0}, B(1) | B(2) | B(3) | B(4) | B(5), 6, 0},                              /* 31 */
    {{0, 0, 0, 0, 0, 0}, 0x3Fu, 6, 0},                                                            /* 32 */
    {{B(2), B(3) | B(5) | B(6), B(4), 0, 0, 0}, B(1), 6, 0},                                      /* 33 */
    {{B(2), B(3), B(4), B(5), B(6), 0}, B(1), 0, 4 << 4 | 6},                                     /* 34: 4 -> 6 */
    {{B(2) | B(3), 0, 0, B(5) | B(6), 0, 0}, B(1) | B(4), 6, 0},                                  /* 35 */
    {{B(3) | B(4) | B(5) | B(6), B(3) | B(4) | B(5) | B(6), 0, 0, 0, 0}, B(1) | B(2), 6, 0},      /* 36 */
    {{B(3) | B(5), B(3) | B(5), B(4), 0, B(6), 0}, B(1) | B(2), 6, 0},                            /* 37 */
    {{B(3) | B(4), B(3) | B(4), 0, 0, B(6), 0}, B(1) | B(2) | B(5), 6, 0},                        /* 38 */
    {{B(5) | B(6), B(5) | B(6), B(5) | B(6), B(5) | B(6), 0, 0}, B(1) | B(2) | B(3) | B(4), 6, 0},/* 39 */
    {{B(5), B(5), B(5), B(5), B(6), 0}, B(1) | B(2) | B(3) | B(4), 6, 0},                         /* 40 */
};
#undef B

/* ------------------------------------------------------------- tables --- */
/* log2 of the coarse ratios 1/128 .. 1/2, 1 .. 32, Q24 */
static const int32_t OPX_COARSE[39] = {
    -117440512, -100663296, -83886080, -67108864, -50331648, -33554432, -16777216, 0, 16777216, 26591258,
    33554432, 38955489, 43368474, 47099600, 50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
    65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858, 74816848, 75892776, 76922906, 77910978,
    78860292, 79773775, 80654032, 81503396, 82323963, 83117622, 83886080};
/* CURVE 10: (1 - 2^-10x) / (1 - 2^-10), x = i / 32, Q24 */
static const int32_t OPX_EXPC[33] = {
    0, 3270638, 5904303, 8025050, 9732770, 11107904, 12215224, 13106888, 13824896, 14403069, 14868639, 15243538,
    15545423, 15788515, 15984263, 16141888, 16268816, 16371023, 16453325, 16519598, 16572965, 16615938, 16650541,
    16678406, 16700844, 16718911, 16733460, 16745176, 16754610, 16762207, 16768324, 16773250, 16777216};

/* a stage's progress per block (Q24 of the whole stage) for TIME10 code c (0 = at once) */
static int32_t opx_stage_inc(int32_t c)
{
    if (c <= 0)
        return 1 << 24;
    c = c > 1023 ? 1023 : c;
    /* 2^24 x 32 / (44.1 x 1 ms x 90000^((c-1)/1022)): log2 23.5373 - 16.4577 (c-1)/1022, as fm6_exp2 takes it */
    return fm6_exp2(-7762931 - 270170 * (c - 1)) >> 0;
}
/* the stage shape at progress x (Q24, 0..1) for CURVE c (0..10), Q24 */
static int32_t opx_shape(int32_t x, int32_t c)
{
    int32_t i = x >> 19, f = x & 0x7FFFF, e;
    if (!c)
        return x;
    if (i >= 32)
        return 1 << 24;
    e = OPX_EXPC[i] + (int32_t)(((int64_t)(OPX_EXPC[i + 1] - OPX_EXPC[i]) * f) >> 19);
    return x + (int32_t)(((int64_t)(e - x) * c) / 10);
}
/* LEVEL 0..100 as an amplitude, Q15: (L / 100)^3 */
static int32_t opx_level_amp(int32_t l)
{
    l = clamp(l, 0, 100);
    return (int32_t)((uint64_t)(l * l * l) * 32767u / 1000000u);
}

/* ------------------------------------------------------ the factory patches --- */
/* OP(level, coarse index, fine x0.001, detune, atk, dec, rel, sus, curve, fb, vel): a ratio operator */
#define OP(l, c, fi, dt, a, d, r, s, cv, fb, v) {(l), (c), (fi), (dt), 0, 0, 0, (a), (d), (r), (s), (cv), (fb), (v)}
#define R1 7                     /* coarse index of ratio 1 (R1 + n - 1: ratio n) */
#define OFF OP(0, R1, 1000, 0, 0, 0, 0, 0, 0, 0, 0)
static const opx_patch_t OPX_FACTORY[] = {
    {"E.PIANO", 4, 0, {{0}}, 0, {   /* algorithm 5: three pairs, a tine (op 3, 4) and a bell (op 5, 6) */
        OP(100, R1, 1000, 2, 1, 682, 300, 0, 8, 0, 60), OP(62, R1, 1000, -2, 1, 620, 300, 0, 8, 0, 70),
        OP(80, R1, 1000, -3, 1, 764, 400, 0, 8, 0, 60), OP(58, R1 + 13, 1000, 3, 1, 450, 300, 0, 8, 0, 80),
        OP(45, R1, 1000, 0, 1, 806, 400, 0, 8, 0, 60), OP(55, R1 + 13, 1000, 0, 1, 414, 300, 0, 9, 40, 80)}},
    {"BELL", 4, 0, {{0}}, 0, {      /* algorithm 5, inharmonic modulators */
        OP(100, R1, 1000, 0, 1, 806, 806, 0, 10, 0, 50), OP(52, R1 + 2, 1410, 0, 1, 764, 764, 0, 10, 0, 60),
        OP(85, R1, 1000, 4, 1, 764, 764, 0, 10, 0, 50), OP(48, R1 + 5, 1000, 0, 1, 682, 682, 0, 10, 0, 60),
        OP(70, R1 + 1, 1000, -4, 1, 720, 720, 0, 10, 0, 50), OP(42, R1 + 7, 1414, 0, 1, 600, 600, 0, 10, 0, 60)}},
    {"FM BASS", 0, 0, {{0}}, 0, {   /* algorithm 1: a pair and a stack */
        OP(100, R1 - 1, 1000, 0, 1, 682, 207, 70, 6, 0, 40), OP(78, R1 - 1, 1000, 0, 1, 476, 207, 30, 6, 0, 60),
        OP(80, R1 - 1, 1000, 1, 1, 620, 207, 60, 6, 0, 40), OP(70, R1, 1000, 0, 1, 414, 207, 20, 6, 0, 60),
        OP(55, R1 + 2, 1000, 0, 1, 306, 207, 0, 6, 0, 60), OP(50, R1, 1000, 0, 1, 351, 207, 0, 6, 60, 60)}},
    {"BRASS", 21, 0, {{0}}, 0, {    /* algorithm 22: one modulator on three carriers, slow bright attack */
        OP(100, R1, 1000, 0, 351, 620, 300, 85, 4, 0, 30), OP(72, R1, 1000, 0, 394, 558, 300, 70, 4, 0, 50),
        OP(90, R1, 1000, 6, 351, 620, 300, 85, 4, 0, 30), OP(90, R1, 1000, -6, 351, 620, 300, 85, 4, 0, 30),
        OP(70, R1 + 1, 1000, 0, 351, 620, 300, 80, 4, 0, 30), OP(74, R1, 1000, 0, 414, 600, 300, 65, 4, 45, 50)}},
    {"SAW LEAD", 31, 0, {{0}}, 0, { /* algorithm 32: three detuned carriers at the sawtooth-like feedback */
        OP(100, R1, 1000, 0, 1, 682, 207, 90, 3, 75, 20), OP(90, R1, 1000, 7, 1, 682, 207, 90, 3, 75, 20),
        OP(90, R1, 1000, -7, 1, 682, 207, 90, 3, 75, 20), OFF, OFF, OFF}},
    {"LOOP PAD", 33, 70, {{0}}, 0, {/* algorithm 34: the 6-operator chain with its 4 -> 6 loop (ALG FB) */
        OP(100, R1, 1000, 0, 600, 826, 682, 90, 5, 0, 20), OP(55, R1, 1000, 3, 620, 826, 682, 80, 5, 0, 30),
        OP(50, R1 + 1, 1000, 0, 656, 826, 682, 70, 5, 0, 30), OP(48, R1, 1000, -3, 682, 826, 682, 70, 5, 0, 30),
        OP(45, R1 + 2, 1000, 0, 682, 826, 682, 60, 5, 0, 30), OP(45, R1, 1000, 0, 718, 826, 682, 60, 5, 0, 30)}},
    {"X-MOD", OPX_USER, 0,          /* USER: two carriers that modulate each other, a third modulating both */
        {{0, 12, 0, 0, 0, 0}, {10, 0, 0, 0, 0, 0}, {35, 35, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}, 0x03u, {
        OP(100, R1, 1000, 0, 1, 764, 414, 50, 6, 0, 40), OP(90, R1 + 1, 1005, 0, 1, 764, 414, 50, 6, 0, 40),
        OP(50, R1 + 2, 1000, 0, 1, 558, 414, 10, 6, 0, 60), OFF, OFF, OFF}},
};
#undef OP
#undef OFF
#define OPX_NFACTORY ((uint32_t)NELEM(OPX_FACTORY))

/* ------------------------------------------------- the patch, per part --- */
/* what one block plays: the patch through the macros. Rebuilt once a block (opx_block); read by the voices */
typedef struct {
    int32_t lg[OPX_NOP];         /* ratio: the operator's log2 pitch offset (Q24); fixed: unused */
    uint32_t fine[OPX_NOP];      /* ratio: FINE, Q16 */
    uint32_t finc[OPX_NOP];      /* fixed: the phase increment (Q32 cycles a sample), 0 = ratio */
    int32_t amp[OPX_NOP];        /* LEVEL (and MLVL), Q15 */
    int32_t fbq[OPX_NOP];        /* self feedback: phase per unit of output (Q32 cycles per Q15) */
    int32_t ainc[OPX_NOP], dinc[OPX_NOP], rinc[OPX_NOP];   /* stage progress per block, Q24 */
    int32_t sus[OPX_NOP];        /* Q24 */
    uint8_t curve[OPX_NOP], vel[OPX_NOP];
    uint8_t wv[OPX_NOP];         /* M2: the waveform (OPX_W_*), | OPX_W_NARROW with a WIDTH; 0 = a plain sine */
    uint32_t wr[OPX_NOP];        /* WIDTH: the phase's speed-up, Q16 (100 / WIDTH) */
    uint32_t wlim[OPX_NOP];      /* WIDTH: the phase where the wave ends (WIDTH % of a cycle), silent after */
    uint8_t md[OPX_NOP];         /* M2b: the mode (OPX_M_*) */
    int32_t mp[OPX_NOP][4];      /* M2b: the mode's values, ready for the samples (opx_mode_y) */
    int8_t kl[OPX_NOP], kh[OPX_NOP];   /* KTRK: the slopes (%), CENTER, the curves (bit 0 low EXP, bit 1 high EXP) */
    uint8_t kc[OPX_NOP], kcv[OPX_NOP];
    uint8_t nin[OPX_NOP];        /* inputs of op k: src[k][i] (0..5), amt[k][i] (Q15 of 100 %) */
    uint8_t src[OPX_NOP][OPX_NOP];
    int16_t amt[OPX_NOP][OPX_NOP];
    uint8_t out;                 /* bit k: op k+1 is heard */
} opx_rt_t;
static opx_rt_t opx_rt[NTRK];
/* the track's own patch (M3: loaded with the project, edited in the editor); 0 = the factory patch SLOT names. A
 * pointer the main loop sets, the audio ISR reads once a block (opx_build) */
static const opx_patch_t *volatile opx_own[NTRK];
#include "opx_patch.c"            /* the stored form, each track's patch, SLOT (OPX_OWN) */

/* M2b: operator k's mode values for the samples (opx_mode_y): RING  mp 0 DEPTH, 1 SHAPE (Q15); WAVE FOLDER  mp 0 the
 * input's gain (Q12: x1 at GAIN 0 .. x8 at 100 %: "higher values fold more times", the factor an assumption), 1 OSC
 * MIX (Q15), 2 BIAS (Q15 of full scale); FILTER / FILTER FM: opx_filt_block's */
static void opx_mode_build(const opx_op_t *o, opx_rt_t *r, uint32_t k)
{
    int32_t *mp = r->mp[k];
    mp[0] = mp[1] = mp[2] = mp[3] = 0;
    switch (r->md[k]) {
    case OPX_M_RING:
        mp[0] = clamp(o->ring_depth, 0, 100) * 32767 / 100;
        mp[1] = clamp(o->ring_shape, 0, 100) * 32767 / 100;
        break;
    case OPX_M_FILTER:
    case OPX_M_FILTER_FM:                            /* mp 0 TYPE, 1 OSC MIX (Q15), 2 CUTOFF's factor (Q15), 3 RESO (Q15) */
        mp[0] = clamp(o->flt_type, 0, 11);
        mp[1] = clamp(o->flt_oscmix, 0, 100) * 32767 / 100;
        mp[2] = fm6_exp2(clamp(o->flt_cutoff, -500, 500) * 139810 - (9 << 24));   /* 2^(cutoff / 12), 0.1 semitone */
        mp[3] = clamp(o->flt_reso, 0, 100) * 32767 / 100;
        break;
    case OPX_M_EFFECT:                               /* mp 0 FX, 1 OSC MIX (Q15), 2 E, 3 F (their units: OPX_FXV) */
        mp[0] = clamp(o->fx_type, 0, 9);
        mp[1] = clamp(o->fx_oscmix, 0, 100) * 32767 / 100;
        mp[2] = OPX_FXV[mp[0]][0] + clamp(o->fx_e, 0, OPX_FXV[mp[0]][1] - OPX_FXV[mp[0]][0]);
        mp[3] = OPX_FXV[mp[0]][2] + clamp(o->fx_f, 0, OPX_FXV[mp[0]][3] - OPX_FXV[mp[0]][2]);
        break;
    case OPX_M_FOLD:
        mp[0] = 4096 + clamp(o->fold_gain, 0, 100) * 7 * 4096 / 100;
        mp[1] = clamp(o->fold_oscmix, 0, 100) * 32767 / 100;
        mp[2] = clamp(o->fold_bias, -100, 100) * 32767 / 100;
        break;
    default:
        break;
    }
}
static void opx_build(const track_t *t, opx_rt_t *r)
{
    const opx_patch_t *pt = opx_own[(uint32_t)(t - trk) % NTRK];
    int32_t alg = t->p[P_E0], datk = clamp(t->p[P_E1], -64, 63) * 4, ddec = clamp(t->p[P_E2], -64, 63) * 4;
#if OPX_FELUCCA_MACROS
    int32_t m = clamp(t->p[P_E3], -64, 63), dfb = clamp(t->p[P_E4], -64, 63) * 100 / 64, mrat = t->p[P_E5];
    int32_t spread = clamp(t->p[P_E6], 0, 127);
#else
    const int32_t m = 0, dfb = 0, mrat = 0, spread = 0;
#endif
    int32_t mlvl, k, j, ncar = 0, ci = 0;
    uint32_t a = (uint32_t)(alg - 1) > OPX_USER ? OPX_USER : (uint32_t)(alg - 1);   /* (ALG PAT: below) */
    int32_t self[OPX_NOP];
    if (!pt)
        pt = &OPX_FACTORY[(uint32_t)clamp(t->p[P_E7], 0, (int32_t)OPX_NFACTORY - 1)];
    if (alg <= 0)
        a = pt->algo;
    /* (USER plays the patch's matrix as it is: one without a heard operator is silent, as the opsix's; the EDIT 1 page
     * shows it, OUT all off) */
    /* the matrix */
    for (k = 0; k < OPX_NOP; k++) {
        r->nin[k] = 0;
        self[k] = 0;
    }
    if (a == OPX_USER) {
        r->out = pt->ua_out & 0x3Fu;
        for (k = 0; k < OPX_NOP; k++)
            for (j = 0; j < OPX_NOP; j++) {
                int32_t v = clamp(pt->ua[j][k], 0, 100);
                if (j == k)
                    self[k] = v;
                else if (v)
                    r->src[k][r->nin[k]] = (uint8_t)j, r->amt[k][r->nin[k]++] = (int16_t)(v * 32767 / 100);
            }
    } else {
        const opx_alg_t *g = &OPX_ALG[a];
        r->out = g->out;
        for (k = 0; k < OPX_NOP; k++)
            for (j = 0; j < OPX_NOP; j++)
                if ((g->mod[k] >> j) & 1u)
                    r->src[k][r->nin[k]] = (uint8_t)j, r->amt[k][r->nin[k]++] = 32767;
        if (g->loop) {
            int32_t fb = clamp(pt->alg_fb + dfb, 0, 100);
            k = (g->loop & 15) - 1;
            j = (g->loop >> 4) - 1;
            if (fb)
                r->src[k][r->nin[k]] = (uint8_t)j, r->amt[k][r->nin[k]++] = (int16_t)(fb * 32767 / 100);
        }
    }
    /* MLVL: 2^(m / 16), +-24 dB over +-64 (the track's FLT adds per voice: opx_render) */
    mlvl = fm6_exp2(m * (1 << 20) - (9 << 24));       /* Q15: 2^(m/16) x 2^15 */
    for (k = 0; k < OPX_NOP; k++)
        ncar += (r->out >> k) & 1;
    for (k = 0; k < OPX_NOP; k++) {
        const opx_op_t *o = &pt->op[k];
        int32_t heard = (r->out >> k) & 1, c = o->coarse, amp = opx_level_amp(o->level), fb;
        if (!heard && mrat)
            c = clamp(c + mrat, 0, 38);
        r->lg[k] = OPX_COARSE[c] + o->trans * ((1 << 24) / 12) + o->detune * 13981;
        if (heard && ncar > 1 && spread)               /* DTUN: the heard operators apart, the outer ones +-12.5 cents */
            r->lg[k] += (ci * 2 - (ncar - 1)) * spread * 13981 * 25 / (2 * 127 * (ncar - 1));
        ci += heard;
        r->fine[k] = (uint32_t)clamp(o->fine, 500, 2000) * 65536u / 1000u;
        r->finc[k] = o->fixed ? (uint32_t)(((uint64_t)clamp((int32_t)o->fixed_freq, 1, 999999) << 32) / 4410000u) : 0u;
        r->amp[k] = heard ? amp : (int32_t)clamp((amp * mlvl) >> 15, 0, 32767);
        fb = clamp(o->fb + self[k] + dfb, 0, 100);
        r->fbq[k] = fb * 417;                          /* 100 %: 2 rad = 0.3183 cycle at a full output: 0.3183 x 2^32 / 32767 / 100 */
        r->ainc[k] = opx_stage_inc(o->atk ? o->atk + datk : 0);
        r->dinc[k] = opx_stage_inc(o->dec ? o->dec + ddec : 0);
        r->rinc[k] = opx_stage_inc(o->rel ? o->rel + ddec : 0);
        r->sus[k] = clamp(o->sus, 0, 100) * ((1 << 24) / 100);
        r->curve[k] = (uint8_t)clamp(o->curve, 0, 10);
        r->vel[k] = (uint8_t)clamp(o->vel, 0, 100);
        r->wv[k] = (uint8_t)(o->wave < OPX_NWAVE ? o->wave : 0u);
        r->wr[k] = r->wlim[k] = 0;
        r->md[k] = (uint8_t)(o->mode < OPX_NMODE ? o->mode : OPX_M_FM);
        opx_mode_build(o, r, (uint32_t)k);
        r->kl[k] = o->ktrk_low, r->kh[k] = o->ktrk_high, r->kc[k] = o->ktrk_center;
        r->kcv[k] = (uint8_t)((o->ktrk_lcrv & 1u) | (o->ktrk_hcrv & 1u) << 1);
        if (r->md[k] != OPX_M_FM)                      /* (FB and WIDTH: FM's; the others, the oscillator as it is) */
            r->fbq[k] = 0;
        if (o->width && o->width < 100u && r->wv[k] < OPX_W_NOISE_SH && r->md[k] == OPX_M_FM) {   /* (noises: no width) */
            r->wv[k] |= OPX_W_NARROW;
            r->wr[k] = (100u << 16) / o->width;
            r->wlim[k] = (uint32_t)(((uint64_t)o->width << 32) / 100u);
        }
    }
}

/* --------------------------------------------------------- the waveforms --- */
/* M2: the opsix's 23 waveforms (Waveform List p.84), in its order, and WIDTH (p.43). How they are made, the manual not
 * saying:
 *   SIN               the sine table (dsp.c sine_i's), as M1
 *   SIN 12BIT / 8BIT  the sine, its low 4 / 8 bits cleared
 *   TRI               a triangle: a table of 4 lines (exact, read between its points)
 *   ADTV              sums of sines: SAW3 1 + 1/2 + 1/3, SQR3 1 + 1/3 (3rd) + 1/5 (5th), TRI3 1 - 1/9 + 1/25,
 *                     12345 the first five harmonics at one level, 1+n the fundamental and harmonic n at one level;
 *                     each scaled to a full-scale peak; tables of 256 points
 *   SAW, SQR          band-limited: a SAW table per power of two of harmonics (1, 2, 4 .. 256), the one for the
 *                     operator's pitch picked each block, its harmonics up to 10 kHz; the HD ones up to 20 kHz:
 *                     "richer high-end overtones" (p.84; the CPU cost is the same here). The harmonics at 1/n, 77 % of
 *                     full scale. SQR is the SAW table less itself half a cycle on (the even harmonics cancel, the odd
 *                     ones double: 0.77 x 4 / pi x the odd ones' 1/n); its lone fundamental, the highest peak, stays in
 *                     range. Each table at 32 points a cycle of its top harmonic (128 at least, 1024 at most): the
 *                     reading's images 60 dB under that harmonic, under it at 1024 points
 *   NOISE             S/H: a random level held for each cycle of the operator (its pitch, unmodulated: the "musical"
 *                     noise); WHITE flat; PINK -3 dB / octave (Paul Kellet's three-pole filter on white); BLUE pink's
 *                     difference (+9 dB over 4 octaves). Made a block at a time; they ignore FM, FB and WIDTH
 *   WIDTH             1..99 %: the whole wave in that part of the cycle, silence in the rest (p.43's pictures: OFF,
 *                     50 %, 25 %). An assumption from the pictures; WIDTH OFF is the full cycle
 * Every periodic wave is a table read a sample (SQR two), as the sine (the device's benchmark, MENU > SYSTEM > INFO:
 * one big per-sample switch over the waves cost as much again as the sine itself). The tables (16 KB of RAM) are built
 * once, in the main loop (opx_wt_build, from opx_poll); until then every wave plays the sine */
#define OPX_MIP 9                                      /* SAW: 1, 2, 4 .. 256 harmonics */
#define OPX_NADTV (OPX_W_A18 - OPX_W_ASAW3 + 1u)
#define OPX_MIP_LOG2(l) ((l) < 2u ? 7u : (l) > 5u ? 10u : (l) + 5u)   /* SAW level l: 2^this points */
enum { WT_ADTV = 5, WT_SAW = WT_ADTV + OPX_NADTV * 257u,   /* (the triangle first: 4 lines and a guard) */
       WT_COUNT = WT_SAW + 3u * 129u + 257u + 513u + 4u * 1025u };
static int16_t opx_wt[WT_COUNT] __attribute__((section(".pool")));
static uint16_t opx_wt_saw[OPX_MIP];                  /* where each SAW level starts in opx_wt */
static volatile uint8_t opx_wt_ok;
/* the ADTV 1+n waves' gain to a full-scale peak, n = 2..8 (the peak of sin x + sin nx: 1.760 1.540 1.928 2 1.967 1.903
 * 1.981) */
static const uint16_t OPX_A1N_GAIN[7] = {18615, 21282, 16993, 16383, 16659, 17219, 16539};
static int32_t opx_sq15(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
/* an ADTV wave (w: OPX_W_ASAW3 ..) at phase p */
static int32_t opx_adtv(uint32_t w, uint32_t p)
{
    int32_t s;
    switch (w) {
    case OPX_W_ASAW3:
        s = sine_i(p) + (sine_i(p * 2u) >> 1) + sine_i(p * 3u) / 3;
        return (s * 22710) >> 15;
    case OPX_W_ASQR3:
        s = sine_i(p) + sine_i(p * 3u) / 3 + sine_i(p * 5u) / 5;
        return opx_sq15((s * 35107) >> 15);
    case OPX_W_ATRI3:
        s = sine_i(p) - sine_i(p * 3u) / 9 + sine_i(p * 5u) / 25;
        return (s * 28465) >> 15;
    case OPX_W_A12345:
        s = sine_i(p) + sine_i(p * 2u) + sine_i(p * 3u) + sine_i(p * 4u) + sine_i(p * 5u);
        return (s * 8269) >> 15;
    default:                                           /* 1+2 .. 1+8 */
        s = sine_i(p) + sine_i(p * (w - OPX_W_A12 + 2u));
        return (s * OPX_A1N_GAIN[(w - OPX_W_A12) % 7u]) >> 15;
    }
}
/* the tables (main loop, once: ~0.3 M sine reads) */
static void opx_wt_build(void)
{
    /* SAW: 0.77 x (2 / pi) sum (-1)^(n+1) sin(nx) / n, rising through 0 at phase 0 as the sine; x 256 (a constant: no
     * float on the device) */
    const int32_t amp = (int32_t)(0.77 * 2.0 * 32767.0 / 3.14159265358979 * 256.0);
    uint32_t i, w, l, h, at = WT_SAW;
    int16_t *t;
    if (opx_wt_ok)
        return;
    opx_wt[0] = 0, opx_wt[1] = 32767, opx_wt[2] = 0, opx_wt[3] = -32767, opx_wt[4] = 0;   /* TRI */
    for (w = 0; w < OPX_NADTV; w++) {
        t = opx_wt + WT_ADTV + w * 257u;
        for (i = 0; i < 256u; i++)
            t[i] = (int16_t)opx_adtv(OPX_W_ASAW3 + w, i << 24);
        t[256] = t[0];                                 /* (the guard: the reads need no mask) */
    }
    for (l = 0; l < OPX_MIP; l++) {
        uint32_t lg = OPX_MIP_LOG2(l), np = 1u << lg;
        opx_wt_saw[l] = (uint16_t)at;
        t = opx_wt + at;
        for (i = 0; i < np; i++) {
            int32_t acc = 0;
            for (h = 1; h <= 1u << l; h++) {
                int32_t c = (int32_t)((uint32_t)amp / h);   /* (an unsigned divide, as the firmware links) */
                acc += (int32_t)(((int64_t)sine_i((i << (32u - lg)) * h) * (h & 1u ? c : -c)) >> 15);
            }
            t[i] = (int16_t)opx_sq15(acc >> 8);
        }
        t[np] = t[0];
        at += np + 1u;
    }
    opx_wt_ok = 1;
}
typedef struct {                 /* an operator's wave for a block (opx_render) */
    const int16_t *t;            /* its table; 0: this block's noise */
    uint32_t sh;                 /* the phase's shift to a point: 32 - log2 of the table's points */
    uint32_t sq;                 /* SQR: the table less itself half a cycle on */
    int32_t mk;                  /* SIN 12BIT / 8BIT: the bits kept */
} opx_wsel_t;
/* wave w's table for a phase step of inc (SAW / SQR: the level its pitch allows); the noises: no table */
static void __attribute__((noinline)) opx_table(uint32_t w, uint32_t inc, opx_wsel_t *v)
{
    uint32_t hmax, l;
    v->t = SINE, v->sh = 22, v->sq = 0, v->mk = -1;
    if (!opx_wt_ok || w == OPX_W_SIN)
        return;
    if (w >= OPX_W_NOISE_SH) {
        v->t = 0;
    } else if (w == OPX_W_SIN12 || w == OPX_W_SIN8) {
        v->mk = w == OPX_W_SIN12 ? ~15 : ~255;
    } else if (w == OPX_W_TRI) {
        v->t = opx_wt, v->sh = 30;
    } else if (w >= OPX_W_ASAW3) {
        v->t = opx_wt + WT_ADTV + (w - OPX_W_ASAW3) * 257u, v->sh = 24;
    } else {   /* SAW SAW HD SQR SQR HD: harmonics up to 10 / 20 kHz: hmax = limit / f = limit x 2^32 / (44100 x inc) */
        hmax = (w == OPX_W_SAWHD || w == OPX_W_SQRHD ? (uint32_t)(20000ull * 4294967296ull / 44100u)
                                                     : (uint32_t)(10000ull * 4294967296ull / 44100u)) / (inc ? inc : 1u);
        for (l = 0; l + 1u < OPX_MIP && (2u << l) <= hmax; l++)
            ;
        v->t = opx_wt + opx_wt_saw[l], v->sh = 32u - OPX_MIP_LOG2(l);
        v->sq = w == OPX_W_SQR || w == OPX_W_SQRHD;
    }
}
/* table t (2^(32 - sh) points) at phase p, linearly between its points (as sine_i) */
static inline int32_t opx_tab(const int16_t *t, uint32_t sh, uint32_t p)
{
    uint32_t i = p >> sh;
    int32_t a = t[i], b = t[i + 1u];
    return a + (((b - a) * (int32_t)((p << (32u - sh)) >> 17)) >> 15);
}
typedef struct {                 /* an operator's noise state (a voice's) */
    uint32_t rng, lastp;         /* the generator; S/H: the last phase (a wrap = a new level) */
    int32_t hold;                /* S/H: the level held */
    int32_t pk[3], prev;         /* PINK / BLUE: the filter's poles (Q15 x ~8), the last raw pink */
} opx_noise_t;
/* PINK's three poles, in 32 bits (the device's 64-bit multiply-add costs ~twice a 32-bit one): pk += c x w - d x pk,
 * d = 1 - the pole; the bigger products halved first so nothing overflows (pk[0] to ~1.4 M, pk[1] ~260 k, pk[2] ~80 k) */
#define OPX_PINK_STEP(w, k0, k1, k2)                                                                               \
    (k0 += ((w * 6491) >> 16) - ((k0 * 154) >> 16),            /* 0.99765, 0.0990460 */                              \
     k1 += ((w * 19433) >> 16) - ((k1 * 2425) >> 16),          /* 0.96300, 0.2965164 */                              \
     k2 += ((w * 34494) >> 15) - (((k2 >> 1) * 28181) >> 15),  /* 0.57000, 1.0526913 */                              \
     k0 + k1 + k2 + ((w * 12111) >> 16))                       /* + 0.1848 white */
/* a block of noise w (OPX_W_NOISE_SH ..) into b; S/H at the operator's own phase ph, step inc. A loop per kind, the
 * state in registers */
static void __attribute__((noinline)) opx_noise_block(uint32_t w, opx_noise_t *z, uint32_t ph, uint32_t inc, int16_t *b)
{
    uint32_t i, rng = z->rng;
    int32_t k0 = z->pk[0], k1 = z->pk[1], k2 = z->pk[2], prev = z->prev, wh, x;
#define OPX_WHITE() (rng = rng * 1664525u + 1013904223u, (int32_t)rng >> 16)
    switch (w) {
    case OPX_W_NOISE_SH: {
        uint32_t last = z->lastp;
        int32_t hold = z->hold;
        for (i = 0; i < CTL; i++, ph += inc) {
            if (ph < last)                               /* a new cycle (the phase wrapped) */
                hold = OPX_WHITE();
            last = ph;
            b[i] = (int16_t)opx_sq15(hold);
        }
        z->lastp = last, z->hold = hold;
        break;
    }
    case OPX_W_WHITE:
        for (i = 0; i < CTL; i++)
            b[i] = (int16_t)opx_sq15(OPX_WHITE());
        break;
    case OPX_W_PINK:
        for (i = 0; i < CTL; i++) {
            wh = OPX_WHITE();
            x = OPX_PINK_STEP(wh, k0, k1, k2);
            b[i] = (int16_t)opx_sq15((x * 205) >> 10);   /* (/ 5) */
        }
        break;
    default:                                             /* BLUE: pink's difference */
        for (i = 0; i < CTL; i++) {
            wh = OPX_WHITE();
            x = OPX_PINK_STEP(wh, k0, k1, k2);
            b[i] = (int16_t)opx_sq15((x - prev) >> 1);
            prev = x;
        }
    }
#undef OPX_WHITE
    z->rng = rng, z->pk[0] = k0, z->pk[1] = k1, z->pk[2] = k2, z->prev = prev;
}
#undef OPX_PINK_STEP
/* operator k's wave at phase p: its table (with its WIDTH), or this block's noise sample nb */
static inline int32_t opx_wave(const opx_rt_t *r, uint32_t k, const opx_wsel_t *v, uint32_t p, int32_t nb)
{
    int32_t s;
    if (!v->t)
        return nb;
    if (r->wr[k]) {                                      /* WIDTH: the cycle squeezed into its first WIDTH % */
        if (p >= r->wlim[k])
            return 0;
        p = (uint32_t)(((uint64_t)p * r->wr[k]) >> 16);
    }
    s = opx_tab(v->t, v->sh, p);
    if (v->sq)
        s -= opx_tab(v->t, v->sh, p + 0x80000000u);
    return s & v->mk;
}

/* ------------------------------------------------------------- the voice --- */
enum { OPX_ATK, OPX_DEC, OPX_SUS, OPX_REL, OPX_DONE };
typedef struct {
    int32_t x, from, lvl;        /* the stage's progress, where it started, the level now (Q24) */
    uint8_t stage;
} opx_eg_t;
typedef struct {                 /* a sounding note: 540 bytes */
    uint32_t ph[OPX_NOP];
    int32_t y[OPX_NOP], yp[OPX_NOP];   /* the last two outputs (Q15) */
    int32_t g[OPX_NOP];          /* the gain at the end of the last block (Q15) */
    int32_t vg[OPX_NOP];         /* velocity's factor on the level (Q15) */
    int32_t kt[OPX_NOP];         /* M2b: KTRK's factor on the level for this note (Q15, up to x16) */
    int32_t fs[OPX_NOP][5];      /* M2b: the operator filter's or effect's state (opx_filt, opx_fx) */
    uint16_t dw;                 /* M2b: the voice's delay line (SHORT DELAY, COMB): where it writes next */
    opx_eg_t eg[OPX_NOP];
    opx_noise_t nz[OPX_NOP];     /* M2: the noise waveforms' state */
    int32_t dc;
    uint8_t live, down, rsv[2];
} opx_note_t;
ENG_MEM_FITS("OPX", (sizeof(opx_note_t) + OPX_DLY * sizeof(int16_t)) * OPX_POLY);

static opx_note_t *opx_note_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < OPX_POLY ? (opx_note_t *)ENG_MEM(t - trk) + i : 0;
}
/* voice v's delay line (SHORT DELAY, COMB): OPX_DLY samples after the part's six notes */
static int16_t *opx_dline(track_t *t, voice_t *v)
{
    return (int16_t *)((opx_note_t *)ENG_MEM(t - trk) + OPX_POLY) + (uint32_t)(v - t->v) % OPX_POLY * OPX_DLY;
}

/* one block of operator k's envelope: its level at the block's end (Q24) */
static int32_t opx_eg_step(opx_eg_t *e, const opx_rt_t *r, uint32_t k)
{
    int32_t s;
    switch (e->stage) {
    case OPX_ATK:                                  /* linear, from where a retrigger found it */
        e->x += r->ainc[k];
        if (e->x >= 1 << 24) {
            e->stage = OPX_DEC, e->x = 0, e->lvl = 1 << 24;
            break;
        }
        e->lvl = e->from + (int32_t)(((int64_t)((1 << 24) - e->from) * e->x) >> 24);
        break;
    case OPX_DEC:
        e->x += r->dinc[k];
        if (e->x >= 1 << 24) {
            e->stage = OPX_SUS, e->lvl = r->sus[k];
            break;
        }
        s = opx_shape(e->x, r->curve[k]);
        e->lvl = (1 << 24) - (int32_t)(((int64_t)((1 << 24) - r->sus[k]) * s) >> 24);
        break;
    case OPX_SUS:
        e->lvl = r->sus[k];                         /* (SUS may move while it holds) */
        break;
    case OPX_REL:
        e->x += r->rinc[k];
        if (e->x >= 1 << 24) {
            e->stage = OPX_DONE, e->lvl = 0;
            break;
        }
        s = opx_shape(e->x, r->curve[k]);
        e->lvl = e->from - (int32_t)(((int64_t)e->from * s) >> 24);
        break;
    default:
        e->lvl = 0;
    }
    return e->lvl;
}

/* KTRK (p.53): operator k's level factor for a note (Q15): from CENTER down LOW's slope, up HIGH's; + raises the level
 * away from CENTER, - lowers it. The manual gives no scale: here 100 % is 1 dB a semitone (LIN; 48 dB over four
 * octaves), EXP the square of the distance over 48 semitones (gentle near CENTER: 3 dB an octave away, 48 dB four),
 * -96 .. +24 dB. The level never goes past the operator's full scale (opx_render) */
static int32_t opx_ktrk(const opx_rt_t *r, uint32_t k, uint32_t note)
{
    int32_t d = (int32_t)note - r->kc[k], sl = d < 0 ? r->kl[k] : r->kh[k], ex = d < 0 ? r->kcv[k] & 1 : r->kcv[k] >> 1 & 1;
    int32_t db8;                                     /* dB x 256 */
    d = d < 0 ? -d : d;
    if (!sl || !d)
        return 32768;
    db8 = ex ? sl * d * d * 256 / (100 * 48) : sl * d * 256 / 100;
    db8 = clamp(db8, -96 * 256, 24 * 256);
    return fm6_exp2(db8 * 10885 - (9 << 24));          /* 2^(dB / 6.0206) x 2^15 (10885: 2^24 / (6.0206 x 256)) */
}
static void opx_note_on(track_t *t, voice_t *v)
{
    opx_note_t *n = opx_note_of(t, v);
    const opx_rt_t *r = &opx_rt[(uint32_t)(t - trk) % NTRK];
    int32_t vel = v->mvel ? v->mvel : v->vel, fresh, k;
    if (!n)
        return;
    fresh = (!v->env && !v->env_out) || !n->live;       /* from silence: phases, outputs, gains from 0 */
    for (k = 0; k < OPX_NOP; k++) {
        opx_eg_t *e = &n->eg[k];
        if (fresh) {
            n->ph[k] = 0;
            n->y[k] = n->yp[k] = n->g[k] = 0;
            e->lvl = 0;
            memset(&n->nz[k], 0, sizeof n->nz[k]);   /* (M2: the noises from a seed of their own per operator) */
            memset(n->fs[k], 0, sizeof n->fs[k]);   /* (M2b: the filters from rest) */
            n->nz[k].rng = 0x9E3779B9u * (uint32_t)(k + 1) + (uint32_t)v->note * 2654435761u;
        }
        e->from = e->lvl;                            /* a retrigger rises from where it is */
        e->x = 0;
        e->stage = OPX_ATK;
        /* LMOD VEL: at 100 % velocity 1 is silent, 127 full; linear in between */
        n->vg[k] = 32767 - (r->vel[k] * (127 - clamp(vel, 1, 127)) * 258 / 100);
        n->kt[k] = opx_ktrk(r, (uint32_t)k, v->note);
    }
    if (fresh) {
        n->dc = 0;
        n->dw = 0;
        memset(opx_dline(t, v), 0, OPX_DLY * sizeof(int16_t));   /* (M2b: no echo of an earlier note) */
    }
    n->live = n->down = 1;
}

static void opx_block(track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NTRK)
        opx_build(t, &opx_rt[tr]);
}

/* every heard operator's envelope has ended (or holds at 0 with nothing left to rise): the voice ends */
static int opx_done(track_t *t, voice_t *v)
{
    opx_note_t *n = opx_note_of(t, v);
    const opx_rt_t *r = &opx_rt[(uint32_t)(t - trk) % NTRK];
    uint32_t k;
    if (!n || !n->live)
        return 1;
    for (k = 0; k < OPX_NOP; k++) {
        const opx_eg_t *e = &n->eg[k];
        if (!((r->out >> k) & 1u) || e->stage == OPX_DONE || !r->amp[k] || r->md[k] == OPX_M_MUTE)
            continue;
        if (e->stage == OPX_SUS && !r->sus[k] && n->g[k] == 0)
            continue;
        return 0;
    }
    n->live = 0;
    return 1;
}

/* a block's samples (opx_render). Two orders, the same arithmetic, the same output to the bit:
 *   operator by operator (opx_op_*): each operator, 6 down to 1, runs the whole block, its state in registers, its
 *     inputs from the higher operators' outputs of the block (opx_ybuf); then the heard ones mixed sample by sample.
 *     The device's benchmark: a voice's cost is its loads and stores (state reloaded every sample cost the
 *     sample-by-sample loop about twice its arithmetic), as FM6's operator-at-a-time loops (fm6_op_run) show
 *   sample by sample (opx_loop): every operator each sample. Only for a patch where an operator takes a lower one's
 *     output (the previous sample's): algorithms 4, 6 and 34's loops (ALG FB), USER's upward links
 * Returns the high-pass's state */
typedef struct {                 /* an operator's state through a block (opx_op_*) */
    uint32_t ph, inc;
    int32_t y, yp, g, dg, fbq;
} opx_os_t;
static int32_t opx_ybuf[OPX_NOP][CTL];         /* the block's outputs (the audio ISR's alone; not reentered) */
static int32_t opx_mbuf[CTL];                  /* an operator's modulation input for the block */
static const int32_t opx_zero[CTL];
static uint8_t opx_sample_order;             /* tests: every patch sample by sample (the two orders compared) */
#define OPX_OP_LOOP(WAVE)                                                                                          \
    uint32_t i, ph = o->ph, inc = o->inc;                                                                            \
    int32_t y = o->y, yp = o->yp, g = o->g, dg = o->dg, fbq = o->fbq;                                                \
    for (i = 0; i < CTL; i++) {                                                                                      \
        uint32_t p = ph + ((uint32_t)mod[i] << 18) + (uint32_t)(((y + yp) >> 1) * fbq);                              \
        int32_t s = (WAVE);                                                                                          \
        (void)p;                                                                                                     \
        yp = y;                                                                                                      \
        y = (s * g) >> 15;                                                                                           \
        g += dg;                                                                                                     \
        ph += inc;                                                                                                   \
        yo[i] = y;                                                                                                   \
    }                                                                                                                \
    o->ph = ph, o->y = y, o->yp = yp, o->g = g
static void __attribute__((noinline)) opx_op_sine(opx_os_t *restrict o, const int32_t *restrict mod,
                                                  int32_t *restrict yo)
{
    OPX_OP_LOOP(sine_i(p));
}
static void __attribute__((noinline)) opx_op_tab(opx_os_t *restrict o, const int32_t *restrict mod,
                                                 int32_t *restrict yo, const int16_t *restrict t, uint32_t sh, int32_t mk)
{
    OPX_OP_LOOP(opx_tab(t, sh, p) & mk);               /* (mk: SIN 12BIT / 8BIT's bits; -1 the others) */
}
static void __attribute__((noinline)) opx_op_sqr(opx_os_t *restrict o, const int32_t *restrict mod,
                                                 int32_t *restrict yo, const int16_t *restrict t, uint32_t sh)
{
    OPX_OP_LOOP(opx_tab(t, sh, p) - opx_tab(t, sh, p + 0x80000000u));
}
/* WIDTH: opx_wave as it is (its branches the same way all block) */
static void __attribute__((noinline)) opx_op_gen(opx_os_t *restrict o, const int32_t *restrict mod,
                                                 int32_t *restrict yo, const opx_rt_t *restrict r, uint32_t k,
                                                 const opx_wsel_t *restrict v)
{
    OPX_OP_LOOP(opx_wave(r, k, v, p, 0));
}
static void __attribute__((noinline)) opx_op_noise(opx_os_t *restrict o, const int32_t *restrict mod,
                                                   int32_t *restrict yo, const int16_t *restrict nb)
{
    OPX_OP_LOOP(nb[i]);
}
#undef OPX_OP_LOOP
/* M2b: the operator filter (FILTER, FILTER FM; TYPE p.34). Its cutoff follows the operator's pitch, CUTOFF semitones
 * from it (-50.0 .. +50.0), at most 0.45 x 44.1 kHz; FILTER FM moves it with the operator's input, +-4 octaves at full
 * scale (an assumption: the manual says only that the input modulates it). RESO 0..100 %, up to self-oscillation.
 *   LPF HPF BPF BRF      a state-variable filter, 12 dB / octave (Zavalishin's trapezoidal one; BPF and BRF unity at
 *                        the centre)
 *   MG LPF12 .. MG BPF12 a ladder of four one-poles with feedback from the last (MG: the Moog-style one), the outputs
 *                        mixed for the 12 / 24 dB low- and high-passes and the 6 / 12 dB band-passes
 *   MS-20 LPF / HPF      the state-variable filter with its resonance limited in its loop (a rough MS-20 character)
 * The coefficients: OPX_TAN (tan(pi f / fs), Q16) and OPX_G1 (the one-pole's tan / (1 + tan), Q14) over f / fs = i /
 * 1024 (tools-made, read between their points); a block's (FILTER), each sample's (FILTER FM) */
static const int32_t OPX_TAN[513] = {
    0, 201, 402, 603, 804, 1005, 1207, 1408, 1609, 1810, 2011, 2213,
    2414, 2615, 2817, 3018, 3220, 3421, 3623, 3825, 4026, 4228, 4430, 4632,
    4834, 5036, 5239, 5441, 5644, 5846, 6049, 6252, 6455, 6658, 6861, 7064,
    7268, 7471, 7675, 7879, 8083, 8287, 8492, 8696, 8901, 9106, 9311, 9516,
    9721, 9927, 10133, 10339, 10545, 10751, 10958, 11165, 11372, 11579, 11786, 11994,
    12202, 12410, 12618, 12827, 13036, 13245, 13454, 13664, 13874, 14084, 14295, 14506,
    14717, 14928, 15140, 15352, 15564, 15776, 15989, 16202, 16416, 16630, 16844, 17058,
    17273, 17489, 17704, 17920, 18136, 18353, 18570, 18787, 19005, 19223, 19442, 19661,
    19880, 20100, 20320, 20541, 20762, 20983, 21205, 21427, 21650, 21873, 22097, 22321,
    22546, 22771, 22997, 23223, 23449, 23676, 23904, 24132, 24360, 24590, 24819, 25049,
    25280, 25511, 25743, 25975, 26208, 26442, 26676, 26911, 27146, 27382, 27618, 27855,
    28093, 28331, 28570, 28810, 29050, 29291, 29533, 29775, 30018, 30261, 30506, 30751,
    30996, 31243, 31490, 31738, 31986, 32236, 32486, 32736, 32988, 33240, 33494, 33748,
    34002, 34258, 34514, 34772, 35030, 35289, 35548, 35809, 36071, 36333, 36596, 36861,
    37126, 37392, 37659, 37927, 38196, 38465, 38736, 39008, 39281, 39555, 39829, 40105,
    40382, 40660, 40939, 41219, 41500, 41782, 42066, 42350, 42636, 42923, 43210, 43500,
    43790, 44081, 44374, 44668, 44963, 45259, 45557, 45856, 46156, 46457, 46760, 47064,
    47369, 47676, 47984, 48294, 48605, 48917, 49231, 49546, 49863, 50181, 50501, 50822,
    51145, 51469, 51795, 52122, 52451, 52782, 53114, 53448, 53784, 54121, 54460, 54801,
    55144, 55488, 55834, 56182, 56532, 56883, 57237, 57592, 57950, 58309, 58670, 59033,
    59398, 59766, 60135, 60506, 60880, 61255, 61633, 62013, 62395, 62780, 63167, 63556,
    63947, 64341, 64737, 65135, 65536, 65939, 66345, 66754, 67165, 67578, 67994, 68413,
    68835, 69259, 69686, 70116, 70548, 70984, 71422, 71864, 72308, 72755, 73206, 73659,
    74116, 74576, 75039, 75505, 75974, 76447, 76924, 77404, 77887, 78374, 78864, 79358,
    79856, 80357, 80863, 81372, 81885, 82402, 82923, 83448, 83977, 84510, 85047, 85589,
    86135, 86686, 87241, 87801, 88365, 88934, 89508, 90086, 90670, 91258, 91852, 92450,
    93054, 93663, 94277, 94897, 95523, 96154, 96791, 97433, 98082, 98736, 99396, 100063,
    100736, 101415, 102101, 102794, 103493, 104199, 104911, 105631, 106358, 107092, 107834, 108583,
    109340, 110105, 110877, 111658, 112447, 113244, 114050, 114864, 115687, 116519, 117361, 118211,
    119071, 119941, 120820, 121710, 122609, 123519, 124440, 125371, 126314, 127267, 128232, 129209,
    130198, 131198, 132211, 133237, 134276, 135327, 136393, 137471, 138564, 139671, 140793, 141929,
    143081, 144248, 145432, 146631, 147847, 149080, 150330, 151598, 152884, 154189, 155512, 156855,
    158218, 159601, 161005, 162430, 163878, 165347, 166840, 168356, 169896, 171460, 173051, 174667,
    176309, 177979, 179677, 181404, 183161, 184948, 186766, 188616, 190499, 192416, 194368, 196356,
    198380, 200443, 202544, 204686, 206870, 209096, 211366, 213681, 216043, 218454, 220914, 223426,
    225990, 228610, 231286, 234021, 236817, 239675, 242598, 245588, 248648, 251780, 254986, 258270,
    261634, 265082, 268617, 272241, 275959, 279774, 283691, 287713, 291845, 296091, 300457, 304947,
    309568, 314324, 319222, 324269, 329472, 334837, 340374, 346089, 351993, 358095, 364405, 370934,
    377693, 384696, 391956, 399487, 407305, 415428, 423872, 432658, 441808, 451344, 461292, 471678,
    482534, 493892, 505787, 518259, 531352, 545112, 559593, 574854, 590958, 607979, 625997, 645102,
    665398, 686999, 710035, 734656, 761030, 789353, 819850, 852780, 888450, 927215, 969499, 1015802,
    1066730, 1123011, 1185539, 1255414, 1334016, 1423089, 1524877, 1642314, 1779314, 1941210, 2135471, 2372887,
    2669641, 3051162, 3559834, 4271948, 5340086, 7120271, 10680573, 16777216, 16777216,
};
static const int16_t OPX_G1[513] = {
    0, 50, 100, 149, 199, 248, 296, 345, 393, 440, 488, 535,
    582, 629, 675, 721, 767, 813, 858, 903, 948, 993, 1037, 1082,
    1126, 1169, 1213, 1256, 1299, 1342, 1384, 1427, 1469, 1511, 1553, 1594,
    1636, 1677, 1718, 1758, 1799, 1839, 1879, 1919, 1959, 1999, 2038, 2077,
    2116, 2155, 2194, 2232, 2271, 2309, 2347, 2385, 2423, 2460, 2497, 2535,
    2572, 2609, 2645, 2682, 2718, 2755, 2791, 2827, 2863, 2898, 2934, 2969,
    3004, 3040, 3075, 3109, 3144, 3179, 3213, 3248, 3282, 3316, 3350, 3384,
    3418, 3451, 3485, 3518, 3551, 3584, 3617, 3650, 3683, 3716, 3748, 3781,
    3813, 3846, 3878, 3910, 3942, 3974, 4005, 4037, 4068, 4100, 4131, 4163,
    4194, 4225, 4256, 4287, 4317, 4348, 4379, 4409, 4440, 4470, 4500, 4531,
    4561, 4591, 4621, 4651, 4680, 4710, 4740, 4769, 4799, 4828, 4858, 4887,
    4916, 4945, 4974, 5003, 5032, 5061, 5090, 5118, 5147, 5176, 5204, 5232,
    5261, 5289, 5317, 5346, 5374, 5402, 5430, 5458, 5486, 5514, 5541, 5569,
    5597, 5624, 5652, 5680, 5707, 5734, 5762, 5789, 5816, 5844, 5871, 5898,
    5925, 5952, 5979, 6006, 6033, 6060, 6087, 6113, 6140, 6167, 6193, 6220,
    6247, 6273, 6300, 6326, 6352, 6379, 6405, 6431, 6458, 6484, 6510, 6536,
    6563, 6589, 6615, 6641, 6667, 6693, 6719, 6745, 6771, 6796, 6822, 6848,
    6874, 6900, 6925, 6951, 6977, 7003, 7028, 7054, 7079, 7105, 7131, 7156,
    7182, 7207, 7233, 7258, 7284, 7309, 7334, 7360, 7385, 7411, 7436, 7461,
    7487, 7512, 7537, 7562, 7588, 7613, 7638, 7663, 7689, 7714, 7739, 7764,
    7790, 7815, 7840, 7865, 7890, 7915, 7941, 7966, 7991, 8016, 8041, 8066,
    8091, 8117, 8142, 8167, 8192, 8217, 8242, 8267, 8293, 8318, 8343, 8368,
    8393, 8418, 8443, 8469, 8494, 8519, 8544, 8569, 8594, 8620, 8645, 8670,
    8695, 8721, 8746, 8771, 8796, 8822, 8847, 8872, 8897, 8923, 8948, 8973,
    8999, 9024, 9050, 9075, 9100, 9126, 9151, 9177, 9202, 9228, 9253, 9279,
    9305, 9330, 9356, 9381, 9407, 9433, 9459, 9484, 9510, 9536, 9562, 9588,
    9613, 9639, 9665, 9691, 9717, 9743, 9769, 9795, 9821, 9848, 9874, 9900,
    9926, 9953, 9979, 10005, 10032, 10058, 10084, 10111, 10137, 10164, 10191, 10217,
    10244, 10271, 10297, 10324, 10351, 10378, 10405, 10432, 10459, 10486, 10513, 10540,
    10568, 10595, 10622, 10650, 10677, 10704, 10732, 10760, 10787, 10815, 10843, 10870,
    10898, 10926, 10954, 10982, 11010, 11038, 11067, 11095, 11123, 11152, 11180, 11208,
    11237, 11266, 11294, 11323, 11352, 11381, 11410, 11439, 11468, 11497, 11526, 11556,
    11585, 11615, 11644, 11674, 11704, 11733, 11763, 11793, 11823, 11853, 11884, 11914,
    11944, 11975, 12005, 12036, 12067, 12097, 12128, 12159, 12190, 12221, 12253, 12284,
    12316, 12347, 12379, 12410, 12442, 12474, 12506, 12538, 12571, 12603, 12636, 12668,
    12701, 12734, 12767, 12800, 12833, 12866, 12899, 12933, 12966, 13000, 13034, 13068,
    13102, 13136, 13171, 13205, 13240, 13275, 13309, 13344, 13380, 13415, 13450, 13486,
    13521, 13557, 13593, 13629, 13666, 13702, 13739, 13775, 13812, 13849, 13887, 13924,
    13961, 13999, 14037, 14075, 14113, 14152, 14190, 14229, 14268, 14307, 14346, 14385,
    14425, 14465, 14505, 14545, 14585, 14626, 14666, 14707, 14748, 14790, 14831, 14873,
    14915, 14957, 15000, 15042, 15085, 15128, 15171, 15215, 15258, 15302, 15347, 15391,
    15436, 15481, 15526, 15571, 15617, 15663, 15709, 15755, 15802, 15849, 15896, 15944,
    15991, 16039, 16088, 16136, 16185, 16235, 16284, 16334, 16383,
};
#define OPX_FCMAX 1932735283u                        /* 0.45 fs, Q32 */
typedef struct {                 /* an operator's filter for a block (opx_filt_block) */
    uint32_t fc0;                /* the cutoff, Q32 of fs */
    int32_t a1, a2, a3, k;       /* the state-variable filter's (Q16) */
    int32_t g1, kl;              /* the ladder's one-pole (Q14), its feedback (Q15) */
    uint8_t type, fm;
} opx_fc_t;
static opx_fc_t opx_fcb[OPX_NOP];              /* (the audio ISR's) */
static inline int32_t opx_tanq(uint32_t fc, int32_t *g1)
{
    uint32_t i = fc >> 22, fr = (fc >> 6) & 0xFFFFu;
    *g1 = OPX_G1[i] + (int32_t)(((OPX_G1[i + 1u] - OPX_G1[i]) * (int32_t)fr) >> 16);
    return OPX_TAN[i] + (int32_t)(((int64_t)(OPX_TAN[i + 1u] - OPX_TAN[i]) * fr) >> 16);
}
static inline void opx_svf_coef(opx_fc_t *c, uint32_t fc)
{
    int32_t g = opx_tanq(fc, &c->g1);
    uint32_t den = 65536u + (uint32_t)(((int64_t)g * (g + c->k)) >> 16);
    c->a1 = (int32_t)(0xFFFFFFFFu / den);             /* 1 / (1 + g (g + k)), Q16 */
    c->a2 = (int32_t)(((int64_t)g * c->a1) >> 16);
    c->a3 = (int32_t)(((int64_t)g * c->a2) >> 16);
}
/* operator k's filter for this block: its cutoff (the operator's step inc x CUTOFF's factor mp[2]), RESO (mp[3]) */
static void opx_filt_block(opx_fc_t *c, const int32_t *mp, uint32_t inc, uint32_t fm)
{
    uint64_t f = ((uint64_t)inc * (uint32_t)mp[2]) >> 15;
    c->type = (uint8_t)mp[0];
    c->fm = (uint8_t)fm;
    c->fc0 = f > OPX_FCMAX ? OPX_FCMAX : (uint32_t)f;
    c->k = 131072 - (int32_t)((int64_t)mp[3] * 128450 >> 15);   /* 2 .. 0.04 (Q16) */
    c->kl = (int32_t)((int64_t)mp[3] * 127139 >> 15);  /* the ladder: 0 .. 3.88 (Q15) */
    opx_svf_coef(c, c->fc0);
}
/* one sample x through operator k's filter (its state st[5]); m: its input (FILTER FM's cutoff) */
static inline int32_t opx_filt(opx_fc_t *c, int32_t *st, int32_t x, int32_t m)
{
    int32_t y;
    if (c->fm) {                                       /* FILTER FM: the cutoff +-4 octaves with the input */
        int32_t e = clamp(m, -32767, 32767) * 2048;    /* (Q24 octaves: 4 x 2^24 / 32767 ~ 2048) */
        uint64_t f = ((uint64_t)c->fc0 * (uint32_t)fm6_exp2(e - (9 << 24))) >> 15;
        opx_svf_coef(c, f > OPX_FCMAX ? OPX_FCMAX : f < 4096u ? 4096u : (uint32_t)f);
    }
    if (c->type >= 4u && c->type <= 9u) {              /* MG: the ladder */
        /* four trapezoidal one-poles (v = G (x - s), y = v + s, s = y + v; G Q13), the feedback from the last one's
         * output a sample late; the states (st) to +-2^17, the products under 2^31 */
        int32_t u = clamp(x - (int32_t)(((int64_t)c->kl * st[4]) >> 15), -65535, 65535), g = c->g1 >> 1, v, y1, y2, y3, y4;
        v = (g * (u - st[0])) >> 13, y1 = v + st[0], st[0] = clamp(y1 + v, -131071, 131071);
        v = (g * (y1 - st[1])) >> 13, y2 = v + st[1], st[1] = clamp(y2 + v, -131071, 131071);
        v = (g * (y2 - st[2])) >> 13, y3 = v + st[2], st[2] = clamp(y3 + v, -131071, 131071);
        v = (g * (y3 - st[3])) >> 13, y4 = v + st[3], st[3] = clamp(y4 + v, -131071, 131071);
        st[4] = y4;                                      /* (the feedback's, next sample) */
        switch (c->type) {
        case 4: y = y2; break;                                                         /* LPF12 */
        case 5: y = y4; break;                                                         /* LPF24 */
        case 6: y = u - 2 * y1 + y2; break;                                            /* HPF12 */
        case 7: y = u - 4 * y1 + 6 * y2 - 4 * y3 + y4; break;                          /* HPF24 */
        case 8: y = 2 * (y1 - y2); break;                                              /* BPF6 */
        default: y = 4 * (y2 - 2 * y3 + y4);                                           /* BPF12 */
        }
    } else {                                           /* the state-variable filter (and MS-20's) */
        int32_t v3 = x - st[1], v1, v2;
        v1 = (int32_t)(((int64_t)c->a1 * st[0] + (int64_t)c->a2 * v3) >> 16);
        v2 = st[1] + (int32_t)(((int64_t)c->a2 * st[0] + (int64_t)c->a3 * v3) >> 16);
        st[0] = clamp(2 * v1 - st[0], -2097152, 2097152);
        st[1] = clamp(2 * v2 - st[1], -2097152, 2097152);
        if (c->type >= 10u)                            /* MS-20: the resonance limited in the loop */
            st[0] = clamp(st[0], -40000, 40000);
        switch (c->type) {
        case 1: case 11: y = x - (int32_t)(((int64_t)c->k * v1) >> 16) - v2; break;   /* HPF */
        case 2: y = (int32_t)(((int64_t)c->k * v1) >> 16); break;                    /* BPF */
        case 3: y = x - (int32_t)(((int64_t)c->k * v1) >> 16); break;                /* BRF */
        default: y = v2;                                                             /* LPF */
        }
    }
    return clamp(y, -65535, 65535);
}

/* M2b: the operator effects (EFFECT, FX p.34): the input and the oscillator at OSC MIX through one of ten. FREQ follows
 * the operator's pitch (PEAK EQ, PHASER, COMB, DEC). Where the manual gives no figure the choice is noted (an
 * assumption each):
 *   PEAK EQ     a peaking biquad at FREQ, GAIN +-18 dB, Q 1 (assumed)
 *   SHELV EQ    a low shelf at 250 Hz and a high one at 4 kHz (first order; the corners assumed), LOW / HIGH +-18 dB
 *   PHASER      four first-order all-passes at FREQ, FB +-100 % (x0.9), the dry signal added (the notches)
 *   SHORT DELAY the dry signal and a delay of TIME with FB, each at -3 dB. The delay line is the voice's own, 800
 *               samples: TIME stops at 17 ms (the manual's 300 ms do not fit the FM-1's memory), and one operator a
 *               voice has it (the highest-numbered one in SHORT DELAY or COMB: the first to run; another plays dry)
 *   COMB        a comb at FREQ (its period read between samples), FB +-100 % (x0.98); the same line, 57 Hz at the lowest
 *   DIST        the input driven x1 .. x50 into a hard clip, LOW the 250 Hz shelf after
 *   DRIVE       x1 .. x10 into a soft (cubic) clip, LOW
 *   DEC         held at FREQ x the operator's pitch (2.00 .. 256.00), BIT 2 .. 24 (whole bits; 16 and over: as it is)
 *   WAVESHAPER  60 sine shapers of rising strength in place of the 01/W's tables (not published): y = sin(k x pi / 2),
 *               k = 1 + TYPE / 4; DAMP a one-pole low-pass after (100 %: down to ~700 Hz)
 *   PUNCH       a fast against a slow envelope of the input: its rise lifts the level (up to x3 at 100 %), HIGH the
 *               4 kHz shelf after */
typedef struct {                 /* an operator's effect for a block (opx_fx_block) */
    int32_t c[6];                /* its coefficients */
    int16_t *dl;                 /* SHORT DELAY / COMB: the voice's line (0: none, dry) */
    uint32_t dw, fr;             /* the line's write place; DEC's step */
    uint8_t type;
} opx_ec_t;
static opx_ec_t opx_ecb[OPX_NOP];              /* (the audio ISR's) */
/* a one-pole (trapezoidal) low-pass step: state s, coefficient G (Q14) */
static inline int32_t opx_lp1(int32_t *s, int32_t x, int32_t g)
{
    int32_t v = (int32_t)(((int64_t)g * (x - *s)) >> 14), y = v + *s;
    *s = clamp(y + v, -262143, 262143);
    return y;
}
#define OPX_G250 504                                     /* OPX_G1 at 250 Hz, 4 kHz: the shelves' corners (Q14) */
#define OPX_G4K 3809
/* 10^(dB x 0.1 / 20) and its 40th, Q15 (x0.1 dB) */
static int32_t opx_db20(int32_t db10) { return fm6_exp2(clamp(db10, -400, 400) * 278667 - (9 << 24)); }
static int32_t opx_db40(int32_t db10) { return fm6_exp2(clamp(db10, -400, 400) * 139334 - (9 << 24)); }
/* the pitch FREQ semitones (x0.1) from the operator's step inc, at most 0.45 fs */
static uint32_t opx_fx_freq(uint32_t inc, int32_t st10)
{
    uint64_t f = ((uint64_t)inc * (uint32_t)fm6_exp2(clamp(st10, -500, 500) * 139810 - (9 << 24))) >> 15;
    return f > OPX_FCMAX ? OPX_FCMAX : f < 65536u ? 65536u : (uint32_t)f;
}
static void opx_fx_block(opx_ec_t *e, const int32_t *mp, uint32_t inc)
{
    int32_t E = mp[2], F = mp[3], *c = e->c, g1;
    uint32_t f;
    e->type = (uint8_t)mp[0];
    switch (e->type) {
    case 0: {                                          /* PEAK EQ: RBJ peaking, Q 1; normalised by a0, Q14 */
        int32_t sn, cs, A, al, aa, ai, a0;
        f = opx_fx_freq(inc, E);
        sn = sine_i(f), cs = sine_i(f + 0x40000000u);
        A = opx_db40(F);
        al = sn >> 1;                                  /* alpha = sin / (2 Q) */
        aa = (int32_t)(((int64_t)al * A) >> 15);
        ai = (al << 15) / A;
        a0 = 32768 + ai;
        c[0] = ((32768 + aa) << 14) / a0;              /* (b0, b1, b2, a1, a2: Q14; 32-bit divides, each under 2^31) */
        c[1] = ((-2 * cs) << 14) / a0;
        c[2] = ((32768 - aa) << 14) / a0;
        c[3] = c[1];
        c[4] = ((32768 - ai) << 14) / a0;
        break;
    }
    case 1:                                            /* SHELV EQ: the shelves' gains less 1 (Q15) */
        c[0] = opx_db20(E) - 32768, c[1] = opx_db20(F) - 32768;
        break;
    case 2:                                            /* PHASER: the all-pass coefficient (tan - 1) / (tan + 1) = 2 G1 - 1 */
        opx_tanq(opx_fx_freq(inc, E), &g1);
        c[0] = 2 * g1 - 16384;                         /* (Q14) */
        c[1] = F * 29491 / 100;                        /* FB x0.9 (Q15) */
        break;
    case 3:                                            /* SHORT DELAY: the delay (samples), FB */
        c[0] = clamp(E * 441 / 10, 1, (int32_t)OPX_DLY - 2);
        c[1] = F * 32767 / 100;
        break;
    case 4:                                            /* COMB: the period, Q8 samples (fs / f), FB x0.98 */
        f = opx_fx_freq(inc, E);
        c[0] = clamp((int32_t)(0xFFFFFFFFu / (f >> 8)), 256, ((int32_t)OPX_DLY - 2) << 8);
        c[1] = F * 32112 / 100;
        break;
    case 5:                                            /* DIST: the drive (Q8, x1 .. x50), LOW's gain less 1 */
        c[0] = 256 + E * 125;
        c[1] = opx_db20(F) - 32768;
        break;
    case 6:                                            /* DRIVE: x1 .. x10 (Q8), LOW */
        c[0] = 256 + E * 23;
        c[1] = opx_db20(F) - 32768;
        break;
    case 7:                                            /* DEC: the hold step (FREQ x the pitch), the bits kept */
        {                                              /* (FREQ / 100 as Q16: no 64-bit divide) */
            uint64_t q = ((uint64_t)inc * (uint32_t)(E * 655)) >> 16;
            e->fr = q > 0x7FFFFFFFu ? 0x7FFFFFFFu : q ? (uint32_t)q : 1u;
        }
        c[0] = F / 100 >= 16 ? 0 : 16 - F / 100;       /* (bits dropped of 16) */
        break;
    case 8:                                            /* WAVESHAPER: k x 16, DAMP's G (Q14: 16384 none .. ~700 Hz) */
        c[0] = 16 + E * 4;
        c[1] = 16384 - F * (16384 - 1370) / 100;
        break;
    default:                                           /* PUNCH: the lift (Q15 per unit of rise), HIGH */
        c[0] = E * 65534 / 100;
        c[1] = opx_db20(F) - 32768;
    }
}
/* one sample x through operator k's effect (state st[5]) */
static inline int32_t opx_fx(opx_ec_t *e, int32_t *st, int32_t x)
{
    const int32_t *c = e->c;
    int32_t y, lo;
    switch (e->type) {
    case 0:                                            /* PEAK EQ: st x1 x2 y1 y2 */
        y = (int32_t)(((int64_t)c[0] * x + (int64_t)c[1] * st[0] + (int64_t)c[2] * st[1] - (int64_t)c[3] * st[2] -
                       (int64_t)c[4] * st[3]) >> 14);
        y = clamp(y, -262143, 262143);
        st[1] = st[0], st[0] = x, st[3] = st[2], st[2] = y;
        break;
    case 1:                                            /* SHELV EQ: st low-pass 250 Hz, low-pass 4 kHz */
        lo = opx_lp1(&st[0], x, OPX_G250);
        y = x + (int32_t)(((int64_t)c[0] * lo) >> 15);
        y += (int32_t)(((int64_t)c[1] * (x - opx_lp1(&st[1], x, OPX_G4K))) >> 15);
        break;
    case 2: {                                          /* PHASER: st the four all-passes, the last output */
        int32_t u = clamp(x + (int32_t)(((int64_t)c[1] * st[4]) >> 15), -131071, 131071), k;
        for (k = 0; k < 4; k++) {
            int32_t v = ((c[0] * u) >> 14) + st[k];
            st[k] = clamp(u - ((c[0] * v) >> 14), -262143, 262143);
            u = clamp(v, -131071, 131071);
        }
        st[4] = u;
        y = (x + u) >> 1;
        break;
    }
    case 3:                                            /* SHORT DELAY */
    case 4: {                                          /* COMB */
        int32_t d;
        if (!e->dl)
            return x;                                  /* (no line for this operator: dry) */
        if (e->type == 3) {
            d = e->dl[(e->dw + OPX_DLY - (uint32_t)c[0]) % OPX_DLY];
            e->dl[e->dw] = (int16_t)clamp(x + ((d * c[1]) >> 15), -32767, 32767);
            y = ((x + d) * 23170) >> 15;
        } else {
            uint32_t p = (uint32_t)c[0], i0 = (e->dw + OPX_DLY - (p >> 8)) % OPX_DLY, i1 = (i0 + OPX_DLY - 1u) % OPX_DLY;
            d = e->dl[i0] + (((e->dl[i1] - e->dl[i0]) * (int32_t)(p & 255u)) >> 8);
            y = clamp(x + ((d * c[1]) >> 15), -32767, 32767);
            e->dl[e->dw] = (int16_t)y;
        }
        e->dw = e->dw + 1u < OPX_DLY ? e->dw + 1u : 0u;
        break;
    }
    case 5:                                            /* DIST: st the LOW shelf's low-pass */
    case 6: {                                          /* DRIVE */
        int32_t u = (int32_t)(((int64_t)x * c[0]) >> 8);
        if (e->type == 5) {
            y = clamp(u, -32767, 32767);
        } else {                                       /* v - 4/27 v^3 to 1.5 full scale: 1.0 there */
            int32_t v = clamp(u, -49152, 49152), v3 = (int32_t)(((int64_t)((v * (int64_t)v) >> 15) * v) >> 15);
            y = v - v3 * 4 / 27;
        }
        y += (int32_t)(((int64_t)c[1] * opx_lp1(&st[0], y, OPX_G250)) >> 15);
        break;
    }
    case 7: {                                          /* DEC: st the held sample, taken yet, the phase */
        uint32_t p0 = (uint32_t)st[2];
        st[2] = (int32_t)(p0 + e->fr);
        if ((uint32_t)st[2] < p0 || !st[1])            /* (a new period: take the sample) */
            st[0] = x, st[1] = 1;
        y = c[0] ? st[0] & ~((1 << c[0]) - 1) : st[0];
        break;
    }
    case 8: {                                          /* WAVESHAPER: st DAMP's low-pass */
        uint32_t ph = (uint32_t)(clamp(x, -65535, 65535) * c[0]) << 11;   /* x k / 4 of a cycle: sin(k x pi / 2) */
        y = opx_lp1(&st[0], sine_i(ph), c[1]);
        break;
    }
    default: {                                         /* PUNCH: st the fast and slow envelopes, the HIGH shelf */
        int32_t a = x < 0 ? -x : x, rise;
        st[0] += (a - st[0]) >> 4;
        st[1] += (a - st[1]) >> 10;
        rise = st[0] - st[1];
        y = x;
        if (rise > 0 && c[0])                          /* (the lift: rise / (slow + 1) of PUNCH, at most x3) */
            y = (int32_t)(((int64_t)x * (32768 + clamp(rise * (c[0] >> 2) / ((st[1] + 512) >> 2), 0, 65536))) >> 15);
        y += (int32_t)(((int64_t)c[1] * (y - opx_lp1(&st[2], y, OPX_G4K))) >> 15);
    }
    }
    return clamp(y, -65535, 65535);
}

/* M2b: the modes other than FM. Their oscillator is not modulated (no FM, no FB, no WIDTH: those are FM's, MODE p.43):
 * a block of it first (opx_osc_block, opx_obuf), then each sample through the mode with the operator's input m (its
 * modulators' sum, as FM's) and its gain g (opx_mode_y):
 *   RING MOD     the oscillator times the input, DEPTH the balance (100 %: the ring alone; 0 %: the oscillator alone),
 *                SHAPE rectifies the input towards |m| (brighter)
 *   WAVE FOLDER  the input, the oscillator at OSC MIX and BIAS, times GAIN's factor, folded back into full scale
 *                (a triangle fold: every 2 x full scale over it, down again)
 *   BYPASS       the input as it comes (the operator's level and envelope not applied: an assumption, the manual says
 *                only that it helps editing); MUTE silence
 *   FILTER       the input and the oscillator at OSC MIX through the operator filter (opx_filt)
 *   FILTER FM    the oscillator through it, the input moving its cutoff
 *   EFFECT       the input and the oscillator at OSC MIX through the operator effect (opx_fx)
 * Both orders (operator by operator, sample by sample) call opx_mode_y: the same output (opx_test) */
static int32_t opx_obuf[OPX_NOP][CTL];         /* the oscillators of the operators not in FM (the audio ISR's) */
static void __attribute__((noinline)) opx_osc_block(const opx_wsel_t *v, const int16_t *nb, uint32_t ph, uint32_t inc,
                                                    int32_t *restrict os)
{
    uint32_t i;
    const int16_t *t = v->t;
    if (!t)
        for (i = 0; i < CTL; i++)
            os[i] = nb[i];
    else if (v->sq)
        for (i = 0; i < CTL; i++, ph += inc)
            os[i] = opx_tab(t, v->sh, ph) - opx_tab(t, v->sh, ph + 0x80000000u);
    else
        for (i = 0; i < CTL; i++, ph += inc)
            os[i] = opx_tab(t, v->sh, ph) & v->mk;
}
static inline __attribute__((always_inline)) int32_t opx_mode_y(uint32_t md, const int32_t *mp, int32_t os, int32_t m,
                                                                 int32_t g, opx_fc_t *fc, int32_t *st)
{
    int32_t x;
    switch (md) {
    case OPX_M_FILTER:                               /* the input and the oscillator at OSC MIX through the filter */
        x = opx_filt(fc, st, clamp(m, -65535, 65535) + ((os * mp[1]) >> 15), 0);
        break;
    case OPX_M_FILTER_FM:                            /* the oscillator through it, its cutoff moved by the input */
        x = opx_filt(fc, st, os, m);
        break;
    case OPX_M_EFFECT:                               /* the input and the oscillator at OSC MIX through the effect */
        x = opx_fx(&opx_ecb[fc - opx_fcb], st, clamp(m, -65535, 65535) + ((os * mp[1]) >> 15));
        break;
    case OPX_M_RING: {
        int32_t mm = clamp(m, -32767, 32767), ring;
        mm += (((mm < 0 ? -mm : mm) - mm) * mp[1]) >> 15;   /* SHAPE: towards |m| */
        ring = (os * mm) >> 15;
        x = os + (((ring - os) * mp[0]) >> 15);
        break;
    }
    case OPX_M_FOLD: {
        uint32_t w;
        x = clamp(m + ((os * mp[1]) >> 15) + mp[2], -262144, 262144);
        x = (x * (mp[0] >> 4)) >> 8;                   /* GAIN: x1 .. x8 */
        w = (uint32_t)(x + 32768) & 131071u;           /* the fold: a triangle of period 4 x full scale */
        x = clamp((int32_t)(w < 65536u ? w : 131072u - w) - 32768, -32767, 32767);
        break;
    }
    case OPX_M_BYPASS:
        return clamp(m, -32767, 32767);
    case OPX_M_MUTE:
        return 0;
    default:
        x = os;
    }
    return (x * g) >> 15;
}
#define OPX_MODE_LOOP(MD)                                                                                          \
    for (i = 0; i < CTL; i++) {                                                                                      \
        yp = y;                                                                                                      \
        y = opx_mode_y(MD, mp, os[i], mod[i], g, fc, st);                                                            \
        g += dg;                                                                                                     \
        yo[i] = y;                                                                                                   \
    }                                                                                                                \
    break
static void __attribute__((noinline)) opx_op_mode(opx_os_t *restrict o, const int32_t *restrict mod,
                                                  int32_t *restrict yo, const int32_t *restrict os, uint32_t md,
                                                  const int32_t *restrict mp, opx_fc_t *restrict fc, int32_t *restrict st)
{
    uint32_t i;
    int32_t y = o->y, yp = o->yp, g = o->g, dg = o->dg;
    switch (md) {                                      /* (a loop per mode: the switch folds away in each) */
    case OPX_M_RING: OPX_MODE_LOOP(OPX_M_RING);
    case OPX_M_FOLD: OPX_MODE_LOOP(OPX_M_FOLD);
    case OPX_M_BYPASS: OPX_MODE_LOOP(OPX_M_BYPASS);
    case OPX_M_MUTE: OPX_MODE_LOOP(OPX_M_MUTE);
    case OPX_M_FILTER: OPX_MODE_LOOP(OPX_M_FILTER);
    case OPX_M_FILTER_FM: OPX_MODE_LOOP(OPX_M_FILTER_FM);
    default: OPX_MODE_LOOP(OPX_M_EFFECT);
    }
    o->y = y, o->yp = yp, o->g = g;
}
#undef OPX_MODE_LOOP
/* the sample-by-sample order (opx_render, patches with a backward link): every operator each sample */
static inline __attribute__((always_inline)) int32_t opx_loop(const opx_rt_t *restrict r, int32_t *restrict out,
    const uint32_t *restrict ord, uint32_t nact, uint32_t *restrict ph, const uint32_t *restrict inc,
    int32_t *restrict g, const int32_t *restrict dg, int32_t *restrict y, int32_t *restrict yp,
    const int32_t *restrict fbq, uint32_t heard, int32_t a, int32_t da, int32_t dc, const opx_wsel_t *tb,
    const int16_t (*nb)[CTL], const int32_t (*os)[CTL], int32_t (*fs)[5], int waves)
{
    uint32_t i, q;
    for (i = 0; i < CTL; i++) {
        int32_t mix = 0, x;
        for (q = 0; q < nact; q++) {
            uint32_t kk = ord[q], c, nin = r->nin[kk];
            int32_t mod = 0, s;
            uint32_t p;
            for (c = 0; c < nin; c++)
                mod += (r->amt[kk][c] * y[r->src[kk][c]]) >> 15;
            if (waves && r->md[kk]) {                  /* (M2b: a mode other than FM) */
                yp[kk] = y[kk];
                y[kk] = opx_mode_y(r->md[kk], r->mp[kk], os[kk][i], mod, g[kk], &opx_fcb[kk], fs[kk]);
                g[kk] += dg[kk];
                ph[kk] += inc[kk];
                if ((heard >> kk) & 1u)
                    mix += y[kk];
                continue;
            }
            p = ph[kk] + ((uint32_t)mod << 18) + (uint32_t)(((y[kk] + yp[kk]) >> 1) * fbq[kk]);
            s = waves ? opx_wave(r, kk, &tb[kk], p, nb[kk][i]) : sine_i(p);
            yp[kk] = y[kk];
            y[kk] = (s * g[kk]) >> 15;
            g[kk] += dg[kk];
            ph[kk] += inc[kk];
            if ((heard >> kk) & 1u)
                mix += y[kk];
        }
        x = mix - (dc >> 10);
        dc += x;                                     /* a one-pole high-pass, ~7 Hz (as FM6) */
        a += da;
        out[i] += (int32_t)(((int64_t)x * a) >> 15);
    }
    return dc;
}
static int32_t __attribute__((noinline)) opx_loop_sine(const opx_rt_t *restrict r, int32_t *restrict out,
    const uint32_t *restrict ord, uint32_t nact, uint32_t *restrict ph, const uint32_t *restrict inc,
    int32_t *restrict g, const int32_t *restrict dg, int32_t *restrict y, int32_t *restrict yp,
    const int32_t *restrict fbq, uint32_t heard, int32_t a, int32_t da, int32_t dc)
{
    return opx_loop(r, out, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc, 0, 0, 0, 0, 0);
}
static int32_t __attribute__((noinline)) opx_loop_wave(const opx_rt_t *restrict r, int32_t *restrict out,
    const uint32_t *restrict ord, uint32_t nact, uint32_t *restrict ph, const uint32_t *restrict inc,
    int32_t *restrict g, const int32_t *restrict dg, int32_t *restrict y, int32_t *restrict yp,
    const int32_t *restrict fbq, uint32_t heard, int32_t a, int32_t da, int32_t dc, const opx_wsel_t *tb,
    const int16_t (*nb)[CTL], const int32_t (*os)[CTL], int32_t (*fs)[5])
{
    return opx_loop(r, out, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc, tb, nb, os, fs, 1);
}
/* one input of an operator for the block: m = (first ? 0 : m) + amt x ys >> 15, sample by sample */
static void __attribute__((noinline)) opx_op_in(int32_t *restrict m, const int32_t *restrict ys, int32_t amt,
                                                uint32_t first)
{
    uint32_t i;
    if (first)
        for (i = 0; i < CTL; i++)
            m[i] = (amt * ys[i]) >> 15;
    else
        for (i = 0; i < CTL; i++)
            m[i] += (amt * ys[i]) >> 15;
}
/* the heard operators' outputs mixed, high-passed, through the voice's amplitude ramp (the operator order's last step) */
static int32_t __attribute__((noinline)) opx_mix(int32_t *restrict out, const int32_t *const *hp, uint32_t nh,
                                                 int32_t a, int32_t da, int32_t dc)
{
    uint32_t i, h;
    for (i = 0; i < CTL; i++) {
        int32_t mix = 0, x;
        for (h = 0; h < nh; h++)
            mix += hp[h][i];
        x = mix - (dc >> 10);
        dc += x;
        a += da;
        out[i] += (int32_t)(((int64_t)x * a) >> 15);
    }
    return dc;
}

static void opx_render(track_t *t, voice_t *v, int32_t *out, uint32_t len, const vmod_t *m)
{
    opx_note_t *n = opx_note_of(t, v);
    const opx_rt_t *r = &opx_rt[(uint32_t)(t - trk) % NTRK];
    uint32_t inc[OPX_NOP], ph[OPX_NOP], q, nact = 0, ord[OPX_NOP], heard = r->out;
    int32_t g[OPX_NOP], dg[OPX_NOP], y[OPX_NOP], yp[OPX_NOP], fbq[OPX_NOP], lf, k, a, da, dc, vf, mf = 32768;
    if (!n || !n->live || len != CTL)
        return;
    if (!v->gate && n->down) {                      /* note-off: every envelope to its release */
        for (k = 0; k < OPX_NOP; k++)
            if (n->eg[k].stage < OPX_REL)
                n->eg[k].stage = OPX_REL, n->eg[k].from = n->eg[k].lvl, n->eg[k].x = 0;
        n->down = 0;
    }
    lf = fm6_note_logfreq(m->pitch16, m->fine);
    vf = clamp((m->shape - (64 << 8)) >> 2, -4096, 4096);   /* SHP: the feedback, x0.5 .. x1.5 at the ends */
    if (m->cutoff)                                  /* FLT (ENV / LFO -> FLT, the matrix's CUT): MLVL, per voice */
        mf = fm6_exp2(clamp(m->cutoff >> 8, -96, 96) * (1 << 20) - (9 << 24));
    for (k = 0; k < OPX_NOP; k++) {
        int32_t lvl = opx_eg_step(&n->eg[k], r, (uint32_t)k), g2;
        uint32_t f = r->finc[k];
        if (!f)
            f = (uint32_t)(((uint64_t)((uint32_t)fm6_freq(lf + r->lg[k]) << 8) * r->fine[k]) >> 16);
        inc[k] = f;
        g2 = mulq15(r->amp[k], n->vg[k]);
        if (n->kt[k] != 32768)                       /* KTRK: never past full scale */
            g2 = (int32_t)clamp((int32_t)(((int64_t)g2 * n->kt[k]) >> 15), 0, 32767);
        g2 = (int32_t)(((int64_t)g2 * lvl) >> 24);
        if (mf != 32768 && !((heard >> k) & 1u))
            g2 = (int32_t)clamp((int32_t)(((int64_t)g2 * mf) >> 15), 0, 32767);
        g[k] = n->g[k];
        dg[k] = (g2 - g[k]) >> CTL_LOG2;
        n->g[k] = g2;
        ph[k] = n->ph[k];
        y[k] = n->y[k];
        yp[k] = n->yp[k];
        fbq[k] = clamp(r->fbq[k] + (int32_t)(((int64_t)r->fbq[k] * vf) >> 13), 0, 417 * 100);
        n->ph[k] += f << CTL_LOG2;
    }
    /* the operators that sound this block, from 6 down to 1 (silent ones output 0) */
    for (k = OPX_NOP - 1; k >= 0; k--) {
        if (g[k] || n->g[k])
            ord[nact++] = (uint32_t)k;
        else
            y[k] = yp[k] = 0;
    }
    /* the voice's amplitude ramp x VOICE_FS / 2 a heard operator at full (as FM6's carrier) */
    k = t->p[P_VOICE] == V_UNISON ? VOICE_FS / 5 : VOICE_FS / 2;
    a = mulq15(m->amp0, k);
    da = (mulq15(m->amp1, k) - a) >> CTL_LOG2;
    dc = n->dc;
    {   /* the samples: operator by operator, unless an operator takes a lower one's output (sample by sample) */
        uint32_t wv = 0, back = 0, act = 0, c, mdx = 0, dly_op = 6u;
        opx_wsel_t tb[OPX_NOP];
        int16_t nb[OPX_NOP][CTL];
        for (q = 0; q < nact; q++)
            act |= 1u << ord[q];
        for (q = 0; q < nact; q++) {
            uint32_t kk = ord[q];
            wv |= r->wv[kk];
            mdx |= r->md[kk];
            for (c = 0; c < r->nin[kk]; c++)
                back |= r->src[kk][c] < kk && ((act >> r->src[kk][c]) & 1u);
        }
        wv |= mdx;
        if (wv)
            for (q = 0; q < nact; q++) {
                uint32_t kk = ord[q], w = r->wv[kk] & ~OPX_W_NARROW;
                uint32_t fi = r->wr[kk] ? (uint32_t)(((uint64_t)inc[kk] * r->wr[kk]) >> 16) : inc[kk];   /* (WIDTH) */
                opx_table(w, fi, &tb[kk]);
                if (!tb[kk].t)
                    opx_noise_block(w, &n->nz[kk], ph[kk], inc[kk], nb[kk]);
                if (r->md[kk])                         /* (M2b: the oscillator alone, for its mode) */
                    opx_osc_block(&tb[kk], nb[kk], ph[kk], inc[kk], opx_obuf[kk]);
                if (r->md[kk] == OPX_M_FILTER || r->md[kk] == OPX_M_FILTER_FM)
                    opx_filt_block(&opx_fcb[kk], r->mp[kk], inc[kk], r->md[kk] == OPX_M_FILTER_FM);
                if (r->md[kk] == OPX_M_EFFECT) {
                    opx_fx_block(&opx_ecb[kk], r->mp[kk], inc[kk]);
                    opx_ecb[kk].dl = 0;
                    if ((r->mp[kk][0] == 3 || r->mp[kk][0] == 4) && dly_op > 5u) {   /* (the voice's line: the first) */
                        dly_op = kk;
                        opx_ecb[kk].dl = opx_dline(t, v);
                        opx_ecb[kk].dw = n->dw % OPX_DLY;
                    }
                }
            }
        if (back || opx_sample_order) {
            dc = wv ? opx_loop_wave(r, out, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc, tb,
                                    (const int16_t (*)[CTL])nb, (const int32_t (*)[CTL])opx_obuf, n->fs)
                    : opx_loop_sine(r, out, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc);
        } else {
            const int32_t *hp[OPX_NOP];
            uint32_t nh = 0;
            for (q = 0; q < nact; q++) {               /* 6 down to 1: every input is already this block's */
                uint32_t kk = ord[q], nin = r->nin[kk], first = 1;
                const int32_t *mod = opx_zero;
                opx_os_t o;
                for (c = 0; c < nin; c++) {            /* the inputs, summed term by term (as the sample loop) */
                    uint32_t sc = r->src[kk][c];
                    if (!((act >> sc) & 1u))
                        continue;                      /* (a silent operator gives 0) */
                    opx_op_in(opx_mbuf, opx_ybuf[sc], r->amt[kk][c], first);
                    first = 0, mod = opx_mbuf;
                }
                o.ph = ph[kk], o.inc = inc[kk], o.y = y[kk], o.yp = yp[kk], o.g = g[kk], o.dg = dg[kk];
                o.fbq = fbq[kk];
                if (r->md[kk])
                    opx_op_mode(&o, mod, opx_ybuf[kk], opx_obuf[kk], r->md[kk], r->mp[kk], &opx_fcb[kk], n->fs[kk]);
                else if (!r->wv[kk])
                    opx_op_sine(&o, mod, opx_ybuf[kk]);
                else if (!tb[kk].t)
                    opx_op_noise(&o, mod, opx_ybuf[kk], nb[kk]);
                else if (r->wr[kk])
                    opx_op_gen(&o, mod, opx_ybuf[kk], r, kk, &tb[kk]);
                else if (tb[kk].sq)
                    opx_op_sqr(&o, mod, opx_ybuf[kk], tb[kk].t, tb[kk].sh);
                else
                    opx_op_tab(&o, mod, opx_ybuf[kk], tb[kk].t, tb[kk].sh, tb[kk].mk);
                y[kk] = o.y, yp[kk] = o.yp;            /* (ph, g: not read after; n->ph / n->g are set above) */
                if ((heard >> kk) & 1u)
                    hp[nh++] = opx_ybuf[kk];
            }
            dc = opx_mix(out, hp, nh, a, da, dc);
        }
    }
    n->dc = dc;
    for (k = 0; k < OPX_NOP; k++) {
        if (r->md[k] == OPX_M_EFFECT && opx_ecb[k].dl && opx_ecb[k].dl == opx_dline(t, v))   /* (the line's next place) */
            n->dw = (uint16_t)opx_ecb[k].dw;
        n->y[k] = y[k];
        n->yp[k] = yp[k];
    }
}

/* engine 14 (OPX fork, ENGI_ROW14): reserved, as DIGITAL's 1 is: never selectable (eng_ok), no presets, silent. Its
 * favourites row (favorites.factory[14]) is the MIDI LEARN map, which is why OPX is 15. */
static void row14_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void row14_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_ROW14 = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = row14_note_on,
    .render = row14_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};

/* ------------------------------------------------------------ the engine --- */
static const char *const N_OPX_ALG[] = {"PAT", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26",
                                        "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
                                        "40", "USR", 0};
static const char *const N_OPX_SLOT[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "OWN", 0};
_Static_assert(NELEM(N_OPX_ALG) == OPX_NALG + 3u, "an ALG name per value");
_Static_assert(NELEM(N_OPX_SLOT) == NELEM(OPX_FACTORY) + 2u, "a SLOT name per factory patch, then OWN");

/* {ALG, ATK, DEC, MLVL, FB, MRAT, DTUN, SLOT}: each factory patch as it is */
static const preset_t OPX_PRESETS[] = {
    {"E.PIANO", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 40, 20, 35), PAT(6)},
    {"BELL", {0, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 127, 0}, 0, 0, FX(0, 10, 30, 70), PAT(7)},
    {"FM BASS", {0, 0, 0, 0, 0, 0, 0, 2}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"BRASS", {0, 0, 0, 0, 0, 0, 0, 3}, {0, 0, 127, 0}, 0, 0, FX(0, 25, 20, 40), PAT(4)},
    {"SAW LEAD", {0, 0, 0, 0, 0, 0, 0, 4}, {0, 0, 127, 0}, 0, 1, FX(10, 30, 30, 30), PAT(4)},
    {"LOOP PAD", {0, 0, 0, 0, 0, 0, 0, 5}, {0, 0, 127, 0}, 0, 0, FX(0, 60, 30, 70), PAT(5)},
    {"X-MOD", {0, 0, 0, 0, 0, 0, 0, 6}, {0, 0, 127, 0}, 0, 0, FX(0, 20, 30, 40), PAT(3)},
};

static const engine_t ENG_OPX = {
    .name = "OPX",
    .page_title = {"ALGO", "MOD"},
    .edit = {                                        /* (E3..E6: Felucca's macros, off: OPX_FELUCCA_MACROS) */
        {"ALG", F_INT, 0, OPX_NALG + 1, 0, N_OPX_ALG, 0},
        {"ATK", F_BIPCT, -64, 63, 0, 0, 0},
        {"DEC", F_BIPCT, -64, 63, 0, 0, 0},
        {"MLVL", F_BIPCT, -64, 63, 0, 0, 0},
        {"FB", F_BIPCT, -64, 63, 0, 0, 0},
        {"MRAT", F_INT, -16, 16, 0, 0, 0},
        {"DTUN", F_PCT, 0, 127, 0, 0, 0},
        {"SLOT", F_INT, 0, (int16_t)NELEM(OPX_FACTORY), 0, N_OPX_SLOT, 0},
    },
    .presets = OPX_PRESETS,
    .npresets = NELEM(OPX_PRESETS),
    .note_on = opx_note_on,
    .render = opx_render,
    .knob = {P_E0, P_E1, P_E2, P_E7},                /* (HOME: ALG ATK DEC SLOT) */
    .poly = OPX_POLY,
    .block = opx_block,
    .ownenv = 1,
    .done = opx_done,
    .mem = (sizeof(opx_note_t) + OPX_DLY * sizeof(int16_t)) * OPX_POLY,   /* (the notes, the delay lines) */
};
