/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX (OPX fork): the patch as stored, and each track's patch.
 *
 * The stored form is the bit-packed record of tools/opx_patch_spec.py (OPX_PK_BYTES, 434 bytes: every opsix program
 * parameter, each in the fewest bits that hold its range). The layout is generated from that spec (build.py ->
 * build/gen/opx_fields.h: each field's bit offset, width, range and default), never written here. A track keeps its
 * patch in that form (opx_pk), so fields the engine does not play yet (the operator modes, LFOs, effects, ...) are
 * kept through every edit, save and load, and a later firmware finds them as they were set.
 *
 * What the engine plays is decoded from it (opx_decode -> opx_patch_t, eng_opx.c). Each track has two decoded copies:
 * the main loop decodes into the one the audio ISR is not using, then publishes it (opx_own, one pointer write), so
 * the ISR never sees half a patch.
 *
 * SLOT (P_E7): F1..F7 load that factory patch into the track (opx_poll, the main loop); OWN (OPX_OWN) is the track's own
 * patch: an edited one (the operator pages, ui_fm6op.c), or one a project brought (opx_store.c); INIT (OPX_INIT, after
 * OWN so that stored OWN values keep their meaning) loads the INIT patch: the spec's defaults, as the opsix's
 * INITIALIZE (OP1 alone heard, algorithm 1, ratio 1, no modulation). */
#include "opx_fields.h"          /* build/gen: tools/opx_patch_spec.py --emit-c */

/* operator k's (0..5) field f, f given as OP1's */
#define OPXF_OP_STRIDE ((uint32_t)(OPXF_OP2_MODE - OPXF_OP1_MODE))
#define OPXF_OP(k, f) ((uint32_t)(f) + (uint32_t)(k) * OPXF_OP_STRIDE)
_Static_assert(OPXF_OP6_LMOD_LFO1_CTRL - OPXF_OP1_LMOD_LFO1_CTRL == 5 * (OPXF_OP2_MODE - OPXF_OP1_MODE), "op stride");
/* USER ALG: op x -> op y (x != y), x's self feedback, x's direct out (x, y 0..5) */
#define OPXF_UA_STRIDE 7u
static uint32_t opxf_ua(uint32_t x, uint32_t y)
{
    return OPXF_UA_OP1_TO_OP2 + x * OPXF_UA_STRIDE + (x == y ? 5u : y < x ? y : y - 1u);
}
#define OPXF_UA_OUT(x) (OPXF_UA_OP1_TO_OP2 + (uint32_t)(x) * OPXF_UA_STRIDE + 6u)
_Static_assert(OPXF_UA_OP6_OUT - OPXF_UA_OP1_TO_OP2 == 5 * 7 + 6, "USER ALG field order");

/* field f of a packed record */
static int32_t opx_get(const uint8_t *pk, uint32_t f)
{
    const opx_field_t *d = &OPX_FIELDS[f % OPXF_COUNT];
    uint32_t off = d->off, v = 0, k;
    for (k = 0; k < d->bits; k++, off++)
        v |= (uint32_t)((pk[off >> 3] >> (off & 7u)) & 1u) << k;
    return (int32_t)v + d->lo;
}
/* field f = v (clamped into its range) */
static void opx_put(uint8_t *pk, uint32_t f, int32_t v)
{
    const opx_field_t *d = &OPX_FIELDS[f % OPXF_COUNT];
    uint32_t off = d->off, u = (uint32_t)(clamp(v, d->lo, d->hi) - d->lo), k;
    for (k = 0; k < d->bits; k++, off++) {
        uint8_t m = (uint8_t)(1u << (off & 7u));
        pk[off >> 3] = (uint8_t)(((u >> k) & 1u) ? pk[off >> 3] | m : pk[off >> 3] & ~m);
    }
}
static void opx_defaults(uint8_t *pk)
{
    uint32_t f;
    memset(pk, 0, OPX_PK_BYTES);
    for (f = 0; f < OPXF_COUNT; f++)
        opx_put(pk, f, OPX_FIELDS[f].def);
}
/* a record this firmware can take: its version, and every field inside its range */
static int opx_pk_ok(const uint8_t *pk)
{
    uint32_t f;
    if (opx_get(pk, OPXF_VERSION) != OPX_FORMAT_VERSION)
        return 0;
    for (f = 0; f < OPXF_COUNT; f++)
        if (opx_get(pk, f) > OPX_FIELDS[f].hi)
            return 0;
    return 1;
}

/* what the engine plays of a record */
static void opx_decode(const uint8_t *pk, opx_patch_t *p)
{
    uint32_t k, j, out = 0;
    memset(p, 0, sizeof *p);
    p->algo = (uint8_t)opx_get(pk, OPXF_ALGO);
    p->alg_fb = (uint8_t)opx_get(pk, OPXF_PROG_ALG_FB);
    for (k = 0; k < OPX_NOP; k++) {
        opx_op_t *o = &p->op[k];
        o->level = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_LEVEL));
        o->coarse = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_COARSE));
        o->fine = (uint16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FINE));
        o->detune = (int8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_DETUNE));
        o->trans = (int8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_TRANS));
        o->fixed = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FREQ_FIXED));
        o->fixed_freq = (uint32_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FIXED_FREQ));
        o->atk = (uint16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_EG_ATK));
        o->dec = (uint16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_EG_DEC));
        o->rel = (uint16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_EG_REL));
        o->sus = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_EG_SUS));
        o->curve = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_EG_CURVE));
        o->fb = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FM_FB));
        o->vel = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_LMOD_VEL));
        o->wave = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_WAVE));
        o->width = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FM_WIDTH));
        o->mode = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_MODE));
        o->ring_depth = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_RING_DEPTH));
        o->ring_shape = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_RING_SHAPE));
        o->fold_gain = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FOLD_GAIN));
        o->fold_oscmix = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FOLD_OSCMIX));
        o->fold_bias = (int8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FOLD_BIAS));
        o->flt_type = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FLT_TYPE));
        o->flt_reso = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FLT_RESO));
        o->flt_oscmix = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FLT_OSCMIX));
        o->flt_cutoff = (int16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FLT_CUTOFF));
        o->fx_type = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FX_TYPE));
        o->fx_oscmix = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FX_OSCMIX));
        o->fx_e = (uint16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FX_E));
        o->fx_f = (uint16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_FX_F));
        o->ktrk_low = (int8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_KTRK_LOW));
        o->ktrk_high = (int8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_KTRK_HIGH));
        o->ktrk_center = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_KTRK_CENTER));
        o->ktrk_lcrv = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_KTRK_LCRV));
        o->ktrk_hcrv = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_KTRK_HCRV));
        o->pm_eg1 = (int16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_PMOD_EG1));   /* (M4) */
        o->pm_lfo1 = (int16_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_PMOD_LFO1));
        o->pm_vel = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_PMOD_VEL));
        o->pm_dest = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_PMOD_VEL_DEST));
        o->lm_lfo1 = (int8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_LMOD_LFO1));
        o->pm_eg1_ctrl = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_PMOD_EG1_CTRL));
        o->pm_lfo1_ctrl = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_PMOD_LFO1_CTRL));
        o->lm_lfo1_ctrl = (uint8_t)opx_get(pk, OPXF_OP(k, OPXF_OP1_LMOD_LFO1_CTRL));
        for (j = 0; j < OPX_NOP; j++)
            p->ua[k][j] = (uint8_t)opx_get(pk, opxf_ua(k, j));
        out |= (uint32_t)opx_get(pk, OPXF_UA_OUT(k)) << k;
    }
    p->ua_out = (uint8_t)out;
    p->eg1.atk = (uint16_t)opx_get(pk, OPXF_EG1_ATK);    /* (M4) */
    p->eg1.dec = (uint16_t)opx_get(pk, OPXF_EG1_DEC);
    p->eg1.rel = (uint16_t)opx_get(pk, OPXF_EG1_REL);
    p->eg1.sus = (uint8_t)opx_get(pk, OPXF_EG1_SUS);
    p->eg1.curve = (uint8_t)opx_get(pk, OPXF_EG1_CURVE);
    p->lfo1.wave = (uint8_t)opx_get(pk, OPXF_LFO1_WAVE);
    p->lfo1.speed = (uint16_t)opx_get(pk, OPXF_LFO1_SPEED);
    p->lfo1.sync = (uint8_t)opx_get(pk, OPXF_LFO1_SYNC);
    p->lfo1.ksync = (uint8_t)opx_get(pk, OPXF_LFO1_KSYNC);
    p->lfo1.phase = (int16_t)opx_get(pk, OPXF_LFO1_PHASE);
    p->lfo1.tempo = (uint8_t)opx_get(pk, OPXF_LFO1_TEMPO);
    p->lfo1.fade = (uint16_t)opx_get(pk, OPXF_LFO1_FADE);
    p->prog_lfo1 = (int16_t)opx_get(pk, OPXF_PROG_LFO1);
    p->prog_trans = (int8_t)opx_get(pk, OPXF_PROG_TRANS);
    p->prog_oct = (int8_t)opx_get(pk, OPXF_PROG_OCT);
    p->prog_lfo1_ctrl = (uint8_t)opx_get(pk, OPXF_PROG_LFO1_CTRL);
}
/* a patch (a factory one) as a record: the defaults, then what the patch sets */
static void opx_encode(const opx_patch_t *p, uint8_t *pk)
{
    uint32_t k, j, i;
    opx_defaults(pk);
    opx_put(pk, OPXF_ALGO, p->algo);
    opx_put(pk, OPXF_PROG_ALG_FB, p->alg_fb);
    for (k = 0; k < OPX_NOP; k++) {
        const opx_op_t *o = &p->op[k];
        opx_put(pk, OPXF_OP(k, OPXF_OP1_LEVEL), o->level);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_COARSE), o->coarse);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_FINE), o->fine);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_DETUNE), o->detune);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_TRANS), o->trans);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_FREQ_FIXED), o->fixed);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_FIXED_FREQ), o->fixed ? (int32_t)o->fixed_freq : 44000);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_EG_ATK), o->atk);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_EG_DEC), o->dec);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_EG_REL), o->rel);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_EG_SUS), o->sus);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_EG_CURVE), o->curve);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_FM_FB), o->fb);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_LMOD_VEL), o->vel);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_WAVE), o->wave);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_FM_WIDTH), o->width);
        opx_put(pk, OPXF_OP(k, OPXF_OP1_MODE), o->mode);
        if (o->mode == OPX_M_RING) {                  /* (a factory patch sets its mode's values; the rest: defaults) */
            opx_put(pk, OPXF_OP(k, OPXF_OP1_RING_DEPTH), o->ring_depth);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_RING_SHAPE), o->ring_shape);
        } else if (o->mode == OPX_M_FILTER || o->mode == OPX_M_FILTER_FM) {
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FLT_TYPE), o->flt_type);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FLT_RESO), o->flt_reso);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FLT_OSCMIX), o->flt_oscmix);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FLT_CUTOFF), o->flt_cutoff);
        } else if (o->mode == OPX_M_EFFECT) {
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FX_TYPE), o->fx_type);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FX_OSCMIX), o->fx_oscmix);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FX_E), o->fx_e);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FX_F), o->fx_f);
        } else if (o->mode == OPX_M_FOLD) {
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FOLD_GAIN), o->fold_gain);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FOLD_OSCMIX), o->fold_oscmix);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_FOLD_BIAS), o->fold_bias);
        }
        if (o->ktrk_center) {
            opx_put(pk, OPXF_OP(k, OPXF_OP1_KTRK_LOW), o->ktrk_low);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_KTRK_HIGH), o->ktrk_high);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_KTRK_CENTER), o->ktrk_center);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_KTRK_LCRV), o->ktrk_lcrv);
            opx_put(pk, OPXF_OP(k, OPXF_OP1_KTRK_HCRV), o->ktrk_hcrv);
        }
        for (j = 0; j < OPX_NOP; j++)
            opx_put(pk, opxf_ua(k, j), p->ua[k][j]);
        opx_put(pk, OPXF_UA_OUT(k), (int32_t)((p->ua_out >> k) & 1u));
    }
    for (i = 0; i < 16u; i++)                         /* the name, padded with spaces */
        opx_put(pk, OPXF_PROG_NAME0 + i, p->name && i < str_len(p->name) ? p->name[i] : ' ');
}

