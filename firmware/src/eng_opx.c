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
 *   SLOT  F1..F8 (Solo: F126) load a factory patch into the track; OWN the track's own (edited on the operator pages, ui_fm6op.c,
 *         or loaded with a project, opx_store.c); INIT the INIT patch (the opsix's INITIALIZE). The patch is
 *         opx_patch.c's packed record per track
 * Felucca's FM6-style macros (E3..E6: MLVL, FB, MRAT, DTUN) are not the opsix's: off (OPX_FELUCCA_MACROS 0; the track
 * keeps their values, nothing reads them, no page shows them: ui.c page_visible). So are Felucca's general modulation
 * routes into the engine: FLT (ENV / LFO -> FLT, the matrix's CUT: the modulators' level) and SHP (the feedback).
 * PIT stays: it is the note's pitch itself (bend, glide, vibrato), which the opsix has too
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done). The track's PIT moves the
 * pitch (FLT and SHP: off, above).
 *
 * State: per voice an opx_note_t in the part's engine memory (engines.c eng_mem), per part the patch through the
 * macros (opx_rt, rebuilt once a block in opx_block). */
#ifndef OPX_FELUCCA_MACROS
#define OPX_FELUCCA_MACROS 0     /* MLVL FB MRAT DTUN (E3..E6): Felucca's, not the opsix's; off */
#endif
#if FELUCCA_OPX_ONLY
#define OPX_DLY 2816u            /* (OPX Solo, 0.2.0: its one part's region is ENG_MEM_PART 37992: a voice's line 63.9 ms
                                  * at the full rate; SHORT DELAY's longer TIMEs run it slower, to 300 ms) */
#else
#define OPX_DLY 728u             /* M2b (768 since M4, 736 M5a: EG2 EG3 LFO2 LFO3 took 72 bytes a note; 728 M5b: the filter 20): a voice's delay line (int16, SHORT DELAY / COMB), after the six notes in its part: the region full to the byte (692-byte notes) */
#endif
#define OPX_POLY 6               /* engine_t.poly: as FM6, until the target's cost is measured */
#define OPX_NOP 6
#define OPX_NALG 40              /* preset algorithms; 40 = USER */
#define OPX_USER 40u
/* M2: the waveforms (opx_table, below) */
enum { OPX_W_SIN, OPX_W_SIN12, OPX_W_SIN8, OPX_W_TRI, OPX_W_SAW, OPX_W_SAWHD, OPX_W_SQR, OPX_W_SQRHD, OPX_W_ASAW3,
       OPX_W_ASQR3, OPX_W_ATRI3, OPX_W_A12345, OPX_W_A12, OPX_W_A13, OPX_W_A14, OPX_W_A15, OPX_W_A16, OPX_W_A17,
       OPX_W_A18, OPX_W_NOISE_SH, OPX_W_WHITE, OPX_W_PINK, OPX_W_BLUE, OPX_NWAVE };
#define OPX_W_NARROW 0x80u
/* M4: the LFO Wave List (p.56) in the order an opsix's WAVE knob goes through (seen on one: the manual's columns read
 * down in turn); GTR and the "+" ones 0 .. +1, the others -1 .. +1 */
enum { OPX_L_TRI, OPX_L_SAWD, OPX_L_SAWU, OPX_L_SQR, OPX_L_SIN, OPX_L_SH, OPX_L_GTR, OPX_L_XTRI, OPX_L_XSAWD,
       OPX_L_XSAWU, OPX_L_TRI4, OPX_L_TRI6, OPX_L_SAW4D, OPX_L_SAW6D, OPX_L_SRT, OPX_L_SRLT, OPX_L_RL, OPX_L_RT,
       OPX_L_RLT, OPX_L_TRIP, OPX_L_SAWDP, OPX_L_SAWUP, OPX_L_SQRP, OPX_NLFOW };
#define OPX_L_RANDOM(w) ((w) == OPX_L_SH || ((w) >= OPX_L_SRT && (w) <= OPX_L_RLT))   /* S/H, S.RND, RND */
#define OPX_L_HELD(w) ((w) == OPX_L_SH || (w) == OPX_L_SRT || (w) == OPX_L_SRLT)     /* .. held a segment */
#define OPX_L_UNI(w) ((w) == OPX_L_GTR || (w) >= OPX_L_TRIP)                          /* 0 .. +1 */
/* M4: CTRL, the Control Source List (p.85) after OFF: the source's value multiplies the INT it controls ("The value
 * selected here is multiplied with ... Int", p.51). OFF (index 0, not in the manual's list) leaves the INT as it is */
enum { OPX_C_OFF, OPX_C_MW, OPX_C_PW, OPX_C_PWP, OPX_C_PWM, OPX_C_PEDAL, OPX_C_NOTE, OPX_C_VEL, OPX_C_XVEL, OPX_C_RVEL,
       OPX_C_AT, OPX_C_EG1, OPX_C_EG2, OPX_C_EG3, OPX_C_LFO1, OPX_C_LFO2, OPX_C_LFO3, OPX_NCTRL };
/* M5: V.PATCH sources (the Virtual Patch Source List, p.88, in its order): OFF; CTRL MOD WHEEL .. PEDAL and KBD NOTE ..
 * REL VEL (as OPX_C_MW .. OPX_C_RVEL, the same numbers); EG/LFO EG1 .. LFO3; CC+ 1..119; CC+/- 1..119; MIDI AFTER
 * TOUCH, POLY AFTER */
enum { OPX_VS_EG1 = 10, OPX_VS_LFO3 = 15, OPX_VS_CCU = 16, OPX_VS_CCB = OPX_VS_CCU + 119, OPX_VS_AT = OPX_VS_CCB + 119,
       OPX_VS_PAT, OPX_NVS };
/* M5: V.PATCH destinations (the Destination List, p.89-90, in its order), scope and destination as one index: OFF;
 * PROG's 16; OP1..6's 39 each; EG1..3's 4 each; LFO1..3's 4 each; FILTER's 2; FX1..3's 4 each; ARP's 3; SEQ's 1 */
enum { OPX_VD_PROG = 1, OPX_VD_OP = 17, OPX_VD_EG = OPX_VD_OP + 6 * 39, OPX_VD_LFO = OPX_VD_EG + 12,
       OPX_VD_FLT = OPX_VD_LFO + 12, OPX_VD_FX = OPX_VD_FLT + 2, OPX_VD_ARP = OPX_VD_FX + 12, OPX_VD_SEQ = OPX_VD_ARP + 3,
       OPX_NVD };
enum { OPX_DP_PITCH, OPX_DP_OCT, OPX_DP_TRANS, OPX_DP_LEVEL, OPX_DP_PAN, OPX_DP_ATK, OPX_DP_DEC, OPX_DP_REL, OPX_DP_SUS };
enum { OPX_DO_PITCH, OPX_DO_COARSE, OPX_DO_OCT, OPX_DO_TRANS, OPX_DO_LEVEL, OPX_DO_ATK, OPX_DO_DEC, OPX_DO_REL,
       OPX_DO_SUS, OPX_DO_FB };
enum { OPX_DL_WAVE, OPX_DL_SPEED, OPX_DL_PHASE, OPX_DL_FADE };
/* the destinations OPX plays (M5a): PROG PITCH OCT TRANS LEVEL ATK DECAY REL SUS; each operator's PITCH COARSE OCT TRANS
 * LEVEL ATK DECAY REL SUS FM FB; EG1..3's four; LFO1..3's SPEED PHASE FADE; (M5b) FILTER CUTOFF RESO. The others (the
 * modes' values, the effects, ARP, SEQ, PAN TEMPO ALGO GLIDE UNISON) are kept in the patch and do nothing yet */
static int opx_vd_on(uint32_t d)
{
    if (d >= OPX_VD_PROG && d < OPX_VD_OP)
        return d - OPX_VD_PROG <= OPX_DP_SUS && d - OPX_VD_PROG != OPX_DP_PAN;
    if (d >= OPX_VD_OP && d < OPX_VD_EG)
        return (d - OPX_VD_OP) % 39u <= OPX_DO_FB;
    if (d >= OPX_VD_EG && d < OPX_VD_LFO)
        return 1;
    if (d >= OPX_VD_LFO && d < OPX_VD_FLT)
        return (d - OPX_VD_LFO) % 4u != OPX_DL_WAVE;
    return d >= OPX_VD_FLT && d < OPX_VD_FX;          /* (M5b: the filter's CUTOFF, RESO) */
}
static uint8_t opx_cc[NTRK][120];    /* M5: each part's CC 1..119 as last received + 1 (0: none yet; midi_control.c) */
static int16_t opx_ctl_bend[NTRK];   /* M4: each part's pitch wheel (MIDI, -8192..8191) and damper pedal (midi_control.c) */
static uint8_t opx_ctl_pedal[NTRK];
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
    int16_t pm_eg1, pm_lfo1;     /* M4: P MOD (p.51) EG1 / LFO1 INT, x0.01 semitone (-14400..14400) */
    uint8_t pm_vel, pm_dest;     /* M4: P MOD VEL 0..100 %, its DEST (0 FINE, 1 COARSE; RATIO only) */
    int8_t lm_lfo1;              /* M4: LMOD (p.54) LFO1 INT, -100..100 % */
    uint8_t pm_eg1_ctrl, pm_lfo1_ctrl, lm_lfo1_ctrl;   /* M4: their CTRL (OPX_C_*: the Control Source List, p.85) */
} opx_op_t;
typedef struct {                 /* M4: an EG (MOD p.55): EG1 the operators' pitch */
    uint16_t atk, dec, rel;      /* TIME10 */
    uint8_t sus, curve;
} opx_egp_t;
typedef struct {                 /* M4: an LFO (p.55-57): LFO1 the operators' pitch and level, PROG PITCH */
    uint8_t wave;                /* OPX_L_* (the LFO Wave List) */
    uint16_t speed;              /* HZ10: 0.01 Hz x 10000^(c / 1023) (TEMPO off) */
    uint8_t sync, tempo, ksync;  /* TEMPO on: a note value (OPX_SYNC); KSYNC 0 OFF, 1 COMMON, 2 VOICE */
    int16_t phase;               /* -180..180 degrees, -181 RANDOM */
    uint16_t fade;               /* ms, 0..9900 (no effect with KSYNC OFF) */
} opx_lfop_t;
/* M5: a virtual patch (p.71): SRC (OPX_VS_*: the Virtual Patch Source List, p.88, scope and source as one index), INT
 * (x0.01: its meaning by destination, opx_vp_apply), CTRL (OPX_C_*), DEST (OPX_VD_*: the Destination List, p.89-90) */
