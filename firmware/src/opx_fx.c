/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX fork, M5c: the program effects FX1..3 (EFFECT p.61, the Effect Parameter List p.91-94), in series on a part
 * after its voices (the engine's post: voice.c track_render). Included by eng_opx.c.
 *
 * The types and their knobs: tools/opx_patch_spec.py FX_TYPES (OPX_FXT). Kind 0 types play in every build; kinds 1
 * (a buffer of a few KB: CHO U.ENS FLNG RTRY ENH) and 2 (long buffers: the delays, reverbs, GRAIN; experimental) are
 * kept in the patch and pass the sound through until they are built (shown dimmed, as V.PATCH's destinations).
 *
 * A part's voices are mono (or, with SPREAD, a left and a right); the chain runs on one channel until an effect makes
 * two (PAN), then on both; a chain that made two goes to the mix at full width (voice.c spread_wide). The manual gives
 * the knobs and their ranges, not the designs; each is a standard one, its choices noted (assumptions to hear):
 *   PHA      four first-order all-passes swept 200 Hz .. x2^(5 x DEPTH) by a triangle LFO, RESO x0.9 fed back; MIX
 *            50 the deepest notches
 *   PAN      the left gain 1 - DEPTH x the LFO, the right the same LFO PHASE later (50 %: opposite, 0 %: together)
 *   A.WAH    a band-pass (state-variable) from 300 Hz up 3.5 octaves as the input's level (SENS: all of it at -12 dBFS)
 *            rises; SHAPE < 0 sweeps down, |SHAPE| bends the curve
 *   EXC      the input above FREQ (one pole), saturated as BLEND goes up (+100: all saturated), added at MIX
 *   FLTR     a low-pass (state-variable) at CUTOFF (20 Hz x 2^(10 x %)), the triangle LFO +-4 octaves x DEPTH
 *   3EQ      shelves at 250 Hz and 4 kHz (first order, as the operators' SHELV EQ) and a peaking EQ (Q 1) at 2 FREQ
 *   DIST     DRIVE x1 .. x50 into a cubic soft clip, a ~40 Hz high-pass (its DC), a peaking EQ (Q 1) at EQFREQ, LEVEL
 *   G.AMP    a 100 Hz high-pass, the type's gain x DRIVE into an asymmetric soft clip, a ~40 Hz high-pass (its DC),
 *            HEQ a 3 kHz shelf (+-12 dB), the type's cabinet (a two-pole low-pass at its corner), VOLUME; the 16 types
 *            are gains and corners, not models
 *   DEC      held at FREQ, quantised to BIT bits of full scale (fractional bits: the step between), DAMP a low-pass
 *            20 kHz .. ~1 kHz
 *   LIMIT    a peak follower (instant attack, REL) on both channels; gain min(CEIL / THRE, CEIL / level), at most CEIL
 *   COMP     a peak follower (ATTACK 0.5 .. 50 ms, release 150 ms); 4:1 above a threshold SENS lowers (0 .. -40 dB),
 *            made up by 0.3 dB a step of SENS; LEVEL; MIX in parallel
 * Coefficients are made once a block (32 samples); the gains of PAN, LIMIT and COMP move across the block. */
#define OPX_FXFS 48000                    /* 0 dBFS in a part's samples (one voice at full is VOICE_FS: ~-6 dBFS) */
#define OPX_FXLIM (1 << 23)               /* the states' bounds */
#define OPX_FXTAIL 48u                    /* blocks the chain runs on after the last voice (~35 ms) */
typedef struct {
    int32_t st[2][8];                     /* each channel's state */
    int32_t c[5];                         /* a peaking EQ's coefficients (3EQ, DIST), kept while its knobs stay */
    int16_t key[4];                       /* .. the knobs they were made for */
    uint32_t ph;                          /* the LFO's phase (Q32); DEC's hold phase */
    int32_t env, g;                       /* the level follower (both channels'); the gain at the last block's end */
    uint8_t type;                         /* the type the state is for (another: cleared) */
} opx_fxs_t;
static opx_fxs_t opx_fxs[NTRK][3] __attribute__((section(".pool")));
static uint8_t opx_fxtail[NTRK];
static int32_t opx_fxr[CTL];             /* the right channel (the audio ISR's alone) */

/* the types the chain plays (the rest pass the sound): OPX the 13 with no buffer; the OPX-only build
 * (OPX_FX_ALL) all of them, once built. The UI shows the others NOT AVAILABLE, their knobs kept, so a patch moves
 * between the builds unchanged */
#ifndef OPX_FX_ALL
#define OPX_FX_ALL FELUCCA_OPX_ONLY
#endif
static int opx_fx_played(uint32_t t) { return t && t < OPX_NFXT && (OPX_FXT[t].kind == 0u || OPX_FX_ALL); }
/* an effect that changes the sound: on, played, and not a 3EQ at 0 dB all three (the INIT's FX2: skipped, no cost) */
static int opx_fx_live(const opx_fxp_t *x)
{
    return x->on && opx_fx_played(x->type) && !(x->type == 14u && !x->v[0] && !x->v[1] && !x->v[2]);
}

/* 10^(dB / 20), Q15, x0.1 dB; -85.0 and below: 0 (-Inf) */
static int32_t opx_fxdb(int32_t db10)
{
    return db10 <= -850 ? 0 : fm6_exp2(clamp(db10, -900, 480) * 278667 - (9 << 24));
}
/* Hz as a step (Q32 of the sample rate), at most 0.45 fs */
static uint32_t opx_fxhz(uint32_t hz)
{
    uint64_t f = (uint64_t)hz * 97392u;
    return f > OPX_FCMAX ? OPX_FCMAX : f < 4096u ? 4096u : (uint32_t)f;
}
/* base Hz x 2^(oct, Q24) as a step */
static uint32_t opx_fxoct(uint32_t base, int32_t oct)
{
    uint64_t f = ((uint64_t)(uint32_t)fm6_exp2(clamp(oct, -(12 << 24), 5 << 24) - (8 << 24)) * opx_fxhz(base)) >> 16;
    return f > OPX_FCMAX ? OPX_FCMAX : f < 4096u ? 4096u : (uint32_t)f;
}
/* an LFO's step a sample: SPEED in mHz, or (sync) a note value at the song's tempo */
static uint32_t opx_fxlfo(int32_t v, int sync)
{
    if (sync) {                                     /* 2^32 x BPM / 60 / 44100 / (4 x num / den) = 405.8 x BPM x den / num */
        const uint8_t *nv = OPX_SYNC[(uint32_t)v < NELEM(OPX_SYNC) ? (uint32_t)v : OPX_SYNC_DEF];
        return (uint32_t)(406 * clamp(song.g[G_BPM], 20, 300)) * nv[1] / nv[0];
    }
    return (uint32_t)clamp(v, 20, 20000) * 97391u / 1000u;   /* (2^32 / 44100 / 1000) */
}
/* a wave of the LFO at phase ph, 0 .. 32767 (PAN's TYPE: TRI SINE VINTAGE UP DOWN SQUARE) */
static int32_t opx_fxwave(uint32_t w, uint32_t ph)
{
    int32_t p = (int32_t)(ph >> 16), s;
    switch (w) {
    case 1: return (sine_i(ph) + 32767) >> 1;
    case 2: s = sine_i(ph);                          /* (a sine flattened at its ends) */
        s = (3 * s - (int32_t)(((int64_t)s * s * s) >> 30)) >> 1;
        return clamp((s + 32767) >> 1, 0, 32767);
    case 3: return p >> 1;
    case 4: return 32767 - (p >> 1);
    case 5: return p < 32768 ? 32767 : 0;
    default: return p < 32768 ? clamp(p * 2, 0, 32767) : clamp((65535 - p) * 2, 0, 32767);
    }
}
/* a one-pole low-pass step (trapezoidal), G Q14 */
static inline int32_t opx_fxlp(int32_t *s, int32_t x, int32_t g)
{
    int32_t v = (int32_t)(((int64_t)g * (x - *s)) >> 14), y = v + *s;
    *s = clamp(y + v, -OPX_FXLIM, OPX_FXLIM);
    return y;
}
/* a one-pole high-pass (x less opx_fxlp's low-pass, the same g), its state 6 bits finer: opx_fxlp's truncation leaves a dead band
 * (|x - st| x g < 2^15: no move) where a high-pass after a clip held up to ~1 % of full scale as DC (0.2.0) */
static inline int32_t opx_fxhp(int32_t *st, int32_t x, int32_t g)
{
    int32_t v = (int32_t)(((int64_t)g * ((int64_t)x * 64 - *st)) >> 14), y = v + *st;
    *st = clamp(y + v, -OPX_FXLIM * 64, OPX_FXLIM * 64);
    return x - (y >> 6);
}
static int32_t opx_fxg1(uint32_t f) { int32_t g1; opx_tanq(f, &g1); return g1; }
/* a state-variable filter step: st[0..1], c's a1 a2 a3 (Q16); *bp the band-pass; returns the low-pass */
static inline int32_t opx_fxsvf(int32_t *st, const opx_fc_t *c, int32_t x, int32_t *bp)
{
    int32_t v3 = x - st[1], v1, v2;
    v1 = (int32_t)(((int64_t)c->a1 * st[0] + (int64_t)c->a2 * v3) >> 16);
    v2 = st[1] + (int32_t)(((int64_t)c->a2 * st[0] + (int64_t)c->a3 * v3) >> 16);
    st[0] = clamp(2 * v1 - st[0], -OPX_FXLIM, OPX_FXLIM);
    st[1] = clamp(2 * v2 - st[1], -OPX_FXLIM, OPX_FXLIM);
    *bp = v1;
    return v2;
}
/* a peaking EQ (RBJ, Q 1) at f (step) and dB x0.1 (+-18) into c[5] (Q14). 32-bit divides only (the audio code may
 * call no library routine: a 64-bit divide is one): every product under 2^31 */
static void opx_fxpeq(int32_t *c, uint32_t f, int32_t db10)
{
    int32_t sn = sine_i(f), cs = sine_i(f + 0x40000000u), A = opx_db40(clamp(db10, -180, 180)), al = sn >> 1;
    int32_t aa = (al * A) >> 15, ai = (al << 15) / (A ? A : 1), a0 = 32768 + ai;
    c[0] = (32768 + aa) * 16384 / a0;
    c[1] = -2 * cs * 16384 / a0;
    c[2] = (32768 - aa) * 16384 / a0;
    c[3] = c[1];
    c[4] = (32768 - ai) * 16384 / a0;
}
/* (0.2.0: with the rounding's remainder carried to the next sample, st[6]: a filter low in frequency has its poles
 * near 1, and without it the truncation held a constant output after the input had stopped, e.g. DIST's EQ at 80 Hz) */
static inline int32_t opx_fxbq(int32_t *st, const int32_t *c, int32_t x)   /* st: x1 x2 y1 y2, [6] the remainder */
{
    int64_t a = (int64_t)c[0] * x + (int64_t)c[1] * st[0] + (int64_t)c[2] * st[1] - (int64_t)c[3] * st[2] -
                (int64_t)c[4] * st[3] + st[6];
    int32_t y = (int32_t)(a >> 14);
    st[6] = (int32_t)(a & 16383);
    y = clamp(y, -OPX_FXLIM, OPX_FXLIM);
    st[1] = st[0], st[0] = x, st[3] = st[2], st[2] = y;
    return y;
}
/* a cubic soft clip at l: x - x^3 / (3 l^2) up to |x| = l, 2 l / 3 beyond. il: 2^30 / l (a block's: no divide a
 * sample) */
static inline int32_t opx_fxsat(int32_t x, int32_t l, int32_t il)
{
    int32_t u = (int32_t)clamp((int32_t)(((int64_t)x * il) >> 15), -32768, 32768), u3;   /* (x / l, Q15) */
    u3 = (int32_t)(((int64_t)u * u >> 15) * u >> 15);
    return (int32_t)(((int64_t)(u - ((u3 * 10923) >> 15)) * l) >> 15);
}
/* log2 of x > 0, Q24 (the top bit and a line between: within 0.09 octave) */
static int32_t opx_fxlog2(uint32_t x)
{
    int32_t b = 31;
    if (!x)
        return -(32 << 24);
    while (!(x >> b))
        b--;
    return (b << 24) + (int32_t)((((uint64_t)x << (32 - b)) & 0xFFFFFFFFu) >> 8);
}
/* G.AMP's 16 types: the gain (x0.1 dB) at DRIVE 0 .. 100 % (+ 40 dB), the cabinet's corner (Hz), the clip's
 * asymmetry (the negative side's limit, of 32: 32 symmetric) */
static const struct { int16_t gain, cab; uint8_t neg; } OPX_GAMP[16] = {
    {60, 5200, 30}, {90, 5600, 28}, {80, 4800, 30}, {120, 5200, 26}, {140, 4200, 26}, {160, 4000, 24},
    {200, 3800, 24}, {240, 3600, 22}, {280, 3400, 22}, {300, 3300, 20}, {340, 3100, 20}, {180, 4400, 24},
    {40, 6000, 32}, {220, 3500, 22}, {100, 4600, 28}, {130, 4300, 28}};

/* one effect on a block: l (and, st, r) in place; returns whether there are two channels after it */
static uint32_t __attribute__((noinline)) opx_fx_run(opx_fxs_t *s, const opx_fxp_t *p, int32_t *l, int32_t *r,
                                                     uint32_t st)
{
    const int16_t *v = p->v;
    uint32_t i, ch, nch = st ? 2u : 1u, t = p->type;
    int32_t mq = clamp(v[0], 0, 100) * 32767 / 100;  /* (the MIX of the types that have one first) */
    opx_fc_t c;
    switch (t) {
    case 3: case 4: {                                /* PHA, PHA BPM */
        int32_t u = opx_fxwave(0, s->ph), a, fb = clamp(v[3], 0, 1000) * 29491 / 1000;
        s->ph += opx_fxlfo(v[2], t == 4u) * CTL;
        a = 2 * opx_fxg1(opx_fxoct(200, (int32_t)(((int64_t)u * clamp(v[1], 0, 100) * 838861) >> 15))) - 16384;   /* (5 oct) */
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, *q = s->st[ch];
            for (i = 0; i < CTL; i++) {
                int32_t x = b[i], w = clamp(x + (int32_t)(((int64_t)fb * q[4]) >> 15), -OPX_FXLIM, OPX_FXLIM), k;
                for (k = 0; k < 4; k++) {
                    int32_t y = (int32_t)(((int64_t)a * w) >> 14) + q[k];
                    q[k] = clamp(w - (int32_t)(((int64_t)a * y) >> 14), -OPX_FXLIM, OPX_FXLIM);
                    w = clamp(y, -OPX_FXLIM, OPX_FXLIM);
                }
                q[4] = w;
                b[i] = x + (int32_t)(((int64_t)(w - x) * mq) >> 15);
            }
        }
        return st;
    }
    case 5: case 6: {                                /* PAN, PAN BPM: two channels from here */
        uint32_t inc = opx_fxlfo(v[2], t == 6u) * CTL, w = (uint32_t)clamp(v[1], 0, 5);
        uint32_t off = (uint32_t)clamp(v[3], 0, 1000) * 4294967u;   /* (PHASE: 2^32 / 1000 a step) */
        int32_t d = clamp(v[0], 0, 100) * 32767 / 100;
        int32_t gl0 = 32767 - ((d * opx_fxwave(w, s->ph)) >> 15), gr0 = 32767 - ((d * opx_fxwave(w, s->ph + off)) >> 15);
        int32_t gl1, gr1, dl, dr;
        s->ph += inc;
        gl1 = 32767 - ((d * opx_fxwave(w, s->ph)) >> 15), gr1 = 32767 - ((d * opx_fxwave(w, s->ph + off)) >> 15);
        dl = (gl1 - gl0) / CTL, dr = (gr1 - gr0) / CTL;
        for (i = 0; i < CTL; i++, gl0 += dl, gr0 += dr) {
            int32_t x = l[i], y = st ? r[i] : x;
            l[i] = (int32_t)(((int64_t)x * gl0) >> 15);
            r[i] = (int32_t)(((int64_t)y * gr0) >> 15);
        }
        return 1;
    }
    case 10: {                                       /* A.WAH */
        int32_t e = (int32_t)clamp((int32_t)(((int64_t)s->env * clamp(v[1], 0, 1000) * 2863) >> 20), 0, 32767), sh = clamp(v[2], -100, 100), a = sh < 0 ? -sh : sh;
        if (sh < 0)
            e = 32767 - e;
        e += a * (((e * e) >> 15) - e) / 100;
        c.k = 131072 - clamp(v[3], 0, 100) * 1245;
        opx_svf_coef(&c, opx_fxoct(300, e * 1792));
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, *q = s->st[ch];
            for (i = 0; i < CTL; i++) {
                int32_t x = b[i], bp, ax = x < 0 ? -x : x;
                if (!ch)
                    s->env += ax > s->env ? (ax - s->env) >> 6 : (ax - s->env) >> 11;
                opx_fxsvf(q, &c, x, &bp);
                bp = (int32_t)(((int64_t)c.k * bp) >> 16);   /* (k x band: 1 at its peak) */
                b[i] = x + (int32_t)(((int64_t)(bp - x) * mq) >> 15);
            }
        }
        return st;
    }
    case 11: {                                       /* EXC */
        int32_t g = opx_fxg1(opx_fxhz((uint32_t)clamp(v[1], 3000, 24000))), bl = (clamp(v[2], -100, 100) + 100) * 32767 / 200;
        int32_t il = (1 << 30) / (OPX_FXFS / 2);
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, *q = s->st[ch];
            for (i = 0; i < CTL; i++) {
                int32_t x = b[i], h = x - opx_fxlp(&q[0], x, g), z = opx_fxsat(clamp(4 * h, -OPX_FXLIM, OPX_FXLIM), OPX_FXFS / 2, il);
                int32_t e = h + (int32_t)(((int64_t)(z - h) * bl) >> 15);
                b[i] = x + (int32_t)(((int64_t)e * mq) >> 15);
            }
        }
        return st;
    }
    case 13: {                                       /* FLTR: CUTOFF RESO DEPTH SPEED */
        int32_t u = opx_fxwave(0, s->ph) - 16384;     /* (-1 .. +1, Q14) */
        int32_t oct = clamp(v[0], 0, 100) * 1677722 + (int32_t)(((int64_t)u * clamp(v[2], 0, 100) * 671089) >> 14);   /* (10, 4 oct) */
        s->ph += opx_fxlfo(v[3], 0) * CTL;
        c.k = 131072 - clamp(v[1], 0, 100) * 1245;
        opx_svf_coef(&c, opx_fxoct(20, oct));
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, *q = s->st[ch], bp;
            for (i = 0; i < CTL; i++)
                b[i] = opx_fxsvf(q, &c, b[i], &bp);
        }
        return st;
    }
    case 14: case 15: {                              /* 3EQ: 1 LOW, 3 HIGH, 2 GAIN, 2 FREQ; DIST: DRIVE LEVEL EQFREQ EQGAIN */
        int32_t gl = 0, gh = 0, dr = 256, lv = 32768, il = (1 << 30) / OPX_FXFS;
        if (s->key[0] != v[0] || s->key[1] != v[1] || s->key[2] != v[2] || s->key[3] != v[3]) {
            if (t == 14u)
                opx_fxpeq(s->c, opx_fxhz((uint32_t)clamp(v[3], 20, 20000)), v[2]);
            else
                opx_fxpeq(s->c, opx_fxhz((uint32_t)clamp(v[2], 20, 20000)), v[3]);
            memcpy(s->key, v, sizeof s->key);
        }
        if (t == 14u)
            gl = opx_fxdb(v[0]) - 32768, gh = opx_fxdb(v[1]) - 32768;
        else
            dr = 256 + clamp(v[0], 0, 100) * 125, lv = opx_fxdb(v[1]);
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, *q = s->st[ch];
            for (i = 0; i < CTL; i++) {
                int32_t x = b[i], y;
                if (t == 14u) {
                    y = x + (int32_t)(((int64_t)gl * opx_fxlp(&q[4], x, OPX_G250)) >> 15);
                    y += (int32_t)(((int64_t)gh * (x - opx_fxlp(&q[5], x, OPX_G4K))) >> 15);
                    y = opx_fxbq(q, s->c, clamp(y, -OPX_FXLIM, OPX_FXLIM));
                } else {                             /* (a ~40 Hz high-pass after the clip: an uneven wave clipped
                                                      * has DC) */
                    y = opx_fxsat(clamp((int32_t)(((int64_t)x * dr) >> 8), -OPX_FXLIM, OPX_FXLIM), OPX_FXFS, il);
                    y = opx_fxhp(&q[4], y, 47);
                    y = opx_fxbq(q, s->c, y * 3 / 2);
                    y = (int32_t)(((int64_t)y * lv) >> 15);
                }
                b[i] = y;
            }
        }
        return st;
    }
    case 16: {                                       /* G.AMP: DRIVE (x0.1 %) TYPE HEQ VOLUME */
        uint32_t ty = (uint32_t)clamp(v[1], 0, 15);
        int32_t dr = opx_fxdb(OPX_GAMP[ty].gain + clamp(v[0], 0, 1000) * 400 / 1000 - 240);   /* (Q15 of x16) */
        int32_t gh = opx_fxdb((clamp(v[2], 0, 100) - 50) * 240 / 100) - 32768, vol = opx_fxdb(v[3]);
        int32_t ghp = opx_fxg1(opx_fxhz(100)), g3k = opx_fxg1(opx_fxhz(3000)), lneg = OPX_FXFS * OPX_GAMP[ty].neg / 32;
        int32_t il = (1 << 30) / OPX_FXFS, iln = (1 << 30) / lneg;
        c.k = 92682;                                 /* (Q 0.71) */
        opx_svf_coef(&c, opx_fxhz((uint32_t)OPX_GAMP[ty].cab));
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, *q = s->st[ch], bp;
            for (i = 0; i < CTL; i++) {
                int32_t x = b[i] - opx_fxlp(&q[0], b[i], ghp), y;
                y = clamp((int32_t)(((int64_t)x * dr) >> 11), -OPX_FXLIM, OPX_FXLIM);
                y = y >= 0 ? opx_fxsat(y, OPX_FXFS, il) : opx_fxsat(y, lneg, iln);
                y = opx_fxhp(&q[4], y, 47);              /* (0.2.0: ~40 Hz high-pass: the uneven clip's DC, as DIST) */
                y += (int32_t)(((int64_t)gh * (y - opx_fxlp(&q[1], y, g3k))) >> 15);
                y = opx_fxsvf(&q[2], &c, y, &bp);
                b[i] = (int32_t)(((int64_t)y * vol) >> 15);
            }
        }
        return st;
    }
    case 17: {                                       /* DEC: MIX FREQ (x10 Hz) BIT (x0.1) DAMP */
        uint64_t hz = (uint64_t)clamp(v[1], 100, 4800) * 10u;
        uint32_t inc = hz >= 44100u ? 0u : (uint32_t)(hz * 97392u);
        int32_t stp = (int32_t)(((int64_t)fm6_exp2(-clamp(v[2], 40, 240) * 1677722) * (2 * OPX_FXFS)) >> 24);
        uint32_t inv = stp > 1 ? 0xFFFFFFFFu / (uint32_t)stp : 0u;
        int32_t gd = opx_fxg1(opx_fxoct(20000, -clamp(v[3], 0, 100) * 721420));   /* (4.3 oct) */
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, *q = s->st[ch];
            uint32_t ph = s->ph;
            for (i = 0; i < CTL; i++) {
                int32_t x = b[i], y;
                uint32_t o = ph;
                ph += inc;
                if (!inc || ph < o)                  /* (a new sample held: at FREQ) */
                    q[0] = x;
                y = q[0];
                if (inv)                             /* (to the nearest step) */
                    y = (int32_t)((((int64_t)y + stp / 2) * inv) >> 32) * stp;
                if (v[3])                            /* (DAMP 0: none) */
                    y = opx_fxlp(&q[1], y, gd);
                b[i] = x + (int32_t)(((int64_t)(y - x) * mq) >> 15);
            }
            if (ch + 1u == nch)
                s->ph = ph;
        }
        return st;
    }
    case 19: case 20: {                              /* LIMIT: THRE CEIL REL; COMP: MIX SENS ATTACK LEVEL */
        int32_t pk = 0, g0 = s->g ? s->g : 4096, g1, dg, ceil = 0, ra, rr;
        if (t == 19u)
            ra = 65536, rr = (int32_t)(6553600u / (uint32_t)(clamp(v[2], 6, 10000) * 441 + 100));   /* (REL: 0.1 ms) */
        else
            ra = (int32_t)(65536 / (22 + clamp(v[2], 0, 100) * 22)), rr = 65536 / 6615;   /* (0.5 .. 50 ms; 150 ms) */
        for (i = 0; i < CTL; i++) {                   /* the level, both channels */
            int32_t a = l[i] < 0 ? -l[i] : l[i];
            if (st && (r[i] < 0 ? -r[i] : r[i]) > a)
                a = r[i] < 0 ? -r[i] : r[i];
            s->env += (int32_t)(((int64_t)(a - s->env) * (a > s->env ? ra : rr)) >> 16);
            if (s->env > pk)
                pk = s->env;
        }
        if (t == 19u) {                              /* min(CEIL / THRE, CEIL / level), Q12 */
            int32_t th = (int32_t)(((int64_t)opx_fxdb(v[0]) * OPX_FXFS) >> 15);
            ceil = (int32_t)(((int64_t)opx_fxdb(v[1]) * OPX_FXFS) >> 15);
            g1 = (ceil << 12) / (pk > th ? pk : th > 0 ? th : 1);   /* (ceil << 12 under 2^28) */
        } else {                                     /* 4:1 above the threshold, in octaves of level */
            int32_t sn = clamp(v[1], 0, 100), th = opx_fxlog2(OPX_FXFS) - sn * 1114619;   /* (-0.4 dB a step, Q24 octaves) */
            int32_t over = opx_fxlog2((uint32_t)(pk > 1 ? pk : 1)) - th, gl = over > 0 ? -over * 3 / 4 : 0;
            gl += sn * 835964;                           /* (made up: +0.3 dB a step) */
            g1 = (int32_t)(((int64_t)fm6_exp2(clamp(gl, -(20 << 24), 4 << 24) - (12 << 24)) * opx_fxdb(v[3])) >> 15);
        }
        g1 = clamp(g1, 0, 32 << 12);
        dg = (g1 - g0) / (int32_t)CTL;
        for (ch = 0; ch < nch; ch++) {
            int32_t *b = ch ? r : l, g = g0;
            for (i = 0; i < CTL; i++, g += dg) {
                int32_t x = b[i], y = (int32_t)(((int64_t)x * g) >> 12);
                if (t == 19u)
                    y = clamp(y, -ceil, ceil);
                else
                    y = x + (int32_t)(((int64_t)(y - x) * mq) >> 15);
                b[i] = y;
            }
        }
        s->g = g1;
        return st;
    }
    default:                                         /* (not built yet: the sound as it is) */
        return st;
    }
}

