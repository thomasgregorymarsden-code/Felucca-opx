/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX (OPX fork, milestone M0): the engine slot of the coming altered-FM engine (a 6-operator design after the Korg
 * opsix; docs/OPX_STORAGE.md, tools/opx_patch_spec.py). M0 only proves the plumbing: engine 15 (14 is reserved: ENG_ROW14 below), its presets, the
 * tables every engine is in, the editor, the projects and the golden renders. The sound is a plain two-operator FM
 * pair, and its parameters are placeholders that M1 replaces.
 *
 * A modulator (ratio MRAT, detune DTUN in 1/8 semitone steps, self-feedback FB) phase-modulates a carrier (ratio
 * CRAT) by an index MLVL (0..127: up to 2 pi), which falls from MLVL towards zero with MDEC (0 = it stays);
 * KTR lowers the index for higher keys (127: -1/2 per octave above C4). MIX adds the modulator to the output.
 * SHP (the track's shape and its modulation) adds to the index.
 *
 * State: ph[0] the carrier phase, ph[1] the modulator's; s[0] the index envelope (Q15), s[1] s[2] the modulator's
 * last two outputs (feedback). */
static const char *const N_OPX_RATIO[] = {"0.5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
                                          "13", "14", "15", "16"};
static const uint32_t OPX_RATIO_Q16[17] = {32768u, 65536u, 2u << 16, 3u << 16, 4u << 16, 5u << 16, 6u << 16,
                                           7u << 16, 8u << 16, 9u << 16, 10u << 16, 11u << 16, 12u << 16, 13u << 16,
                                           14u << 16, 15u << 16, 16u << 16};

/* phase increment inc x ratio (Q16), 32-bit wrap above the audio band (once per block) */
static uint32_t opx_inc(uint32_t inc, uint32_t r) { return (uint32_t)(((uint64_t)inc * r) >> 16); }

static void opx_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = 0;                          /* a note from silence sounds the same each time */
    v->s[0] = 32767;
    v->s[1] = v->s[2] = 0;
}

static void opx_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t mr = OPX_RATIO_Q16[clamp(p[P_E0], 0, 16)], cr = OPX_RATIO_Q16[clamp(p[P_E5], 0, 16)], i;
    int32_t det = clamp(p[P_E4], -7, 7), env = v->s[0], y1 = v->s[1], y2 = v->s[2];
    int32_t lvl = clamp(p[P_E1] + (m->shape >> 8) - 64, 0, 127), fb = clamp(p[P_E2], 0, 127) * 258;   /* <= 32766 */
    int32_t mix = clamp(p[P_E7], 0, 127) * 258, ktr = clamp(p[P_E6], 0, 127), oct, k0, k1, k, dk;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], cinc = opx_inc(m->inc, cr), minc;
    /* the modulator: its ratio, then the detune (1/8 semitone a step: inc x 2^(det/96)) */
    minc = opx_inc(m->inc, mr);
    if (det)
        minc = opx_inc(minc, (uint32_t)(65536 + det * 473));   /* 2^(1/96) - 1 = 0.00725: 473 / 65536 */
    /* the index over the block: MLVL x the envelope x the key scaling, Q16 of a full cycle per Q15 of modulator */
    oct = (m->pitch16 - 60 * 16) / 192;                /* octaves above C4 */
    k0 = 65535 - (oct > 0 ? clamp(oct * ktr * 258, 0, 49152) : 0);    /* 127: -1/2 an octave, at most -3/4 */
    {
        int32_t dec = clamp(p[P_E3], 0, 127);
        int32_t e1 = dec ? env - ((env >> 6) * (128 - dec) >> 6) - 1 : env;   /* (128 - dec): faster for low MDEC */
        e1 = clamp(e1, 0, 32767);
        k1 = (int32_t)(((int64_t)lvl * 516 * e1 >> 15) * k0 >> 16);           /* 127 x 516 = 65532 */
        k0 = (int32_t)(((int64_t)lvl * 516 * env >> 15) * k0 >> 16);
        env = e1;
    }
    k = k0;
    dk = n ? (k1 - k0) / (int32_t)n : 0;
    for (i = 0; i < n; i++, k += dk) {
        int32_t y, c;
        uint32_t f = (uint32_t)(((y1 + y2) >> 1) * fb) << 1;                   /* feedback: up to 2 pi */
        y = sine_i(ph1 + f);
        y2 = y1;
        y1 = y;
        c = sine_i(ph0 + ((uint32_t)(y * k) << 1));                            /* |y x k| < 2^31 */
        if (mix)
            c += mulq15(y, mix);
        out[i] += voice_amp(c, m, i);
        ph0 += cinc;
        ph1 += minc;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = env;
    v->s[1] = y1;
    v->s[2] = y2;
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

/*   name       MRAT MLVL FB MDEC DTUN CRAT KTR MIX   ATK DEC SUS REL */
static const preset_t OPX_PRESETS[] = {
    {"INIT", {1, 40, 0, 0, 0, 1, 0, 0}, {0, 60, 110, 30}, 0, 0, FX(0, 0, 0, 20), PAT(0)},
    {"BELL", {4, 70, 0, 70, 2, 1, 40, 0}, {0, 100, 0, 90}, 0, 0, FX(0, 10, 20, 60), PAT(0)},
    {"BASS", {1, 90, 40, 60, 0, 1, 64, 0}, {0, 70, 60, 20}, 0, 1, FX(10, 0, 0, 10), PAT(0)},
};

static const engine_t ENG_OPX = {
    .name = "OPX",
    .page_title = {"OP", "MOD"},
    .edit = {
        {"MRAT", F_ENUM, 0, 16, 1, N_OPX_RATIO, 0},
        {"MLVL", F_PCT, 0, 127, 40, 0, 0},
        {"FB", F_PCT, 0, 127, 0, 0, 0},
        {"MDEC", F_PCT, 0, 127, 0, 0, 0},
        {"DTUN", F_INT, -7, 7, 0, 0, 0},
        {"CRAT", F_ENUM, 0, 16, 1, N_OPX_RATIO, 0},
        {"KTR", F_PCT, 0, 127, 0, 0, 0},
        {"MIX", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = OPX_PRESETS,
    .npresets = NELEM(OPX_PRESETS),
    .note_on = opx_note_on,
    .render = opx_render,
    .knob = {P_E1, P_E0, P_E2, P_REL},
    .keep = 0,
};
