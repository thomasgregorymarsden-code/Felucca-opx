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
 * The device's eight EDIT values are macros over the patch, as FM6's:
 *   ALG   PAT = the patch's algorithm, 1..40 another one, USR the patch's user matrix
 *   ATK   all operators' attack times, longer (+) or shorter (-) (the opsix's HOME ATK)
 *   DEC   all operators' decay and release times (HOME DECAY)
 *   MLVL  the level of every operator that is not heard (-24 .. +24 dB): the brightness
 *   FB    added to every operator's feedback and to ALG FB
 *   MRAT  added to the coarse ratio of the operators that are not heard
 *   DTUN  spreads the heard operators apart (up to +-25 cents between the outer ones)
 *   SLOT  F1..F7 load a factory patch into the track; OWN the track's own (edited on the operator pages, ui_fm6op.c,
 *         or loaded with a project, opx_store.c). The patch is opx_patch.c's packed record per track
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done). The track's FLT moves MLVL,
 * SHP the feedback, PIT the pitch.
 *
 * State: per voice an opx_note_t in the part's engine memory (engines.c eng_mem), per part the patch through the
 * macros (opx_rt, rebuilt once a block in opx_block). */
#define OPX_POLY 6               /* engine_t.poly: as FM6, until the target's cost is measured */
#define OPX_NOP 6
#define OPX_NALG 40              /* preset algorithms; 40 = USER */
#define OPX_USER 40u
/* M2: the waveforms (opx_table, below) */
enum { OPX_W_SIN, OPX_W_SIN12, OPX_W_SIN8, OPX_W_TRI, OPX_W_SAW, OPX_W_SAWHD, OPX_W_SQR, OPX_W_SQRHD, OPX_W_ASAW3,
       OPX_W_ASQR3, OPX_W_ATRI3, OPX_W_A12345, OPX_W_A12, OPX_W_A13, OPX_W_A14, OPX_W_A15, OPX_W_A16, OPX_W_A17,
       OPX_W_A18, OPX_W_NOISE_SH, OPX_W_WHITE, OPX_W_PINK, OPX_W_BLUE, OPX_NWAVE };
#define OPX_W_NARROW 0x80u
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

