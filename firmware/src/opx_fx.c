/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX fork, M5c: the program effects FX1..3 (EFFECT p.61, the Effect Parameter List p.91-94), in series on a part
 * after its voices (the engine's post: voice.c track_render). Included by eng_opx.c.
 *
 * The types and their knobs: tools/opx_patch_spec.py FX_TYPES (OPX_FXT). Kind 0 types play in every build; kind 1
 * (a buffer of a few KB: CHO U.ENS FLNG RTRY ENH) in the OPX-only firmware (OPX Solo: opx_fx_buf), and kind 2 (long
 * buffers: the delays, reverbs, GRAIN) there too, in Felucca's DELAY bus's line (opx_fx_long). OPX shows both NOT
 * AVAILABLE (dimmed), their knobs kept: they pass the sound.
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
static uint16_t opx_fxtail[NTRK];      /* blocks the chain runs on (the long types: their echoes, their tails) */
static int32_t opx_fxr[CTL];             /* the right channel (the audio ISR's alone) */

/* the types the chain plays (the rest pass the sound): OPX the 13 with no buffer; the OPX-only build
 * (OPX_FX_ALL) all of them, once built. The UI shows the others NOT AVAILABLE, their knobs kept, so a patch moves
 * between the builds unchanged */
#ifndef OPX_FX_ALL
#define OPX_FX_ALL FELUCCA_OPX_ONLY
#endif
/* the long types built so far (M5c part 3, OPX Solo) */
#define OPX_FX_LONG(t) ((t) == 18u || ((t) >= 21u && (t) <= 30u))
static int opx_fx_played(uint32_t t)
{
    return t && t < OPX_NFXT && (OPX_FXT[t].kind == 0u || (OPX_FX_ALL && (OPX_FXT[t].kind == 1u || OPX_FX_LONG(t))));
}
#if OPX_FX_ALL
/* M5c part 2 (the OPX-only firmware): the short-buffer types' delay lines, a channel each for each of part 1's three
 * slots (the one part that plays OPX there): 23 ms a channel, at half scale (int16 of the chain's +-65535) */
#define OPX_FXBN 1024u
static int16_t opx_fxbuf[3][2][OPX_FXBN] __attribute__((section(".pool")));
static inline void opx_dw(int16_t *b, uint32_t w, int32_t x)
{
    b[w & (OPX_FXBN - 1u)] = (int16_t)(clamp(x, -65535, 65535) >> 1);
}
/* the line d8 samples back (Q8, 1 .. 1022), linearly between its samples, at full scale */
static inline int32_t opx_dr(const int16_t *b, uint32_t w, int32_t d8)
{
    uint32_t i = w - (uint32_t)(d8 >> 8);
    int32_t a = b[i & (OPX_FXBN - 1u)], c = b[(i - 1u) & (OPX_FXBN - 1u)];
    return (a + (((c - a) * (d8 & 255)) >> 8)) * 2;
}
#endif
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

#if OPX_FX_ALL
/* M5c part 2: the types with a short buffer (the OPX-only firmware), slot k's lines, w their write position; returns
 * it moved on. Assumptions to hear (the manual gives the knobs, not the designs):
 *   CHO     a line 8 ms long, +-DEPTH x 4 ms by a sine at SPEED, the right PHASE later: two channels; MIX
 *   U.ENS   VOICE taps 6 .. 14 ms, each its own phase of one sine (RATE 0.2 .. 6 Hz), +-DEPTH x 2 ms, every other one
 *           to the left / right
 *   FLNG    0.2 ms + DEPTH x 5 ms by a triangle at SPEED (BPM: a note value), FDBK +-90 % back into the line; MIX 50
 *           the deepest notches; the channels as they come
 *   RTRY    the input in one, crossed over at 800 Hz: the horn (above) a Doppler line 1.5 +- 0.4 ms and a level
 *           swing, the drum (below) a level swing, each heard from two microphones a quarter turn apart; SLOW
 *           0.83 / 0.67 Hz, FAST 6.75 / 5.8 Hz, SPEED +-1 octave, the horn reaching its speed in ~0.7 s, the drum ~3 s;
 *           BAL 0 the drum, 100 the horn; SPKR: the +DIR ones add the input, RESON a 2.5 kHz peak on the horn,
 *           BRIGHT the horn up 4 dB, CX-3 BRIGHT into a soft clip
 *   ENH     the input above 300 Hz, DELAY x 20 ms later, added to the left and taken from the right x DEPTH x SPREAD
 *           (a Haas widening); MIX */
static uint32_t __attribute__((noinline)) opx_fx_buf(uint32_t k, opx_fxs_t *s, const opx_fxp_t *p, int32_t *l,
                                                     int32_t *r, uint32_t st, uint32_t w, int32_t mq)
{
    const int16_t *v = p->v;
    int16_t *b0 = opx_fxbuf[k][0], *b1 = opx_fxbuf[k][1];
    uint32_t i, t = p->type;
    switch (t) {
    case 1: {                                        /* CHO: MIX DEPTH SPEED PHASE */
        uint32_t inc = opx_fxlfo(v[2], 0), off = (uint32_t)clamp(v[3], 0, 1000) * 4294967u, ph = s->ph;
        int32_t dm = clamp(v[1], 0, 100) * 45158 / 200;   /* (+- 4 ms a side, Q8) */
        for (i = 0; i < CTL; i++, w++, ph += inc) {
            int32_t xl = l[i], xr = st ? r[i] : xl, yl, yr;
            opx_dw(b0, w, xl);
            if (st)
                opx_dw(b1, w, xr);
            yl = opx_dr(b0, w, 90317 + ((dm * sine_i(ph)) >> 15));
            yr = opx_dr(st ? b1 : b0, w, 90317 + ((dm * sine_i(ph + off)) >> 15));
            l[i] = xl + (int32_t)(((int64_t)(yl - xl) * mq) >> 15);
            r[i] = xr + (int32_t)(((int64_t)(yr - xr) * mq) >> 15);
        }
        s->ph = ph;
        return w;
    }
    case 2: {                                        /* U.ENS: MIX DEPTH RATE VOICE */
        int32_t rt = clamp(v[2], 0, 100), n = clamp(v[3], 2, 8), j, dm = clamp(v[1], 0, 100) * 226;   /* (+- 2 ms) */
        uint32_t inc = opx_fxlfo(200 + rt * rt * 58 / 100, 0), ph = s->ph, sp = 0xFFFFFFFFu / (uint32_t)n;
        static const int32_t G[5] = {0, 65536, 46341, 37837, 32768};
        int32_t g = G[(n + 1) / 2];                  /* (each side its taps / sqrt(their number): their phases apart) */
        for (i = 0; i < CTL; i++, w++, ph += inc) {
            int32_t xl = l[i], xr = st ? r[i] : xl, al = 0, ar = 0;
            opx_dw(b0, w, st ? (xl + xr) >> 1 : xl);
            for (j = 0; j < n; j++) {
                int32_t y = opx_dr(b0, w, 67738 + j * 90317 / (n - 1) + ((dm * sine_i(ph + (uint32_t)j * sp)) >> 15));
                if (j & 1)
                    ar += y;
                else
                    al += y;
            }
            al = (int32_t)(((int64_t)al * g) >> 16), ar = (int32_t)(((int64_t)ar * g) >> 16);
            l[i] = xl + (int32_t)(((int64_t)(al - xl) * mq) >> 15);
            r[i] = xr + (int32_t)(((int64_t)(ar - xr) * mq) >> 15);
        }
        s->ph = ph;
        return w;
    }
    case 7: case 8: {                                /* FLNG, FLNG BPM: MIX DEPTH SPEED FDBK */
        uint32_t inc = opx_fxlfo(v[2], t == 8u), ph = s->ph, ch;
        int32_t dm = clamp(v[1], 0, 100) * 56448 / 100, fb = clamp(v[3], -100, 100) * 29491 / 100;   /* (5 ms) */
        for (ch = 0; ch < (st ? 2u : 1u); ch++) {
            int32_t *x = ch ? r : l;
            int16_t *b = ch ? b1 : b0;
            uint32_t q = ph + (ch ? 0x40000000u : 0u), ww = w;
            for (i = 0; i < CTL; i++, ww++, q += inc) {
                int32_t u = (int32_t)(q >> 16), tri = u < 32768 ? u : 65535 - u;   /* (0 .. 32767) */
                int32_t y = opx_dr(b, ww, 2258 + ((dm * tri) >> 15));
                opx_dw(b, ww, x[i] + (int32_t)(((int64_t)fb * y) >> 15));
                x[i] = x[i] + (int32_t)(((int64_t)(y - x[i]) * mq) >> 15);
            }
        }
        s->ph = ph + inc * CTL;
        return w + CTL;
    }
    case 9: {                                        /* RTRY: BAL SPD SW SPEED SPKR */
        int32_t sw = clamp(v[1], 0, 1), sk = clamp(v[3], 0, 6), bal = clamp(v[0], 0, 100) * 655;   /* (Q16) */
        int32_t f = fm6_exp2(clamp(v[2], -1000, 1000) * 16777 - (8 << 24));   /* (2^(SPEED / 1000), Q16) */
        int32_t th = (int32_t)(((int64_t)(sw ? 6750 : 830) * f) >> 16), tr = (int32_t)(((int64_t)(sw ? 5800 : 670) * f) >> 16);
        int32_t *q = s->st[1], glo = opx_fxg1(opx_fxhz(800)), gbr = sk >= 4 ? 52000 : 32768;   /* (BRIGHT: +4 dB) */
        int32_t dir = sk & 1 ? 9830 : 0, gh = (bal * 2 > 65536 ? 65536 : bal * 2) >> 1, gr = ((65536 - bal) * 2 > 65536 ? 65536 : (65536 - bal) * 2) >> 1;
        uint32_t hph = s->ph, rph = (uint32_t)q[0], hinc, rinc;
        opx_fc_t c;
        if (!q[1])
            q[1] = th, q[2] = tr;                    /* (from rest: at its speed) */
        q[1] += (th - q[1]) >> 10, q[2] += (tr - q[2]) >> 12;   /* (the horn ~0.7 s, the drum ~3 s, a block a step) */
        hinc = opx_fxlfo(q[1], 0), rinc = opx_fxlfo(q[2], 0);
        c.k = 40000;
        opx_svf_coef(&c, opx_fxhz(2500));
        for (i = 0; i < CTL; i++, w++, hph += hinc, rph += rinc) {
            int32_t x = st ? (l[i] + r[i]) >> 1 : l[i], lo = opx_fxlp(&s->st[0][0], x, glo), hi = x - lo, bp;
            int32_t sh = sine_i(hph), ch_ = sine_i(hph + 0x40000000u), sr = sine_i(rph), cr = sine_i(rph + 0x40000000u);
            int32_t hl, hr, rl, rr;
            if (sk == 2 || sk == 3) {                /* (RESON: a peak on the horn) */
                opx_fxsvf(&s->st[0][2], &c, hi, &bp);
                hi += (int32_t)(((int64_t)c.k * bp) >> 17);
            }
            hi = (int32_t)(((int64_t)hi * gbr) >> 15);
            if (sk == 6)                             /* (CX-3: into a soft clip) */
                hi = opx_fxsat(clamp(hi * 2, -OPX_FXLIM, OPX_FXLIM), OPX_FXFS, (1 << 30) / OPX_FXFS) / 2;
            opx_dw(b0, w, hi);
            hl = opx_dr(b0, w, 16934 + ((4516 * sh) >> 15));   /* (1.5 +- 0.4 ms) */
            hr = opx_dr(b0, w, 16934 + ((4516 * ch_) >> 15));
            hl = (int32_t)(((int64_t)hl * (24576 + (sh >> 2))) >> 15), hr = (int32_t)(((int64_t)hr * (24576 + (ch_ >> 2))) >> 15);
            rl = (int32_t)(((int64_t)lo * (26214 + (sr / 5))) >> 15), rr = (int32_t)(((int64_t)lo * (26214 + (cr / 5))) >> 15);
            l[i] = (int32_t)(((int64_t)hl * gh + (int64_t)rl * gr) >> 15) + (int32_t)(((int64_t)x * dir) >> 15);
            r[i] = (int32_t)(((int64_t)hr * gh + (int64_t)rr * gr) >> 15) + (int32_t)(((int64_t)x * dir) >> 15);
        }
        s->ph = hph, q[0] = (int32_t)rph;
        return w;
    }
    default: {                                       /* ENH: MIX DEPTH DELAY SPREAD */
        int32_t gh = opx_fxg1(opx_fxhz(300)), d8 = 256 + clamp(v[2], 0, 1000) * 225792 / 1000;   /* (to 20 ms) */
        int32_t a = (int32_t)(((int64_t)clamp(v[1], 0, 100) * 327 * clamp(v[3], 0, 1000)) / 1000);   /* (Q15) */
        for (i = 0; i < CTL; i++, w++) {
            int32_t xl = l[i], xr = st ? r[i] : xl, m = (xl + xr) >> 1, h = m - opx_fxlp(&s->st[0][0], m, gh), y;
            opx_dw(b0, w, h);
            y = (int32_t)(((int64_t)opx_dr(b0, w, d8) * a) >> 15);
            l[i] = xl + (int32_t)(((int64_t)y * mq) >> 15);
            r[i] = xr - (int32_t)(((int64_t)y * mq) >> 15);
        }
        return w;
    }
    }
}
#endif

#if OPX_FX_ALL
/* M5c part 3 (the OPX-only firmware): the long types (the delays, reverbs, GRAIN) in Felucca's DELAY bus's line
 * (core.h dly_buf, 64 K samples, 1.49 s): lent while any is live on part 1 (opx_lend: fx.c's DELAY bus is silent
 * meanwhile, and its line is cleared before it plays again). The line is shared out in equal regions among the live
 * long slots (one: all of it; two: half each; three: a third), each cleared before its effect plays (a slice a block:
 * the effect passes the sound meanwhile, ~23 ms at most). Lines hold int16 at half scale (opx_dw's). A delay longer
 * than its line holds at the full rate runs its line at a half or a quarter of it (an average of the samples after a
 * one-pole low-pass, read back linearly between them: the echoes darker, ~8 / ~4 kHz). Times beyond even that are
 * cut to what fits. */
typedef struct {
    uint32_t base, n, clr;                /* the slot's region of dly_buf: where, how long; cleared up to here */
    uint32_t w[8];                        /* the lines' write positions (a delay's: [0] [1]; a reverb's: its 8) */
    uint32_t q, sub;                      /* input samples a line sample (1, 2, 4); the ones taken of the next */
    int32_t acc[2], dlp[2];               /* the decimators' sums, their low-passes */
    int32_t z[16];                        /* the types' filter states */
    uint32_t ph, ph2, cnt;                /* LFOs, a counter */
    uint8_t type;                         /* the type the region was given for */
} opx_fxl_t;
static opx_fxl_t opx_fxl[3] __attribute__((section(".pool")));
#define OPX_FXL_SLICE 2048u               /* samples cleared a block */

/* a line of n samples at b: write x (full scale), w on */
static inline void opx_lw(int16_t *b, uint32_t n, uint32_t *w, int32_t x)
{
    b[*w] = (int16_t)(clamp(x, -65535, 65535) >> 1);
    if (++*w >= n)
        *w = 0;
}
/* the line read d8 (Q8, >= 256) line samples before its write position w (1: the newest), linearly between */
static inline int32_t opx_lr(const int16_t *b, uint32_t n, uint32_t w, int32_t d8)
{
    int32_t pos = (int32_t)(w << 8) - d8, i = pos >> 8, f = pos & 255, a, c;
    uint32_t u = (uint32_t)(i < 0 ? i + (int32_t)n : i), v = u + 1u >= n ? 0u : u + 1u;
    a = b[u], c = b[v];
    return (a + (((c - a) * f) >> 8)) * 2;
}
/* a delay's line at its rate q: x in (each input sample; written each q-th); returns whether one was written */
static inline void opx_lin(opx_fxl_t *l, uint32_t ch, int16_t *b, uint32_t n, int32_t x, int32_t g)
{
    if (l->q == 1u) {
        opx_lw(b, n, &l->w[ch], x);
        return;
    }
    l->acc[ch] += opx_fxlp(&l->dlp[ch], x, g);
    if (l->sub + 1u >= l->q) {
        opx_lw(b, n, &l->w[ch], l->acc[ch] >> (l->q >> 1));   /* (q 1, 2, 4: q >> 1 its log2; no divide) */
        l->acc[ch] = 0;
    }
}
/* the line d8 input samples back (Q8) at its rate, as of the input sample now in (l->sub of q taken) */
static inline int32_t opx_lout(const opx_fxl_t *l, uint32_t ch, const int16_t *b, uint32_t n, int32_t d8)
{
    uint32_t qs = l->q >> 1;                         /* (q 1, 2, 4: its log2; a shift, not a divide, a sample) */
    int32_t dd = (d8 >> qs) - (int32_t)((l->sub << 8) >> qs);
    return opx_lr(b, n, l->w[ch], dd < 256 ? 256 : dd);
}
/* the rate a delay of d input samples needs in a line of n (1, 2, 4: the longest that fits at the slowest) */
static uint32_t opx_lq(int32_t d, uint32_t n)
{
    return d <= (int32_t)n - 8 ? 1u : d <= 2 * ((int32_t)n - 8) ? 2u : 4u;
}
/* ms as input samples Q8 */
static int32_t opx_ms8(int32_t ms) { return clamp(ms, 0, 3000) * 11290; }
/* a note value (OPX_SYNC code) at the song's tempo as input samples Q8 (a whole note: 10584000 / BPM samples) */
static int32_t opx_note8(int32_t c)
{
    const uint8_t *nv = OPX_SYNC[(uint32_t)c < NELEM(OPX_SYNC) ? (uint32_t)c : OPX_SYNC_DEF];
    int32_t whole = 10584000 / clamp(song.g[G_BPM], 20, 300);
    return clamp(whole * nv[0] / nv[1], 0, 3000 * 441 / 10) << 8;
}
/* the rate a long type's line needs in a region of len (its knobs as they are now) */
static uint32_t opx_fxl_rate(const opx_fxp_t *p, uint32_t len)
{
    const int16_t *v = p->v;
    int32_t a, b;
    switch (p->type) {
    case 21: a = opx_ms8(v[2]), b = opx_ms8(v[3]); return opx_lq((a > b ? a : b) >> 8, len / 2u);
    case 22: a = opx_note8(v[2]), b = opx_note8(v[3]); return opx_lq((a > b ? a : b) >> 8, len / 2u);
    case 23: return opx_lq(opx_ms8(v[2]) >> 8, len);
    case 24: return opx_lq(opx_note8(v[2]) >> 8, len);
    case 25: a = opx_ms8(v[2]); return opx_lq((a + (a >> 9)) >> 8, len);
    case 26: a = opx_note8(v[2]); return opx_lq((a + (a >> 9)) >> 8, len);
    case 27: return opx_lq(clamp(v[2], 10, 1600) * 441 / 10 + 70, len);
    default: return 1u;
    }
}
/* the regions for part 1's live long slots (each block, before the chain): a slot whose type or region changed starts
 * again (cleared, its state at rest) */
static void opx_fxl_assign(const opx_patch_t *p)
{
    uint32_t k, n = 0, j = 0, live[3], len;
    for (k = 0; k < 3u; k++) {
        live[k] = opx_fx_live(&p->fx[k]) && OPX_FXT[p->fx[k].type].kind == 2u;
        n += live[k];
    }
    opx_lend = n != 0u;
    len = n ? (DLY_LEN / n) & ~3u : 0u;
    for (k = 0; k < 3u; k++) {
        opx_fxl_t *l = &opx_fxl[k];
        uint32_t ty = live[k] ? p->fx[k].type : 0u, base = live[k] ? len * j++ : 0u;
        if (ty != l->type || base != l->base || (live[k] && len != l->n)) {
            memset(l, 0, sizeof *l);
            l->type = (uint8_t)ty, l->base = base, l->n = live[k] ? len : 0u;
            l->q = live[k] ? opx_fxl_rate(&p->fx[k], len) : 1u;
            if (ty == 28u || ty == 29u)              /* (the reverbs: the size the region is cleared for) */
                l->cnt = ty == 28u ? (uint32_t)clamp(p->fx[k].v[1], 0, 7) + 1u : 1u;
        }
    }
}
/* blocks a long type sounds on after its input stops (its echoes or its tail to ~-80 dB), at most ~30 s */
static uint32_t opx_fxl_tail(const opx_fxp_t *p)
{
    const int16_t *v = p->v;
    int32_t d8 = 0, fb = 0, ms;
    switch (p->type) {
    case 21: d8 = opx_ms8(v[2] > v[3] ? v[2] : v[3]), fb = v[1]; break;
    case 22: d8 = opx_note8(v[2] > v[3] ? v[2] : v[3]), fb = v[1]; break;
    case 23: d8 = opx_ms8(v[2]), fb = 35; break;
    case 24: d8 = opx_note8(v[2]), fb = 35; break;
    case 25: d8 = opx_ms8(v[2]), fb = v[1]; break;
    case 26: d8 = opx_note8(v[2]), fb = v[1]; break;
    case 27: return (uint32_t)clamp(v[2], 10, 1600) * 1378u / 1000u + 64u;
    case 28: return (uint32_t)clamp(v[2], 200, 10000) * 1378u * 3u / 2000u + 64u;
    case 29: return (uint32_t)(500 + clamp(v[2], 0, 100) * 115) * 1378u * 3u / 2000u + 64u;
    case 30: return (uint32_t)clamp(v[2], 49, 4000) * 1378u * 3u / 2000u + 64u;
    default: return 2u * 1378u;              /* (GRAIN: two seconds) */
    }
    /* repeats to -80 dB at FDBK fb %: 80 / -20 log10(fb / 100), from a table of 20 log10 by 10 % steps */
    {
        static const uint8_t DB[11] = {255, 20, 14, 10, 8, 6, 4, 3, 2, 1, 0};   /* (-dB a repeat, rounded down) */
        uint32_t reps = fb >= 100 ? 60u : (uint32_t)(80 / (DB[clamp(fb, 0, 100) / 10] ? DB[clamp(fb, 0, 100) / 10] : 1)) + 1u;
        ms = (int32_t)(((int64_t)d8 * (int32_t)reps) >> 8) / 44;   /* (samples to ms) */
    }
    return (uint32_t)clamp(ms, 0, 30000) * 1378u / 1000u + 64u;
}

/* the reverbs' network (REVERB, SHVERB): four all-passes in series diffuse the input (Schroeder, g diff), then four
 * lines (lengths L x sz / 256) feed back through a 4 x 4 Hadamard matrix, each with a one-pole low-pass (TONE) and its
 * gain for TIME's 60 dB (10^(-3 L / (T fs))); the left the lines 0 and 2, the right 1 and 3. SPACE: line 3's length
 * swings +-6 samples at 0.3 Hz. Lines in the region one after another, each 16 samples longer than read (the swing) */
static const uint16_t OPX_RVAP[4] = {142, 107, 379, 277}, OPX_RVL[4] = {1693, 2011, 2459, 2903};
static uint32_t opx_rv_len(uint32_t sz)              /* the network's samples in its region at size sz */
{
    uint32_t j, n = 0;
    for (j = 0; j < 4u; j++)
        n += OPX_RVAP[j] + (OPX_RVL[j] * sz >> 8) + 16u;
    return n;
}
typedef struct { int32_t g[4], gl, dif, sz, mod; uint32_t n[4], off[4]; } opx_rv_t;
/* one sample in (mono), the left and right out (each line's, as they are read) */
static inline void opx_rv_step(opx_fxl_t *x, int16_t *b, const opx_rv_t *c, int32_t in, int32_t *ol, int32_t *orr)
{
    int32_t o[4], a, bb, cc, d;
    uint32_t j, off = 0;
    for (j = 0; j < 4u; j++) {                       /* (the all-passes: d the line's end, v = x + g d in, y = d - g v) */
        uint32_t n = OPX_RVAP[j];
        int32_t dd = b[off + x->w[j]] * 2, vv = in + (int32_t)(((int64_t)c->dif * dd) >> 15);
        b[off + x->w[j]] = (int16_t)(clamp(vv, -65535, 65535) >> 1);
        if (++x->w[j] >= n)
            x->w[j] = 0;
        in = dd - (int32_t)(((int64_t)c->dif * vv) >> 15);
        off += n;
    }
    for (j = 0; j < 4u; j++) {                       /* (the lines' ends, damped and scaled: a line read where it is
                                                      * written next, its whole length back; SPACE's line 3 swung) */
        if (j == 3u && c->mod)
            o[j] = opx_lr(b + c->off[j], c->n[j], x->w[4 + j],
                          ((int32_t)(c->n[j] - 8u) << 8) + (int32_t)(((int64_t)sine_i(x->ph) * (6 << 8)) >> 15));
        else
            o[j] = b[c->off[j] + x->w[4 + j]] * 2;
        o[j] = opx_fxlp(&x->z[j], o[j], c->gl);
    }
    *ol = o[0] + o[2], *orr = o[1] + o[3];
    for (j = 0; j < 4u; j++)
        o[j] = (int32_t)(((int64_t)o[j] * c->g[j]) >> 15);
    a = o[0] + o[1], bb = o[0] - o[1], cc = o[2] + o[3], d = o[2] - o[3];   /* (Hadamard / 2: lossless) */
    o[0] = (a + cc) >> 1, o[1] = (bb + d) >> 1, o[2] = (a - cc) >> 1, o[3] = (bb - d) >> 1;
    for (j = 0; j < 4u; j++)
        opx_lw(b + c->off[j], c->n[j], &x->w[4 + j], (j & 1u ? -in : in) + o[j]);
}
/* the network's gains for 60 dB in t ms at size sz, its damping at tone Hz */
static void opx_rv_set(opx_rv_t *c, int32_t t, uint32_t sz, uint32_t tone, int32_t dif, int32_t mod)
{
    int32_t k = 3791306 / clamp(t, 50, 30000), j;   /* (log2 of a line's gain: -0.22598 L / T ms, Q24) */
    for (j = 0; j < 4; j++)
        c->g[j] = fm6_exp2(-(int32_t)(OPX_RVL[j] * sz >> 8) * k - (9 << 24));
    c->gl = opx_fxg1(opx_fxhz(tone)), c->dif = dif, c->sz = (int32_t)sz, c->mod = mod;
    {                                                /* (the lines' places after the all-passes: each L x sz / 256 + 16
                                                      * long in the region, the swung one using all of it) */
        uint32_t off = OPX_RVAP[0] + OPX_RVAP[1] + OPX_RVAP[2] + OPX_RVAP[3];
        for (j = 0; j < 4; j++) {
            uint32_t L = OPX_RVL[j] * sz >> 8;
            c->off[j] = off, c->n[j] = j == 3 && mod ? L + 16u : L;
            off += L + 16u;
        }
    }
}
/* The long types, slot k's region; returns whether there are two channels after it. Assumptions to hear (the manual
 * gives the knobs, not the designs); LEVEL is the wet sound's, added to the dry, which passes whole:
 *   DLY, DLY BPM    a line each side, DLY L / DLY R long, FDBK back into its own side (no cross-feed)
 *   PDLY, PDL BPM   one line DLY long, a fixed 35 % fed back (it has no FDBK knob), its echo panned by a sine at
 *                   SPEED (BPM: a note value), DEPTH how far
 *   TDLY, TDL BPM   one line DLY long, a slow wow (0.6 Hz, +-0.15 % of DLY), FDBK through a soft clip; DAMP > 0 a
 *                   low-pass in the loop (12 kHz .. 1.5 kHz as it goes to 100 %), < 0 a high-pass (40 Hz .. 1.2 kHz);
 *                   the echoes in the middle
 *   REF             one line, eight taps over TIME (one at TIME), every other one left / right: SHARP close early taps
 *                   falling fast, LOOSE spread taps falling slowly, MOD LOOSE's taps swinging +-1.5 ms at 0.7 Hz,
 *                   REVERSE SHARP's taps rising to the last; HEQ a 3 kHz shelf on them (+-15 dB)
 *   REVERB          opx_rv_step's network, TYPE its size (HALL 1, L.HALL 1.45, S.HALL 0.7, CHAMBR 0.55, PLATE 0.45 more
 *                   diffused, ROOM 0.3, W.ROOM 0.35 darker, SPACE 1.8 swung), TIME to -60 dB, TONE the damping
 *   SHVERB          the HALL network, its output pitch-shifted back into its input (two heads over 2048 samples,
 *                   crossfaded): CLEAR +12, RISER +7, SUBMAR -12, HORROR -1 semitones; TIME 0.5 .. 12 s, TONE
 *                   1 .. 16 kHz
 *   SPRING          a loop of LENGTH (25 .. 75 ms) through twelve first-order all-passes (CHIRP their coefficient
 *                   0.3 .. 0.8: the spring's dispersion, each trip again) and a 4.5 kHz low-pass, a 110 Hz low cut in;
 *                   TIME its 60 dB; the left the loop's end, the right a tap at 0.7 of it
 *   GRAIN           at each SPEED (a note value at the tempo; at most half the region) the first TIME of it (10 ms ..
 *                   all of it) plays live and is kept as a grain, then played over and over till the next (2 ms
 *                   crossfades where it loops); RETRIG starts the grid again: NOTE and VEL at each note-on,
 *                   MODW and DAMPER as they rise past half, LFO1..3 as the LFO rises through 0; MIX dry / grain */
static uint32_t __attribute__((noinline)) opx_fx_long(uint32_t k, opx_fxs_t *s, const opx_fxp_t *p, int32_t *l,
                                                      int32_t *r, uint32_t st, int32_t mq)
{
    opx_fxl_t *x = &opx_fxl[k];
    const int16_t *v = p->v;
    int16_t *b = dly_buf + x->base;
    uint32_t i, t = p->type, n = x->n;
    int32_t lev = clamp(v[0], 0, 100) * 327;          /* (LEVEL, Q15) */
    (void)s;
    if (!n)
        return st;
    if (x->clr < n) {                                /* (its region from rest: cleared a slice a block, the sound passed) */
        uint32_t m = n - x->clr < OPX_FXL_SLICE ? n - x->clr : OPX_FXL_SLICE;
        memset(b + x->clr, 0, m * sizeof *b);
        x->clr += m;
        return st;
    }
    switch (t) {
    case 21: case 22: {                              /* DLY, DLY BPM: LEVEL FDBK DLY L DLY R */
        uint32_t h = n / 2u, q, ch;
        int32_t dl = t == 21u ? opx_ms8(v[2]) : opx_note8(v[2]), dr = t == 21u ? opx_ms8(v[3]) : opx_note8(v[3]);
        int32_t fb = clamp(v[1], 0, 100) * 324, g;    /* (FDBK, Q15: 100 % = 0.99) */
        q = opx_fxl_rate(p, n);
        if (q != x->q) {                             /* (another rate: the lines from rest) */
            uint32_t keep = x->base, nn = x->n;
            uint8_t ty = x->type;
            memset(x, 0, sizeof *x);
            x->base = keep, x->n = nn, x->type = ty, x->q = q;
            return st;
        }
        dl = clamp(dl, 256 * 2 * (int32_t)q, ((int32_t)h - 4) * (int32_t)q * 256);
        dr = clamp(dr, 256 * 2 * (int32_t)q, ((int32_t)h - 4) * (int32_t)q * 256);
        g = q == 2u ? opx_fxg1(opx_fxhz(8000)) : opx_fxg1(opx_fxhz(4000));
        for (i = 0; i < CTL; i++) {
            int32_t xl = l[i], xr = st ? r[i] : xl, yl, yr;
            yl = opx_lout(x, 0, b, h, dl), yr = opx_lout(x, 1, b + h, h, dr);
            opx_lin(x, 0, b, h, xl + (int32_t)(((int64_t)yl * fb) >> 15), g);
            opx_lin(x, 1, b + h, h, xr + (int32_t)(((int64_t)yr * fb) >> 15), g);
            if (++x->sub >= x->q)
                x->sub = 0;
            l[i] = xl + (int32_t)(((int64_t)yl * lev) >> 15);
            r[i] = xr + (int32_t)(((int64_t)yr * lev) >> 15);
        }
        (void)ch;
        return 1u;
    }
    case 23: case 24: {                              /* PDLY, PDL BPM: LEVEL DEPTH DLY SPEED */
        int32_t d = t == 23u ? opx_ms8(v[2]) : opx_note8(v[2]), dep = clamp(v[1], 0, 100) * 327, g;
        uint32_t q = opx_fxl_rate(p, n), inc = opx_fxlfo(v[3], t == 24u), ph = x->ph;
        if (q != x->q) {
            uint32_t keep = x->base, nn = x->n;
            uint8_t ty = x->type;
            memset(x, 0, sizeof *x);
            x->base = keep, x->n = nn, x->type = ty, x->q = q;
            return st;
        }
        d = clamp(d, 256 * 2 * (int32_t)q, ((int32_t)n - 4) * (int32_t)q * 256);
        g = q == 2u ? opx_fxg1(opx_fxhz(8000)) : opx_fxg1(opx_fxhz(4000));
        for (i = 0; i < CTL; i++, ph += inc) {
            int32_t xl = l[i], xr = st ? r[i] : xl, m = (xl + xr) >> 1, y = opx_lout(x, 0, b, n, d);
            int32_t pan = (int32_t)(((int64_t)sine_i(ph) * dep) >> 15);   /* (-1 .. 1 x DEPTH, Q15) */
            int32_t gl = 23170 - (int32_t)(((int64_t)pan * 23170) >> 15), gr = 23170 + (int32_t)(((int64_t)pan * 23170) >> 15);
            opx_lin(x, 0, b, n, m + (int32_t)(((int64_t)y * 11469) >> 15), g);   /* (35 % back) */
            if (++x->sub >= x->q)
                x->sub = 0;
            y = (int32_t)(((int64_t)y * lev) >> 15);
            l[i] = xl + (int32_t)(((int64_t)y * gl) >> 15);
            r[i] = xr + (int32_t)(((int64_t)y * gr) >> 15);
        }
        x->ph = ph;
        return 1u;
    }
    case 25: case 26: {                              /* TDLY, TDL BPM: LEVEL FDBK DLY DAMP */
        int32_t d = t == 25u ? opx_ms8(v[2]) : opx_note8(v[2]), fb = clamp(v[1], 0, 100) * 324, dm = clamp(v[3], -1000, 1000);
        int32_t gd = dm > 0 ? opx_fxg1(opx_fxoct(12000, -(int32_t)(((int64_t)dm * 50331648) / 1000))) :   /* (3 oct) */
                     opx_fxg1(opx_fxoct(40, (int32_t)(((int64_t)-dm * 81759191) / 1000))), g;   /* (~4.9 oct) */
        uint32_t q = opx_fxl_rate(p, n), inc = 58434u, ph = x->ph;   /* (0.6 Hz) */
        if (q != x->q) {
            uint32_t keep = x->base, nn = x->n;
            uint8_t ty = x->type;
            memset(x, 0, sizeof *x);
            x->base = keep, x->n = nn, x->type = ty, x->q = q;
            return st;
        }
        d = clamp(d, 256 * 4 * (int32_t)q, ((int32_t)n - 8) * (int32_t)q * 256 - (d >> 9));
        g = q == 2u ? opx_fxg1(opx_fxhz(8000)) : opx_fxg1(opx_fxhz(4000));
        for (i = 0; i < CTL; i++, ph += inc) {
            int32_t xl = l[i], xr = st ? r[i] : xl, m = (xl + xr) >> 1;
            int32_t y = opx_lout(x, 0, b, n, d + (int32_t)(((int64_t)(d >> 10) * sine_i(ph)) >> 15) * 3 / 2), f;   /* (+-0.15 %) */
            f = dm > 0 ? opx_fxlp(&x->z[0], y, gd) : dm < 0 ? opx_fxhp(&x->z[0], y, gd) : y;
            f = opx_fxsat((int32_t)(((int64_t)f * fb) >> 15), OPX_FXFS, (1 << 30) / OPX_FXFS);
            opx_lin(x, 0, b, n, m + f, g);
            if (++x->sub >= x->q)
                x->sub = 0;
            y = (int32_t)(((int64_t)y * lev) >> 15);
            l[i] = xl + y;
            r[i] = xr + y;
        }
        x->ph = ph;
        return st;
    }
    case 18: {                                       /* GRAIN: MIX TIME SPEED RETRIG */
        int32_t per = clamp(opx_note8(v[2]) >> 8, 882, (int32_t)n / 2), g = 441 + (int32_t)(((int64_t)(per - 441) *
                      clamp(v[1], 0, 100)) / 100), rt = clamp(v[3], 0, 7), trig = 0, now;
        const track_t *tk = &trk[0];
        switch (rt) {                                /* (the retrigger: the source's event since the last block) */
        case 1: now = tk->mw >= 64; trig = now && !x->z[1]; x->z[1] = now; break;
        case 2: now = opx_ctl_pedal[0] != 0; trig = now && !x->z[1]; x->z[1] = now; break;
        case 3: case 4: trig = x->z[1] != (int32_t)opx_non[0]; x->z[1] = (int32_t)opx_non[0]; break;
        case 5: case 6: case 7: now = opx_lfov[0][rt - 5] >= 0; trig = now && !x->z[1]; x->z[1] = now; break;
        default: break;
        }
        if (!x->z[2]) {                              /* (the first block: no event, the grid from here) */
            x->z[2] = 1, trig = 0;
            x->cnt = (uint32_t)per;                  /* (a grain from here) */
        }
        if (trig)
            x->cnt = (uint32_t)per;
        for (i = 0; i < CTL; i++) {
            int32_t xl = l[i], xr = st ? r[i] : xl, m = (xl + xr) >> 1, gl = x->z[3] ? x->z[3] : g, ph, d, y, f;
            uint32_t a;
            opx_lw(b, n, &x->w[0], m);
            if (x->cnt >= (uint32_t)per) {           /* (a new grain: the last g samples, from here) */
                x->cnt = 0, x->z[4] = 0;
                x->z[3] = gl = g;
            }
            ph = x->z[4];                            /* (the place in the grain: cnt mod its length, counted) */
            if (x->cnt < (uint32_t)gl)               /* (the grain's first pass: the live sound, recorded) */
                y = m;
            else {
                d = (int32_t)x->cnt - ph + 1;        /* (back to the grain's sample now: 1 the newest; <= per <= n / 2) */
                a = x->w[0] >= (uint32_t)d ? x->w[0] - (uint32_t)d : x->w[0] + n - (uint32_t)d;
                y = b[a] * 2;
                if (ph < 88) {                       /* (2 ms: from where the last pass ran on into this one) */
                    uint32_t a2 = a + (uint32_t)gl >= n ? a + (uint32_t)gl - n : a + (uint32_t)gl;
                    f = ph * 372;                    /* (Q15) */
                    y = (int32_t)(((int64_t)y * f + (int64_t)(x->cnt == (uint32_t)(gl + ph) ? m : b[a2] * 2) *
                                   (32768 - f)) >> 15);
                }
            }
            x->cnt++;
            if (++x->z[4] >= gl)
                x->z[4] = 0;
            l[i] = xl + (int32_t)(((int64_t)(y - xl) * mq) >> 15);
            r[i] = xr + (int32_t)(((int64_t)(y - xr) * mq) >> 15);
        }
        return 1u;
    }
    case 27: {                                       /* REF: LEVEL TYPE TIME HEQ */
        static const uint8_t POS[2][8] = {{20, 38, 59, 84, 115, 148, 195, 255}, {13, 43, 54, 92, 105, 159, 182, 243}};
        static const int16_t GN[2][8] = {{29491, 24576, 20316, 16384, 13107, 9830, 7209, 4915},
                                         {22938, 21299, 19661, 18022, 16384, 14746, 13107, 11469}};
        int32_t ty = clamp(v[1], 0, 3), d = clamp(v[2], 10, 1600) * 11290, g, sh = opx_fxdb(clamp(v[3], -150, 150)) - 32768;
        int32_t gh = opx_fxg1(opx_fxhz(3000)), ps = ty == 1 || ty == 2, j;
        uint32_t q = opx_lq(d >> 8, n), inc = 68174u;    /* (0.7 Hz) */
        if (q != x->q) {
            uint32_t keep = x->base, nn = x->n;
            uint8_t tt = x->type;
            memset(x, 0, sizeof *x);
            x->base = keep, x->n = nn, x->type = tt, x->q = q;
            return st;
        }
        g = q == 2u ? opx_fxg1(opx_fxhz(8000)) : opx_fxg1(opx_fxhz(4000));
        d = clamp(d, 256 * 4 * (int32_t)q, ((int32_t)n - 8) * (int32_t)q * 256 - 16934);   /* (MOD's +-1.5 ms) */
        {
            int32_t dd[8], gn[8];                    /* (the taps' delays and gains a block: MOD's 0.7 Hz moves little in 32) */
            for (j = 0; j < 8; j++) {
                dd[j] = (int32_t)(((int64_t)d * POS[ps][j]) >> 8);
                if (ty == 2)
                    dd[j] += (int32_t)(((int64_t)sine_i(x->ph + (uint32_t)j * 0x20000000u) * 16934) >> 15);
                dd[j] = dd[j] < 512 * (int32_t)q ? 512 * (int32_t)q : dd[j];
                gn[j] = GN[ps][ty == 3 ? 7 - j : j];
            }
            x->ph += inc * CTL;
        for (i = 0; i < CTL; i++) {
            int32_t xl = l[i], xr = st ? r[i] : xl, al = 0, ar = 0;
            opx_lin(x, 0, b, n, (xl + xr) >> 1, g);
            for (j = 0; j < 8; j += 2) {
                al += (int32_t)(((int64_t)opx_lout(x, 0, b, n, dd[j]) * gn[j]) >> 15);
                ar += (int32_t)(((int64_t)opx_lout(x, 0, b, n, dd[j + 1]) * gn[j + 1]) >> 15);
            }
            if (++x->sub >= x->q)
                x->sub = 0;
            al += (int32_t)(((int64_t)opx_fxhp(&x->z[0], al, gh) * sh) >> 15);   /* (HEQ: the highs by its gain) */
            ar += (int32_t)(((int64_t)opx_fxhp(&x->z[1], ar, gh) * sh) >> 15);
            l[i] = xl + (int32_t)(((int64_t)al * lev) >> 16);
            r[i] = xr + (int32_t)(((int64_t)ar * lev) >> 16);
        }
        }
        return 1u;
    }
    case 28: case 29: {                              /* REVERB: LEVEL TYPE TIME TONE; SHVERB: LEVEL TYPE TIME TONE */
        static const uint16_t SZ[8] = {256, 371, 179, 141, 115, 77, 90, 461};
        static const int32_t SHIFT[4] = {-32768, -16329, 16384, 1838};   /* (the heads' delay a sample: 1 - the ratio,
                                                                          * Q15: +12 +7 -12 -1 semitones) */
        int32_t ty = t == 28u ? clamp(v[1], 0, 7) : 0, tm, tone, dif = ty == 4 ? 24576 : 19661;
        uint32_t sz = SZ[ty], need = opx_rv_len(sz) + (t == 29u ? 2048u : 0u);
        opx_rv_t c;
        if (x->cnt != (uint32_t)ty + 1u) {           /* (another size: the network from rest) */
            uint32_t keep = x->base, nn = x->n;
            uint8_t tt = x->type;
            memset(x, 0, sizeof *x);
            x->base = keep, x->n = nn, x->type = tt, x->q = 1, x->cnt = (uint32_t)ty + 1u;
            return st;
        }
        if (need > n)                                /* (cannot be: the smallest region holds the largest) */
            return st;
        if (t == 28u)
            tm = clamp(v[2], 200, 10000), tone = clamp(v[3], 500, 20000) * (ty == 6 ? 3 : 5) / 5;
        else
            tm = 500 + clamp(v[2], 0, 100) * 115, tone = (int32_t)(((int64_t)1000 * fm6_exp2(clamp(v[3], 0, 100) * 671089 - (8 << 24))) >> 16);
        opx_rv_set(&c, tm, sz, (uint32_t)tone, dif, ty == 7);
        for (i = 0; i < CTL; i++) {
            int32_t xl = l[i], xr = st ? r[i] : xl, in = (xl + xr) >> 2, ol, orr;
            if (t == 29u) {                          /* (SHVERB: the wet, shifted, back in) */
                int16_t *pb = b + opx_rv_len(sz);
                uint32_t sh = (uint32_t)clamp(v[1], 0, 3), h;
                int32_t a0, a1, ph = x->z[6];
                pb[x->ph2 & 2047u] = (int16_t)(clamp(x->z[7], -65535, 65535) >> 1);   /* (the last wet, mono) */
                ph = (ph + SHIFT[sh]) & 0x3FFFFFF;   /* (head A's delay, Q15 samples, within the 2048) */
                h = (uint32_t)ph >> 15;              /* (0 .. 2047: head A's delay) */
                a0 = opx_lr(pb, 2048u, (x->ph2 + 1u) & 2047u, (int32_t)((h + 2u) << 8));
                a1 = opx_lr(pb, 2048u, (x->ph2 + 1u) & 2047u, (int32_t)((((h + 1024u) & 2047u) + 2u) << 8));
                {                                    /* (a triangle across each head's sweep: one silent at its jump) */
                    int32_t wa = (int32_t)(h < 1024u ? h : 2047u - h) * 32;
                    in += (int32_t)(((int64_t)a0 * wa + (int64_t)a1 * (32767 - wa)) >> 16) * 2 / 5;
                }
                x->ph2++;
                x->z[6] = ph;
            }
            opx_rv_step(x, b, &c, in, &ol, &orr);
            if (t == 29u)
                x->z[7] = (ol + orr) >> 1;
            x->ph += 6067u;                          /* (0.3 Hz: SPACE's swing) */
            l[i] = xl + (int32_t)(((int64_t)ol * lev) >> 16);
            r[i] = xr + (int32_t)(((int64_t)orr * lev) >> 16);
        }
        return 1u;
    }
    case 30: {                                       /* SPRING: LEVEL LENGTH TIME CHIRP */
        int32_t len = (25 + clamp(v[1], 0, 100) / 2) * 441 / 10, a = -(9830 + clamp(v[3], 0, 100) * 164), j;   /* (Q15) */
        int32_t k = 3791306 / clamp(v[2], 49, 4000), gfb = fm6_exp2(-len * k - (9 << 24));
        int32_t glo = opx_fxg1(opx_fxhz(110)), ghi = opx_fxg1(opx_fxhz(4500));
        int32_t *ap = x->z + 4;                     /* (the twelve all-passes' states: z[4..15]) */
        for (i = 0; i < CTL; i++) {
            int32_t xl = l[i], xr = st ? r[i] : xl, y = opx_lr(b, n, x->w[0], len << 8), u, tap;
            tap = opx_lr(b, n, x->w[0], (len * 7 / 10) << 8);
            u = (int32_t)(((int64_t)y * gfb) >> 15) + opx_fxhp(&x->z[0], (xl + xr) >> 2, glo);   /* (the low cut on the
                                                                                    * way in: in the loop it shortened TIME's
                                                                                    * low notes, 1 dB a trip at 220 Hz) */
            for (j = 0; j < 12; j++) {               /* (first order: y = a x + s; s = x - a y) */
                int32_t o = (int32_t)(((int64_t)a * u) >> 15) + ap[j];
                ap[j] = clamp(u - (int32_t)(((int64_t)a * o) >> 15), -OPX_FXLIM, OPX_FXLIM);
                u = o;
            }
            u = opx_fxlp(&x->z[1], u, ghi);
            opx_lw(b, n, &x->w[0], u);
            l[i] = xl + (int32_t)(((int64_t)y * lev) >> 15);
            r[i] = xr + (int32_t)(((int64_t)tap * lev) >> 15);
        }
        return 1u;
    }
    default:                                         /* (not built yet: the sound as it is) */
        return st;
    }
}
#endif

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
#if OPX_FX_ALL
    case 1: case 2: case 7: case 8: case 9: case 12: {
        uint32_t k = (uint32_t)(s - &opx_fxs[0][0]), w = (uint32_t)s->g;
        if (k >= 3u)                                 /* (the part with the buffers: part 1, OPX's in this firmware) */
            return st;
        w = opx_fx_buf(k, s, p, l, r, st, w, mq);
        s->g = (int32_t)(w & 0x7FFFFFFFu);
        return t == 7u || t == 8u ? st : 1u;
    }
    case 18: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29: case 30: {
        uint32_t k = (uint32_t)(s - &opx_fxs[0][0]);
        return k < 3u ? opx_fx_long(k, s, p, l, r, st, mq) : st;
    }
#endif
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
#if OPX_FX_ALL
        if (!tr && opx_lend) {                       /* (no long type now: the line back to the DELAY bus) */
            opx_lend = 0;
            memset(opx_fxl, 0, sizeof opx_fxl);
        }
#endif
        return 0;
    }
#if OPX_FX_ALL
    if (!tr)
        opx_fxl_assign(p);
#endif
    if (nr) {
        uint32_t tl = OPX_FXTAIL;
#if OPX_FX_ALL
        for (k = 0; k < 3u; k++)                     /* (the long types: their echoes and tails) */
            if (!tr && opx_fxl[k].n) {
                uint32_t u = opx_fxl_tail(&p->fx[k]);
                tl = u > tl ? u : tl;
            }
#endif
        opx_fxtail[tr] = (uint16_t)(tl > 65535u ? 65535u : tl);
    }
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
#if OPX_FX_ALL
            if (!tr)                                 /* (and its lines silent) */
                memset(opx_fxbuf[k], 0, sizeof opx_fxbuf[k]);
#endif
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