/* ----------------------------------------------------------- per track --- */
#define OPX_OWN OPX_NFACTORY     /* SLOT: the track's own patch (F1..F7 before it) */
#define OPX_INIT (OPX_OWN + 1u)  /* SLOT: the INIT patch (opx_defaults) */
static uint8_t opx_pk[NTRK][OPX_PK_BYTES] __attribute__((section(".pool")));   /* the tracks' patches (main loop) */
static opx_patch_t opx_tp[NTRK][2] __attribute__((section(".pool")));          /* decoded: the ISR's and the next */
static uint8_t opx_slot[NTRK] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};   /* the SLOT value last loaded; 0xFF none yet */
static volatile uint8_t opx_pgen[NTRK];          /* + 1 after each change of a track's patch (the panel redraws) */
_Static_assert(NTRK == 4, "opx_slot's initializer");

/* track tr's patch = record pk (main loop) */
static void opx_set_pk(uint32_t tr, const uint8_t *pk)
{
    opx_patch_t *n;
    tr %= NTRK;
    if (pk != opx_pk[tr])
        memcpy(opx_pk[tr], pk, OPX_PK_BYTES);
    n = opx_own[tr] == &opx_tp[tr][0] ? &opx_tp[tr][1] : &opx_tp[tr][0];
    opx_decode(opx_pk[tr], n);
    RING_PUBLISH();
    opx_own[tr] = n;
    opx_pgen[tr]++;
}
static void opx_load_factory(uint32_t tr, uint32_t i)
{
    uint8_t pk[OPX_PK_BYTES];
    i = i < OPX_NFACTORY ? i : 0u;
    opx_encode(&OPX_FACTORY[i], pk);
    opx_set_pk(tr, pk);
    opx_slot[tr % NTRK] = (uint8_t)i;
}
static void opx_load_init(uint32_t tr)
{
    uint8_t pk[OPX_PK_BYTES];
    opx_defaults(pk);
    opx_set_pk(tr, pk);
    opx_slot[tr % NTRK] = (uint8_t)OPX_INIT;
}
/* track tr's patch by SLOT value s: F n, INIT; anything else F1 (OWN without a patch of its own) */
static void opx_load_slot(uint32_t tr, int32_t s)
{
    if (s == (int32_t)OPX_INIT)
        opx_load_init(tr);
    else
        opx_load_factory(tr, s >= 0 && s < (int32_t)OPX_NFACTORY ? (uint32_t)s : 0u);
}
/* field f of track tr's patch = v (main loop; the operator pages): the patch is the track's own from now on */
static void opx_edit(uint32_t tr, uint32_t f, int32_t v)
{
    tr %= NTRK;
    if (opx_get(opx_pk[tr], f) == clamp(v, OPX_FIELDS[f].lo, OPX_FIELDS[f].hi))
        return;
    opx_put(opx_pk[tr], f, v);
    opx_set_pk(tr, opx_pk[tr]);
    opx_slot[tr] = (uint8_t)OPX_OWN;
    trk[tr].p[P_E7] = (int16_t)OPX_OWN;
}

/* main loop: SLOT turned to F n -> that factory patch; at power-on every track gets F1 (or its SLOT's) */
static void opx_wt_build(void);                       /* (eng_opx.c) */
static void opx_poll(void)
{
    uint32_t tr;
    opx_wt_build();                                    /* (M2: the waveform tables, once: eng_opx.c) */
    for (tr = 0; tr < NTRK; tr++) {
        int32_t s = trk[tr].p[P_E7];
        if (opx_slot[tr] == 0xFFu)                     /* (first time: a patch to edit even before OPX is picked) */
            opx_load_slot(tr, trk[tr].eng_req == ENGI_OPX ? s : 0);
        if (trk[tr].eng_req != ENGI_OPX || s == opx_slot[tr])
            continue;
        if ((s >= 0 && s < (int32_t)OPX_NFACTORY) || s == (int32_t)OPX_INIT)
            opx_load_slot(tr, s);
        else
            opx_slot[tr] = (uint8_t)OPX_OWN;           /* OWN: the patch the track has */
    }
}
