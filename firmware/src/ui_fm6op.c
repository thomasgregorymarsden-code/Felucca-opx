/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6's operator pages (EDIT family, an FM6 track only, after EDIT 2; Discussion #76): the six operators of the track's
 * own patch (eng_fm6.c fm6_patch) edited on the device. KNOB 1 is the operator (OP 1..6, as the MOD page's SLOT: one
 * choice for the three pages, kept while one goes round them), KNOB 2..4 its bytes:
 *   OPERATOR    OP  RATIO / FIXED (the coarse frequency: 0.50, 1..31; fixed 1 10 100 1000 HZ)  FINE (0..99)  LEVEL (0..99)
 *   OP ENV      OP  STAGE (1 ATK, 2 DEC, 3 SUS, 4 REL: the envelope's stage KNOB 3 / 4 edit)  RATE  LEVEL (0..99)
 *   OPERATOR 2  OP  MODE (RATIO / FIXED)  DTUN (-7..+7)  VEL (the velocity sensitivity, 0..7)
 * The panel: OPERATOR and OPERATOR 2 the algorithm chart (ui_graph.c graph_fm6) with the operator picked in the accent
 * and its frequency top right ("x1.50", "440 HZ"); OP ENV its envelope (graph_fm6env), the stage picked in the accent.
 * A detent writes one byte of the patch (fm6_set_patch: the audio ISR takes it at its next block) and makes the patch
 * the track's own: SLOT OWN (as a patch from the editor; a factory F n edited is OWN from then on). Each knob gesture is
 * one undo (SAVE held: the sound before it, the patch and SLOT with it; ui.c edit_undo_take). The patch is saved with
 * projects and user presets as before (no new format, no new parameter ids: nothing here is automatable or lockable).
 * The values shown are the patch's own: EDIT 1 / 2's macros still apply on top of it as before (MLVL moves the
 * modulators' LEVEL, MRAT their RATIO, MEG their RATEs, VMOD their VEL, DTUN the carriers' pitch, FB the feedback, ALG
 * another algorithm: the chart and which operators are carriers follow ALG). Included by ui_graph.c (after icons.c: the
 * cards' icons); the cards: ui_draw.c, the knobs: ui_input.c edit_param. */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* (ui_input.c) */
static uint8_t fop_op;                               /* the operator shown: 0..5 = OP 1..6 (the patch holds OP 6 first) */
static uint8_t fop_stg;                              /* OP ENV: the stage KNOB 3 / 4 edit, 0..3 */

enum { FO_OP, FO_STG, FO_CRS, FO_FINE, FO_LVL, FO_MODE, FO_DTUN, FO_VEL, FO_RATE, FO_ELVL };
static const uint8_t FO_PAGE[3][4] = {
    {FO_OP, FO_CRS, FO_FINE, FO_LVL},                /* OPERATOR */
    {FO_OP, FO_MODE, FO_DTUN, FO_VEL},               /* OPERATOR 2 */
    {FO_OP, FO_STG, FO_RATE, FO_ELVL},               /* OP ENV */
};
/* per field: its byte in an operator's 21 (RATE / LEVEL: + the stage) */
static const uint8_t FO_OFF[10] = {0, 0, FP_FC, FP_FF, FP_OL, FP_MODE, FP_DET, FP_KVS, FP_R1, FP_L1};

static int xop_page(uint32_t g) { return g >= GR_OPXL && g <= GR_OPXF; }   /* (OPX fork: OPERATORS, ENVELOPE, OP LEVELS,
                                                                             * HOME's OP MIX, EG1, LFO1) */
static int fop_page(uint32_t g) { return (g >= GR_FMOP && g <= GR_FMENV) || xop_page(g); }
static int fop_ok(void) { return TSEL->eng_req % NENGINES == ENGI_FM6; }
static uint32_t fop_field(uint32_t slot)
{
    uint32_t g = cur_page()->graph;
    return FO_PAGE[g == GR_FMOP2 ? 1u : g == GR_FMENV ? 2u : 0u][slot & 3u];
}
/* the selected operator's 21 bytes in track tr's patch */
static const uint8_t *fop_bytes(uint32_t tr) { return fm6_patch[tr % NTRK] + (5u - fop_op % 6u) * FP_OP; }
/* field f's byte in the 155-byte voice (the selected operator, the stage) */
static uint32_t fop_byte(uint32_t f)
{
    return (5u - fop_op % 6u) * FP_OP + FO_OFF[f] + (f == FO_RATE || f == FO_ELVL ? fop_stg & 3u : 0u);
}

/* byte b of the selected track's patch = v: the track's own patch now (SLOT OWN), one undo per knob gesture */
static void fop_write(uint32_t b, uint32_t v)
{
    uint32_t tr = song.sel % NTRK;
    track_t *t = TSEL;
    uint8_t p[FP_SIZE + 1u];
    if (fm6_patch[tr][b] == v)
        return;
    edit_undo_take(t, 0x1000u | b, UNDO_SOUND);      /* (a knob turned on, this byte: one copy) */
    memcpy(p, fm6_patch[tr], FP_SIZE);
    p[b] = (uint8_t)v;
    fm6_set_patch(tr, p);
    fm6_own_ok &= (uint8_t)~(1u << tr);              /* (an own patch kept aside for SLOT: this one replaces it) */
    fm6_slot[tr] = FM6_OWN;                          /* (fm6_poll: nothing reloads over it) */
    t->p[P_E7] = FM6_OWN;
    motion_undo_done(t);
}

/* KNOB slot turned by steps on an operator page */
static void fop_knob(uint32_t slot, int32_t steps)
{
    uint32_t f = fop_field(slot), b;
    int32_t max;
    if (!fop_ok() || !steps)
        return;
    if (f == FO_OP) {
        fop_op = (uint8_t)clamp((int32_t)fop_op + steps, 0, 5);
        return;
    }
    if (f == FO_STG) {
        fop_stg = (uint8_t)clamp((int32_t)fop_stg + steps, 0, 3);
        return;
    }
    b = fop_byte(f);
    max = (int32_t)fm6_max(b);
    fop_write(b, (uint32_t)clamp((int32_t)fm6_patch[song.sel % NTRK][b] + accel(EN_K1 + slot, steps, max), 0, max));
}

/* "w.ff" (frac: digits places) */
static void fop_dec(char *s, uint32_t w, uint32_t frac, uint32_t digits)
{
    fmt_int(s, (int32_t)w);
    s += str_len(s);
    *s++ = '.';
    if (digits > 1u)
        *s++ = (char)('0' + frac / 10u % 10u);
    *s++ = (char)('0' + frac % 10u);
    *s = 0;
}
/* the operator's frequency (op: its 21 bytes): "x1.50" (ratio: COARSE, 0 = 0.50, x (1 + FINE / 100)) or "440 HZ"
 * (fixed: 10 ^ (COARSE & 3 + FINE / 100) Hz, as fm6_core.c fm6_op_freq); s holds 12 */
static void fop_freq(const uint8_t *op, char *s)
{
    uint32_t c = op[FP_FC] & 31u, f = op[FP_FF] % 100u, m = 1000u, k;
    if (!op[FP_MODE]) {
        uint32_t r = (c ? c * 100u : 50u) * (100u + f) / 100u;   /* hundredths */
        s[0] = 'x';
        fop_dec(s + 1, r / 100u, r % 100u, 2u);
        return;
    }
    for (k = 0; k < f; k++)
        m = (m * 67063u + 32768u) >> 16;            /* x 10 ^ 0.01 (Q16): m = 1000 .. 9772 */
    for (k = 0; k < (c & 3u); k++)
        m *= 10u;                                    /* mHz */
    if (m < 10000u)
        fop_dec(s, m / 1000u, m % 1000u / 10u, 2u);
    else if (m < 100000u)
        fop_dec(s, m / 1000u, m % 1000u / 100u, 1u);
    else if (m < 1000000u)
        fmt_int(s, (int32_t)(m / 1000u));
    else
        fop_dec(s, m / 1000000u, m % 1000000u / 10000u, 2u);
    str_cpy(s + str_len(s), m < 1000000u ? " HZ" : " KHZ", 5);
}

/* card slot of an operator page: its label, value (12), unit, gauge (0..1000, -1 none), icon; 0 = shown DIM (a level
 * at 0, the operator silent) */
static int fop_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    static const char *const STG[4] = {"ATK", "DEC", "SUS", "REL"};
    uint32_t f = fop_field(slot), v;
    const uint8_t *op = fop_bytes(song.sel);
    *unit = "";
    *ratio = -1;
    if (f == FO_OP) {
        *label = "OP";
        fmt_int(val, (int32_t)fop_op + 1);
        *unit = "/6";
        *ratio = (int32_t)fop_op * 200;
        *icon = ICON_ALGORITHM;
        return 1;
    }
    if (f == FO_STG) {
        *label = "STAGE";
        fmt_int(val, (int32_t)fop_stg + 1);
        *unit = STG[fop_stg & 3u];
        *ratio = (int32_t)fop_stg * 333;
        *icon = ICON_ENV;
        return 1;
    }
    v = fm6_patch[song.sel % NTRK][fop_byte(f)];
    *ratio = (int32_t)(v * 1000u / fm6_max(fop_byte(f)));
    fmt_int(val, (int32_t)v);
    switch (f) {
    case FO_CRS:
        *label = op[FP_MODE] ? "FIXED" : "RATIO";
        *icon = ICON_RATIO;
        if (op[FP_MODE]) {
            static const char *const HZ[4] = {"1", "10", "100", "1000"};
            str_cpy(val, HZ[v & 3u], 12);
            *unit = "HZ";
        } else if (!v) {
            str_cpy(val, "0.50", 12);
        }
        return 1;
    case FO_FINE:
        *label = "FINE";
        *icon = ICON_TUNE;
        return 1;
    case FO_LVL:
    case FO_ELVL:
        *label = "LEVEL";
        *icon = ICON_LEVEL;
        return f == FO_ELVL || v != 0u;
    case FO_MODE:
        *label = "MODE";
        str_cpy(val, v ? "FIXED" : "RATIO", 12);
        *icon = ICON_RATIO;
        return 1;
    case FO_DTUN:
        *label = "DTUN";
        val[0] = v > 7u ? '+' : 0;
        fmt_int(val + (v > 7u), (int32_t)v - 7);
        *icon = ICON_DETUNE;
        return 1;
    case FO_VEL:
        *label = "VEL";
        *icon = ICON_ACCENT;
        return 1;
    default:                                         /* FO_RATE */
        *label = "RATE";
        *icon = ICON_RATE;
        return 1;
    }
}

#if FELUCCA_OPX
/* ------------------------------------------------------------------------------------------- OPX (OPX fork) ---
 * EDIT > OPERATORS, an OPX track's one page over its patch (opx_patch.c opx_pk, the packed record): one long list of
 * the picked operator's parameters in the opsix's page order, then the patch's own (ALG, ALG FB):
 *   MODE   MODE (FM, RING MOD, FILTER, FILTER FM, WAVE FOLDER, EFFECT, BYPASS, MUTE), WAVE (not on BYPASS / MUTE),
 *          then the mode's own (MODE p.43): FM  FB WIDTH; RING MOD  DEPTH SHAPE; FILTER  TYPE OSC MIX CUTOFF RESO;
 *          FILTER FM  TYPE CUTOFF RESO; WAVE FOLDER  GAIN OSC MIX BIAS; EFFECT  FX OSC MIX and the effect's two values
 *   PITCH  FREQ (RATIO / FIXED) RATIO (FIXED: the frequency) FINE DTUN TRANS
 *   EG     LEVEL ATK DEC SUS REL CURVE VEL
 *   KTRK   LOW CENTER HIGH L CRV H CRV (p.53)
 *   USER   TO OP1 .. TO OP6 (the picked operator's own row: "(SELF)", its self feedback), OUT (as ALGO's matrix: its
 *          LEVEL while heard, OFF), the matrix's values as numbers; DIM unless the track plays USER
 *   ALGO   ALG ALG FB
 *   VOICE  the track's VOICE pages (VOICE GLIDE GLIDE MODE PRIORITY, ALLOC DETUNE PAN MUTE, SPREAD): Felucca's own
 *          parameters (TSEL->p, as those pages edit them: motion, MIDI LEARN), which an OPX track shows here only (ui.c
 *          page_visible)
 * Each section is its own list, one on show at a time (xop_sec); the picked operator's: its MODE section follows its
 * mode (xop_list). Each section keeps its own picked row.
 *   KNOB 1  the operator (OP 1..6; shared with FM6's pages: fop_op)
 *   KNOB 2  PAGE: the section (MODE .. VOICE; no wrap)
 *   KNOB 3  the row, within the section
 *   KNOB 4  the row's value (a list's value: one a detent)
 * The panel rolls: 8 rows on show (the right half; the algorithm or the matrix in the left, ui_graph.c), a row of
 * "..." at the bottom (top) while more are below (above); the picked row moves down to the last whole row, then the
 * list moves up under it. A detent of KNOB 4 writes one field (opx_edit: the patch becomes the track's own, SLOT OWN),
 * one undo per knob gesture (SAVE held). The user algorithm's matrix is ALGO's too (ui_fm6op.c xmat_*) */
enum { XS_MODE, XS_PITCH, XS_EG, XS_KTRK, XS_USER, XS_ALGO, XS_EG1, XS_LFO1, XS_PROG, XS_FLT, XS_VP, XS_VOICE, XS_N };
enum { XR_MODE, XR_WAVE, XR_FB, XR_WIDTH, XR_RDEPTH, XR_RSHAPE, XR_FTYPE, XR_FMIX, XR_CUT, XR_RESO, XR_FGAIN, XR_GMIX,
       XR_BIAS, XR_FX, XR_XMIX, XR_FXE, XR_FXF,                                            /* MODE */
       XR_FREQ, XR_RATIO, XR_FINE, XR_DTUN, XR_TRANS,                                      /* PITCH */
       XR_PME, XR_PMEC, XR_PML, XR_PMLC, XR_PMV, XR_PMD,                                   /* .. P MOD (M4) */
       XR_LVL, XR_ATK, XR_DEC, XR_SUS, XR_REL, XR_CRV, XR_VEL, XR_LML, XR_LMLC,            /* EG, LMOD (M4) */
       XR_KLOW, XR_KCTR, XR_KHIGH, XR_KLC, XR_KHC,                                         /* KTRK */
       XR_TO0, XR_TO5 = XR_TO0 + 5, XR_OUT,                                                /* USER */
       XR_ALG, XR_ALGFB,                                                                   /* ALGO */
       XR_E1A, XR_E1D, XR_E1S, XR_E1R, XR_E1C,                                             /* EG1 (M4) */
       XR_LW, XR_LT, XR_LS, XR_LK, XR_LP, XR_LF,                                           /* LFO1 (M4) */
       XR_GL, XR_GLC, XR_GT, XR_GO,                                                        /* PROG PITCH (M4) */
       XR_V0, XR_V8 = XR_V0 + 8,                                                          /* VOICE */
       XR_ESEL, XR_LSEL,                                                                   /* M5: which EG, LFO */
       XR_VSEL, XR_VSS, XR_VSI, XR_VINT, XR_VCTL, XR_VDS, XR_VDI,                          /* M5: V.PATCH */
       XR_MFT, XR_MFE, XR_MFC, XR_MFR, XR_MFE2, XR_MFE2C, XR_MFKL, XR_MFKC, XR_MFKH, XR_MFL2, XR_MFL2C,   /* M5b: FILTER */
       XR_N };
static const char *const XS_NAME[XS_N] = {"MODE", "PITCH", "EG", "KTRK", "USER", "ALGO", "EG1-3", "LFO1-3", "PROG",
                                          "FILTER", "V.PATCH", "VOICE"};
static const char *const N_OPX_MFLT[11] = {"LPF 12", "LPF 24", "LPF MS-20", "LPF POLY6", "HPF 12", "HPF 24", "HPF MS-20",
                                           "BPF 6", "BPF 12", "BRF 6", "BRF 12"};   /* (M5b: p.38) */
static uint8_t xop_egs, xop_lfs, xop_vps;   /* M5: the EG (0..2), LFO (0..2), V.PATCH (0..11) the rows show */
/* M4: the patch's own rows (EG1, LFO1, PROG PITCH): their fields (XR_LS: SPEED, or SYNC with TEMPO on) */
static const uint16_t XP_F[XR_V0 - XR_E1A] = {OPXF_EG1_ATK, OPXF_EG1_DEC, OPXF_EG1_SUS, OPXF_EG1_REL, OPXF_EG1_CURVE,
    OPXF_LFO1_WAVE, OPXF_LFO1_TEMPO, OPXF_LFO1_SPEED, OPXF_LFO1_KSYNC, OPXF_LFO1_PHASE, OPXF_LFO1_FADE, OPXF_PROG_LFO1,
    OPXF_PROG_LFO1_CTRL, OPXF_PROG_TRANS, OPXF_PROG_OCT};
static const char *const N_OPX_LFOW[OPX_NLFOW] = {"TRI", "SAW DOWN", "SAW UP", "SQR", "SIN", "S/H", "GTR", "X.TRI",
    "X.SAW DOWN", "X.SAW UP", "TRI4", "TRI6", "SAW4 DOWN", "SAW6 DOWN", "S.RND TIME", "S.RND LVT", "RND LEVEL",
    "RND TIME", "RND LVTIME", "TRI+", "SAW DOWN+", "SAW UP+", "SQR+"};   /* (p.56; S.RND LVTIME shortened) */
static const char *const N_OPX_CTRL[OPX_NCTRL] = {"OFF", "MOD WHL", "PITCH", "PITCH+", "PITCH-", "PEDAL", "NOTE", "VEL",
    "EXP VEL", "REL VEL", "A.TOUCH", "EG1", "EG2", "EG3", "LFO1", "LFO2", "LFO3"};   /* (p.85, shortened for the list) */
static const uint8_t XV_ID[9] = {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE, P_PAN, P_MUTE, P_SPRD};
static int xop_voice(uint32_t k) { return k >= XR_V0 && k <= XR_V8; }
static const char *const N_OPX_MODE[OPX_NMODE] = {"FM", "RING MOD", "FILTER", "FILTER FM", "WAVE FOLDER", "EFFECT",
                                                  "BYPASS", "MUTE"};
static const char *const N_OPX_FLT[12] = {"LPF", "HPF", "BPF", "BRF", "MG LPF12", "MG LPF24", "MG HPF12", "MG HPF24",
                                          "MG BPF6", "MG BPF12", "MS-20 LPF", "MS-20 HPF"};   /* (p.34) */
static const char *const N_OPX_FX[10] = {"PEAK EQ", "SHELV EQ", "PHASER", "SHORT DLY", "COMB", "DIST", "DRIVE", "DEC",
                                         "WAVESHAPER", "PUNCH"};                                 /* (p.34) */
#define XL_MAX 64u
/* the pages over the list: OPERATORS (every row), ENVELOPE (ATK DEC SUS REL of the picked operator, its EG drawn at the
 * left: graph_opxenv), OP LEVELS (a row per operator, its LEVEL; the row is the picked operator). Each its own row */
enum { XV_ALL, XV_ENV, XV_EG1, XV_LFO, XV_LVL, XV_KNB };   /* (XV_KNB: HOME > OP MIX, the six LEVELs on six knobs; EG1,
                                                         * LFO1 (M4): the patch's, no operator) */
static uint32_t xop_view(void)
{
    uint32_t g = cur_page()->graph;
    return g == GR_OPXE ? XV_ENV : g == GR_OPXV ? XV_LVL : g == GR_OPXK ? XV_KNB : g == GR_OPXP ? XV_EG1 :
           g == GR_OPXF ? XV_LFO : XV_ALL;
}
static int xop_noop(void) { return xop_view() == XV_EG1 || xop_view() == XV_LFO; }   /* (no operator to pick) */
static uint8_t xop_lis[6], xop_tops[6];
static uint8_t xop_sec, xop_slis[XS_N], xop_stops[XS_N];   /* OPERATORS: the section on show (KNOB 2), each one's row and
                                                         * first row on show */
static uint8_t *xop_lip(void) { return xop_view() == XV_ALL ? &xop_slis[xop_sec % XS_N] : &xop_lis[xop_view()]; }
static uint8_t *xop_topp(void) { return xop_view() == XV_ALL ? &xop_stops[xop_sec % XS_N] : &xop_tops[xop_view()]; }
#define xop_li (*xop_lip())                          /* KNOB 3: the picked row */
#define xop_top (*xop_topp())                        /* the first row on show */

static int xop_ok(void) { return TSEL->eng_req % NENGINES == ENGI_OPX; }
static int32_t xop_get(uint32_t tr, uint32_t f1) { return opx_get(opx_pk[tr % NTRK], OPXF_OP(fop_op % 6u, f1)); }
static uint32_t xop_mode(uint32_t tr) { return (uint32_t)xop_get(tr, OPXF_OP1_MODE) % OPX_NMODE; }
static int32_t xop_wave(uint32_t tr) { return xop_get(tr, OPXF_OP1_WAVE); }
static int xop_fixed(uint32_t tr) { return xop_get(tr, OPXF_OP1_FREQ_FIXED) != 0; }
static uint32_t xop_sec_k(uint32_t k)               /* a kind's section */
{
    if (k >= XR_ESEL)                                /* (M5's rows, after VOICE's in the enum) */
        return k == XR_ESEL ? XS_EG1 : k == XR_LSEL ? XS_LFO1 : k >= XR_MFT ? XS_FLT : XS_VP;
    return k < XR_FREQ ? XS_MODE : k < XR_LVL ? XS_PITCH : k < XR_KLOW ? XS_EG : k < XR_TO0 ? XS_KTRK :
           k <= XR_OUT ? XS_USER : k < XR_E1A ? XS_ALGO : k < XR_LW ? XS_EG1 : k < XR_GL ? XS_LFO1 : k < XR_V0 ? XS_PROG :
           XS_VOICE;
}
/* the picked operator's list (its kinds, XR_*): OPERATORS the section on show (MODE by the operator's mode); the
 * other views their own rows. Returns the count */
static uint32_t xop_list(uint8_t *rw)
{
    static const uint8_t MODE_ROWS[OPX_NMODE][5] = {
        {XR_FB, XR_WIDTH}, {XR_RDEPTH, XR_RSHAPE}, {XR_FTYPE, XR_FMIX, XR_CUT, XR_RESO}, {XR_FTYPE, XR_CUT, XR_RESO},
        {XR_FGAIN, XR_GMIX, XR_BIAS}, {XR_FX, XR_XMIX, XR_FXE, XR_FXF}, {0}, {0}};
    uint32_t n = 0, md = xop_mode(song.sel % NTRK), k;
    if (xop_view() == XV_ENV) {
        rw[0] = XR_ATK, rw[1] = XR_DEC, rw[2] = XR_SUS, rw[3] = XR_REL, rw[4] = XR_CRV;
        return 5;
    }
    if (xop_view() == XV_EG1 || xop_view() == XV_LFO) {   /* (an EG: its five; an LFO: its six, then LFO1's PROG
                                                         * PITCH's) */
        uint32_t a = xop_view() == XV_EG1 ? XR_E1A : XR_LW;
        uint32_t b = xop_view() == XV_EG1 ? XR_LW : xop_lfs ? XR_GL : XR_V0;
        for (k = a; k < b; k++)
            rw[n++] = (uint8_t)k;
        return n;
    }
    if (xop_view() >= XV_LVL) {                      /* (row k: operator k's) */
        for (k = 0; k < 6u; k++)
            rw[k] = XR_LVL;
        return 6;
    }
    if (xop_sec % XS_N == XS_MODE) {
        rw[n++] = XR_MODE;
        if (md != OPX_M_BYPASS && md != OPX_M_MUTE)
            rw[n++] = XR_WAVE;
        for (k = 0; k < 5u && MODE_ROWS[md][k]; k++)
            rw[n++] = MODE_ROWS[md][k];
        return n;
    }
    if (xop_sec % XS_N == XS_EG1 || xop_sec % XS_N == XS_LFO1) {   /* (M5: which one, then its rows) */
        rw[n++] = xop_sec % XS_N == XS_EG1 ? XR_ESEL : XR_LSEL;
        for (k = xop_sec % XS_N == XS_EG1 ? XR_E1A : XR_LW; k < (xop_sec % XS_N == XS_EG1 ? XR_LW : XR_GL); k++)
            rw[n++] = (uint8_t)k;
        return n;
    }
    for (k = XR_FREQ; k < XR_N; k++)
        if (xop_sec_k(k) == xop_sec % XS_N)
            rw[n++] = (uint8_t)k;
    return n;
}
static uint32_t xop_n(void) { uint8_t rw[XL_MAX]; return xop_list(rw); }
static uint32_t xop_row(void)
{
    uint32_t n = xop_n();
    if (xop_view() >= XV_LVL)                        /* (OP LEVELS, OP MIX: the row is the operator) */
        return fop_op % 6u;
    return xop_li < n ? xop_li : n - 1u;
}
static uint32_t xop_kind(uint32_t i) { uint8_t rw[XL_MAX]; uint32_t n = xop_list(rw); return rw[i < n ? i : n - 1u]; }
static uint32_t xop_find(uint32_t kind)              /* the row of a kind; on OPERATORS its section put on show
                                                     * (tests). None: the count */
{
    uint8_t rw[XL_MAX];
    uint32_t n, i;
    if (xop_view() == XV_ALL)
        xop_sec = (uint8_t)xop_sec_k(kind);
    n = xop_list(rw);
    for (i = 0; i < n && rw[i] != kind; i++)
        ;
    return i;
}
static uint32_t xop_sec_of(uint32_t i) { return xop_sec_k(xop_kind(i)); }
/* row i's kind (XR_*) and its field (XR_TO*: the amount the picked operator sends to that one; its own: its self
 * feedback. XR_OUT: its OUT bit, though the row shows and sets xop_out) */
static uint32_t xop_rowf(uint32_t i, uint32_t tr, uint32_t *field)
{
    static const uint16_t F[XR_TO0] = {OPXF_OP1_MODE, OPXF_OP1_WAVE, OPXF_OP1_FM_FB, OPXF_OP1_FM_WIDTH,
        OPXF_OP1_RING_DEPTH, OPXF_OP1_RING_SHAPE, OPXF_OP1_FLT_TYPE, OPXF_OP1_FLT_OSCMIX, OPXF_OP1_FLT_CUTOFF,
        OPXF_OP1_FLT_RESO, OPXF_OP1_FOLD_GAIN, OPXF_OP1_FOLD_OSCMIX, OPXF_OP1_FOLD_BIAS, OPXF_OP1_FX_TYPE,
        OPXF_OP1_FX_OSCMIX, OPXF_OP1_FX_E, OPXF_OP1_FX_F, OPXF_OP1_FREQ_FIXED, OPXF_OP1_COARSE, OPXF_OP1_FINE,
        OPXF_OP1_DETUNE, OPXF_OP1_TRANS, OPXF_OP1_PMOD_EG1, OPXF_OP1_PMOD_EG1_CTRL, OPXF_OP1_PMOD_LFO1,
        OPXF_OP1_PMOD_LFO1_CTRL, OPXF_OP1_PMOD_VEL, OPXF_OP1_PMOD_VEL_DEST, OPXF_OP1_LEVEL, OPXF_OP1_EG_ATK,
        OPXF_OP1_EG_DEC, OPXF_OP1_EG_SUS, OPXF_OP1_EG_REL, OPXF_OP1_EG_CURVE, OPXF_OP1_LMOD_VEL, OPXF_OP1_LMOD_LFO1,
        OPXF_OP1_LMOD_LFO1_CTRL, OPXF_OP1_KTRK_LOW, OPXF_OP1_KTRK_CENTER,
        OPXF_OP1_KTRK_HIGH, OPXF_OP1_KTRK_LCRV, OPXF_OP1_KTRK_HCRV};
    uint32_t k = xop_kind(i), op = xop_view() >= XV_LVL ? i % 6u : fop_op % 6u;   /* (OP LEVELS, OP MIX: row i, op i) */
    if (k < XR_TO0)
        *field = (k == XR_RATIO || k == XR_FINE) && xop_fixed(tr) ? OPXF_OP(op, OPXF_OP1_FIXED_FREQ) : OPXF_OP(op, F[k]);
    else if (k <= XR_TO5)
        *field = opxf_ua(op, k - XR_TO0);
    else if (k == XR_OUT)
        *field = OPXF_UA_OUT(op);
    else if (xop_voice(k) || k == XR_ESEL || k == XR_LSEL || k == XR_VSEL)
        *field = 0;                                  /* (VOICE: the track's parameter XV_ID, not the patch's; M5: which
                                                      * EG, LFO, V.PATCH the rows show) */
    else if (k >= XR_MFT)                            /* M5b: the main filter (its fields in the list's order) */
        *field = OPXF_FLT_TYPE + (k == XR_MFT ? 0u : k == XR_MFE ? 1u : k == XR_MFC ? 2u : k == XR_MFR ? 3u : k == XR_MFE2 ? 4u :
                 k == XR_MFE2C ? 5u : k == XR_MFKL ? 6u : k == XR_MFKC ? 7u : k == XR_MFKH ? 8u : k == XR_MFL2 ? 9u : 10u);
    else if (k > XR_VSEL) {                          /* M5: V.PATCH n's SRC, INT, CTRL, DEST */
        uint32_t b = OPXF_VP1_SRC + (uint32_t)(xop_vps % 12u) * (OPXF_VP2_SRC - OPXF_VP1_SRC);
        *field = k <= XR_VSI ? b : k == XR_VINT ? b + 1u : k == XR_VCTL ? b + 2u : b + 3u;
    } else if (k >= XR_E1A && k < XR_LW)             /* M4, M5: EG1..3 */
        *field = XP_F[k - XR_E1A] + (uint32_t)(xop_egs % 3u) * (OPXF_EG2_ATK - OPXF_EG1_ATK);
    else if (k >= XR_LW && k < XR_GL) {              /* M4, M5: LFO1..3 (SPEED: SYNC with TEMPO on) */
        uint32_t o = (uint32_t)(xop_lfs % 3u) * (OPXF_LFO2_WAVE - OPXF_LFO1_WAVE);
        *field = (k == XR_LS && opx_get(opx_pk[tr], OPXF_LFO1_TEMPO + o) ? OPXF_LFO1_SYNC : XP_F[k - XR_E1A]) + o;
    } else if (k >= XR_E1A)                          /* M4: PROG PITCH */
        *field = XP_F[k - XR_E1A];
    else
        *field = k == XR_ALG ? OPXF_ALGO : OPXF_PROG_ALG_FB;
    return k;
}
/* the rolling window: XL_SHOW places; "..." at the bottom (top) when rows are hidden below (above). Keeps the picked
 * row on an item place, moving the window only when it would land on a "..." (or past the end) */
#define XL_SHOW 8u
static void xop_scroll(void)
{
    uint32_t n = xop_n(), cur = xop_row(), top = xop_top, first, last;
    if (n <= XL_SHOW) {
        xop_top = 0;
        return;
    }
    if (top > n - XL_SHOW)
        top = n - XL_SHOW;
    for (;;) {
        first = top ? top + 1u : 0u;                 /* the first item place (after a top "...") */
        last = top + XL_SHOW - 1u < n - 1u ? top + XL_SHOW - 2u : n - 1u;   /* the last (before a bottom "...") */
        if (cur < first && top)
            top--;
        else if (cur > last)
            top++;
        else
            break;
    }
    xop_top = (uint8_t)(top > n - XL_SHOW ? n - XL_SHOW : top);
}
/* the algorithm a track plays (0..39, 40 USER) and its heard operators (bit k: op k+1), as opx_build takes them */
static uint32_t xop_alg(const track_t *t, uint32_t *heard)
{
    const uint8_t *pk = opx_pk[(t - trk) % NTRK];
    int32_t e = t->p[P_E0];
    uint32_t a = e >= 1 && e <= 41 ? (uint32_t)e - 1u : (uint32_t)opx_get(pk, OPXF_ALGO), k, out = 0;
    for (k = 0; k < 6u; k++)
        out |= (uint32_t)opx_get(pk, OPXF_UA_OUT(k)) << k;
    a = a > OPX_USER ? OPX_USER : a;
    *heard = a == OPX_USER ? out : OPX_ALG[a % OPX_NALG].out;
    return a;
}

/* OUT (EDIT 1's matrix and the list's USER > OUT): operator k's output level, its LEVEL while heard, -1 (OFF) while
 * not. A turn: 0..100 sets LEVEL; below 0 OFF (LEVEL kept); up from OFF heard again at that LEVEL. 1 = changed */
static int32_t xop_out(uint32_t tr, uint32_t k)
{
    return opx_get(opx_pk[tr], OPXF_UA_OUT(k)) ? opx_get(opx_pk[tr], OPXF_OP(k, OPXF_OP1_LEVEL)) : -1;
}
static void xop_out_turn(uint32_t tr, uint32_t k, int32_t steps)
{
    uint32_t fo = OPXF_UA_OUT(k), fl = OPXF_OP(k, OPXF_OP1_LEVEL);
    int32_t was = xop_out(tr, k), v;
    v = was < 0 ? (steps > 0 ? opx_get(opx_pk[tr], fl) : -1) : clamp(was + accel(EN_K4, steps, 100), -1, 100);
    if (v == was)
        return;
    edit_undo_take(TSEL, 0x4000u | fo, UNDO_SOUND);
    if (v < 0)
        opx_edit(tr, fo, 0);                             /* (OFF: LEVEL kept) */
    else if (was < 0)
        opx_edit(tr, fo, 1);
    else
        opx_edit(tr, fl, v);
    motion_undo_done(TSEL);
}
/* f x (num / den)^n, at least one step a detent */
static int32_t xop_pow(int32_t f, int32_t n, uint32_t num, uint32_t den)
{
    for (; n > 0; n--) {
        int32_t g = (int32_t)((uint64_t)(uint32_t)f * num / den);
        f = g > f ? g : f + 1;
    }
    for (; n < 0; n++) {
        int32_t g = (int32_t)((uint64_t)(uint32_t)f * den / num);
        f = g < f ? g : f - 1;
    }
    return f;
}
/* the EG times a detent of KNOB 4 goes through: 0, 1..9 ms by 1, 10..95 by 5, 100..950 by 50, 1..9.5 s by 0.5, 10..90 s
 * by 5 (81 places: 10 ms to 1 s is 36 detents). The stored code is the TIME10 one nearest each (tools: its shown value is
 * within 1 % of the place's; the codes in between stay reachable from the web editor, a turn goes to the next place) */
static const uint16_t XOP_TLAD[] = {0, 1, 64, 100, 126, 146, 162, 176, 188, 198, 208, 244, 270, 290, 306, 320, 332, 343,
    352, 361, 368, 375, 382, 388, 394, 399, 405, 409, 414, 450, 476, 496, 512, 526, 538, 548, 558, 566, 574, 581, 588, 594,
    600, 605, 610, 615, 620, 656, 682, 702, 718, 732, 744, 755, 764, 773, 780, 788, 794, 800, 806, 812, 817, 822, 827, 863,
    888, 908, 925, 938, 950, 961, 970, 979, 987, 994, 1000, 1007, 1012, 1018, 1023};
static int32_t xop_time_step(int32_t c, int32_t steps)
{
    int32_t i, n = (int32_t)NELEM(XOP_TLAD);
    for (i = 0; i + 1 < n && XOP_TLAD[i + 1] <= c; i++)
        ;                                            /* (the place at or below c) */
    if (steps < 0 && XOP_TLAD[i] < c)
        steps++;                                     /* (between two places: left goes to the one below first) */
    return XOP_TLAD[clamp(i + steps, 0, n - 1)];
}
/* a list's values (the modes, the filter / effect types, the curves, FREQ; M4: the CTRLs, DEST, the LFO's WAVE TEMPO
 * KSYNC, its SPEED as a note value, OCT): one a detent */
static int xop_enum(uint32_t k)
{
    return k == XR_MODE || k == XR_FTYPE || k == XR_FX || k == XR_KLC || k == XR_KHC || k == XR_FREQ || k == XR_PMEC ||
           k == XR_PMLC || k == XR_PMD || k == XR_LMLC || k == XR_GLC || k == XR_LW || k == XR_LT || k == XR_LK ||
           k == XR_GO || k == XR_VCTL || k == XR_MFT || k == XR_MFE || k == XR_MFE2C || k == XR_MFL2C || k == XR_MFKL ||
           k == XR_MFKH || k == XR_MFKC ||
           (k == XR_LS && opx_get(opx_pk[song.sel % NTRK], OPXF_LFO1_TEMPO + (uint32_t)(xop_lfs % 3u) *
                                  (OPXF_LFO2_WAVE - OPXF_LFO1_WAVE)));
}
/* M5: V.PATCH's SRC and DEST: a scope, then the source or destination within it (the opsix's two knobs each); the
 * patch keeps one index (eng_opx.c OPX_VS_*, OPX_VD_*). Each scope's first index, then the end */
static const uint16_t XVS_AT[8] = {0, 1, 6, OPX_VS_EG1, OPX_VS_CCU, OPX_VS_CCB, OPX_VS_AT, OPX_NVS};
static const char *const XVS_NM[7] = {"OFF", "CTRL", "KBD", "EG/LFO", "CC+", "CC+/-", "MIDI"};
static const uint16_t XVD_AT[21] = {0, OPX_VD_PROG, OPX_VD_OP, OPX_VD_OP + 39, OPX_VD_OP + 78, OPX_VD_OP + 117,
    OPX_VD_OP + 156, OPX_VD_OP + 195, OPX_VD_EG, OPX_VD_EG + 4, OPX_VD_EG + 8, OPX_VD_LFO, OPX_VD_LFO + 4, OPX_VD_LFO + 8,
    OPX_VD_FLT, OPX_VD_FX, OPX_VD_FX + 4, OPX_VD_FX + 8, OPX_VD_ARP, OPX_VD_SEQ, OPX_NVD};
static const char *const XVD_NM[20] = {"OFF", "PROG", "OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "EG1", "EG2", "EG3",
    "LFO1", "LFO2", "LFO3", "FILTER", "FX1", "FX2", "FX3", "ARP", "SEQ"};
static uint32_t xvp_scope(const uint16_t *at, uint32_t n, int32_t v)
{
    uint32_t i = 0;
    while (i + 1u < n && v >= at[i + 1u])
        i++;
    return i;
}
/* a source's name (CC: "CC 74"), a destination's (within its scope; the list p.89-90, shortened) */
static void xvp_src_name(int32_t v, char *val)
{
    static const char *const NM[OPX_VS_CCU] = {"---", "MOD WHL", "PITCH", "PITCH+", "PITCH-", "PEDAL", "NOTE", "VEL",
        "EXP VEL", "REL VEL", "EG1", "EG2", "EG3", "LFO1", "LFO2", "LFO3"};
    if (v < OPX_VS_CCU)
        str_cpy(val, NM[clamp(v, 0, OPX_VS_CCU - 1)], 12);
    else if (v < OPX_VS_AT) {
        str_cpy(val, "CC ", 12);
        fmt_int(val + 3, (v - OPX_VS_CCU) % 119 + 1);
    } else
        str_cpy(val, v == OPX_VS_AT ? "A.TOUCH" : "POLY AT", 12);
}
static void xvp_dst_name(int32_t v, char *val)
{
    static const char *const PR[16] = {"PITCH", "OCT", "TRANS", "LEVEL", "PAN", "ATK", "DECAY", "REL", "SUS", "TEMPO",
        "ALGO", "ALGO FB", "GLIDE", "UNI VCS", "UNI DTN", "UNI SPR"};
    static const char *const OP[39] = {"PITCH", "COARSE", "OCT", "TRANS", "LEVEL", "ATK", "DECAY", "REL", "SUS", "FM FB",
        "WIDTH", "R.DEPTH", "R.SHAPE", "F.GAIN", "F.BIAS", "F.MIX", "CUTOFF", "RESO", "FLT MIX", "FX MIX", "PEQ FRQ",
        "PEQ GN", "SEQ LOW", "SEQ HI", "PHS FRQ", "PHS FB", "SDL TIM", "SDL FB", "CMB FRQ", "COMB FB", "DST AMT",
        "DST LOW", "DRV AMT", "DRV LOW", "DEC FRQ", "DEC BIT", "WS DAMP", "PCH AMT", "PCH HI"};
    static const char *const EG[4] = {"ATK", "DECAY", "REL", "SUS"}, *const LF[4] = {"WAVE", "SPEED", "PHASE", "FADE"};
    static const char *const MS[6] = {"CUTOFF", "RESO", "RESO", "GATE", "OCT", "SWING"};
    if (v <= 0)
        str_cpy(val, "---", 12);
    else if (v < OPX_VD_OP)
        str_cpy(val, PR[v - OPX_VD_PROG], 12);
    else if (v < OPX_VD_EG)
        str_cpy(val, OP[(v - OPX_VD_OP) % 39], 12);
    else if (v < OPX_VD_LFO)
        str_cpy(val, EG[(v - OPX_VD_EG) % 4], 12);
    else if (v < OPX_VD_FLT)
        str_cpy(val, LF[(v - OPX_VD_LFO) % 4], 12);
    else if (v < OPX_VD_FX)
        str_cpy(val, MS[v - OPX_VD_FLT], 12);
    else if (v < OPX_VD_ARP) {
        str_cpy(val, "PARAM ", 12);
        fmt_int(val + 6, (v - OPX_VD_FX) % 4 + 1);
    } else
        str_cpy(val, MS[2 + clamp(v - OPX_VD_ARP, 0, 3)], 12);
}
/* M4: a value through a ladder of step sizes by its size, either side of 0 (lim[i]: below it, a detent is stp[i]); from
 * a value between two places, a detent goes to the next place. P MOD INT (x0.01 semitone): 0.05 to 1, 0.50 to 12, 1.00
 * to 48, 12.00 to 144; FADE (ms): 10 to 100, 50 to 1 s, 100 on */
static int32_t xop_ladder(int32_t v, int32_t steps, const int32_t *lim, const int32_t *stp, uint32_t n, int32_t hi)
{
    for (; steps; steps += steps > 0 ? -1 : 1) {
        int32_t up = steps > 0, neg = v < 0 || (v == 0 && !up), a = v < 0 ? -v : v, away = (up && !neg) || (!up && neg), st;
        uint32_t i;
        if (away) {
            for (i = 0; i + 1u < n && a >= lim[i]; i++)
                ;
            st = stp[i];
            a = (a / st + 1) * st;
        } else {
            for (i = 0; i + 1u < n && a - 1 >= lim[i]; i++)
                ;
            st = stp[i];
            a = (a - 1) / st * st;
        }
        a = a > hi ? hi : a;
        v = neg ? -a : a;
    }
    return v;
}
static const int32_t XPM_LIM[4] = {100, 1200, 4800, 14400}, XPM_STP[4] = {5, 50, 100, 1200};
static const int32_t XFD_LIM[3] = {100, 1000, 9900}, XFD_STP[3] = {10, 50, 100};
static const int32_t XVI_LIM[3] = {100, 1000, 10000}, XVI_STP[3] = {5, 25, 100};
/* OP MIX (HOME): operator op's LEVEL turned by the knob of role `role` (PRESETS OP 1, ALGORITHM OP 2, KNOB 1..4 OP 3..6);
 * the operator becomes the picked one (its row lit); one undo per gesture, as the list's */
static void xop_level_turn(uint32_t op, int32_t steps, uint32_t role)
{
    uint32_t tr = song.sel % NTRK, f = OPXF_OP(op % 6u, OPXF_OP1_LEVEL);
    int32_t was = opx_get(opx_pk[tr], f), v = clamp(was + accel(role, steps, 100), 0, 100);
    fop_op = (uint8_t)(op % 6u);
    if (v == was)
        return;
    edit_undo_take(TSEL, 0x4000u | f, UNDO_SOUND);
    opx_edit(tr, f, v);
    motion_undo_done(TSEL);
}
static void xop_knob(uint32_t slot, int32_t steps)
{
    uint32_t tr = song.sel % NTRK, f, k, n = xop_n();
    int32_t v;
    if (xop_view() == XV_KNB) {                      /* (OP MIX: KNOB 1..4 = OP 3..6's LEVEL) */
        xop_level_turn(slot + 2u, steps, EN_K1 + slot);
        return;
    }
    if (slot == 0u) {
        if (xop_noop()) {                            /* (M5: the EG, LFO pages: which EG, LFO) */
            uint8_t *sel = xop_view() == XV_EG1 ? &xop_egs : &xop_lfs;
            *sel = (uint8_t)clamp((int32_t)*sel + steps, 0, 2);
            xop_li = (uint8_t)xop_row();             /* (LFO2, LFO3: no PROG PITCH rows) */
            xop_scroll();
            return;
        }
        fop_op = (uint8_t)clamp((int32_t)fop_op + steps, 0, 5);
        xop_li = (uint8_t)xop_row();                 /* (another operator, another MODE section: the row kept in range) */
        xop_scroll();
        return;
    }
    if (slot == 1u && xop_view() != XV_ALL)          /* (ENVELOPE, OP LEVELS: no sections) */
        return;
    if (slot == 2u && xop_view() == XV_LVL) {        /* (OP LEVELS: the row is the operator) */
        fop_op = (uint8_t)clamp((int32_t)fop_op + steps, 0, 5);
        return;
    }
    if (slot == 1u) {                                /* PAGE: a section a detent, at the row it was left on */
        xop_sec = (uint8_t)clamp((int32_t)xop_sec + steps, 0, XS_N - 1);
        xop_li = (uint8_t)xop_row();                 /* (MODE: the operator may have changed since) */
        xop_scroll();
        return;
    }
    if (slot == 2u) {
        xop_li = (uint8_t)clamp((int32_t)xop_row() + steps, 0, (int32_t)n - 1);
        xop_scroll();
        return;
    }
    k = xop_rowf(xop_row(), tr, &f);
    if (k == XR_OUT) {
        xop_out_turn(tr, fop_op % 6u, steps);
        return;
    }
    if (k == XR_ESEL || k == XR_LSEL || k == XR_VSEL) {   /* M5: which EG, LFO, V.PATCH */
        uint8_t *sel = k == XR_ESEL ? &xop_egs : k == XR_LSEL ? &xop_lfs : &xop_vps;
        *sel = (uint8_t)clamp((int32_t)*sel + steps, 0, k == XR_VSEL ? 11 : 2);
        return;
    }
    if (k == XR_VSS || k == XR_VSI || k == XR_VDS || k == XR_VDI) {   /* M5: V.PATCH's SRC, DEST: scope, item */
        const uint16_t *at = k <= XR_VSI ? XVS_AT : XVD_AT;
        uint32_t na = k <= XR_VSI ? NELEM(XVS_AT) : NELEM(XVD_AT), sc;
        int32_t was = opx_get(opx_pk[tr], f);
        sc = xvp_scope(at, na, was);
        if (k == XR_VSS || k == XR_VDS)              /* (another scope: its first) */
            v = at[clamp((int32_t)sc + steps, 0, (int32_t)na - 2)];
        else
            v = clamp(was + accel(EN_K4, steps, at[sc + 1u] - at[sc]), at[sc], at[sc + 1u] - 1);
        if (v != was) {
            edit_undo_take(TSEL, 0x4000u | f, UNDO_SOUND);
            opx_edit(tr, f, v);
            motion_undo_done(TSEL);
        }
        return;
    }
    if (xop_voice(k)) {                              /* VOICE: as the VOICE pages' knob (ui_input.c edit_param) */
        uint32_t id = XV_ID[k - XR_V0];
        const param_desc_t *d = track_desc(TSEL, id);
        if (d->max == d->min)
            return;
        TSEL->p[id] = (int16_t)param_turn(d, TSEL->p[id], accel(EN_K4, steps, d->fmt == F_ENUM ? 0 : d->max - d->min));
        motion_capture(TSEL, id, TSEL->p[id]);
        ml_knob(TSEL, id);
        return;
    }
    {
        int32_t lo = OPX_FIELDS[f].lo, hi = OPX_FIELDS[f].hi;
        if (k == XR_FXE || k == XR_FXF) {            /* (E and F: the effect type's own range, OPX_FXV) */
            const int16_t *fv = OPX_FXV[clamp(xop_get(tr, OPXF_OP1_FX_TYPE), 0, 9)];
            lo = 0, hi = k == XR_FXE ? fv[1] - fv[0] : fv[3] - fv[2];
        }
        v = opx_get(opx_pk[tr], f);
        if (f == OPXF_OP(fop_op % 6u, OPXF_OP1_FIXED_FREQ))
            v = k == XR_RATIO ? xop_pow(v, steps, 1059463u, 1000000u) : xop_pow(v, steps, 1000578u, 1000000u);
        else if (k == XR_FINE)                       /* (RATIO's FINE: 0.01 a detent, as shown; from a value between: to it) */
            v = (steps > 0 ? v / 10 : (v + 9) / 10) * 10 + 10 * accel(EN_K4, steps, (hi - lo) / 10);
        else if (k == XR_ATK || k == XR_DEC || k == XR_REL || k == XR_E1A || k == XR_E1D || k == XR_E1R)
            v = xop_time_step(v, accel(EN_K4, steps, (int32_t)NELEM(XOP_TLAD)));
        else if (k == XR_PME || k == XR_PML || k == XR_GL)
            v = xop_ladder(v, accel(EN_K4, steps, 100), XPM_LIM, XPM_STP, 4u, hi);
        else if (k == XR_LF)
            v = xop_ladder(v, accel(EN_K4, steps, 100), XFD_LIM, XFD_STP, 3u, hi);
        else if (k == XR_VINT)                       /* (M5: V.PATCH INT x0.01: 0.05 to 1, 0.25 to 10, 1.00 on) */
            v = xop_ladder(v, accel(EN_K4, steps, 100), XVI_LIM, XVI_STP, 3u, hi);
        else if (k == XR_MFE2 || k == XR_MFL2)       /* (M5b: EG2 / LFO2 INT x0.01 semitone, as P MOD's) */
            v = xop_ladder(v, accel(EN_K4, steps, 100), XPM_LIM, XPM_STP, 4u, hi);
        else if (k == XR_MFR)                        /* (RESO x0.01 %: 1 % a detent) */
            v = (v / 100 + accel(EN_K4, steps, 100)) * 100;
        else if (k == XR_LS && !xop_enum(k))         /* (SPEED: 4 codes a detent, 0.01 .. 100 Hz in 256) */
            v += 4 * accel(EN_K4, steps, 256);
        else if (xop_enum(k))
            v += steps;
        else
            v += accel(EN_K4, steps, hi - lo);
        if (f == OPXF_LFO1_SYNC || f == OPXF_LFO2_SYNC || f == OPXF_LFO3_SYNC)   /* (the note values there are: OPX_SYNC) */
            hi = (int32_t)NELEM(OPX_SYNC) - 1;
        v = clamp(v, lo, hi);
    }
    if (v == opx_get(opx_pk[tr], f))
        return;
    edit_undo_take(TSEL, 0x4000u | f, UNDO_SOUND);   /* (a knob turned on, this field: one copy) */
    opx_edit(tr, f, v);
    if (k == XR_FX) {                                /* another effect: its E and F at their neutral values */
        static const uint16_t DEF[10][2] = {{500, 180}, {180, 180}, {500, 100}, {9, 30}, {500, 150}, {50, 180},
                                            {50, 180}, {1400, 1000}, {0, 0}, {50, 180}};
        opx_edit(tr, OPXF_OP(fop_op % 6u, OPXF_OP1_FX_E), DEF[v][0]);
        opx_edit(tr, OPXF_OP(fop_op % 6u, OPXF_OP1_FX_F), DEF[v][1]);
    }
    motion_undo_done(TSEL);
    xop_scroll();                                    /* (MODE: the section's rows change) */
}

/* TIME10 code c as text: "0", "4.5" MS, "120" MS, "1.25" S, "12.5" S */
static void xop_time(int32_t c, char *val, const char **unit)
{
    static const uint16_t FR[17] = {32768, 34219, 35734, 37316, 38968, 40693, 42495, 44376, 46341, 48393, 50535,
                                    52773, 55109, 57549, 60097, 62757, 65535};   /* 2^(i/16) / 2, Q16 */
    uint32_t e, ms10, fr, i;
    *unit = "MS";
    if (c <= 0) {
        str_cpy(val, "0", 12);
        return;
    }
    e = (uint32_t)(c - 1) * 16853u / 1022u;          /* log2 of the time in ms, x1024 (90000 = 2^16.458) */
    i = (e & 1023u) >> 6;
    fr = FR[i] + (uint32_t)((FR[i + 1u] - FR[i]) * (e & 63u) >> 6);
    ms10 = (uint32_t)(((uint64_t)fr * 20u << (e >> 10)) >> 16);   /* tenths of ms */
    if (ms10 < 100u)
        fop_dec(val, ms10 / 10u, ms10 % 10u, 1u);
    else if (ms10 < 10000u)
        fmt_int(val, (int32_t)(ms10 / 10u));
    else {
        *unit = "S";
        if (ms10 < 100000u)
            fop_dec(val, ms10 / 10000u, ms10 % 10000u / 100u, 2u);
        else
            fop_dec(val, ms10 / 10000u, ms10 % 10000u / 1000u, 1u);
    }
}
/* the ratio of coarse index c: "1/128" .. "1/2", "1" .. "32" */
static void xop_coarse(int32_t c, char *s)
{
    if (c < 7) {
        str_cpy(s, "1/", 12);
        fmt_int(s + 2, 128 >> c);
    } else {
        fmt_int(s, c - 6);
    }
}
/* a fixed frequency (x0.01 Hz) as text: "12.34", "123.4", "1234", "12.3K" */
static void xop_hz(int32_t f, char *s)
{
    if (f < 10000)
        fop_dec(s, (uint32_t)f / 100u, (uint32_t)f % 100u, 2u);
    else if (f < 100000)
        fop_dec(s, (uint32_t)f / 100u, (uint32_t)f % 100u / 10u, 1u);
    else if (f < 1000000)
        fmt_int(s, f / 100);
    else {
        fop_dec(s, (uint32_t)f / 100000u, (uint32_t)f % 100000u / 10000u, 1u);
        str_cpy(s + str_len(s), "K", 2);
    }
}
/* a signed value with its sign: "+12", "-3", "0"; tenths: "+12.5" */
static void xop_signed(char *val, int32_t v, int tenths)
{
    val[0] = v > 0 ? '+' : v < 0 ? '-' : 0;
    if (v < 0)
        v = -v;
    if (tenths)
        fop_dec(val + (val[0] != 0), (uint32_t)v / 10u, (uint32_t)v % 10u, 1u);
    else
        fmt_int(val + (val[0] != 0), v);
}
/* row r: its label, value (12), unit, gauge (0..1000, -1 none); 0 = it does not sound now (DIM) */
static int xop_rowtext(uint32_t r, const char **label, char *val, const char **unit, int32_t *ratio)
{
    static const char *const LBL[XR_N] = {"MODE", "WAVE", "FB", "WIDTH", "DEPTH", "SHAPE", "TYPE", "OSC MIX",
        "CUTOFF", "RESO", "GAIN", "OSC MIX", "BIAS", "FX", "OSC MIX", "E", "F", "FREQ", "RATIO", "FINE", "DTUN", "TRANS",
        "EG1", "EG CTRL", "LFO1", "LFO CTRL", "VEL", "VEL DEST",
        "LEVEL", "ATK", "DEC", "SUS", "REL", "CURVE", "VEL", "LFO1", "LFO CTRL", "LOW", "CENTER", "HIGH", "L CRV", "H CRV",
        "TO OP", "TO OP", "TO OP", "TO OP", "TO OP", "TO OP", "OUT", "ALG", "ALG FB",
        "ATK", "DEC", "SUS", "REL", "CURVE", "WAVE", "TEMPO", "SPEED", "KSYNC", "PHASE", "FADE",
        "LFO1", "LFO CTRL", "TRANS", "OCT",
        "VOICE", "GLIDE", "GLIDE MODE", "PRIORITY", "ALLOC", "DETUNE", "PAN", "MUTE", "SPREAD",
        "EG", "LFO", "V.PATCH", "SRC", "SOURCE", "INT", "CTRL", "DEST", "TARGET",
        "TYPE", "ENABLE", "CUTOFF", "RESO", "EG2", "CTRL", "LOW", "CENTER", "HIGH", "LFO2", "CTRL"};
    static const char NOTE[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint32_t tr = song.sel % NTRK, f, k = xop_rowf(r, tr, &f);
    int32_t v, lo, hi;
    int on = 1;
    if (k == XR_ESEL || k == XR_LSEL || k == XR_VSEL) {   /* M5: which EG, LFO, V.PATCH the rows below show */
        uint32_t x = k == XR_ESEL ? xop_egs : k == XR_LSEL ? xop_lfs : xop_vps;
        *label = LBL[k];
        *unit = "";
        str_cpy(val, k == XR_ESEL ? "EG" : k == XR_LSEL ? "LFO" : "", 12);
        fmt_int(val + str_len(val), (int32_t)x + 1);
        *ratio = (int32_t)x * 1000 / (k == XR_VSEL ? 11 : 2);
        return 1;
    }
    if (xop_voice(k)) {                              /* VOICE: the track's parameter, as its page shows it */
        uint32_t id = XV_ID[k - XR_V0];
        const param_desc_t *d = track_desc(TSEL, id);
        v = TSEL->p[id];
        *label = LBL[k];
        param_format(d, v, val, unit);
        *ratio = d->fmt == F_ENUM || d->max == d->min ? -1 : (int32_t)((int64_t)(v - d->min) * 1000 / (d->max - d->min));
        return 1;
    }
    v = opx_get(opx_pk[tr], f), lo = OPX_FIELDS[f].lo, hi = OPX_FIELDS[f].hi;
    *label = LBL[k];
    *unit = "";
    *ratio = (int32_t)((int64_t)(v - lo) * 1000 / (hi - lo));
    fmt_int(val, v);
    switch (k) {
    case XR_MODE:
        str_cpy(val, N_OPX_MODE[clamp(v, 0, OPX_NMODE - 1)], 12);
        *ratio = -1;
        break;
    case XR_WAVE:
        str_cpy(val, N_OPX_WAVE[clamp(v, 0, OPX_NWAVE - 1)], 12);
        break;
    case XR_FB:                                      /* (the noises ignore the phase: DIM) */
    case XR_WIDTH:
        if (k == XR_WIDTH && !v)
            str_cpy(val, "OFF", 12);
        else
            *unit = "%";
        on = xop_wave(tr) < OPX_W_NOISE_SH;
        break;
    case XR_FTYPE:
        str_cpy(val, N_OPX_FLT[clamp(v, 0, 11)], 12);
        *ratio = -1;
        break;
    case XR_FX:
        str_cpy(val, N_OPX_FX[clamp(v, 0, 9)], 12);
        *ratio = -1;
        break;
    case XR_CUT:                                     /* x0.1 semitone */
        xop_signed(val, v, 1);
        *unit = "ST";
        break;
    case XR_BIAS:
    case XR_KLOW:
    case XR_KHIGH:
        xop_signed(val, v, 0);
        *unit = "%";
        break;
    case XR_KCTR:                                    /* A0 .. C9 (C4 = 60) */
        str_cpy(val, NOTE[v % 12], 12);
        fmt_int(val + str_len(val), v / 12 - 1);
        break;
    case XR_KLC:
    case XR_KHC:
    case XR_CRV:
    case XR_E1C: {
        int cr = k == XR_CRV || k == XR_E1C;
        if (!cr || !v)
            str_cpy(val, v && !cr ? "EXP" : "LIN", 12);
        else if (v == 10)
            str_cpy(val, "EXP", 12);
        if (!cr)
            *ratio = -1;
        break;
    }
    /* M4: P MOD, LMOD, EG1, LFO1, PROG PITCH */
    case XR_PME:
    case XR_PML:
    case XR_GL:                                      /* x0.01 semitone */
        val[0] = v > 0 ? '+' : v < 0 ? '-' : 0;
        fop_dec(val + (val[0] != 0), (uint32_t)(v < 0 ? -v : v) / 100u, (uint32_t)(v < 0 ? -v : v) % 100u, 2u);
        *unit = "ST";
        break;
    case XR_PMEC:
    case XR_PMLC:
    case XR_LMLC:
    case XR_GLC:
        str_cpy(val, N_OPX_CTRL[clamp(v, 0, OPX_NCTRL - 1)], 12);
        *ratio = -1;
        break;
    case XR_PMV:
    case XR_PMD:                                     /* (RATIO only, p.51) */
        if (k == XR_PMD) {
            str_cpy(val, v ? "COARSE" : "FINE", 12);
            *ratio = -1;
        } else {
            *unit = "%";
        }
        on = !xop_fixed(tr);
        break;
    case XR_LML:
        xop_signed(val, v, 0);
        *unit = "%";
        break;
    case XR_E1A:
    case XR_E1D:
    case XR_E1R:
        xop_time(v, val, unit);
        break;
    case XR_LW:
        str_cpy(val, N_OPX_LFOW[clamp(v, 0, OPX_NLFOW - 1)], 12);
        *ratio = -1;
        break;
    case XR_LT:
        str_cpy(val, v ? "ON" : "OFF", 12);
        *ratio = -1;
        break;
    /* M5: V.PATCH */
    case XR_VSS:
    case XR_VDS: {
        uint32_t sc = k == XR_VSS ? xvp_scope(XVS_AT, NELEM(XVS_AT), v) : xvp_scope(XVD_AT, NELEM(XVD_AT), v);
        str_cpy(val, k == XR_VSS ? XVS_NM[sc] : XVD_NM[sc], 12);
        *ratio = -1;
        on = k == XR_VSS || !v || opx_vd_on((uint32_t)v);
        break;
    }
    case XR_VSI:
        xvp_src_name(v, val);
        *ratio = -1;
        on = v != 0;
        break;
    case XR_VDI:
        xvp_dst_name(v, val);
        *ratio = -1;
        on = !v || opx_vd_on((uint32_t)v);          /* (not played yet: DIM) */
        break;
    case XR_VINT: {
        int32_t d = opx_get(opx_pk[tr], f + 2u);    /* (DEST, two fields on) */
        val[0] = v > 0 ? '+' : v < 0 ? '-' : 0;
        fop_dec(val + (val[0] != 0), (uint32_t)(v < 0 ? -v : v) / 100u, (uint32_t)(v < 0 ? -v : v) % 100u, 2u);
        on = !d || opx_vd_on((uint32_t)d);
        break;
    }
    case XR_VCTL:
    case XR_MFE2C:
    case XR_MFL2C:
        str_cpy(val, N_OPX_CTRL[clamp(v, 0, OPX_NCTRL - 1)], 12);
        *ratio = -1;
        break;
    /* M5b: the main filter */
    case XR_MFT:
        str_cpy(val, N_OPX_MFLT[clamp(v, 0, 10)], 12);
        *ratio = -1;
        break;
    case XR_MFE:
        str_cpy(val, v ? "ON" : "OFF", 12);
        *ratio = -1;
        break;
    case XR_MFC: {                                   /* CUT10: 6.489 Hz x (23680 / 6.489)^(c / 1023) */
        uint32_t h = (uint32_t)(((uint64_t)(uint32_t)fm6_exp2(clamp(v, 0, 1023) * 194064 - (8 << 24)) * 6489u) >> 16);
        h += h < 100000u ? 5u : h < 1000000u ? 500u : 5000u;   /* (h: mHz, rounded; "6.49 HZ", "440 HZ", "23.68 KHZ") */
        if (h < 100000u)
            fop_dec(val, h / 1000u, h % 1000u / 10u, 2u), *unit = "HZ";
        else if (h < 1000000u)
            fmt_int(val, (int32_t)(h / 1000u)), *unit = "HZ";
        else
            fop_dec(val, h / 1000000u, h % 1000000u / 10000u, 2u), *unit = "KHZ";
        break;
    }
    case XR_MFR:
        fop_dec(val, (uint32_t)v / 100u, (uint32_t)v % 100u / 10u, 1u);
        *unit = "%";
        break;
    case XR_MFE2:
    case XR_MFL2:
        val[0] = v > 0 ? '+' : v < 0 ? '-' : 0;
        fop_dec(val + (val[0] != 0), (uint32_t)(v < 0 ? -v : v) / 100u, (uint32_t)(v < 0 ? -v : v) % 100u, 2u);
        *unit = "ST";
        break;
    case XR_MFKL:
    case XR_MFKH:                                    /* the slope c / (128 - |c|): 64 = 1.00; 127 INF */
        if (v >= 127 || v <= -127)
            str_cpy(val, v > 0 ? "+INF" : "-INF", 12);
        else {
            uint32_t a = (uint32_t)(v < 0 ? -v : v) * 100u / (128u - (uint32_t)(v < 0 ? -v : v));
            val[0] = v > 0 ? '+' : v < 0 ? '-' : 0;
            fop_dec(val + (val[0] != 0), a / 100u, a % 100u, 2u);
        }
        break;
    case XR_MFKC:                                    /* C-1 .. G9 */
        str_cpy(val, NOTE[v % 12], 12);
        fmt_int(val + str_len(val), v / 12 - 1);
        *ratio = -1;
        break;
    case XR_LS:
        if (f == OPXF_LFO1_SYNC || f == OPXF_LFO2_SYNC || f == OPXF_LFO3_SYNC) {   /* a note value: "1/16", "3/8" */
            const uint8_t *nv = OPX_SYNC[clamp(v, 0, (int32_t)NELEM(OPX_SYNC) - 1)];
            fmt_int(val, nv[0]);
            str_cpy(val + str_len(val), "/", 2);
            fmt_int(val + str_len(val), nv[1]);
            *ratio = v * 1000 / ((int32_t)NELEM(OPX_SYNC) - 1);
        } else {                                     /* HZ10: 0.01 Hz x 10000^(c / 1023), shown to 0.01 Hz */
            uint32_t h = (uint32_t)(fm6_exp2(clamp(v, 0, 1023) * 217926 - (16 << 24)) + 128) >> 8;
            fop_dec(val, h / 100u, h % 100u, 2u);
            *unit = "HZ";
        }
        break;
    case XR_LK: {
        static const char *const KS[3] = {"OFF", "COMMON", "VOICE"};
        str_cpy(val, KS[clamp(v, 0, 2)], 12);
        *ratio = -1;
        break;
    }
    case XR_LP:
        if (v < -180)
            str_cpy(val, "RND", 12);
        else
            xop_signed(val, v, 0), *unit = "DEG";
        break;
    case XR_LF:                                      /* ms; no effect with KSYNC OFF (p.57) */
        if (v < 1000)
            fmt_int(val, v), *unit = "MS";
        else
            fop_dec(val, (uint32_t)v / 1000u, (uint32_t)v % 1000u / 10u, 2u), *unit = "S";
        on = opx_get(opx_pk[tr], f - OPXF_LFO1_FADE + OPXF_LFO1_KSYNC) != 0;
        break;
    case XR_GT:
        xop_signed(val, v, 0);
        *unit = "ST";
        break;
    case XR_GO:
        xop_signed(val, v, 0);
        *unit = "OCT";
        *ratio = -1;
        break;
    case XR_FXE:
    case XR_FXF: {                                   /* the effect type's own name and units (OPX_FXV) */
        static const char *const NM[10][2] = {{"FREQ", "GAIN"}, {"LOW", "HIGH"}, {"FREQ", "FB"}, {"TIME", "FB"},
            {"FREQ", "FB"}, {"DIST", "LOW"}, {"DRIVE", "LOW"}, {"FREQ", "BIT"}, {"TYPE", "DAMP"}, {"PUNCH", "HIGH"}};
        /* the units: 0 % (unsigned), 1 % signed, 2 semitones x0.1, 3 dB x0.1, 4 ms, 5 x0.01, 6 a number */
        static const uint8_t UN[10][2] = {{2, 3}, {3, 3}, {2, 1}, {4, 0}, {2, 1}, {0, 3}, {0, 3}, {5, 5}, {6, 0}, {0, 3}};
        uint32_t ty = (uint32_t)clamp(xop_get(tr, OPXF_OP1_FX_TYPE), 0, 9), w = k == XR_FXF;
        int32_t lo2 = OPX_FXV[ty][w * 2u], x = v + lo2;
        *label = NM[ty][w];
        *ratio = (int32_t)((int64_t)v * 1000 / (OPX_FXV[ty][w * 2u + 1u] - lo2));
        switch (UN[ty][w]) {
        case 1: xop_signed(val, x, 0); *unit = "%"; break;
        case 2: xop_signed(val, x, 1); *unit = "ST"; break;
        case 3: xop_signed(val, x, 1); *unit = "DB"; break;
        case 4: fmt_int(val, x); *unit = "MS"; break;
        case 5: fop_dec(val, (uint32_t)x / 100u, (uint32_t)x % 100u, 2u); break;
        case 6: val[0] = (char)('0' + x / 10), val[1] = (char)('0' + x % 10), val[2] = 0; break;
        default: fmt_int(val, x); *unit = "%";
        }
        return 1;
    }
    case XR_FREQ:
        str_cpy(val, v ? "FIXED" : "RATIO", 12);
        *ratio = -1;
        break;
    case XR_RATIO:
    case XR_FINE:
        if (f == OPXF_OP(fop_op % 6u, OPXF_OP1_FIXED_FREQ)) {
            if (k == XR_RATIO) {
                *label = "FIXED";
                xop_hz(v, val);
            } else {                                 /* FINE: the same frequency to 0.01 Hz (what a cent moves) */
                fop_dec(val, (uint32_t)v / 100u, (uint32_t)v % 100u, 2u);
            }
            *unit = "HZ";
            *ratio = -1;
        } else if (k == XR_RATIO) {
            xop_coarse(v, val);
        } else {
            fop_dec(val, (uint32_t)v / 1000u, (uint32_t)v % 1000u / 10u, 2u);   /* x0.001: shown to 0.01 */
            *unit = "X";
        }
        break;
    case XR_DTUN:
    case XR_TRANS:
        xop_signed(val, v, 0);
        *unit = k == XR_DTUN ? "CT" : "ST";
        break;
    case XR_ATK:
    case XR_DEC:
    case XR_REL:
        xop_time(v, val, unit);
        break;
    case XR_LVL:
        on = v != 0;
        *unit = "%";
        if (xop_view() >= XV_LVL) {                  /* (OP LEVELS, OP MIX: a row per operator) */
            static char lb[8] = "OP ?";
            lb[3] = (char)('1' + r % 6u);
            *label = lb;
        }
        break;
    case XR_OUT: {
        uint32_t heard;
        v = xop_out(tr, fop_op % 6u);
        if (v < 0) {
            str_cpy(val, "OFF", 12);
            *ratio = 0;
        } else {
            fmt_int(val, v);
            *unit = "%";
            *ratio = v * 10;
        }
        on = xop_alg(TSEL, &heard) == OPX_USER;
        break;
    }
    case XR_ALG:
        if (v == (int32_t)OPX_USER)
            str_cpy(val, "USER", 12);
        else
            fmt_int(val, v + 1);
        break;
    default:
        if (k >= XR_TO0 && k <= XR_TO5) {
            static char to[16];
            uint32_t dst = k - XR_TO0, heard;
            str_cpy(to, "TO OP", sizeof to);
            to[5] = (char)('1' + dst);
            to[6] = 0;
            if (dst == fop_op % 6u)
                str_cpy(to + 6, " (SELF)", sizeof to - 6u);   /* (a fixed list: its own row is its self feedback) */
            *label = to;
            on = xop_alg(TSEL, &heard) == OPX_USER;
        }
        *unit = "%";                                 /* SUS VEL ALG FB DEPTH SHAPE OSC MIX RESO GAIN TO */
    }
    return on;
}
static int xop_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    static const uint8_t ICN[XR_N] = {ICON_ALGORITHM, ICON_WAVE, ICON_FEEDBACK, ICON_PULSE, ICON_MIX, ICON_SHAPE,
        ICON_CUTOFF, ICON_MIX, ICON_CUTOFF, ICON_RESO, ICON_FOLD, ICON_MIX, ICON_SHAPE, ICON_DIST, ICON_MIX,
        ICON_GENERIC, ICON_GENERIC, ICON_RATIO, ICON_RATIO, ICON_TUNE, ICON_DETUNE, ICON_TRANSPOSE,
        ICON_PITCH, ICON_MOD, ICON_VIBRATO, ICON_MOD, ICON_ACCENT, ICON_RATIO, ICON_LEVEL,
        ICON_ATTACK, ICON_DECAY, ICON_SUSTAIN, ICON_RELEASE, ICON_ENV, ICON_ACCENT, ICON_LFO_WAVE, ICON_MOD, ICON_KEYTRACK,
        ICON_KEYTRACK, ICON_KEYTRACK, ICON_KEYTRACK, ICON_KEYTRACK, ICON_MOD, ICON_MOD, ICON_MOD, ICON_MOD, ICON_MOD,
        ICON_MOD, ICON_MIX, ICON_ALGORITHM, ICON_FEEDBACK,
        ICON_ATTACK, ICON_DECAY, ICON_SUSTAIN, ICON_RELEASE, ICON_ENV, ICON_LFO_WAVE, ICON_TEMPO, ICON_RATE, ICON_PHASE,
        ICON_PHASE, ICON_FADE, ICON_VIBRATO, ICON_MOD, ICON_TRANSPOSE, ICON_OCTAVE,
        ICON_NONE, ICON_NONE, ICON_NONE, ICON_NONE, ICON_NONE, ICON_NONE,
        ICON_NONE, ICON_NONE, ICON_NONE,                 /* (VOICE: the parameter's own, param_icon) */
        ICON_ENV, ICON_LFO_WAVE, ICON_MOD, ICON_MOD, ICON_MOD, ICON_ACCENT, ICON_MOD, ICON_MOD, ICON_MOD,   /* (M5) */
        ICON_CUTOFF, ICON_CUTOFF, ICON_CUTOFF, ICON_RESO, ICON_ENV, ICON_MOD, ICON_KEYTRACK, ICON_KEYTRACK, ICON_KEYTRACK,
        ICON_LFO_WAVE, ICON_MOD};                                                                        /* (M5b) */
    uint32_t tr = song.sel % NTRK, f;
    *unit = "";
    *ratio = -1;
    if (xop_view() == XV_KNB) {                      /* (OP MIX: KNOB 1..4 = OP 3..6's LEVEL) */
        static const char *const LB[4] = {"OP 3", "OP 4", "OP 5", "OP 6"};
        int32_t v = opx_get(opx_pk[tr], OPXF_OP(slot + 2u, OPXF_OP1_LEVEL));
        *label = LB[slot & 3u];
        fmt_int(val, v);
        *unit = "%";
        *ratio = v * 10;
        *icon = ICON_LEVEL;
        return v != 0;
    }
    if (slot == 0u) {
        *label = "OP";
        fmt_int(val, (int32_t)fop_op + 1);
        *unit = "/6";
        *ratio = (int32_t)fop_op * 200;
        *icon = ICON_ALGORITHM;
        if (xop_noop()) {                            /* (M5: the EG, LFO pages: which one) */
            uint32_t x = xop_view() == XV_EG1 ? xop_egs : xop_lfs;
            *label = xop_view() == XV_EG1 ? "EG" : "LFO";
            fmt_int(val, (int32_t)x + 1);
            *unit = "/3";
            *ratio = (int32_t)x * 500;
            *icon = xop_view() == XV_EG1 ? ICON_ENV : ICON_LFO_WAVE;
            return 1;
        }
        return xop_view() != XV_ALL || xop_sec_of(xop_row()) < XS_ALGO;   /* (ALGO's rows on: the patch's, VOICE's the
                                                                           * track's: no operator) */
    }
    if (slot == 1u) {
        uint32_t sc = xop_sec_of(xop_row());
        *label = "PAGE";
        str_cpy(val, XS_NAME[sc], 12);
        *ratio = (int32_t)sc * 1000 / (XS_N - 1);
        *icon = ICON_ENV;
        if (xop_view() != XV_ALL) {                  /* (ENVELOPE, OP LEVELS: no sections; KNOB 2 does nothing) */
            str_cpy(val, xop_view() == XV_ENV ? "EG" : xop_view() == XV_EG1 ? "EG1-3" : xop_view() == XV_LFO ? "LFO1-3" : "LEVEL", 12);
            *ratio = -1;
            return 0;
        }
        return 1;
    }
    if (slot == 2u) {
        static char of[6];
        uint32_t n = xop_n();
        *label = "ROW";
        fmt_int(val, (int32_t)xop_row() + 1);
        of[0] = '/';
        fmt_int(of + 1, (int32_t)n);
        *unit = of;
        *ratio = n > 1u ? (int32_t)(xop_row() * 1000u / (n - 1u)) : 0;   /* (MODE of BYPASS / MUTE: one row) */
        *icon = ICON_STEPS;
        return 1;
    }
    {
        uint32_t k = xop_rowf(xop_row(), tr, &f);
        int on = xop_rowtext(xop_row(), label, val, unit, ratio);
        int self = k >= XR_TO0 && k <= XR_TO5 && k - XR_TO0 == fop_op % 6u;
        *icon = self ? ICON_FEEDBACK : xop_voice(k) ? param_icon(track_desc(TSEL, XV_ID[k - XR_V0]), TSEL->p[XV_ID[k - XR_V0]])
                                              : ICN[k];
        if (self)
            *label = "SELF";                         /* (the card's title is short: "TO OP3 (SELF)" in the list) */
        else if (k == XR_PMEC || k == XR_PMLC || k == XR_LMLC || k == XR_GLC)
            *label = "CTRL";                         /* (M4: the opsix's own knob name; the list says whose) */
        else if (k == XR_PMD)
            *label = "DEST";
        if (k == XR_WAVE) {                          /* (the card is narrow: the list has the manual's full names) */
            static const char *const SHORT[OPX_NWAVE] = {"SIN", "SIN12", "SIN8", "TRI", "SAW", "SAWHD", "SQR", "SQRHD",
                "SAW3", "SQR3", "TRI3", "12345", "1+2", "1+3", "1+4", "1+5", "1+6", "1+7", "1+8", "S/H", "WHITE", "PINK",
                "BLUE"};
            str_cpy(val, SHORT[clamp(opx_get(opx_pk[tr], f), 0, OPX_NWAVE - 1)], 12);
        } else if (k == XR_MODE) {
            static const char *const SHORT[OPX_NMODE] = {"FM", "RING", "FILT", "FFM", "FOLD", "FX", "BYP", "MUTE"};
            str_cpy(val, SHORT[clamp(opx_get(opx_pk[tr], f), 0, OPX_NMODE - 1)], 12);
        } else if (k == XR_FTYPE || k == XR_FX) {    /* (the card: the type's number; the list its name) */
            fmt_int(val, opx_get(opx_pk[tr], f) + 1);
        }
        return on;
    }
}
_Static_assert(XR_N == 95, "xop_rowtext's LBL, xop_card's ICN: a label and an icon per kind");
#endif

/* the dispatch: FM6's pages, or OPX's OPERATORS (OPX fork) */
static int fop_any_ok(void)
{
#if FELUCCA_OPX
    if (xop_page(cur_page()->graph))
        return xop_ok();
#endif
    return fop_ok();
}
static void fop_any_knob(uint32_t slot, int32_t steps)
{
#if FELUCCA_OPX
    if (xop_page(cur_page()->graph)) {
        if (xop_ok() && steps)
            xop_knob(slot, steps);
        return;
    }
#endif
    fop_knob(slot, steps);
}
static int fop_any_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
#if FELUCCA_OPX
    if (xop_page(cur_page()->graph))
        return xop_card(slot, label, val, unit, ratio, icon);
#endif
    return fop_card(slot, label, val, unit, ratio, icon);
}