#define OPX_NVP 12
typedef struct { uint8_t src, ctrl; int16_t amt; uint16_t dst; } opx_vpp_t;
/* M5b: the main filter (p.58-60): TYPE (OPX_MF_*: the manual's list, p.38), ENABLE, CUTOFF (CUT10: 6.489 Hz x
 * (23680 / 6.489)^(c / 1023)), RESO (x0.01 %), EG2 and LFO2 INT (x0.01 semitone of the cutoff: 142.00 its whole
 * range), their CTRLs, KTRK LOW and HIGH (-127..127: the slope c / (128 - |c|): 64 = 1.00, the key's own; 127 ~ Inf;
 * an assumption) about CENTER (a MIDI note) */
enum { OPX_MF_LPF12, OPX_MF_LPF24, OPX_MF_LPFMS, OPX_MF_LPFP6, OPX_MF_HPF12, OPX_MF_HPF24, OPX_MF_HPFMS, OPX_MF_BPF6,
       OPX_MF_BPF12, OPX_MF_BRF6, OPX_MF_BRF12, OPX_NMF };
typedef struct {
    uint8_t type, on, eg2_ctrl, lfo2_ctrl, kc;
    int8_t kl, kh;
    uint16_t cutoff, reso;
    int16_t eg2, lfo2;
} opx_mflt_t;
/* M5c: a program effect (EFFECT FX1..3 p.61, the list p.91-94): its type (OPX_FXT, 0 OFF), ENABLE, knobs B C E F as
 * values (the field less the type's lo added back) */
typedef struct {
    uint8_t type, on;
    int16_t v[4];
} opx_fxp_t;