static void opx_build(const track_t *t, opx_rt_t *r)
{
    const opx_patch_t *pt = opx_own[(uint32_t)(t - trk) % NTRK];
    int32_t alg = t->p[P_E0], datk = clamp(t->p[P_E1], -64, 63) * 4, ddec = clamp(t->p[P_E2], -64, 63) * 4;
    int32_t m = clamp(t->p[P_E3], -64, 63), dfb = clamp(t->p[P_E4], -64, 63) * 100 / 64, mrat = t->p[P_E5];
    int32_t spread = clamp(t->p[P_E6], 0, 127), mlvl, k, j, ncar = 0, ci = 0;
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
        if (o->width && o->width < 100u && r->wv[k] < OPX_W_NOISE_SH) {   /* (the noises have no width) */
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
 *   SIN 12BIT / 8BIT  the sine table at 12 / 8 bits
 *   TRI               a triangle table
 *   ADTV              sums of sines: SAW3 1 + 1/2 + 1/3, SQR3 1 + 1/3 (3rd) + 1/5 (5th), TRI3 1 - 1/9 + 1/25,
 *                     12345 the first five harmonics at one level, 1+n the fundamental and harmonic n at one level;
 *                     each scaled to a full-scale peak
 *   SAW, SQR          band-limited: a table per power of two of harmonics (1, 2, 4 .. 256), the one for the operator's
 *                     pitch picked each block, its harmonics up to 10 kHz; the HD ones up to 20 kHz: "richer high-end
 *                     overtones" (p.84; the CPU cost is the same here). The harmonics at 1/n; SAW at 83 %, SQR at 77 %
 *                     of full scale: the highest peak of any table (SAW's Gibbs overshoot, 1.177; SQR's lone
 *                     fundamental, 4 / pi) stays in range
 *   NOISE             S/H: a random level held for each cycle of the operator (its pitch, unmodulated: the "musical"
 *                     noise); WHITE flat; PINK -3 dB / octave (Paul Kellet's three-pole filter on white); BLUE pink's
 *                     difference (+9 dB over 4 octaves). Made a block at a time; they ignore FM, FB and WIDTH
 *   WIDTH             1..99 %: the whole wave in that part of the cycle, silence in the rest (p.43's pictures: OFF,
 *                     50 %, 25 %). An assumption from the pictures; WIDTH OFF is the full cycle
 * Every periodic wave is one table read a sample, as the sine (the device's benchmark, MENU > SYSTEM > INFO: one big
 * per-sample switch over the waves cost as much again as the sine itself). The tables are built once, in the main loop
 * (opx_wt_build, from opx_poll), in RAM; until then every wave plays the sine */
#define OPX_MIP 9                                      /* SAW / SQR: 1, 2, 4 .. 256 harmonics */
enum { WT_SIN12, WT_SIN8, WT_TRI, WT_ADTV, WT_SAW = WT_ADTV + (OPX_W_A18 - OPX_W_ASAW3 + 1), WT_SQR = WT_SAW + OPX_MIP,
       WT_COUNT = WT_SQR + OPX_MIP };
static int16_t opx_wt[WT_COUNT][1025] __attribute__((section(".pool")));   /* 1024 points and a guard, as SINE */
static volatile uint8_t opx_wt_ok;
/* the ADTV 1+n waves' gain to a full-scale peak, n = 2..8 (the peak of sin x + sin nx: 1.760 1.540 1.928 2 1.967 1.903
 * 1.981) */
static const uint16_t OPX_A1N_GAIN[7] = {18615, 21282, 16993, 16383, 16659, 17219, 16539};
static int32_t opx_sq15(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
/* point i of an ADTV wave (w: OPX_W_ASAW3 ..) */
static int32_t opx_adtv(uint32_t w, uint32_t i)
{
    uint32_t p = i << 22;
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
/* the tables (main loop, once: ~1 M table reads) */
static void opx_wt_build(void)
{
    uint32_t i, w, set, l, h;
    if (opx_wt_ok)
        return;
    for (i = 0; i < 1024u; i++) {
        opx_wt[WT_SIN12][i] = (int16_t)(SINE[i] & ~15);
        opx_wt[WT_SIN8][i] = (int16_t)(SINE[i] & ~255);
        opx_wt[WT_TRI][i] = (int16_t)opx_sq15(osc_tri((i << 22) + 0x40000000u));   /* (from 0 rising, as the sine) */
        for (w = OPX_W_ASAW3; w <= OPX_W_A18; w++)
            opx_wt[WT_ADTV + w - OPX_W_ASAW3][i] = (int16_t)opx_adtv(w, i);
    }
    /* SAW: 0.83 x (2 / pi) sum (-1)^(n+1) sin(nx) / n (rising through 0 at phase 0, as the sine); SQR: 0.77 x (4 / pi)
     * sum over odd n of sin(nx) / n. Level l: harmonics 1 .. 2^l (256 at most: 4 points a cycle of the table) */
    for (set = 0; set < 2u; set++) {
        int32_t amp = set ? (int32_t)(0.77 * 4.0 * 32767.0 / 3.14159265358979 * 256.0)   /* x 256 (constants: */
                          : (int32_t)(0.83 * 2.0 * 32767.0 / 3.14159265358979 * 256.0);  /* no float on the device) */
        for (l = 0; l < OPX_MIP; l++)
            for (i = 0; i < 1024u; i++) {
                int32_t acc = 0;
                for (h = 1; h <= 1u << l; h += set ? 2u : 1u) {
                    int32_t c = (int32_t)((uint32_t)amp / h);   /* (an unsigned divide, as the firmware links) */
                    acc += (int32_t)(((int64_t)sine_i((i << 22) * h) * (set || (h & 1u) ? c : -c)) >> 15);
                }
                opx_wt[(set ? WT_SQR : WT_SAW) + l][i] = (int16_t)opx_sq15(acc >> 8);
            }
    }
    for (i = 0; i < WT_COUNT; i++)
        opx_wt[i][1024] = opx_wt[i][0];                /* (the guard: the reads need no mask) */
    opx_wt_ok = 1;
}
/* wave w's table for a phase step of inc (SAW / SQR: the level its pitch allows); the noises: 0 */
static const int16_t *opx_table(uint32_t w, uint32_t inc)
{
    uint32_t hmax, l;
    if (!opx_wt_ok || w == OPX_W_SIN)
        return SINE;
    if (w >= OPX_W_NOISE_SH)
        return 0;
    if (w <= OPX_W_TRI)
        return opx_wt[WT_SIN12 + w - OPX_W_SIN12];
    if (w >= OPX_W_ASAW3)
        return opx_wt[WT_ADTV + w - OPX_W_ASAW3];
    /* SAW SAW HD SQR SQR HD: harmonics up to 10 / 20 kHz: hmax = limit / f = limit x 2^32 / (44100 x inc) */
    hmax = (w == OPX_W_SAWHD || w == OPX_W_SQRHD ? (uint32_t)(20000ull * 4294967296ull / 44100u)
                                                 : (uint32_t)(10000ull * 4294967296ull / 44100u)) / (inc ? inc : 1u);
    for (l = 0; l + 1u < OPX_MIP && (2u << l) <= hmax; l++)
        ;
    return opx_wt[(w == OPX_W_SAW || w == OPX_W_SAWHD ? WT_SAW : WT_SQR) + l];
}
/* table t at phase p, linearly between its points (as sine_i) */
static inline int32_t opx_tab(const int16_t *t, uint32_t p)
{
    uint32_t i = p >> 22;
    int32_t a = t[i], b = t[i + 1u];
    return a + (((b - a) * (int32_t)((p >> 7) & 0x7FFFu)) >> 15);
}
typedef struct {                 /* an operator's noise state (a voice's) */
    uint32_t rng, lastp;         /* the generator; S/H: the last phase (a wrap = a new level) */
    int32_t hold;                /* S/H: the level held */
    int32_t pk[3], prev;         /* PINK / BLUE: the filter's poles (Q15 x ~8), the last raw pink */
} opx_noise_t;
static int32_t opx_white(opx_noise_t *z)
{
    z->rng = z->rng * 1664525u + 1013904223u;
    return (int32_t)z->rng >> 16;                        /* -32768..32767 */
}
static int32_t opx_pink_raw(opx_noise_t *z)
{
    int32_t w = opx_white(z);
    z->pk[0] = (int32_t)(((int64_t)z->pk[0] * 65382 + (int64_t)w * 6491) >> 16);    /* 0.99765, 0.0990460 */
    z->pk[1] = (int32_t)(((int64_t)z->pk[1] * 63111 + (int64_t)w * 19433) >> 16);   /* 0.96300, 0.2965164 */
    z->pk[2] = (int32_t)(((int64_t)z->pk[2] * 37355 + (int64_t)w * 68989) >> 16);   /* 0.57000, 1.0526913 */
    return z->pk[0] + z->pk[1] + z->pk[2] + ((w * 12111) >> 16);                    /* + 0.1848 white */
}
/* a block of noise w (OPX_W_NOISE_SH ..) into b; S/H at the operator's own phase ph, step inc */
static void opx_noise_block(uint32_t w, opx_noise_t *z, uint32_t ph, uint32_t inc, int16_t *b)
{
    uint32_t i;
    for (i = 0; i < CTL; i++, ph += inc) {
        int32_t v;
        switch (w) {
        case OPX_W_NOISE_SH:
            if (ph < z->lastp)                           /* a new cycle (the phase wrapped) */
                z->hold = opx_white(z);
            z->lastp = ph;
            v = z->hold;
            break;
        case OPX_W_WHITE:
            v = opx_white(z);
            break;
        case OPX_W_PINK:
            v = (int32_t)(((int64_t)opx_pink_raw(z) * 13107) >> 16);   /* (/ 5) */
            break;
        default: {                                       /* BLUE */
            int32_t x = opx_pink_raw(z);
            v = (x - z->prev) >> 1;
            z->prev = x;
        }
        }
        b[i] = (int16_t)opx_sq15(v);
    }
}
/* operator k's wave at phase p: its table (with its WIDTH), or this block's noise sample nb */
static inline int32_t opx_wave(const opx_rt_t *r, uint32_t k, const int16_t *t, uint32_t p, int32_t nb)
{
    if (!t)
        return nb;
    if (r->wr[k]) {                                      /* WIDTH: the cycle squeezed into its first WIDTH % */
        if (p >= r->wlim[k])
            return 0;
        p = (uint32_t)(((uint64_t)p * r->wr[k]) >> 16);
    }
    return opx_tab(t, p);
}

/* ------------------------------------------------------------- the voice --- */
enum { OPX_ATK, OPX_DEC, OPX_SUS, OPX_REL, OPX_DONE };
typedef struct {
    int32_t x, from, lvl;        /* the stage's progress, where it started, the level now (Q24) */
    uint8_t stage;
} opx_eg_t;
typedef struct {                 /* a sounding note: 232 bytes */
    uint32_t ph[OPX_NOP];
    int32_t y[OPX_NOP], yp[OPX_NOP];   /* the last two outputs (Q15) */
    int32_t g[OPX_NOP];          /* the gain at the end of the last block (Q15) */
    int32_t vg[OPX_NOP];         /* velocity's factor on the level (Q15) */
    opx_eg_t eg[OPX_NOP];
    opx_noise_t nz[OPX_NOP];     /* M2: the noise waveforms' state */
    int32_t dc;
    uint8_t live, down, rsv[2];
} opx_note_t;
ENG_MEM_FITS("OPX", sizeof(opx_note_t) * OPX_POLY);

static opx_note_t *opx_note_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < OPX_POLY ? (opx_note_t *)ENG_MEM(t - trk) + i : 0;
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
            n->nz[k].rng = 0x9E3779B9u * (uint32_t)(k + 1) + (uint32_t)v->note * 2654435761u;
        }
        e->from = e->lvl;                            /* a retrigger rises from where it is */
        e->x = 0;
        e->stage = OPX_ATK;
        /* LMOD VEL: at 100 % velocity 1 is silent, 127 full; linear in between */
        n->vg[k] = 32767 - (r->vel[k] * (127 - clamp(vel, 1, 127)) * 258 / 100);
    }
    if (fresh)
        n->dc = 0;
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
        if (!((r->out >> k) & 1u) || e->stage == OPX_DONE || !r->amp[k])
            continue;
        if (e->stage == OPX_SUS && !r->sus[k] && n->g[k] == 0)
            continue;
        return 0;
    }
    n->live = 0;
    return 1;
}

/* a block's samples (opx_render): the operators that sound, 6 down to 1, each its inputs, its feedback, its wave;
 * the heard ones mixed, high-passed, through the voice's amplitude ramp. Two copies: M1's sine loop as it was, inlined
 * in opx_render (every patch without a waveform), and opx_loop_wave (M2): the same with a table (or a block of noise)
 * per operator; a function of its own, its own line in tests/target_budget.txt. Returns the high-pass's state */
static inline __attribute__((always_inline)) int32_t opx_loop(const opx_rt_t *restrict r, int32_t *restrict out,
    const uint32_t *restrict ord, uint32_t nact, uint32_t *restrict ph, const uint32_t *restrict inc,
    int32_t *restrict g, const int32_t *restrict dg, int32_t *restrict y, int32_t *restrict yp,
    const int32_t *restrict fbq, uint32_t heard, int32_t a, int32_t da, int32_t dc, const int16_t *const *tb,
    const int16_t (*nb)[CTL], int waves)
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
            p = ph[kk] + ((uint32_t)mod << 18) + (uint32_t)(((y[kk] + yp[kk]) >> 1) * fbq[kk]);
            s = waves ? opx_wave(r, kk, tb[kk], p, nb[kk][i]) : sine_i(p);
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
static int32_t __attribute__((noinline)) opx_loop_wave(const opx_rt_t *restrict r, int32_t *restrict out,
    const uint32_t *restrict ord, uint32_t nact, uint32_t *restrict ph, const uint32_t *restrict inc,
    int32_t *restrict g, const int32_t *restrict dg, int32_t *restrict y, int32_t *restrict yp,
    const int32_t *restrict fbq, uint32_t heard, int32_t a, int32_t da, int32_t dc, const int16_t *const *tb,
    const int16_t (*nb)[CTL])
{
    return opx_loop(r, out, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc, tb, nb, 1);
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
        g2 = (int32_t)(((int64_t)mulq15(r->amp[k], n->vg[k]) * lvl) >> 24);
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
    {   /* the samples: M1's sine loop, or (M2) the same with a table or a block of noise per operator */
        uint32_t wv = 0;
        for (q = 0; q < nact; q++)
            wv |= r->wv[ord[q]];
        if (wv) {
            const int16_t *tb[OPX_NOP];
            int16_t nb[OPX_NOP][CTL];
            for (q = 0; q < nact; q++) {
                uint32_t kk = ord[q], w = r->wv[kk] & ~OPX_W_NARROW;
                uint32_t fi = r->wr[kk] ? (uint32_t)(((uint64_t)inc[kk] * r->wr[kk]) >> 16) : inc[kk];   /* (WIDTH) */
                tb[kk] = opx_table(w, fi);
                if (!tb[kk])
                    opx_noise_block(w, &n->nz[kk], ph[kk], inc[kk], nb[kk]);
            }
            dc = opx_loop_wave(r, out, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc, tb,
                               (const int16_t (*)[CTL])nb);
        } else {                                     /* (inlined: M1's loop, here as it was) */
            dc = opx_loop(r, out, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc, 0, 0, 0);
        }
    }
    n->dc = dc;
    for (k = 0; k < OPX_NOP; k++) {
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
    .edit = {
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
    .knob = {P_E3, P_E4, P_E2, P_E7},
    .poly = OPX_POLY,
    .block = opx_block,
    .ownenv = 1,
    .done = opx_done,
    .mem = sizeof(opx_note_t) * OPX_POLY,
};
