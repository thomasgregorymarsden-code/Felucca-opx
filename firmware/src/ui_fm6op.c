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

static int fop_page(uint32_t g) { return (g >= GR_FMOP && g <= GR_FMENV) || g == GR_OPX3; }   /* (GR_OPX3: OPX fork) */
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
 * The same pages for an OPX track, over its patch (opx_patch.c opx_pk, the packed record), plus OPERATOR 3 (OPX only).
 * KNOB 1 is the operator (shared with FM6's pages: fop_op), KNOB 2..4:
 *   OPERATOR    OP  RATIO (1/128 .. 32; FIXED: the frequency, a semitone a detent)  FINE (x0.500 .. x2.000; FIXED: a cent)  LEVEL
 *   OPERATOR 2  OP  REL  CURVE (LIN 1..9 EXP)  FB
 *   OP ENV      OP  ATK  DEC  SUS
 *   OPERATOR 3  OP  MODE (RATIO / FIXED)  DTUN (cents)  VEL (level velocity)
 * A detent writes one field (opx_edit: the patch becomes the track's own, SLOT OWN), one undo per knob gesture. The
 * macros (EDIT 1 / 2) still apply on top, as FM6's. */
enum { XO_OP, XO_RATIO, XO_FINE, XO_LVL, XO_ATK, XO_DEC, XO_SUS, XO_REL, XO_CRV, XO_FB, XO_MODE, XO_DTUN, XO_VEL };
static const uint8_t XO_PAGE[4][4] = {
    {XO_OP, XO_RATIO, XO_FINE, XO_LVL},              /* OPERATOR */
    {XO_OP, XO_REL, XO_CRV, XO_FB},                  /* OPERATOR 2 */
    {XO_OP, XO_ATK, XO_DEC, XO_SUS},                 /* OP ENV */
    {XO_OP, XO_MODE, XO_DTUN, XO_VEL},               /* OPERATOR 3 */
};
static int xop_ok(void) { return TSEL->eng_req % NENGINES == ENGI_OPX; }
static uint32_t xop_field(uint32_t slot)
{
    uint32_t g = cur_page()->graph;
    return XO_PAGE[g == GR_FMOP2 ? 1u : g == GR_FMENV ? 2u : g == GR_OPX3 ? 3u : 0u][slot & 3u];
}
static int xop_fixed(uint32_t tr) { return opx_get(opx_pk[tr % NTRK], OPXF_OP(fop_op % 6u, OPXF_OP1_FREQ_FIXED)) != 0; }
/* the patch field of x for the selected operator */
static uint32_t xop_f(uint32_t x, uint32_t tr)
{
    static const uint16_t F[13] = {0, OPXF_OP1_COARSE, OPXF_OP1_FINE, OPXF_OP1_LEVEL, OPXF_OP1_EG_ATK, OPXF_OP1_EG_DEC,
                                   OPXF_OP1_EG_SUS, OPXF_OP1_EG_REL, OPXF_OP1_EG_CURVE, OPXF_OP1_FM_FB,
                                   OPXF_OP1_FREQ_FIXED, OPXF_OP1_DETUNE, OPXF_OP1_LMOD_VEL};
    if ((x == XO_RATIO || x == XO_FINE) && xop_fixed(tr))
        return OPXF_OP(fop_op % 6u, OPXF_OP1_FIXED_FREQ);
    return OPXF_OP(fop_op % 6u, F[x % 13u]);
}
/* f x 2^(n / div), at least one step a detent */
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
static void xop_knob(uint32_t slot, int32_t steps)
{
    uint32_t x = xop_field(slot), tr = song.sel % NTRK, f;
    int32_t v;
    if (x == XO_OP) {
        fop_op = (uint8_t)clamp((int32_t)fop_op + steps, 0, 5);
        return;
    }
    f = xop_f(x, tr);
    v = opx_get(opx_pk[tr], f);
    if (f == OPXF_OP(fop_op % 6u, OPXF_OP1_FIXED_FREQ))
        v = x == XO_RATIO ? xop_pow(v, steps, 1059463u, 1000000u) : xop_pow(v, steps, 1000578u, 1000000u);
    else if (x == XO_MODE)
        v = steps > 0;
    else
        v += accel(EN_K1 + slot, steps, OPX_FIELDS[f].hi - OPX_FIELDS[f].lo);
    v = clamp(v, OPX_FIELDS[f].lo, OPX_FIELDS[f].hi);
    if (v == opx_get(opx_pk[tr], f))
        return;
    edit_undo_take(TSEL, 0x4000u | f, UNDO_SOUND);   /* (a knob turned on, this field: one copy) */
    opx_edit(tr, f, v);
    motion_undo_done(TSEL);
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
static int xop_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    static const char *const LBL[13] = {"OP", "RATIO", "FINE", "LEVEL", "ATK", "DEC", "SUS", "REL", "CURVE", "FB",
                                        "MODE", "DTUN", "VEL"};
    static const uint8_t ICN[13] = {ICON_ALGORITHM, ICON_RATIO, ICON_TUNE, ICON_LEVEL, ICON_ATTACK, ICON_DECAY,
                                    ICON_SUSTAIN, ICON_RELEASE, ICON_ENV, ICON_FEEDBACK, ICON_RATIO, ICON_DETUNE,
                                    ICON_ACCENT};
    uint32_t x = xop_field(slot), tr = song.sel % NTRK, f;
    int32_t v, lo, hi;
    *label = LBL[x % 13u];
    *icon = ICN[x % 13u];
    *unit = "";
    if (x == XO_OP) {
        fmt_int(val, (int32_t)fop_op + 1);
        *unit = "/6";
        *ratio = (int32_t)fop_op * 200;
        return 1;
    }
    f = xop_f(x, tr);
    v = opx_get(opx_pk[tr], f);
    lo = OPX_FIELDS[f].lo;
    hi = OPX_FIELDS[f].hi;
    *ratio = (int32_t)((int64_t)(v - lo) * 1000 / (hi - lo));
    fmt_int(val, v);
    switch (x) {
    case XO_RATIO:
    case XO_FINE:
        if (f == OPXF_OP(fop_op % 6u, OPXF_OP1_FIXED_FREQ)) {
            if (x == XO_RATIO)
                *label = "FIXED";
            xop_hz(v, val);
            *unit = "HZ";
            *ratio = -1;
        } else if (x == XO_RATIO) {
            xop_coarse(v, val);
        } else {
            fop_dec(val, (uint32_t)v / 1000u, (uint32_t)v % 1000u / 10u, 2u);   /* x0.001: shown to 0.01 */
            *unit = "X";
        }
        return 1;
    case XO_LVL:
        *unit = "%";
        return v != 0;
    case XO_SUS:
    case XO_FB:
    case XO_VEL:
        *unit = "%";
        return 1;
    case XO_ATK:
    case XO_DEC:
    case XO_REL:
        xop_time(v, val, unit);
        return 1;
    case XO_CRV:
        if (!v)
            str_cpy(val, "LIN", 12);
        else if (v == 10)
            str_cpy(val, "EXP", 12);
        return 1;
    case XO_MODE:
        str_cpy(val, v ? "FIXED" : "RATIO", 12);
        *ratio = -1;
        return 1;
    default:                                         /* XO_DTUN */
        val[0] = v > 0 ? '+' : 0;
        fmt_int(val + (v > 0), v);
        *unit = "CT";
        return 1;
    }
}
/* the algorithm a track plays (0..39, 40 USER) and its heard operators (bit k: op k+1), as opx_build takes them */
static uint32_t xop_alg(const track_t *t, uint32_t *heard)
{
    const uint8_t *pk = opx_pk[(t - trk) % NTRK];
    int32_t e = t->p[P_E0];
    uint32_t a = e >= 1 && e <= 41 ? (uint32_t)e - 1u : (uint32_t)opx_get(pk, OPXF_ALGO), k, out = 0;
    for (k = 0; k < 6u; k++)
        out |= (uint32_t)opx_get(pk, OPXF_UA_OUT(k)) << k;
    if (a == OPX_USER && !out)
        a = (uint32_t)opx_get(pk, OPXF_ALGO) == OPX_USER ? 0u : (uint32_t)opx_get(pk, OPXF_ALGO);
    *heard = a == OPX_USER ? out : OPX_ALG[a % OPX_NALG].out;
    return a;
}
#endif

/* the dispatch: an FM6 or an OPX track (OPERATOR 3: OPX only) */
static int fop_any_ok(void)
{
#if FELUCCA_OPX
    if (xop_ok())
        return 1;
#endif
    return fop_ok() && cur_page()->graph != GR_OPX3;
}
static void fop_any_knob(uint32_t slot, int32_t steps)
{
#if FELUCCA_OPX
    if (xop_ok()) {
        if (steps)
            xop_knob(slot, steps);
        return;
    }
#endif
    fop_knob(slot, steps);
}
static int fop_any_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
#if FELUCCA_OPX
    if (xop_ok())
        return xop_card(slot, label, val, unit, ratio, icon);
#endif
    return fop_card(slot, label, val, unit, ratio, icon);
}