typedef struct {
    const char *name;
    uint8_t algo;                /* 0..39 = algorithm 1..40, 40 = USER */
    uint8_t alg_fb;              /* 0..100 %: the loop of algorithms 4, 6, 34 */
    uint8_t ua[OPX_NOP][OPX_NOP];   /* USER: ua[x][y] op x+1 -> op y+1 (%), ua[x][x] op x+1's self feedback */
    uint8_t ua_out;              /* USER: bit k: op k+1 is heard (DIRECT OUT) */
    opx_op_t op[OPX_NOP];        /* op[0] = OP1 */
    opx_egp_t eg[3];             /* M4: EG1 PITCH; M5: EG2 FLTR, EG3 */
    opx_lfop_t lfo[3];           /* M4: LFO1 OP; M5: LFO2 FLTR, LFO3 */
    int16_t prog_lfo1;           /* M4: PROG PITCH (p.72) LFO1 INT, x0.01 semitone; TRANS (semitones), OCT */
    int8_t prog_trans, prog_oct;
    uint8_t prog_lfo1_ctrl;      /* M4: its CTRL */
    opx_vpp_t vp[OPX_NVP];       /* M5: V.PATCH 1..12 */
    opx_mflt_t flt;              /* M5b: the main filter (FILTER, FLTR MOD p.58-60) */
    opx_fxp_t fx[3];             /* M5c: FX1..3, in series after the voices */
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
/* (0.2.0: tools/opx_factory_bank.py, compiled to opx_factory.h by tools/opx_factory.py: F1..F12 in every OPX build,
 * F13..F126 in OPX Solo. A patch leaves out an EG, LFO, filter or virtual patch: INIT's; its effects: OFF) */
static const opx_patch_t OPX_FACTORY[] = {       /* (F1..F8: a track plays these before it has its own copy) */
#define OPX_FACTORY_PATCHES
#include "opx_factory.h"
#undef OPX_FACTORY_PATCHES
};
#define OPX_FACTORY_RECORDS                       /* (F9..: as records, opx_patch.c opx_factory_pk) */
#include "opx_factory.h"
#undef OPX_FACTORY_RECORDS
#define OPX_NSTRUCT ((uint32_t)NELEM(OPX_FACTORY))
#define OPX_NFACTORY (OPX_NSTRUCT + (uint32_t)NELEM(OPX_FOFF) - 1u)
_Static_assert(OPX_NSTRUCT + NELEM(OPX_FOFF) - 1u <= 126u, "SLOT: F1..F126, then OWN, INIT: a project stores it in a byte (-64..127)");

/* ------------------------------------------------- the patch, per part --- */
typedef struct {                 /* an LFO as the patch sets it: wave, KSYNC, PHASE RANDOM, on: it reaches something */
    uint8_t w, ks, rnd, on;
    uint32_t inc, ph0;           /* phase per block (Q32), the phase a reset starts it at */
    int32_t fi;                  /* FADE: per block (Q24 of the whole) */
} opx_lfr_t;
typedef struct {                 /* M5: a virtual patch that does something: source, CTRL, destination, INT (Q15 of
                                  * the destination's whole move, opx_vp_apply) */
    uint8_t src, ctrl;
    uint16_t dst;
    int32_t amt;
} opx_vpr_t;
/* what one block plays: the patch through the macros. Rebuilt once a block (opx_block); read by the voices */
typedef struct {
    int32_t lg[OPX_NOP];         /* ratio: the operator's log2 pitch offset (Q24); fixed: unused */
    uint32_t fine[OPX_NOP];      /* ratio: FINE, Q16 */
    uint32_t finc[OPX_NOP];      /* fixed: the phase increment (Q32 cycles a sample), 0 = ratio */
    int32_t amp[OPX_NOP];        /* LEVEL (and MLVL), Q15 */
    int32_t fbq[OPX_NOP];        /* self feedback: phase per unit of output (Q32 cycles per Q15) */
    int32_t ainc[OPX_NOP + 3], dinc[OPX_NOP + 3], rinc[OPX_NOP + 3];   /* stage progress per block, Q24 (6..8: EG1..3) */
    int32_t sus[OPX_NOP + 3];    /* Q24 */
    uint16_t tc[OPX_NOP + 3][3]; /* M5: the stages' TIME10 codes (ATK DEC REL, the macros in): V.PATCH moves them */
    uint8_t curve[OPX_NOP + 3], vel[OPX_NOP];
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
    /* M4: the modulation (opx_render): P MOD EG1 / LFO1 INT at a full source (Q24 octaves), LMOD LFO1 INT (Q15),
     * P MOD VEL (%: FINE's addition at full velocity, Q16 of 1.000; COARSE steps), PROG PITCH */
    int32_t pme[OPX_NOP], pml[OPX_NOP], lml[OPX_NOP];
    uint8_t pmv[OPX_NOP], pmd[OPX_NOP], cidx[OPX_NOP];
    uint16_t crq7[OPX_NOP];      /* COARSE as a ratio, Q7 (1/128 = 1 .. 32 = 4096): P MOD VEL's addition over it */
    uint8_t cpe[OPX_NOP], cpl[OPX_NOP], clm[OPX_NOP], cpg;   /* their CTRL (OPX_C_*; OFF: the INT as it is) */
    int32_t plfo, poff;          /* PROG LFO1 INT (Q24 octaves), TRANS + OCT (Q24 octaves, on the note) */
    opx_lfr_t lf[3];             /* LFO1..3 (M5: LFO2, LFO3) */
    uint8_t lvl[OPX_NOP], fbp[OPX_NOP];   /* M5: LEVEL, FB (the macros in) as V.PATCH moves them; mlv: MLVL (Q15) */
    int32_t mlv;
    uint8_t eon[3], pmf[OPX_NOP];   /* EG1..3 reach something; operator k has a P MOD / LMOD route on (the render skips
                                  * the rest: most patches use few routes) */
    uint8_t nvp;                 /* M5: the virtual patches that do something, vp[0 .. nvp-1] */
    uint8_t son;                 /* an EG1..3 or LFO1..3 runs (else the render skips them) */
    /* M5b: the main filter: on (it does something: an LPF wide open, RESO 0, nothing moving it is skipped), its kind
     * (OPX_MF_*), the cutoff (Q24 octaves above 6.489 Hz), EG2 / LFO2 INT (Q24 octaves at a full source), their
     * CTRLs, KTRK's slopes (Q12) and CENTER, RESO's SVF damping k (Q16) and ladder feedback (Q15), the outputs' mix */
    uint8_t fon, fty, fce, fcl, fkc, fms;
    int32_t fcut, fe2, fl2, fkl, fkh, fk, fkq;
    int16_t fmix[5];             /* (SVF: x, v1 (band), v2 (low); ladder: u, y1 .. y4) */
    opx_vpr_t vp[OPX_NVP];
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
/* M4: EG1 (index OPX_NOP of the stage tables), LFO1, PROG PITCH. LFO1's SPEED: HZ10 (0.01 Hz x 10000^(c/1023)), or
 * TEMPO on a note value at the song's tempo: OPX_SYNC, the opsix's 24 as its SPEED knob goes (read off an opsix: the
 * manual gives only 1/48 .. 4/1): for each length its triplet, dotted, straight value, 1/32 triplet .. 2x breve */
static const uint8_t OPX_SYNC[24][2] = {{1, 48}, {3, 128}, {1, 32}, {1, 24}, {3, 64}, {1, 16}, {1, 12}, {3, 32}, {1, 8},
    {1, 6}, {3, 16}, {1, 4}, {1, 3}, {3, 8}, {1, 2}, {2, 3}, {3, 4}, {1, 1}, {4, 3}, {3, 2}, {2, 1}, {8, 3}, {3, 1},
    {4, 1}};                                          /* {num, den} of a whole note */
#define OPX_SYNC_DEF 11u                              /* (1/4: a value out of range plays this) */
/* an LFO's phase per block (Q32) at SPEED code c (HZ10) */
static uint32_t opx_lfo_inc(int32_t c)
{                                                    /* 0.01 Hz x 2^32 x 32 / 44100 = 31165.5, x 2^(13.2877 c / 1023) */
    return (uint32_t)(((uint64_t)(uint32_t)fm6_exp2(clamp(c, 0, 1023) * 217926 - (9 << 24)) * 1021231104u) >> 30);
}
static void opx_build_mod(const opx_patch_t *pt, opx_rt_t *r)
{
    uint32_t k, i, need = 0;                         /* need: bit c, CTRL source c (OPX_C_EG1 ..) is taken */
    r->cpg = (uint8_t)(pt->prog_lfo1_ctrl < OPX_NCTRL ? pt->prog_lfo1_ctrl : 0u);
    r->plfo = clamp(pt->prog_lfo1, -14400, 14400) * 13981;
    if (r->plfo)                                     /* (each INT that is on takes its source and its CTRL's) */
        need |= 1u << OPX_C_LFO1 | 1u << r->cpg;
    for (k = 0; k < OPX_NOP; k++) {
        if (r->pme[k])
            need |= 1u << OPX_C_EG1 | 1u << r->cpe[k];
        if (r->pml[k])
            need |= 1u << OPX_C_LFO1 | 1u << r->cpl[k];
        if (r->lml[k])
            need |= 1u << OPX_C_LFO1 | 1u << r->clm[k];
        r->pmf[k] = (uint8_t)(r->pme[k] != 0 || r->pml[k] != 0 || r->lml[k] != 0 || r->plfo != 0);
    }
    for (r->nvp = 0, i = 0; i < OPX_NVP; i++) {      /* M5: the virtual patches that do something */
        const opx_vpp_t *q = &pt->vp[i];
        if (!q->src || q->src >= OPX_NVS || !q->amt || !opx_vd_on(q->dst))
            continue;
        r->vp[r->nvp].src = q->src, r->vp[r->nvp].ctrl = (uint8_t)(q->ctrl < OPX_NCTRL ? q->ctrl : 0u);
        r->vp[r->nvp].dst = q->dst;
        r->vp[r->nvp].amt = (int32_t)clamp(q->amt, -10000, 10000) * 32767 / 10000;
        if (q->src >= OPX_VS_EG1 && q->src <= OPX_VS_LFO3)
            need |= 1u << (OPX_C_EG1 + q->src - OPX_VS_EG1);
        need |= 1u << r->vp[r->nvp].ctrl;
        r->nvp++;
    }
    {                                                /* M5b: the main filter */
        static const int16_t MIX[OPX_NMF][5] = {{0, 0, 1}, {0, 0, 0, 0, 1}, {0, 0, 1}, {0, 0, 0, 0, 1}, {1, -1, -1},
            {1, -4, 6, -4, 1}, {1, -1, -1}, {0, 2, -2}, {0, 1, 0}, {1, -2, 2}, {1, -1, 0}};
        static const uint16_t LAD = 1u << OPX_MF_LPF24 | 1u << OPX_MF_LPFP6 | 1u << OPX_MF_HPF24 | 1u << OPX_MF_BPF6 |
                                   1u << OPX_MF_BRF6;
        const opx_mflt_t *f = &pt->flt;
        int32_t rq = clamp(f->reso, 0, 10000) * 32767 / 10000, moved = 0;
        r->fty = (uint8_t)(f->type < OPX_NMF ? f->type : 0u);
        r->fms = (uint8_t)((LAD >> r->fty & 1u) ? 2u : r->fty == OPX_MF_LPFMS || r->fty == OPX_MF_HPFMS ? 1u : 0u);
        r->fcut = clamp(f->cutoff, 0, 1023) * 194064;   /* (Q24 octaves: 142 semitones over the 1023 codes) */
        r->fe2 = clamp(f->eg2, -14200, 14200) * 13981, r->fl2 = clamp(f->lfo2, -14200, 14200) * 13981;
        r->fce = (uint8_t)(f->eg2_ctrl < OPX_NCTRL ? f->eg2_ctrl : 0u);
        r->fcl = (uint8_t)(f->lfo2_ctrl < OPX_NCTRL ? f->lfo2_ctrl : 0u);
        r->fkl = (int32_t)clamp(f->kl, -127, 127) * 4096 / (128 - (f->kl < 0 ? -f->kl : f->kl));
        r->fkh = (int32_t)clamp(f->kh, -127, 127) * 4096 / (128 - (f->kh < 0 ? -f->kh : f->kh));
        r->fkc = f->kc;
        r->fk = 131072 - (int32_t)((int64_t)rq * 128450 >> 15), r->fkq = (int32_t)((int64_t)rq * 127139 >> 15);
        for (k = 0; k < 5u; k++)
            r->fmix[k] = MIX[r->fty][k];
        for (k = 0; k < r->nvp; k++)
            moved |= r->vp[k].dst == OPX_VD_FLT || r->vp[k].dst == OPX_VD_FLT + 1u;
        /* (an LPF open past 19.7 kHz, no RESO, nothing moving it: as good as none, skipped; the opsix's INIT is that) */
        r->fon = (uint8_t)(f->on && (r->fty > OPX_MF_LPFP6 || f->cutoff < 1000u || rq || f->eg2 || f->lfo2 || f->kl ||
                                     f->kh || moved));
        if (r->fon && r->fe2)
            need |= 1u << OPX_C_EG2 | 1u << r->fce;
        if (r->fon && r->fl2)
            need |= 1u << OPX_C_LFO2 | 1u << r->fcl;
    }
    for (i = 0; i < 3u; i++) {                       /* EG1..3 (p.55): built while something takes them */
        const opx_egp_t *e = &pt->eg[i];
        r->eon[i] = (uint8_t)(need >> (OPX_C_EG1 + i) & 1u);
        if (!r->eon[i])
            continue;
        r->tc[OPX_NOP + i][0] = e->atk, r->tc[OPX_NOP + i][1] = e->dec, r->tc[OPX_NOP + i][2] = e->rel;
        r->ainc[OPX_NOP + i] = opx_stage_inc(e->atk);
        r->dinc[OPX_NOP + i] = opx_stage_inc(e->dec);
        r->rinc[OPX_NOP + i] = opx_stage_inc(e->rel);
        r->sus[OPX_NOP + i] = clamp(e->sus, 0, 100) * ((1 << 24) / 100);
        r->curve[OPX_NOP + i] = (uint8_t)clamp(e->curve, 0, 10);
    }
    r->poff = clamp(pt->prog_trans, -12, 12) * ((1 << 24) / 12) + clamp(pt->prog_oct, -2, 2) * (1 << 24);
    r->son = (uint8_t)((need >> OPX_C_EG1 & 0x3Fu) != 0);   /* (EG1..3, LFO1..3: bits OPX_C_EG1 .. OPX_C_LFO3) */
    for (i = 0; i < 3u; i++) {                       /* LFO1..3 (p.55-57) */
        const opx_lfop_t *l = &pt->lfo[i];
        opx_lfr_t *f = &r->lf[i];
        f->on = (uint8_t)(need >> (OPX_C_LFO1 + i) & 1u);
        f->w = (uint8_t)(l->wave < OPX_NLFOW ? l->wave : 0u);
        f->ks = (uint8_t)(l->ksync < 3u ? l->ksync : 0u);
        f->rnd = l->phase < -180;
        f->ph0 = (uint32_t)(l->phase < -180 ? 0 : clamp(l->phase, -180, 180)) * 11930465u;   /* (2^32 / 360) */
        f->fi = l->fade && f->ks ? (int32_t)(12174240u / l->fade) : 1 << 24;   /* (2^24 x 32 / 44.1 / ms) */
        if (!f->on)                                  /* (its speed: while something takes it) */
            continue;
        if (l->tempo) {                              /* 2^32 x 32 / 44100 x BPM / 60 / (4 x num / den): 12985.6 x BPM x den / num */
            const uint8_t *nv = OPX_SYNC[l->sync < NELEM(OPX_SYNC) ? l->sync : OPX_SYNC_DEF];
            f->inc = (uint32_t)(12986 * clamp(song.g[G_BPM], 20, 300)) * nv[1] / nv[0];
        } else
            f->inc = opx_lfo_inc(l->speed);
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
        pt = &OPX_FACTORY[(uint32_t)clamp(t->p[P_E7], 0, (int32_t)OPX_NSTRUCT - 1)];   /* (before opx_poll's first) */
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
    r->mlv = mlvl;
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
        r->lvl[k] = (uint8_t)clamp(o->level, 0, 100), r->fbp[k] = (uint8_t)fb;
        r->fbq[k] = fb * 417;                          /* 100 %: 2 rad = 0.3183 cycle at a full output: 0.3183 x 2^32 / 32767 / 100 */
        r->tc[k][0] = (uint16_t)(o->atk ? clamp(o->atk + datk, 1, 1023) : 0);
        r->tc[k][1] = (uint16_t)(o->dec ? clamp(o->dec + ddec, 1, 1023) : 0);
        r->tc[k][2] = (uint16_t)(o->rel ? clamp(o->rel + ddec, 1, 1023) : 0);
        r->ainc[k] = opx_stage_inc(r->tc[k][0]);
        r->dinc[k] = opx_stage_inc(r->tc[k][1]);
        r->rinc[k] = opx_stage_inc(r->tc[k][2]);
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
            r->fbq[k] = 0, r->fbp[k] = 0;
        if (o->width && o->width < 100u && r->wv[k] < OPX_W_NOISE_SH && r->md[k] == OPX_M_FM) {   /* (noises: no width) */
            r->wv[k] |= OPX_W_NARROW;
            r->wr[k] = (100u << 16) / o->width;
            r->wlim[k] = (uint32_t)(((uint64_t)o->width << 32) / 100u);
        }
        /* M4: P MOD, LMOD (x0.01 semitone -> Q24 octaves: x 2^24 / 1200) */
        r->pme[k] = clamp(o->pm_eg1, -14400, 14400) * 13981;
        r->pml[k] = clamp(o->pm_lfo1, -14400, 14400) * 13981;
        r->lml[k] = clamp(o->lm_lfo1, -100, 100) * 32767 / 100;
        r->cpe[k] = (uint8_t)(o->pm_eg1_ctrl < OPX_NCTRL ? o->pm_eg1_ctrl : 0u);
        r->cpl[k] = (uint8_t)(o->pm_lfo1_ctrl < OPX_NCTRL ? o->pm_lfo1_ctrl : 0u);
        r->clm[k] = (uint8_t)(o->lm_lfo1_ctrl < OPX_NCTRL ? o->lm_lfo1_ctrl : 0u);
        r->pmv[k] = (uint8_t)(o->fixed ? 0 : clamp(o->pm_vel, 0, 100));   /* (RATIO only, p.51) */
        r->pmd[k] = (uint8_t)(o->pm_dest & 1u);
        r->cidx[k] = (uint8_t)clamp(c, 0, 38);
        r->crq7[k] = (uint16_t)(r->cidx[k] < 7u ? 1u << r->cidx[k] : (r->cidx[k] - 6u) * 128u);
    }
    opx_build_mod(pt, r);
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
/* M4: an LFO's state: its phase (Q32), a random generator, the random waves' last and next levels (Q15), their
 * segment's speed (Q8: the phase steps x mul / 256) */
typedef struct {
    uint32_t ph, rng;
    int16_t a, b;
    uint16_t mul, rsv;
} opx_lfo_t;
_Static_assert(sizeof(opx_lfo_t) == 16u, "opx_lfo_t");
static opx_lfo_t opx_lfoc[NTRK][3];            /* M4: each part's LFO1..3 (KSYNC OFF, COMMON), stepped in opx_block */
static int32_t opx_lfov[NTRK][3];              /* .. their values this block (Q15) */
static uint32_t opx_lfo_rnd(opx_lfo_t *l) { return l->rng = l->rng * 1664525u + 1013904223u; }
static void opx_lfo_reset(opx_lfo_t *l, const opx_lfr_t *r, uint32_t seed)
{
    if (!l->rng)
        l->rng = 0x2545F491u ^ seed;
    l->ph = r->rnd ? opx_lfo_rnd(l) : r->ph0;
    l->a = l->b = (int16_t)((int32_t)opx_lfo_rnd(l) >> 17);
    l->mul = 256;
}
/* the LFO Wave List's shapes (p.56; the manual draws them, gives no formula: each here a reading of its name) at
 * phase u (0..65535 a cycle), Q15. TRI starts at 0 rising, as SIN; TRI4 TRI6 SAW4 SAW6 the shape held over 4 / 6
 * steps a cycle; the X. ones on CURVE 10's exponential approach (OPX_EXPC); GTR (1 - cos) / 2, 0 .. +1 (a guitar's
 * vibrato bends up only); the "+" ones the shape lifted to 0 .. +1 */
static int32_t opx_lfo_shape(uint32_t w, uint32_t u)
{
    int32_t v;
    switch (w) {
    case OPX_L_SQR: case OPX_L_SQRP: v = u < 32768u ? 32767 : -32767; break;
    case OPX_L_SIN: v = sine_i(u << 16); break;
    case OPX_L_SAWD: case OPX_L_SAWDP: v = 32767 - (int32_t)u; break;
    case OPX_L_SAWU: case OPX_L_SAWUP: v = (int32_t)u - 32768; break;
    case OPX_L_SAW4D: case OPX_L_SAW6D: {
        uint32_t n = w == OPX_L_SAW4D ? 4u : 6u, st = u * n >> 16;
        v = 32767 - (int32_t)(st * 65535u / (n - 1u));
        break;
    }
    case OPX_L_TRI4: case OPX_L_TRI6: {               /* (TRI4 at the step's start: 0 +1 0 -1; TRI6 at its middle, so it
                                                       * reaches +-1 too) */
        uint32_t n = w == OPX_L_TRI4 ? 4u : 6u;
        u = (u * n >> 16) * 65536u / n + (n == 6u ? 65536u / 12u : 0u);
    }   /* fall through */
    case OPX_L_TRI: case OPX_L_TRIP:
        v = u < 16384u ? (int32_t)u * 2 : u < 49152u ? 65536 - (int32_t)u * 2 : (int32_t)u * 2 - 131072;
        break;
    case OPX_L_XSAWD: case OPX_L_XSAWU: {
        uint32_t x = w == OPX_L_XSAWD ? u : 65535u - u;
        v = 32767 - (opx_shape((int32_t)x << 8, 10) >> 8);
        break;
    }
    case OPX_L_XTRI:
        v = u < 32768u ? -32767 + (opx_shape((int32_t)u << 9, 10) >> 8) : 32767 - (opx_shape((int32_t)(u - 32768u) << 9, 10) >> 8);
        break;
    default:                                          /* GTR */
        return (32767 - sine_i((u << 16) + 0x40000000u)) >> 1;
    }
    v = clamp(v, -32767, 32767);
    return w >= OPX_L_TRIP ? (v + 32767) >> 1 : v;   /* (GTR returned above) */
}
/* one block of LFO l: its value (Q15) after the step */
static int32_t opx_lfo_step(opx_lfo_t *l, const opx_lfr_t *r, uint32_t inc)
{
    uint32_t w = r->w, p0 = l->ph;
    if (OPX_L_RANDOM(w)) {             /* the random ones: a segment a cycle (LVTIME, TIME: of random length) */
        l->ph += (uint32_t)(((uint64_t)inc * l->mul) >> 8);
        if (l->ph < p0) {                             /* (a new segment) */
            uint32_t x = opx_lfo_rnd(l);
            l->a = l->b;
            if (w == OPX_L_SRT || w == OPX_L_RT)      /* TIME: a pulse, its level flips at random times */
                l->b = (int16_t)(l->b >= 0 ? -32767 : 32767);
            else
                l->b = (int16_t)((int32_t)x >> 17);
            l->mul = w == OPX_L_SH || w == OPX_L_RL ? 256u : (uint16_t)(128u + (opx_lfo_rnd(l) >> 24) * 3u / 2u);   /* x0.5 .. x2 */
        }
        if (OPX_L_HELD(w))                            /* S/H, S.RND: held; RND: from the last level to the next */
            return l->b;
        return l->a + (int32_t)(((int64_t)(l->b - l->a) * (int32_t)(l->ph >> 16)) >> 16);
    }
    l->ph += inc;
    return opx_lfo_shape(w, l->ph >> 16);
}
typedef struct {                 /* a sounding note: 692 bytes (OPX Solo 700) */
    uint32_t ph[OPX_NOP];
    int32_t y[OPX_NOP], yp[OPX_NOP];   /* the last two outputs (Q15) */
    int32_t g[OPX_NOP];          /* the gain at the end of the last block (Q15) */
    int32_t vg[OPX_NOP];         /* velocity's factor on the level (Q15) */
    int32_t kt[OPX_NOP];         /* M2b: KTRK's factor on the level for this note (Q15, up to x16) */
    int32_t fs[OPX_NOP][5];      /* M2b: the operator filter's or effect's state (opx_filt, opx_fx) */
    uint16_t dw;                 /* M2b: the voice's delay line (SHORT DELAY, COMB): where it writes next */
#if FELUCCA_OPX_ONLY
    uint8_t dsub;                /* (OPX Solo: SHORT DELAY's line at 1 / q the rate: the samples of the next taken) */
    int32_t dacc;                /* .. their sum */
#endif
    opx_eg_t eg[OPX_NOP];
    opx_noise_t nz[OPX_NOP];     /* M2: the noise waveforms' state */
    int32_t dc;
    uint8_t live, down, rsv[2];
    opx_eg_t egm[3];             /* M4: EG1; M5: EG2, EG3 */
    opx_lfo_t lfo[3];            /* M4: LFO1; M5: LFO2, LFO3: KSYNC VOICE (else the part's: opx_lfoc) */
    int32_t lfade[3];            /* .. FADE's level, Q24 */
    int32_t fst[5];              /* M5b: the main filter's state */
    uint32_t vfq[OPX_NOP];       /* M4: P MOD VEL: what this note adds to FINE (Q16, over COARSE), worked out at the
                                  * note-on so the render has no divide (FINE / COARSE turned during the note: kept) */
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
static int32_t opx_eg_stepv(opx_eg_t *e, int32_t ai, int32_t di, int32_t ri, int32_t su, int32_t cv)
{
    int32_t s;
    switch (e->stage) {
    case OPX_ATK:                                  /* linear, from where a retrigger found it */
        e->x += ai;
        if (e->x >= 1 << 24) {
            e->stage = OPX_DEC, e->x = 0, e->lvl = 1 << 24;
            break;
        }
        e->lvl = e->from + (int32_t)(((int64_t)((1 << 24) - e->from) * e->x) >> 24);
        break;
    case OPX_DEC:
        e->x += di;
        if (e->x >= 1 << 24) {
            e->stage = OPX_SUS, e->lvl = su;
            break;
        }
        s = opx_shape(e->x, cv);
        e->lvl = (1 << 24) - (int32_t)(((int64_t)((1 << 24) - su) * s) >> 24);
        break;
    case OPX_SUS:
        e->lvl = su;                                /* (SUS may move while it holds) */
        break;
    case OPX_REL:
        e->x += ri;
        if (e->x >= 1 << 24) {
            e->stage = OPX_DONE, e->lvl = 0;
            break;
        }
        s = opx_shape(e->x, cv);
        e->lvl = e->from - (int32_t)(((int64_t)e->from * s) >> 24);
        break;
    default:
        e->lvl = 0;
    }
    return e->lvl;
}
/* envelope k (0..5 the operators', 6..8 EG1..3) a block on, as the patch sets it */
static int32_t opx_eg_step(opx_eg_t *e, const opx_rt_t *r, uint32_t k)
{
    return opx_eg_stepv(e, r->ainc[k], r->dinc[k], r->rinc[k], r->sus[k], r->curve[k]);
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
static uint32_t opx_non[NTRK];                       /* M5c: each part's note-ons so far (GRAIN's RETRIG NOTE) */
static void opx_note_on(track_t *t, voice_t *v)
{
    opx_note_t *n = opx_note_of(t, v);
    opx_non[(uint32_t)(t - trk) % NTRK]++;
    const opx_rt_t *r = &opx_rt[(uint32_t)(t - trk) % NTRK];
    int32_t vel = v->mvel ? v->mvel : v->vel, fresh, k, others = 0;
    if (!n)
        return;
    for (k = 0; k < OPX_POLY; k++) {                    /* (M4: KSYNC COMMON resets LFO1 when no other key is down) */
        const opx_note_t *o = (const opx_note_t *)ENG_MEM(t - trk) + k;
        others |= o != n && o->live && o->down;
    }
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
        /* M4: P MOD VEL (RATIO only; measured on an opsix): INT % x velocity / 127 x 10. DEST COARSE: that to the
         * nearest whole number added to COARSE, FINE then multiplying (COARSE 2, FINE 0.50, 55 % at 127: (2 + 6) x 0.5
         * = 4). DEST FINE: added to the final ratio as it is (100 % at 127: 1 -> 11, COARSE 2 -> 12; at 64: 1 -> 6.04;
         * with FINE away from 1.00 not yet measured) */
        {
            uint32_t a = (uint32_t)(r->pmv[k] * clamp(vel, 0, 127));
            if (!a || !r->crq7[k])                   /* (VEL 0, velocity 0, or no patch built yet) */
                n->vfq[k] = 0;
            else if (r->pmd[k])                           /* (COARSE + n, then x FINE: FINE x n / COARSE; <= 2^28) */
                n->vfq[k] = r->fine[k] * ((a * 10u + 6350u) / 12700u) * 128u / r->crq7[k];
            else                                     /* (+ the addition on the final ratio, Q12 -> Q16 over COARSE) */
                n->vfq[k] = ((a * 40960u / 12700u) << 11) / r->crq7[k];
        }
    }
    /* M4: EG1 as the operators' envelopes; LFO1 from its PHASE (KSYNC VOICE: this note's; COMMON: the part's, when
     * no other key is down; OFF: free), its FADE from 0 (not with KSYNC OFF) */
    for (k = 0; k < 3; k++) {                         /* (M5: EG2 EG3 LFO2 LFO3 alike) */
        opx_eg_t *e = &n->egm[k];
        const opx_lfr_t *l = &r->lf[k];
        if (fresh)
            e->lvl = 0;
        e->from = e->lvl, e->x = 0, e->stage = OPX_ATK;
        if (l->ks == 2u)
            opx_lfo_reset(&n->lfo[k], l, (uint32_t)v->note * 2654435761u + (uint32_t)k);
        else if (l->ks == 1u && !others)
            opx_lfo_reset(&opx_lfoc[(uint32_t)(t - trk) % NTRK][k], l, (uint32_t)v->note * 2654435761u + (uint32_t)k);
        n->lfade[k] = l->ks ? 0 : 1 << 24;
    }
    if (fresh) {
        n->dc = 0;
        n->dw = 0;
#if FELUCCA_OPX_ONLY
        n->dacc = 0, n->dsub = 0;
#endif
        memset(n->fst, 0, sizeof n->fst);
        memset(opx_dline(t, v), 0, OPX_DLY * sizeof(int16_t));   /* (M2b: no echo of an earlier note) */
    }
    n->live = n->down = 1;
}

static void opx_block(track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NTRK) {
        opx_build(t, &opx_rt[tr]);
        uint32_t i;
        for (i = 0; i < 3u; i++)                       /* M4: the part's LFO1..3 (KSYNC OFF, COMMON) a block on */
            if (opx_rt[tr].lf[i].on && opx_rt[tr].lf[i].ks != 2u)
                opx_lfov[tr][i] = opx_lfo_step(&opx_lfoc[tr][i], &opx_rt[tr].lf[i], opx_rt[tr].lf[i].inc);
    }
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
static void __attribute__((noinline)) opx_filt_block(opx_fc_t *c, const int32_t *mp, uint32_t inc, uint32_t fm)
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
         * output a sample late; the states (st) to +-2^17, the products under 2^31. (0.2.0: v rounded, not truncated:
         * truncated, each stage stopped short of its input, always below it, and a low cutoff held ~1 % DC) */
        int32_t u = clamp(x - (int32_t)(((int64_t)c->kl * st[4]) >> 15), -65535, 65535), g = c->g1 >> 1, v, y1, y2, y3, y4;
        v = (g * (u - st[0]) + 4096) >> 13, y1 = v + st[0], st[0] = clamp(y1 + v, -131071, 131071);
        v = (g * (y1 - st[1]) + 4096) >> 13, y2 = v + st[1], st[1] = clamp(y2 + v, -131071, 131071);
        v = (g * (y2 - st[2]) + 4096) >> 13, y3 = v + st[2], st[2] = clamp(y3 + v, -131071, 131071);
        v = (g * (y3 - st[3]) + 4096) >> 13, y4 = v + st[3], st[3] = clamp(y4 + v, -131071, 131071);
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
 *   SHORT DELAY the dry signal and a delay of TIME with FB, each at -3 dB. The delay line is the voice's own, 728
 *               samples: TIME stops at 16.5 ms (the manual's 300 ms do not fit the FM-1's memory; OPX Solo: 2816,
 *               63.9 ms at the full rate, longer TIMEs at 1 / 2 .. 1 / 5 of it, the mean of each q samples kept:
 *               300 ms at 8.8 kHz, the repeats darker), and one operator a
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
    int32_t dacc;                /* (OPX Solo) SHORT DELAY at 1 / q the rate: the sum of the samples of the next, */
    uint8_t type, dsub;          /* .. how many */
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
static void __attribute__((noinline)) opx_fx_block(opx_ec_t *e, const int32_t *mp, uint32_t inc)
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
#if FELUCCA_OPX_ONLY
        {                                              /* (OPX Solo: past the line at the full rate, at 1 / q of it: c[0]
                                                        * the delay in line samples Q8, c[2] q, c[3] 65536 / q) */
            int32_t sm = clamp(E * 441 / 10, 1, 13230), q = (sm + (int32_t)OPX_DLY - 5) / ((int32_t)OPX_DLY - 4);
            q = clamp(q, 1, 5);
            c[2] = q, c[3] = 65536 / q;
            c[0] = q == 1 ? clamp(sm, 1, (int32_t)OPX_DLY - 2) : clamp((sm << 8) / q, 512, ((int32_t)OPX_DLY - 3) << 8);
        }
#else
        c[0] = clamp(E * 441 / 10, 1, (int32_t)OPX_DLY - 2);
#endif
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
            int32_t v = (int32_t)(((int64_t)c[0] * u) >> 14) + st[k];   /* (64-bit: st up to 2^18) */
            st[k] = clamp(u - (int32_t)(((int64_t)c[0] * v) >> 14), -262143, 262143);
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
#if FELUCCA_OPX_ONLY
        if (e->type == 3 && c[2] > 1) {                /* (the line at 1 / q the rate: the q samples' mean written, read
                                                        * linearly between, as of the sample now) */
            int32_t pos = (int32_t)(e->dw << 8) + ((e->dsub * c[3]) >> 8) - c[0], i = pos >> 8, f = pos & 255;
            uint32_t a = (uint32_t)(i < 0 ? i + (int32_t)OPX_DLY : i), b = a + 1u >= OPX_DLY ? 0u : a + 1u;
            d = e->dl[a] + (((e->dl[b] - e->dl[a]) * f) >> 8);
            e->dacc += clamp(x + ((d * c[1]) >> 15), -32767, 32767);
            y = ((x + d) * 23170) >> 15;
            if (++e->dsub >= (uint8_t)c[2]) {
                e->dl[e->dw] = (int16_t)((int32_t)(((int64_t)e->dacc * c[3]) >> 16));
                e->dacc = 0, e->dsub = 0;
                e->dw = e->dw + 1u < OPX_DLY ? e->dw + 1u : 0u;
            }
            break;
        }
#endif
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

/* M4: CTRL source c's value now (Q15: OFF 32768, the INT whole). The wheels, pedal, aftertouch: the part's MIDI ones;
 * PITCH WHEEL -1 .. +1, PITCH WHL+ / WHL- 0 .. +1 as the wheel goes up / down; NOTE -1 .. +1 over C4 +- 64 notes (the
 * manual: "the center note is 60"; the span an assumption); EXP VEL velocity squared; REL VEL 64 (Felucca's notes
 * carry no release velocity: the MIDI default); EG1..3 0 .. +1; LFO1..3 their values. ms: EG1..3 (Q24), LFO1..3 (Q15) */
static int32_t opx_ctl(const track_t *t, const voice_t *v, uint32_t c, const int32_t *ms)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK;
    int32_t b = opx_ctl_bend[tr], x;
    switch (c) {
    case OPX_C_OFF: return 32768;
    case OPX_C_MW: return t->mw * 258;
    case OPX_C_PW: return b * 4;
    case OPX_C_PWP: return b > 0 ? b * 4 : 0;
    case OPX_C_PWM: return b < 0 ? -b * 4 : 0;
    case OPX_C_PEDAL: return opx_ctl_pedal[tr] ? 32767 : 0;
    case OPX_C_NOTE: return clamp(((int32_t)v->note - 60) * 512, -32767, 32767);
    case OPX_C_VEL: return clamp(v->vel, 0, 127) * 258;
    case OPX_C_XVEL: x = clamp(v->vel, 0, 127); return x * x * 2;
    case OPX_C_RVEL: return 64 * 258;
    case OPX_C_AT: return t->at * 258;
    case OPX_C_EG1: case OPX_C_EG2: case OPX_C_EG3: return ms[c - OPX_C_EG1] >> 9;
    case OPX_C_LFO1: case OPX_C_LFO2: case OPX_C_LFO3: return ms[3 + c - OPX_C_LFO1];
    default: return 0;
    }
}
#define OPX_CQ(a, cv) ((cv) == 32768 ? (a) : (int32_t)(((int64_t)(a) * (cv)) >> 15))   /* an INT x its CTRL */

/* M5: V.PATCH source s's value now (Q15): CTRL and KBD as CTRL's (opx_ctl); EG1..3 0 .. +1; LFO1..3 their values;
 * CC+ 0 .. +1 over 0..127, CC+/- -1 .. +1 about 64 (none received: 0); AFTER TOUCH the part's; POLY AFTER 0 (Felucca
 * keeps no poly aftertouch) */
static int32_t opx_vsrc(const track_t *t, const voice_t *v, uint32_t s, const int32_t *ms)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, c;
    if (s <= OPX_C_RVEL)
        return opx_ctl(t, v, s, ms);
    if (s <= OPX_VS_LFO3)
        return s < OPX_VS_EG1 + 3u ? ms[s - OPX_VS_EG1] >> 9 : ms[s - OPX_VS_EG1];
    if (s < OPX_VS_CCB) {
        c = opx_cc[tr][s - OPX_VS_CCU + 1u];
        return c ? (int32_t)(c - 1u) * 258 : 0;
    }
    if (s < OPX_VS_AT) {
        c = opx_cc[tr][s - OPX_VS_CCB + 1u];
        return c ? clamp(((int32_t)c - 65) * 520, -32767, 32767) : 0;
    }
    return s == OPX_VS_AT ? t->at * 258 : 0;
}
/* M5: what the virtual patches do to a voice this block */
typedef struct {
    int32_t pitch;                   /* PROG PITCH OCT TRANS: the voice's pitch, Q24 octaves */
    int32_t lev;                     /* PROG LEVEL: the heard operators x (1 + lev), Q15 */
    int32_t op[OPX_NOP];             /* each operator's PITCH COARSE OCT TRANS, Q24 octaves */
    int16_t lv[OPX_NOP], fb[OPX_NOP];   /* LEVEL, FM FB: points */
    int16_t td[OPX_NOP + 3][3];      /* the stages' TIME10 codes moved (the operators', PROG's ATK DECAY REL too; EG1..3) */
    int16_t sd[OPX_NOP + 3];         /* SUS: points */
    int16_t lsp[3], lfd[3];          /* LFO1..3 SPEED (HZ10 codes), FADE (ms) */
    uint32_t lph[3];                 /* .. PHASE (Q32) */
    uint16_t eg;                     /* bit k: envelope k (0..5, 6..8 EG1..3) has a stage moved */
    int32_t fcut, freso;             /* M5b: FILTER CUTOFF (Q24 octaves), RESO (points) */
} opx_vm_t;
/* the INT's scale (an assumption: the manual says only "depends on destination"): INT 100.00 with its source at full
 * moves the destination over its whole range (PITCH: 100 semitones; OCT 4 octaves; TRANS 24 semitones; COARSE 38
 * steps; LEVEL SUS FB 100; a stage's time the 1023 codes of 0 ms .. 90 s; SPEED 0.01 .. 100 Hz; PHASE 360 degrees;
 * FADE 9.9 s; PROG LEVEL x0 .. x2). x: the source x CTRL x INT, Q15 */
static void opx_vp_add(opx_vm_t *vm, const opx_rt_t *r, uint32_t d, int32_t x)
{
    int32_t st, i;
    if (d < OPX_VD_OP) {
        switch (d - OPX_VD_PROG) {
        case OPX_DP_PITCH: vm->pitch += x * 4267; break;               /* (100 semitones: 100 x 2^24 / 12 / 32767) */
        case OPX_DP_OCT: vm->pitch += ((x * 4 + (x < 0 ? -16383 : 16383)) / 32767) << 24; break;
        case OPX_DP_TRANS: vm->pitch += (x * 24 + (x < 0 ? -16383 : 16383)) / 32767 * ((1 << 24) / 12); break;
        case OPX_DP_LEVEL: vm->lev += x; break;
        case OPX_DP_SUS:
            for (i = 0; i < OPX_NOP; i++)
                vm->sd[i] = (int16_t)(vm->sd[i] + x * 100 / 32767);
            vm->eg |= 0x3Fu;
            break;
        default:                                                       /* ATK DECAY REL: every operator's */
            st = (int32_t)(d - OPX_VD_PROG) - OPX_DP_ATK;
            for (i = 0; i < OPX_NOP; i++)
                vm->td[i][st] = (int16_t)(vm->td[i][st] + x * 1023 / 32767);
            vm->eg |= 0x3Fu;
        }
    } else if (d < OPX_VD_EG) {
        uint32_t k = (d - OPX_VD_OP) / 39u, j = (d - OPX_VD_OP) % 39u;
        switch (j) {
        case OPX_DO_PITCH: vm->op[k] += x * 4267; break;
        case OPX_DO_COARSE:
            st = clamp(r->cidx[k] + (x * 38 + (x < 0 ? -16383 : 16383)) / 32767, 0, 38);
            vm->op[k] += OPX_COARSE[st] - OPX_COARSE[r->cidx[k]];
            break;
        case OPX_DO_OCT: vm->op[k] += ((x * 4 + (x < 0 ? -16383 : 16383)) / 32767) << 24; break;
        case OPX_DO_TRANS: vm->op[k] += (x * 24 + (x < 0 ? -16383 : 16383)) / 32767 * ((1 << 24) / 12); break;
        case OPX_DO_LEVEL: vm->lv[k] = (int16_t)(vm->lv[k] + x * 100 / 32767); break;
        case OPX_DO_SUS: vm->sd[k] = (int16_t)(vm->sd[k] + x * 100 / 32767), vm->eg |= 1u << k; break;
        case OPX_DO_FB: vm->fb[k] = (int16_t)(vm->fb[k] + x * 100 / 32767); break;
        default:                                                       /* ATK DECAY REL */
            st = (int32_t)j - OPX_DO_ATK;
            vm->td[k][st] = (int16_t)(vm->td[k][st] + x * 1023 / 32767), vm->eg |= 1u << k;
        }
    } else if (d < OPX_VD_LFO) {                                       /* EG1..3: ATK DECAY REL SUS */
        uint32_t e = (d - OPX_VD_EG) / 4u, j = (d - OPX_VD_EG) % 4u;
        if (j < 3u)
            vm->td[OPX_NOP + e][j] = (int16_t)(vm->td[OPX_NOP + e][j] + x * 1023 / 32767);
        else
            vm->sd[OPX_NOP + e] = (int16_t)(vm->sd[OPX_NOP + e] + x * 100 / 32767);
        vm->eg |= 1u << (OPX_NOP + e);
    } else if (d >= OPX_VD_FLT) {                                      /* M5b: FILTER CUTOFF (142 semitones), RESO */
        if (d == OPX_VD_FLT)
            vm->fcut += x * 6059;                       /* (11.83 octaves at full, Q24) */
        else
            vm->freso += x * 100 / 32767;
    } else {                                                           /* LFO1..3: SPEED PHASE FADE */
        uint32_t l = (d - OPX_VD_LFO) / 4u, j = (d - OPX_VD_LFO) % 4u;
        if (j == OPX_DL_SPEED)
            vm->lsp[l] = (int16_t)(vm->lsp[l] + x * 1023 / 32767);
        else if (j == OPX_DL_PHASE)
            vm->lph[l] += (uint32_t)x * 131076u;                        /* (360 degrees: 2^32) */
        else
            vm->lfd[l] = (int16_t)(vm->lfd[l] + x * 9900 / 32767);
    }
}
/* an LFO's value without stepping it (V.PATCH reads its sources before the block steps them) */
static int32_t opx_lfo_peek(const opx_lfo_t *l, uint32_t w)
{
    if (OPX_L_RANDOM(w))
        return OPX_L_HELD(w) ? l->b : l->a + (int32_t)(((int64_t)(l->b - l->a) * (int32_t)(l->ph >> 16)) >> 16);
    return opx_lfo_shape(w, l->ph >> 16);
}
static void __attribute__((noinline)) opx_vp_eval(const track_t *t, const voice_t *v, const opx_note_t *n, const opx_rt_t *r, opx_vm_t *vm)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, i;
    int32_t ms[6];
    memset(vm, 0, sizeof *vm);
    for (i = 0; i < 3u; i++) {                       /* (the sources as the last block left them: one block late) */
        int32_t lv = 0;
        ms[i] = r->eon[i] ? n->egm[i].lvl : 0;
        if (r->lf[i].on) {
            lv = r->lf[i].ks == 2u ? opx_lfo_peek(&n->lfo[i], r->lf[i].w) : opx_lfov[tr][i];
            if (n->lfade[i] < 1 << 24)
                lv = (int32_t)(((int64_t)lv * n->lfade[i]) >> 24);
        }
        ms[3 + i] = lv;
    }
    for (i = 0; i < r->nvp; i++) {
        const opx_vpr_t *q = &r->vp[i];
        int32_t a = q->amt, x;
        if (q->ctrl)
            a = OPX_CQ(a, opx_ctl(t, v, q->ctrl, ms));
        x = (int32_t)(((int64_t)opx_vsrc(t, v, q->src, ms) * a) >> 15);
        if (x)
            opx_vp_add(vm, r, q->dst, x);
    }
}
/* envelope k a block on, its stages moved by V.PATCH */
static int32_t __attribute__((noinline)) opx_eg_stepm(opx_eg_t *e, const opx_rt_t *r, uint32_t k, const opx_vm_t *vm)
{
    int32_t su = clamp(r->sus[k] / ((1 << 24) / 100) + vm->sd[k], 0, 100) * ((1 << 24) / 100);
    return opx_eg_stepv(e, opx_stage_inc(clamp(r->tc[k][0] + vm->td[k][0], 0, 1023)),
                        opx_stage_inc(clamp(r->tc[k][1] + vm->td[k][1], 0, 1023)),
                        opx_stage_inc(clamp(r->tc[k][2] + vm->td[k][2], 0, 1023)), su, r->curve[k]);
}
/* M5b: a voice's block (in, the voice's samples) through the main filter into out. At half scale (a voice of several
 * carriers stays inside the filters' ranges), back to full after. The SVF (LPF12, HPF12, BPF12, BRF12; MS-20's with
 * its resonance limited in the loop) or the ladder (LPF24, POLY6, HPF24, BPF6, BRF6), each output a mix of the
 * filter's points (r->fmix), no branch a sample */
static void __attribute__((noinline)) opx_mflt(const opx_rt_t *r, const opx_fc_t *c, int32_t *st, const int32_t *in,
                                               int32_t *out)
{
    const int16_t *mx = r->fmix;
    uint32_t i;
    if (r->fms == 2u) {                                /* the ladder (opx_filt's; v rounded, as there) */
        int32_t g = c->g1 >> 1, kl = c->kl;
        for (i = 0; i < CTL; i++) {
            int32_t u = clamp((in[i] >> 1) - (int32_t)(((int64_t)kl * st[4]) >> 15), -65535, 65535), v, y1, y2, y3, y4;
            v = (g * (u - st[0]) + 4096) >> 13, y1 = v + st[0], st[0] = clamp(y1 + v, -131071, 131071);
            v = (g * (y1 - st[1]) + 4096) >> 13, y2 = v + st[1], st[1] = clamp(y2 + v, -131071, 131071);
            v = (g * (y2 - st[2]) + 4096) >> 13, y3 = v + st[2], st[2] = clamp(y3 + v, -131071, 131071);
            v = (g * (y3 - st[3]) + 4096) >> 13, y4 = v + st[3], st[3] = clamp(y4 + v, -131071, 131071);
            st[4] = y4;
            out[i] += clamp(mx[0] * u + mx[1] * y1 + mx[2] * y2 + mx[3] * y3 + mx[4] * y4, -65535, 65535) * 2;
        }
    } else {                                           /* the state-variable filter */
        int32_t lim = r->fms ? 40000 : 2097152;
        for (i = 0; i < CTL; i++) {
            int32_t x = clamp(in[i] >> 1, -65535, 65535), v3 = x - st[1], v1, v2;
            v1 = (int32_t)(((int64_t)c->a1 * st[0] + (int64_t)c->a2 * v3) >> 16);
            v2 = st[1] + (int32_t)(((int64_t)c->a2 * st[0] + (int64_t)c->a3 * v3) >> 16);
            st[0] = clamp(2 * v1 - st[0], -lim, lim);
            st[1] = clamp(2 * v2 - st[1], -2097152, 2097152);
            out[i] += clamp(mx[0] * x + mx[1] * (int32_t)(((int64_t)c->k * v1) >> 16) + mx[2] * v2, -65535, 65535) * 2;
        }
    }
}
/* M5b: the main filter's coefficients for a voice this block: CUTOFF + EG2 x INT + LFO2 x INT (their CTRLs) + KTRK +
 * V.PATCH, RESO (+ V.PATCH) */
static void __attribute__((noinline)) opx_mflt_coef(const track_t *t, const voice_t *v, const opx_rt_t *r,
                                                    const int32_t *ms, const opx_vm_t *vm, opx_fc_t *c)
{
    int32_t st = r->fcut + vm->fcut, d = (int32_t)v->note - r->fkc, kq = r->fkq, e;
    uint64_t f;
    if (r->fe2)
        st += (int32_t)(((int64_t)(r->fce ? OPX_CQ(r->fe2, opx_ctl(t, v, r->fce, ms)) : r->fe2) * ms[1]) >> 24);
    if (r->fl2)
        st += (int32_t)(((int64_t)(r->fcl ? OPX_CQ(r->fl2, opx_ctl(t, v, r->fcl, ms)) : r->fl2) * ms[4]) >> 15);
    if (d)                                           /* (KTRK: LOW + raises the cutoff below CENTER, HIGH + above) */
        {
            int64_t sh = (int64_t)(d < 0 ? -r->fkl : r->fkh) * d * 1398101 / 4096;   /* (Q24 octaves; +-Inf: past the range) */
            st += (int32_t)(sh > (20LL << 24) ? 20LL << 24 : sh < -(20LL << 24) ? -(20LL << 24) : sh);
        }
    st = clamp(st, -(1 << 24), (25 << 24) / 2);
    e = fm6_exp2(st - (8 << 24));                    /* 2^(16 + octaves) */
    f = ((uint64_t)(uint32_t)e * 631958u) >> 16;     /* (6.489 Hz as Q32 of fs: 631958) */
    c->k = r->fk;
    if (vm->freso) {
        kq = clamp(kq + vm->freso * 1271, 0, 127139);
        c->k = 131072 - (int32_t)((int64_t)kq * 128450 / 127139);
    }
    c->type = 0, c->fm = 0;
    c->fc0 = f > OPX_FCMAX ? OPX_FCMAX : f < 4096u ? 4096u : (uint32_t)f;
    if (r->fms == 2u && c->fc0 > 1202590843u)        /* (the ladder: at most 0.28 fs, 12.3 kHz: its feedback a sample
                                                      * late, higher with RESO it runs away) */
        c->fc0 = 1202590843u;
    if (r->fms == 2u && c->fc0 > 429496730u)         /* (.. and above 0.1 fs its feedback less, to 55 % at the top:
                                                      * (0.5 fs - fc) / 0.4 fs; 32-bit) */
        kq = (int32_t)((uint32_t)kq * ((2147483648u - c->fc0) >> 17) / 13107u);
    c->kl = kq;
    opx_svf_coef(c, c->fc0);
}
/* M5: operator k's level as V.PATCH moves it (LEVEL: points; PROG LEVEL: the heard ones x (1 + lev)), Q15 */
static int32_t __attribute__((noinline)) opx_vp_amp(const opx_rt_t *r, const opx_vm_t *vm, uint32_t k, uint32_t heard)
{
    int32_t am = r->amp[k];
    if (vm->lv[k]) {
        am = opx_level_amp(clamp(r->lvl[k] + vm->lv[k], 0, 100));
        if (!heard)
            am = (int32_t)clamp((am * r->mlv) >> 15, 0, 32767);
    }
    if (vm->lev && heard)
        am = (int32_t)clamp((int32_t)(((int64_t)am * clamp(32768 + vm->lev, 0, 65536)) >> 15), 0, 32767);
    return am;
}
/* M4, M5: EG1..3 (Q24; while something takes them) and LFO1..3 (Q15, their FADE in) a block on, as V.PATCH moves them:
 * ms[0..2], ms[3..5] (out of the render: most patches run none) */
static void __attribute__((noinline)) opx_srcs(track_t *t, opx_note_t *n, const opx_rt_t *r, const opx_vm_t *vm, int32_t *ms)
{
    int32_t k, lv;
    for (k = 0; k < 3; k++) {
        const opx_lfr_t *l = &r->lf[k];
        ms[k] = !r->eon[k] ? 0 : vm->eg >> (OPX_NOP + k) & 1u ? opx_eg_stepm(&n->egm[k], r, (uint32_t)(OPX_NOP + k), vm)
                                                              : opx_eg_step(&n->egm[k], r, (uint32_t)(OPX_NOP + k));
        lv = 0;
        if (l->on) {
            int32_t fi = l->fi;
            uint32_t tr = (uint32_t)(t - trk) % NTRK;
            if (r->nvp && (vm->lsp[k] || vm->lph[k] || vm->lfd[k])) {   /* (V.PATCH: SPEED moves a KSYNC VOICE LFO's) */
                if (l->ks == 2u) {
                    uint32_t inc = vm->lsp[k] ? (uint32_t)(((uint64_t)l->inc * (uint32_t)fm6_exp2(clamp(vm->lsp[k],
                                   -1023, 1023) * 217926 - (9 << 24))) >> 15) : l->inc;
                    lv = opx_lfo_step(&n->lfo[k], l, inc);
                    if (vm->lph[k] && !OPX_L_RANDOM(l->w))
                        lv = opx_lfo_shape(l->w, (n->lfo[k].ph + vm->lph[k]) >> 16);
                } else
                    lv = vm->lph[k] && !OPX_L_RANDOM(l->w) ? opx_lfo_shape(l->w, (opx_lfoc[tr][k].ph + vm->lph[k]) >> 16)
                                                          : opx_lfov[tr][k];
                if (vm->lfd[k] && l->ks) {
                    int32_t ms_ = 0;                 /* (the FADE time moved: from the rate back to ms) */
                    ms_ = fi < 1 << 24 ? (int32_t)(12174240u / (uint32_t)(fi ? fi : 1)) : 0;
                    ms_ = clamp(ms_ + vm->lfd[k], 0, 9900);
                    fi = ms_ ? (int32_t)(12174240u / (uint32_t)ms_) : 1 << 24;
                }
            } else
                lv = l->ks == 2u ? opx_lfo_step(&n->lfo[k], l, l->inc) : opx_lfov[tr][k];
            if (n->lfade[k] < 1 << 24) {
                n->lfade[k] = n->lfade[k] + fi > 1 << 24 ? 1 << 24 : n->lfade[k] + fi;
                lv = (int32_t)(((int64_t)lv * n->lfade[k]) >> 24);
            }
        }
        ms[3 + k] = lv;
    }
}
static void opx_render(track_t *t, voice_t *v, int32_t *out, uint32_t len, const vmod_t *m)
{
    opx_note_t *n = opx_note_of(t, v);
    const opx_rt_t *r = &opx_rt[(uint32_t)(t - trk) % NTRK];
    uint32_t inc[OPX_NOP], ph[OPX_NOP], q, nact = 0, ord[OPX_NOP], heard = r->out;
    int32_t g[OPX_NOP], dg[OPX_NOP], y[OPX_NOP], yp[OPX_NOP], fbq[OPX_NOP], lf, k, a, da, dc, vf, mf = 32768, e1, lv, pg;
    int32_t ms[6];                                  /* EG1..3 (Q24), LFO1..3 (Q15) this block */
    int32_t vb[CTL], *dst;                          /* M5b: the voice's block before the main filter */
    const int lup = OPX_L_UNI(r->lf[0].w);   /* (M4: LFO1 0 .. +1) */
    opx_vm_t vm;
    if (!n || !n->live || len != CTL)
        return;
    if (!v->gate && n->down) {                      /* note-off: every envelope to its release */
        for (k = 0; k < OPX_NOP; k++)
            if (n->eg[k].stage < OPX_REL)
                n->eg[k].stage = OPX_REL, n->eg[k].from = n->eg[k].lvl, n->eg[k].x = 0;
        for (k = 0; k < 3; k++)
            if (n->egm[k].stage < OPX_REL)
                n->egm[k].stage = OPX_REL, n->egm[k].from = n->egm[k].lvl, n->egm[k].x = 0;
        n->down = 0;
    }
    if (r->nvp)                                     /* M5: V.PATCH, from the sources as they stand */
        opx_vp_eval(t, v, n, r, &vm);
    else
        vm.eg = 0, vm.pitch = vm.lev = vm.fcut = vm.freso = 0;
    lf = fm6_note_logfreq(m->pitch16, m->fine) + r->poff;   /* (M4: PROG TRANS, OCT on the note: not the FIXED ones) */
    ms[0] = ms[1] = ms[2] = ms[3] = ms[4] = ms[5] = 0;
    if (r->son)
        opx_srcs(t, n, r, &vm, ms);
    e1 = ms[0], lv = ms[3];
    pg = r->cpg && r->plfo ? OPX_CQ(r->plfo, opx_ctl(t, v, r->cpg, ms)) : r->plfo;   /* PROG LFO1 INT x its CTRL */
#if OPX_FELUCCA_MACROS
    vf = clamp((m->shape - (64 << 8)) >> 2, -4096, 4096);   /* SHP: the feedback, x0.5 .. x1.5 at the ends */
    if (m->cutoff)                                  /* FLT (ENV / LFO -> FLT, the matrix's CUT): MLVL, per voice */
        mf = fm6_exp2(clamp(m->cutoff >> 8, -96, 96) * (1 << 20) - (9 << 24));
#else
    vf = 0;                                         /* (Felucca's FLT and SHP routes: off on OPX, as its macros) */
#endif
    for (k = 0; k < OPX_NOP; k++) {
        int32_t lvl = vm.eg >> k & 1u ? opx_eg_stepm(&n->eg[k], r, (uint32_t)k, &vm) : opx_eg_step(&n->eg[k], r, (uint32_t)k), g2;
        /* M4: P MOD (EG1, LFO1 and PROG LFO1 INT: Q24 octaves), VEL's FINE / COARSE (this note's) */
        int32_t lm = 0, d = 0;
        if (r->pmf[k]) {                             /* (no route on: none of it) */
            int32_t pe = r->pme[k], pl = r->pml[k];
            lm = r->lml[k];
            if (r->cpe[k] && pe)                     /* (CTRL: the source x the INT) */
                pe = OPX_CQ(pe, opx_ctl(t, v, r->cpe[k], ms));
            if (r->cpl[k] && pl)
                pl = OPX_CQ(pl, opx_ctl(t, v, r->cpl[k], ms));
            if (r->clm[k] && lm)
                lm = OPX_CQ(lm, opx_ctl(t, v, r->clm[k], ms));
            if (pe && e1)
                d = (int32_t)(((int64_t)pe * e1) >> 24);
            if (pl + pg && lv)
                d += (int32_t)(((int64_t)(pl + pg) * lv) >> 15);
        }
        if (r->nvp)                                  /* M5: V.PATCH's PITCH, the voice's and the operator's */
            d += vm.pitch + vm.op[k];
        uint32_t f = r->finc[k];
        if (!f) {
            int32_t x = lf + r->lg[k] + d;
            uint32_t fq = r->fine[k] + n->vfq[k];    /* (Q16; x has COARSE: P MOD VEL's addition over COARSE) */
            uint64_t q = ((uint64_t)((uint32_t)fm6_freq(x) << 8) * fq) >> 16;
            f = q > OPX_FCMAX ? OPX_FCMAX : (uint32_t)q;
        } else if (d) {                              /* (FIXED: its frequency moved; below 0.45 of the sample rate) */
            uint64_t q = ((uint64_t)f * (uint32_t)fm6_exp2(clamp(d, -20 << 24, 5 << 24) - (9 << 24))) >> 15;
            f = q > OPX_FCMAX ? OPX_FCMAX : (uint32_t)q;
        }
        inc[k] = f;
        g2 = mulq15(r->amp[k], n->vg[k]);
        if (r->nvp && (vm.lv[k] || (vm.lev && ((heard >> k) & 1u))))   /* M5: V.PATCH's LEVEL, PROG LEVEL */
            g2 = mulq15(opx_vp_amp(r, &vm, (uint32_t)k, (heard >> k) & 1u), n->vg[k]);
        if (lm && lv) {                       /* M4: LMOD LFO1: the level dips by INT at the LFO's far end, never
                                                      * above the operator's own (- INT: the LFO turned over) */
            int32_t al = lm < 0 ? -lm : lm, u = lm < 0 ? -lv : lv;
            int32_t dip = lup ? (lm < 0 ? lv : 32767 - lv) : (32767 - u) >> 1;
            g2 = (int32_t)(((int64_t)g2 * (32767 - ((al * clamp(dip, 0, 32767)) >> 15))) >> 15);
        }
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
        if (r->nvp && vm.fb[k] && r->md[k] == OPX_M_FM)   /* M5: V.PATCH's FM FB */
            fbq[k] = clamp(r->fbp[k] + vm.fb[k], 0, 100) * 417;
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
    memset(vb, 0, sizeof vb);                       /* M5b: with the main filter on, the voice into its own block
                                                     * (cleared either way: no branch out of line, 32 stores) */
    dst = r->fon ? vb : out;
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
#if FELUCCA_OPX_ONLY
                        opx_ecb[kk].dacc = n->dacc, opx_ecb[kk].dsub = n->dsub;
#endif
                    }
                }
            }
        if (back || opx_sample_order) {
            dc = wv ? opx_loop_wave(r, dst, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc, tb,
                                    (const int16_t (*)[CTL])nb, (const int32_t (*)[CTL])opx_obuf, n->fs)
                    : opx_loop_sine(r, dst, ord, nact, ph, inc, g, dg, y, yp, fbq, heard, a, da, dc);
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
            dc = opx_mix(dst, hp, nh, a, da, dc);
        }
    }
    n->dc = dc;
    if (r->fon) {
        opx_fc_t fc;
        opx_mflt_coef(t, v, r, ms, &vm, &fc);
        opx_mflt(r, &fc, n->fst, vb, out);
    }
    for (k = 0; k < OPX_NOP; k++) {
        if (r->md[k] == OPX_M_EFFECT && opx_ecb[k].dl && opx_ecb[k].dl == opx_dline(t, v))   /* (the line's next place) */
            n->dw = (uint16_t)opx_ecb[k].dw;
#if FELUCCA_OPX_ONLY
            n->dacc = opx_ecb[k].dacc, n->dsub = opx_ecb[k].dsub;
#endif
        n->y[k] = y[k];
        n->yp[k] = yp[k];
    }
}

#include "opx_fx.c"               /* M5c: the program effects FX1..3 (the engine's post) */

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
static const char *const N_OPX_SLOT[] = {
#define OPX_FACTORY_SLOTS
#include "opx_factory.h"
#undef OPX_FACTORY_SLOTS
    "OWN", "INIT", 0};
_Static_assert(NELEM(N_OPX_ALG) == OPX_NALG + 3u, "an ALG name per value");
_Static_assert(NELEM(N_OPX_SLOT) == 129u, "a SLOT name per value (F1..F126, OWN, INIT), then 0");

/* {ALG, ATK, DEC, MLVL, FB, MRAT, DTUN, SLOT}: each factory patch as it is */
static const preset_t OPX_PRESETS[] = {
#define OPX_FACTORY_PRESETS
#include "opx_factory.h"
#undef OPX_FACTORY_PRESETS
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
        {"SLOT", F_INT, 0, 127, 0, N_OPX_SLOT, 0},   /* (F1..F126 0..125: this firmware's, the others skipped; OWN, INIT) */
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
    .post = opx_post,
};