/* the engine's post (voice.c track_render): a part's block through FX1..3. out: its voices (with SPREAD, left the
 * left ones' of them); nr: the voices rendered. Returns bit 0: the chain sounds on (a tail), bit 1: it made two
 * channels (left: the left one, out both: the mix at full width). sp: SPREAD is on */
static uint32_t opx_post(track_t *t, int32_t *out, int32_t *left, uint32_t n, uint32_t sp, uint32_t nr)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, k, i, st, made = 0, any = 0;
    const opx_patch_t *p = opx_own[tr];
    if (!p)                                          /* (no own copy yet: the factory patch SLOT names, as opx_build) */
        p = &OPX_FACTORY[(uint32_t)clamp(t->p[P_E7], 0, (int32_t)OPX_NSTRUCT - 1)];
    if (n != CTL)
        return 0;
    for (k = 0; k < 3u; k++)
        any |= (uint32_t)opx_fx_live(&p->fx[k]);
    if (!any) {
        opx_fxtail[tr] = 0;
        return 0;
    }
    if (nr)
        opx_fxtail[tr] = OPX_FXTAIL;
    else if (!opx_fxtail[tr])
        return 0;
    else
        opx_fxtail[tr]--;
    st = sp != 0;                                    /* (SPREAD: the voices' two sides) */
    if (st)
        for (i = 0; i < CTL; i++)
            opx_fxr[i] = out[i] - left[i], out[i] = left[i];
    for (k = 0; k < 3u; k++) {
        const opx_fxp_t *x = &p->fx[k];
        opx_fxs_t *s = &opx_fxs[tr][k];
        uint32_t was = st;
        if (!opx_fx_live(x))
            continue;
        if (s->type != x->type) {                     /* (another type: its own state, from rest) */
            memset(s, 0, sizeof *s);
            s->type = x->type, s->key[0] = 0x7FFF;
        }
        st = opx_fx_run(s, x, out, opx_fxr, st);
        made |= st && !was;
    }
    if (st) {
        for (i = 0; i < CTL; i++)
            left[i] = out[i], out[i] += opx_fxr[i];
    }
    return 1u | (made ? 2u : 0u);
}