#if FELUCCA_OPX
/* -------------------------------------------------------------------------------- OPX's EDIT 1: the matrix ---
 * EDIT 1 (ALGO) on an OPX track that plays USER (ALG USR, or PAT on a patch whose algorithm is USER): the user algorithm
 * as the opsix's FM MATRIX (p.74): a column per operator sending (OP 1 left), a row per operator receiving (OP 1 at the
 * top) with a dot as big as the amount, the diagonal its self feedback, and the bottom row OUT (DIRECT OUT, heard or
 * not). KNOB 1 is ALG as ever; KNOB 2 the row, KNOB 3 the column, KNOB 4 the amount; on OUT, the operator's output
 * level: 0..100 = its LEVEL (heard), below 0 OFF (not heard, its LEVEL kept; up again: heard at
 * that LEVEL). One undo per gesture. (ATK DEC MLVL come back with a preset algorithm.) EDIT 2 shows the same matrix, nothing picked. */
static uint8_t xmat_row, xmat_col;                   /* 0..6 (6 = OUT), 0..5 */
static int xmat_user(const track_t *t)
{
    uint32_t heard;
    return t->eng_req % NENGINES == ENGI_OPX && xop_alg(t, &heard) == OPX_USER;
}
static int xmat_on(void) { const page_t *pg = cur_page(); return !ui.home && pg->scope == SC_ENGINE && pg->id[0] == P_E0 && xmat_user(TSEL); }
/* the matrix's field at (row, column) */
static uint32_t xmat_field(uint32_t row, uint32_t col) { return row >= 6u ? OPXF_UA_OUT(col % 6u) : opxf_ua(col % 6u, row); }
static void xmat_knob(uint32_t slot, int32_t steps)
{
    uint32_t tr = song.sel % NTRK, f;
    int32_t v;
    if (slot == 1u) {
        xmat_row = (uint8_t)clamp((int32_t)xmat_row + steps, 0, 6);
        return;
    }
    if (slot == 2u) {
        xmat_col = (uint8_t)clamp((int32_t)xmat_col + steps, 0, 5);
        return;
    }
    if (xmat_row >= 6u) {                                /* OUT: the operator's output level (xop_out) */
        xop_out_turn(tr, xmat_col, steps);
        return;
    }
    f = xmat_field(xmat_row, xmat_col);
    v = clamp(opx_get(opx_pk[tr], f) + accel(EN_K4, steps, 100), OPX_FIELDS[f].lo, OPX_FIELDS[f].hi);
    if (v == opx_get(opx_pk[tr], f))
        return;
    edit_undo_take(TSEL, 0x4000u | f, UNDO_SOUND);
    opx_edit(tr, f, v);
    motion_undo_done(TSEL);
}
/* the cards: ALG, ROW, COL, the amount ("2>3", "SELF", "OUT") */
static void xmat_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    static char lb[8];
    uint32_t tr = song.sel % NTRK;
    int32_t v;
    *unit = "";
    *ratio = -1;
    if (slot == 0u) {
        int32_t a = TSEL->p[P_E0];
        *label = "ALG";
        str_cpy(val, N_OPX_ALG[clamp(a, 0, OPX_NALG + 1)], 12);
        *ratio = a * 1000 / (OPX_NALG + 1);
        *icon = ICON_ALGORITHM;
        return;
    }
    if (slot == 1u) {
        *label = "ROW";
        if (xmat_row >= 6u)
            str_cpy(val, "OUT", 12);
        else
            str_cpy(val, "OP", 12), val[2] = (char)('1' + xmat_row), val[3] = 0;
        *ratio = xmat_row * 1000 / 6;
        *icon = ICON_STEPS;
        return;
    }
    if (slot == 2u) {
        *label = "COL";
        str_cpy(val, "OP", 12), val[2] = (char)('1' + xmat_col), val[3] = 0;
        *ratio = xmat_col * 200;
        *icon = ICON_STEPS;
        return;
    }
    if (xmat_row >= 6u) {
        *label = "OUT";
        v = xop_out(tr, xmat_col);
        *icon = ICON_MIX;
        if (v < 0) {
            str_cpy(val, "OFF", 12);
            *ratio = 0;
            return;
        }
        fmt_int(val, v);
        *unit = "%";
        *ratio = v * 10;
        return;
    }
    v = opx_get(opx_pk[tr], xmat_field(xmat_row, xmat_col));
    if (xmat_row == xmat_col) {
        *label = "SELF";
        *icon = ICON_FEEDBACK;
    } else {
        lb[0] = (char)('1' + xmat_col), lb[1] = '>', lb[2] = (char)('1' + xmat_row), lb[3] = 0;   /* "2>3": OP 2 to OP 3 */
        *label = lb;
        *icon = ICON_MOD;
    }
    fmt_int(val, v);
    *unit = "%";
    *ratio = v * 10;
}
#endif
