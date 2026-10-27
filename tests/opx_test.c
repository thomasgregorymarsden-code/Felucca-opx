/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX engine test (firmware/src/eng_opx.c, OPX fork, M1), through hostsim.c as fm6_test.c.
 *   build/host/opx_test [DEMODIR]          (run_tests.sh: build/opx_demo)
 * 1. reference: every algorithm (1..40, USER) against a floating-point re-implementation written here from the
 *    engine's stated rules (operators 6 -> 1 each sample, an input from a lower operator one sample late, self
 *    feedback the mean of the last two outputs, LEVEL^3, 2 cycles of phase at a full input), modulators at moderate
 *    levels (no chaos): over the first 4096 samples the error stays below -45 dB of the signal (what is left is the
 *    sine table's interpolation through the modulation; a wrong connection shows near 0 dB, an input one sample
 *    early or late near -36 dB). The algorithms' carriers and inputs are checked against the charts (Owner's Manual p.83)
 *    as a table of strings written apart from the engine's bit masks.
 * 2. envelopes: an operator's attack reaches 90 % at its time (+-10 %), decay settles at SUS, release falls to
 *    -40 dB within its time; CURVE 10 is below LIN half way down; the voice ends once released (engine_t.done).
 * 3. feedback: FB 0 is a sine (2nd harmonic < -60 dB); FB 75 % has harmonics 2..5 each within 10 dB of 1/n (the
 *    manual's "sawtooth-like"); ALG FB 0 opens algorithm 34's loop (no output change from op 4 -> 6).
 * 4. velocity: VEL 100 % makes velocity 64 about 6 dB below 127; VEL 0 makes no difference.
 * 5. a note from silence renders the same samples twice; every factory patch, C1..C7 at velocity 127: no
 *    sample beyond 2 x VOICE_FS, |mean| < 1 % of VOICE_FS.
 * 6. cost: host CPU time per voice against FM6 (six voices held, six operators sounding each), the ratio printed
 *    (no instruction counter here; the device budget is FM6's, which runs six voices).
 * 7. demos into DEMODIR: every factory preset playing its suggested pattern. */
#define OPX_FIELD_NAMES 1        /* (--dump: every field by name) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <time.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("opx: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ------------------------------------------------------------- set-up --- */
static opx_patch_t P;            /* the patch under test (opx_own[0]) */
static const opx_op_t OP_FLAT = {80, 7, 1000, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0};   /* sustained, instant */

static void setup(const opx_patch_t *p, const int16_t *e)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(eng_mem, 0, sizeof eng_mem);
    memset(opx_rt, 0, sizeof opx_rt);
    host_tracks_init();
    host_preset(&trk[0], ENGI_OPX, 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e ? e[i] : 0;
    opx_own[0] = p;
    memset(opx_fxs, 0, sizeof opx_fxs);              /* (M5c: the effects from rest: each render the same) */
    memset(opx_fxtail, 0, sizeof opx_fxtail);
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}

static int32_t opx_t_cut, opx_t_shape;          /* (Felucca's FLT / SHP routes: test_macros_off) */
/* the engine alone, one voice: note at vel, n samples, gate released at rel (0: held). y: the render's out[] */
static double *opx_t_right;                         /* M5c: the right channel, when the effects make two (else y) */
static uint32_t opx_t_wide;                         /* .. they did */
static void engine_render(uint32_t note, uint32_t vel, double *y, uint32_t n, uint32_t rel, int *ended)
{
    static int32_t b[CTL], lb[CTL];
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
    vmod_t m;
    uint32_t i, k;
    memset(v, 0, sizeof *v);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    memset(&m, 0, sizeof m);
    m.amp0 = m.amp1 = 32767;
    m.pitch16 = (int32_t)note * 16;
    m.inc = pitch_inc((uint32_t)m.pitch16);
    m.shape = (64 << 8) + opx_t_shape;
    m.cutoff = opx_t_cut;
    opx_block(t);
    opx_note_on(t, v);
    if (ended)
        *ended = 0;
    for (i = 0; i < n; i += CTL) {
        if (rel && i >= rel)
            v->gate = 0;
        opx_block(t);
        if (ended && !*ended && opx_done(t, v))
            *ended = (int)(i + 1u);
        memset(b, 0, sizeof b);
        opx_render(t, v, b, CTL, &m);
        {                                            /* M5c: the part's effects (the post voice.c runs) */
            uint32_t f = opx_post(t, b, lb, CTL, 0, 1);
            opx_t_wide |= f >> 1 & 1u;
            for (k = 0; k < CTL && i + k < n; k++) {
                if (opx_t_right)
                    opx_t_right[i + k] = f & 2u ? b[k] - lb[k] : b[k];
                if (f & 2u)
                    b[k] = lb[k];
            }
        }
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k];
    }
}

/* ----------------------------------------------------- the reference --- */
/* the same patch in floating point: constant gains after a first block's linear ramp, no envelopes (the test
 * patches sustain at 100 % with attack 0), velocity 127 */
static void reference(const opx_patch_t *p, uint32_t alg, uint32_t note, double *y, uint32_t n)
{
    double ph[6] = {0}, inc[6], gain[6], yo[6] = {0}, yp[6] = {0}, amt[6][6] = {{0}}, fb[6], dc = 0;
    double f0 = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
    uint32_t out = 0, i, k, j;
    const double scale = 32767.0 * (double)mulq15(32767, VOICE_FS / 2) / 32768.0;
    for (k = 0; k < 6u; k++) {
        const opx_op_t *o = &p->op[k];
        double r = k < 6u ? pow(2.0, OPX_COARSE[o->coarse] / 16777216.0) : 1;
        inc[k] = f0 * r * o->fine / 1000.0 * pow(2.0, o->detune / 1200.0 + o->trans / 12.0) / 44100.0;
        if (o->fixed)
            inc[k] = o->fixed_freq / 100.0 / 44100.0;
        gain[k] = pow(o->level / 100.0, 3);
        fb[k] = o->fb / 100.0 * 2.0 / (2 * M_PI);       /* cycles per unit output */
    }
    if (alg == 40u) {
        out = p->ua_out;
        for (j = 0; j < 6u; j++)
            for (k = 0; k < 6u; k++)
                if (j == k)
                    fb[k] += p->ua[j][k] / 100.0 * 2.0 / (2 * M_PI);
                else
                    amt[j][k] = p->ua[j][k] / 100.0;
    } else {
        out = OPX_ALG[alg].out;
        for (k = 0; k < 6u; k++)
            for (j = 0; j < 6u; j++)
                if ((OPX_ALG[alg].mod[k] >> j) & 1u)
                    amt[j][k] = 1.0;
        if (OPX_ALG[alg].loop)
            amt[(OPX_ALG[alg].loop >> 4) - 1][(OPX_ALG[alg].loop & 15) - 1] = p->alg_fb / 100.0;
    }
    for (i = 0; i < n; i++) {
        double mix = 0, x, rampf = i < CTL ? (double)i / CTL : 1.0;   /* the first block's ramp from 0 */
        for (k = 6; k-- > 0;) {
            double mod = 0, s;
            for (j = 0; j < 6u; j++)
                mod += amt[j][k] * yo[j];
            s = sin(2 * M_PI * (ph[k] + 2.0 * mod + fb[k] * (yo[k] + yp[k]) / 2));
            yp[k] = yo[k];
            yo[k] = s * gain[k] * rampf;
            ph[k] += inc[k];
            if ((out >> k) & 1u)
                mix += yo[k];
        }
        x = mix - dc / 1024.0;
        dc += x;
        y[i] = x * scale;
    }
}

static double err_db(const double *a, const double *b, uint32_t from, uint32_t n)
{
    double e = 0, s = 0;
    uint32_t i;
    for (i = from; i < n; i++) {
        e += (a[i] - b[i]) * (a[i] - b[i]);
        s += b[i] * b[i];
    }
    return s > 0 ? 10 * log10(e / s + 1e-30) : 0;
}

/* the charts (Owner's Manual p.83), written apart from OPX_ALG: per algorithm the heard operators, then for each
 * operator 1..6 the operators modulating it ("-" none), and a loop as "src>dst" */
static const char *const CHART[40] = {
    "13 2,-,4,5,6,-", "13 2,-,4,5,6,-", "14 2,3,-,5,6,-", "14 2,3,-,5,6,- 4>6", "135 2,-,4,-,6,-",
    "135 2,-,4,-,6,- 5>6", "13 2,-,45,-,6,-", "13 2,-,45,-,6,-", "13 2,-,45,-,6,-", "14 2,3,-,56,-,-",
    "14 2,3,-,56,-,-", "13 2,-,456,-,-,-", "13 2,-,456,-,-,-", "13 2,-,4,56,-,-", "13 2,-,4,56,-,-",
    "1 235,-,4,-,6,-", "1 235,-,4,-,6,-", "1 234,-,-,5,6,-", "145 2,3,-,6,6,-", "124 3,3,-,56,-,-",
    "1245 3,3,-,6,6,-", "1345 2,-,6,6,6,-", "1245 -,3,-,6,6,-", "12345 -,-,6,6,6,-", "12345 -,-,-,6,6,-",
    "124 -,3,-,56,-,-", "124 -,3,-,56,-,-", "136 2,-,4,5,-,-", "1235 -,-,4,-,6,-", "1236 -,-,4,5,-,-",
    "12345 -,-,-,-,6,-", "123456 -,-,-,-,-,-", "1 2,356,4,-,-,-", "1 2,3,4,5,6,- 4>6", "14 23,-,-,56,-,-",
    "12 3456,3456,-,-,-,-", "12 35,35,4,-,6,-", "125 34,34,-,-,6,-", "1234 56,56,56,56,-,-", "1234 5,5,5,5,6,-"};

static int chart_ok(uint32_t a)
{
    const char *s = CHART[a];
    const opx_alg_t *g = &OPX_ALG[a];
    uint32_t out = 0, k = 0, m = 0;
    for (; *s && *s != ' '; s++)
        out |= 1u << (*s - '1');
    if (out != g->out)
        return 0;
    for (s++; *s && *s != ' '; s++) {
        if (*s == ',') {
            if (m != g->mod[k++])
                return 0;
            m = 0;
        } else if (*s != '-') {
            m |= 1u << (*s - '1');
        }
    }
    if (k != 5u || m != g->mod[5])
        return 0;
    if (*s == ' ')
        return g->loop == (uint8_t)((s[1] - '0') << 4 | (s[3] - '0'));
    return g->loop == 0;
}

static void test_reference(void)
{
    static double y[FS / 4], r[FS / 4];
    uint32_t a, n = FS / 4, worst_a = 0, charts = 0;
    double worst = -200;
    for (a = 0; a < 40u; a++)
        charts += chart_ok(a);
    check("algorithms 1..40: heard operators, inputs and loops as the charts (p.83)", charts == 40u);
    memset(&P, 0, sizeof P);
    for (a = 0; a < 6u; a++) {
        P.op[a] = OP_FLAT;
        P.op[a].level = (uint8_t)(a < 1 ? 90 : 30 + 3 * a);   /* modulators moderate: no chaos (index < ~1 rad a stage) */
        P.op[a].coarse = (uint8_t)(7 + a % 3);           /* ratios 1, 2, 3 */
        P.op[a].fine = (uint16_t)(1000 + 7 * a);
        P.op[a].detune = (int8_t)(a - 2);
        P.op[a].fb = (uint8_t)(a == 5 ? 20 : 0);
    }
    P.alg_fb = 30;
    for (a = 0; a <= 40u; a++) {
        double e;
        int16_t ev[8] = {(int16_t)(a + 1), 0, 0, 0, 0, 0, 0, 0};
        if (a == 40u) {                                   /* USER: a cross-coupled matrix with upward links */
            static const uint8_t UA[6][6] = {{0, 40, 0, 0, 0, 0}, {60, 20, 0, 0, 0, 0}, {50, 50, 0, 0, 0, 0},
                                             {0, 0, 80, 10, 0, 0}, {30, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}};
            memcpy(P.ua, UA, sizeof UA);
            P.ua_out = 0x13u;                            /* ops 1, 2, 5 heard */
        }
        setup(&P, ev);
        engine_render(57, 127, y, n, 0, 0);
        reference(&P, a, 57, r, n);
        e = err_db(y, r, CTL, 4096);                       /* (later the slow drift of the pitch's last bits) */
        if (getenv("OPX_VERBOSE"))
            printf("opx:   algorithm %u: %.1f dB (to 1024: %.1f, 1024..2048: %.1f, 8192..: %.1f)\n", a + 1u, e,
                   err_db(y, r, CTL, 1024), err_db(y, r, 1024, 2048), err_db(y, r, 8192, n));
        if (e > worst)
            worst = e, worst_a = a;
    }
    {
        char what[160];
        snprintf(what, sizeof what, "every algorithm and USER = the floating-point reference (worst %s%u: %.1f dB)",
                 worst_a == 40u ? "USER" : "", worst_a == 40u ? 0u : worst_a + 1u, worst);
        check(what, worst < -45.0);
    }
}

/* ------------------------------------------------------------ envelopes --- */
static double env_at(const double *y, uint32_t i)   /* RMS over the 441 samples (10 ms) from i */
{
    double s = 0;
    uint32_t k;
    for (k = 0; k < 441u; k++)
        s += y[i + k] * y[i + k];
    return sqrt(s / 441);
}

static void test_envelopes(void)
{
    static double y[FS * 4];
    double full, half_lin, half_exp;
    uint32_t i, t90 = 0;
    int ended;
    memset(&P, 0, sizeof P);
    P.algo = 31;                                         /* algorithm 32: op 1 alone heard */
    P.op[0] = OP_FLAT;
    P.op[0].level = 100;
    P.op[0].atk = 414;                                   /* 100 ms */
    P.op[0].dec = 476;                                   /* 200 ms */
    P.op[0].sus = 50;
    P.op[0].rel = 414;                                   /* 100 ms */
    setup(&P, 0);
    engine_render(69, 127, y, FS * 2, FS, &ended);
    full = env_at(y, (uint32_t)(0.15 * FS)) ;           /* (decaying already: measured against the sustain) */
    for (i = 0; i + 441 < FS / 2; i += 32)
        if (env_at(y, i) >= 0.9 * env_at(y, (uint32_t)(0.0995 * FS))) {
            t90 = i + 220;
            break;
        }
    check("attack 100 ms: 90 % of its peak at 90 ms +-10 % (linear)", t90 > 0.081 * FS && t90 < 0.099 * FS);
    {
        double s = env_at(y, (uint32_t)(0.8 * FS)), pk = env_at(y, (uint32_t)(0.0995 * FS));
        check("decay settles at SUS 50 % (-6 dB of the peak, +-0.5 dB)", fabs(20 * log10(s / pk) + 6.02) < 0.5);
        check("release: -40 dB within its 100 ms (and the voice ends)",
              env_at(y, FS + (uint32_t)(0.1 * FS)) < 0.01 * s && ended && ended < (int)(FS + 0.13 * FS));
    }
    (void)full;
    /* CURVE: half way through the release, EXP is far below LIN */
    P.op[0].rel = 620;                                   /* 1 s */
    P.op[0].curve = 0;
    setup(&P, 0);
    engine_render(69, 127, y, FS * 3, FS, 0);
    half_lin = env_at(y, FS + FS / 2) / env_at(y, FS - 441);
    P.op[0].curve = 10;
    setup(&P, 0);
    engine_render(69, 127, y, FS * 3, FS, 0);
    half_exp = env_at(y, FS + FS / 2) / env_at(y, FS - 441);
    check("release half way: LIN near 1/2, CURVE 10 below 1/20", fabs(half_lin - 0.5) < 0.05 && half_exp < 0.05);
}

/* ------------------------------------------------------- spectrum, level --- */
static double harm_db(const double *y, uint32_t from, uint32_t n, double f)   /* the level at f (Goertzel) */
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = from; i < from + n; i++) {
        double hann = 0.5 - 0.5 * cos(2 * M_PI * (i - from) / n);
        s0 = y[i] * hann + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 10 * log10(s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-9);
}

static void test_feedback(void)
{
    static double y[FS], z[FS];
    double f = 440.0 * pow(2.0, (60 - 69) / 12.0), h1, h2, worst = 0;
    uint32_t h;
    memset(&P, 0, sizeof P);
    P.algo = 31;
    P.op[0] = OP_FLAT;
    P.op[0].level = 100;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    h1 = harm_db(y, FS / 4, 8192, f);
    h2 = harm_db(y, FS / 4, 8192, 2 * f);
    check("FB 0: a sine (2nd harmonic below -60 dB)", h2 - h1 < -60);
    P.op[0].fb = 75;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    h1 = harm_db(y, FS / 4, 8192, f);
    for (h = 2; h <= 5u; h++) {
        double d = harm_db(y, FS / 4, 8192, h * f) - h1 + 20 * log10((double)h);
        worst = fabs(d) > worst ? fabs(d) : worst;
    }
    {
        char what[120];
        snprintf(what, sizeof what, "FB 75 %%: harmonics 2..5 within 10 dB of a sawtooth's 1/n (worst %.1f dB)", worst);
        check(what, worst < 10);
    }
    /* algorithm 34 with ALG FB 0 = the open chain (the loop 4 -> 6 carries nothing) */
    memset(&P, 0, sizeof P);
    for (h = 0; h < 6u; h++)
        P.op[h] = OP_FLAT;
    P.algo = 33;
    P.alg_fb = 0;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 4, 0, 0);
    P.alg_fb = 80;
    setup(&P, 0);
    engine_render(60, 127, z, FS / 4, 0, 0);
    check("algorithm 34: ALG FB 80 % changes the sound, 0 % leaves the chain open",
          err_db(z, y, CTL, FS / 4) > -20.0);
}

/* M2: the waveforms (eng_opx.c opx_table, opx_noise_block, opx_wave). One operator heard (algorithm 32, the others at 0), C4, no feedback; the
 * levels of its harmonics against the fundamental (dB), and the noises' spectra */
static double wave_h[9];
static void wave_render(uint32_t w, uint32_t width, uint32_t note, double *y, uint32_t n)
{
    memset(&P, 0, sizeof P);
    P.algo = 31;
    P.op[0] = OP_FLAT;
    P.op[0].level = 100;
    P.op[0].wave = (uint8_t)w;
    P.op[0].width = (uint8_t)width;
    setup(&P, 0);
    engine_render(note, 127, y, n, 0, 0);
}
static void wave_harm(uint32_t w, double *y)
{
    double f = 440.0 * pow(2.0, (60 - 69) / 12.0), h1;
    uint32_t h;
    wave_render(w, 0, 60, y, FS / 2);
    h1 = harm_db(y, FS / 4, 8192, f);
    for (h = 1; h <= 8u; h++)
        wave_h[h] = harm_db(y, FS / 4, 8192, h * f) - h1;
}
/* the mean power (dB) in a band of bins around f: Goertzel over 1024-sample blocks, 64 blocks */
static double band_db(const double *y, double f)
{
    double sum = 0;
    uint32_t b, j;
    for (b = 0; b < 64u; b++)
        for (j = 0; j < 8u; j++)
            sum += pow(10.0, harm_db(y, 4096u + b * 1024u, 1024u, f * (0.9 + 0.025 * j)) / 10);
    return 10 * log10(sum / (64 * 8));
}
static int near(double v, double want, double tol) { return fabs(v - want) <= tol; }
#define BAND_N (4096u + 64u * 1024u)             /* (band_db's samples) */
static void test_waves(void)
{
    static double y[BAND_N];
    static const struct { uint8_t w; int8_t h[8]; } EXACT[] = {   /* ADTV: the harmonics' levels, dB (-99: none) */
        {OPX_W_ASAW3, {0, -6, -10, -99, -99, -99, -99, -99}},
        {OPX_W_ASQR3, {0, -99, -10, -99, -14, -99, -99, -99}},
        {OPX_W_ATRI3, {0, -99, -19, -99, -28, -99, -99, -99}},
        {OPX_W_A12345, {0, 0, 0, 0, 0, -99, -99, -99}},
        {OPX_W_A12, {0, 0, -99, -99, -99, -99, -99, -99}},
        {OPX_W_A15, {0, -99, -99, -99, 0, -99, -99, -99}},
        {OPX_W_A18, {0, -99, -99, -99, -99, -99, -99, 0}},
    };
    static const double LV[6] = {0, -6.02, -9.54, -12.04, -13.98, -15.56};   /* 20 log 1/n */
    uint32_t i, h;
    int ok;
    char what[160];
    check("waves: the tables fill opx_wt exactly (TRI, ADTV, the SAW levels)",
          opx_wt_ok && opx_wt_saw[0] == WT_SAW && opx_wt_saw[OPX_MIP - 1] + 1025u == WT_COUNT);
    check("waves: 23 names, SIN first, NOISE BLUE last", OPX_NWAVE == 23 && !strcmp(N_OPX_WAVE[0], "SIN") &&
          !strcmp(N_OPX_WAVE[OPX_NWAVE - 1], "NOISE BLUE") && !N_OPX_WAVE[OPX_NWAVE]);
    /* SAW, SAW HD: every harmonic 1/n (to 6); SQR, SQR HD: odd ones 1/n, no even ones; TRI: odd ones 1/n^2 */
    for (i = 0; i < 2u; i++) {
        double worst = 0;
        wave_harm(i ? OPX_W_SAWHD : OPX_W_SAW, y);
        for (h = 2; h <= 6u; h++)
            worst = fmax(worst, fabs(wave_h[h] - LV[h - 1]));
        snprintf(what, sizeof what, "waves: %s: harmonics 2..6 at 1/n (worst %.2f dB off)", i ? "SAW HD" : "SAW", worst);
        check(what, worst < 1.0);
    }
    for (i = 0; i < 2u; i++) {
        wave_harm(i ? OPX_W_SQRHD : OPX_W_SQR, y);
        ok = near(wave_h[3], LV[2], 1.0) && near(wave_h[5], LV[4], 1.0) && wave_h[2] < -50 && wave_h[4] < -50;
        snprintf(what, sizeof what, "waves: %s: 3rd %.1f, 5th %.1f dB (1/n); 2nd %.0f, 4th %.0f dB (none)",
                 i ? "SQR HD" : "SQR", wave_h[3], wave_h[5], wave_h[2], wave_h[4]);
        check(what, ok);
    }
    wave_harm(OPX_W_TRI, y);
    snprintf(what, sizeof what, "waves: TRI: 3rd %.1f, 5th %.1f dB (1/n^2: -19.1, -28.0); 2nd %.0f dB", wave_h[3], wave_h[5], wave_h[2]);
    check(what, near(wave_h[3], -19.08, 0.5) && near(wave_h[5], -27.96, 0.7) && wave_h[2] < -50);
    ok = 1;
    for (i = 0; i < NELEM(EXACT); i++) {
        wave_harm(EXACT[i].w, y);
        for (h = 1; h <= 8u; h++) {
            double want = EXACT[i].h[h - 1];
            int good = want < -90 ? wave_h[h] < -55 : near(wave_h[h], want == -6 ? -6.02 : want == -10 ? -9.54 :
                       want == -14 ? -13.98 : want == -19 ? -19.08 : want == -28 ? -27.96 : want, 0.3);
            if (!good) {
                printf("opx: %s harmonic %u: %.2f dB, want %d\n", N_OPX_WAVE[EXACT[i].w], h, wave_h[h], EXACT[i].h[h - 1]);
                ok = 0;
            }
        }
    }
    check("waves: ADTV SAW3 SQR3 TRI3 12345 1+2 1+5 1+8: their harmonics exactly, nothing else (-55 dB)", ok);
    /* SIN 12BIT / 8BIT: the sine, quantized (the error: about -70 / -45 dB) */
    {
        static double z[FS];
        double e12 = 0, e8 = 0, p0 = 0;
        wave_render(OPX_W_SIN, 0, 60, z, FS / 4);
        wave_render(OPX_W_SIN12, 0, 60, y, FS / 4);
        for (i = FS / 8; i < FS / 4; i++)
            e12 += (y[i] - z[i]) * (y[i] - z[i]), p0 += z[i] * z[i];
        wave_render(OPX_W_SIN8, 0, 60, y, FS / 4);
        for (i = FS / 8; i < FS / 4; i++)
            e8 += (y[i] - z[i]) * (y[i] - z[i]);
        e12 = 10 * log10(e12 / p0 + 1e-12);
        e8 = 10 * log10(e8 / p0 + 1e-12);
        snprintf(what, sizeof what, "waves: SIN 12BIT / 8BIT: the sine with an error of %.0f / %.0f dB", e12, e8);
        check(what, e12 < -55 && e12 > -90 && e8 < -30 && e8 > e12 + 15);
    }
    /* the noises: WHITE flat, PINK -3 dB / octave, BLUE +3 dB / octave (500 Hz against 8 kHz: 4 octaves) */
    {
        double d[3];
        for (i = 0; i < 3u; i++) {
            wave_render(OPX_W_WHITE + i, 0, 60, y, BAND_N);
            d[i] = band_db(y, 8000) - band_db(y, 500);
        }
        snprintf(what, sizeof what, "waves: NOISE WHITE / PINK / BLUE: 8 kHz against 500 Hz %+.1f / %+.1f / %+.1f dB (0, -12, +12)",
                 d[0], d[1], d[2]);
        check(what, near(d[0], 0, 2) && near(d[1], -12, 2) && near(d[2], 12, 2));
    }
    /* the waveform alone: NOISE S/H holds a level for a cycle; WIDTH 50 % the whole cycle in its first half */
    {
        opx_rt_t r;
        opx_noise_t z;
        opx_wsel_t ws = {SINE, 22, 0, -1};
        int16_t nb[CTL];
        uint32_t p = 0, inc = 0x01000000u, steps = 0, j;
        int32_t last = 0, v;
        memset(&r, 0, sizeof r);
        memset(&z, 0, sizeof z);
        z.rng = 1;
        for (i = 0; i < 256u * 10u / CTL; i++, p += inc * CTL) {
            opx_noise_block(OPX_W_NOISE_SH, &z, p, inc, nb);
            for (j = 0; j < CTL; j++) {
                v = nb[j];
                steps += (i || j) && v != last;
                last = v;
            }
        }
        check("waves: NOISE S/H: a new level each cycle, held through it (10 cycles: 9 or 10 changes)", steps >= 9u && steps <= 10u);
        r.wr[0] = (100u << 16) / 50u;
        r.wlim[0] = 0x80000000u;
        ok = 1;
        for (p = 0; p < 0xFF000000u; p += 0x01000000u) {
            int32_t a = opx_wave(&r, 0, &ws, p, 0);
            int32_t want = p < 0x80000000u ? sine_i(p * 2u) : 0;
            ok &= abs(a - want) <= 2;
        }
        r.wr[0] = (100u << 16) / 25u;
        r.wlim[0] = 0x40000000u;
        ok &= opx_wave(&r, 0, &ws, 0x10000000u, 0) > 32000 && opx_wave(&r, 0, &ws, 0x50000000u, 0) == 0;
        check("waves: WIDTH 50 % / 25 %: the whole cycle in the first half / quarter, silence after", ok);
        /* the engine: WIDTH 50 on a sine changes its sound; WIDTH OFF (0) is the plain sine to the bit */
        {
            static double z0[FS / 4], z1[FS / 4];
            wave_render(OPX_W_SIN, 0, 60, z0, FS / 4);
            memset(&P, 0, sizeof P);
            P.algo = 31;
            P.op[0] = OP_FLAT;
            P.op[0].level = 100;
            setup(&P, 0);
            engine_render(60, 127, z1, FS / 4, 0, 0);
            ok = !memcmp(z0, z1, sizeof z0);
            wave_render(OPX_W_SIN, 50, 60, z1, FS / 4);
            check("waves: SIN, WIDTH OFF = M1's sine to the bit; WIDTH 50 % changes it", ok && memcmp(z0, z1, sizeof z0));
        }
    }
    /* aliasing: SAW, SAW HD, SQR, SQR HD at FIXED 2151.2 Hz (44100 / 20.5): every alias falls halfway between two
     * harmonics, where nothing else is; the strongest one against the fundamental. And the HD ones' high end: harmonics
     * up to 20 kHz (8 for SAW HD, 7 for SQR HD here), the others' up to 10 kHz (4, 3) */
    {
    double al[4], hi[4];
    for (i = 0; i < 4u; i++) {
        static const uint8_t W[4] = {OPX_W_SAW, OPX_W_SAWHD, OPX_W_SQR, OPX_W_SQRHD};
        double f = 44100.0 / 20.5, h1, worst = -200;
        uint32_t b;
        memset(&P, 0, sizeof P);
        P.algo = 31;
        P.op[0] = OP_FLAT;
        P.op[0].level = 100;
        P.op[0].wave = W[i];
        P.op[0].fixed = 1;
        P.op[0].fixed_freq = 215122;
        setup(&P, 0);
        engine_render(60, 127, y, FS / 2, 0, 0);
        h1 = harm_db(y, FS / 4, 8192, f);
        for (b = 0; b < 10u; b++)
            worst = fmax(worst, harm_db(y, FS / 4, 8192, f * (b + 0.5)) - h1);
        hi[i] = harm_db(y, FS / 4, 8192, f * (i < 2u ? 8 : 7)) - h1;
        snprintf(what, sizeof what, "waves: %s at 2151 Hz: the strongest alias %.1f dB below the fundamental; harmonic %u %.1f dB",
                 N_OPX_WAVE[W[i]], -worst, i < 2u ? 8u : 7u, hi[i]);
        check(what, worst < -70);
        al[i] = worst;
    }
    (void)al;
    check("waves: SAW HD and SQR HD keep their harmonics to 20 kHz (8th: -18 dB, 7th: -17 dB), SAW and SQR stop at 10 kHz",
          near(hi[1], -18.06, 1.0) && near(hi[3], -16.9, 1.0) && hi[0] < -60 && hi[2] < -60);
    }
}

/* the operator-by-operator order against the sample-by-sample one, to the bit: every algorithm (with every operator
 * sounding, feedback, ALG FB), every waveform, WIDTH, a USER matrix with an upward link, voices fading in and out */
static void test_orders(void)
{
    static double y0[FS / 4], y1[FS / 4];
    uint32_t a, w, k, bad = 0, n = 0;
    for (a = 0; a < OPX_NALG + 1u + OPX_NWAVE + 2u + 8u; a++) {
        memset(&P, 0, sizeof P);
        for (k = 0; k < 6u; k++) {
            P.op[k] = OP_FLAT;
            P.op[k].level = (uint8_t)(70 + 5 * k);
            P.op[k].fb = (uint8_t)(k == 0 ? 40 : 10 * k);
            P.op[k].coarse = (uint8_t)(10 + 3 * k);
            P.op[k].rel = 40;
        }
        P.alg_fb = 60;
        if (a < OPX_NALG) {
            P.algo = (uint8_t)a;
        } else if (a == OPX_NALG) {                    /* USER: a forward chain and an upward link (2 -> 5) */
            P.algo = OPX_USER;
            P.ua[5][4] = 80, P.ua[4][0] = 60, P.ua[1][4] = 50, P.ua[3][3] = 30, P.ua_out = 0x09u;
        } else if (a >= OPX_NALG + 1u + OPX_NWAVE + 2u) {   /* M2b: the modes, every operator, rotated; waves too */
            w = a - (OPX_NALG + 1u + OPX_NWAVE + 2u);
            P.algo = (uint8_t)(w < 4u ? 4 : w < 6u ? 0 : 31);   /* (5: pairs; 1: chains; 32: all heard) */
            for (k = 0; k < 6u; k++) {
                P.op[k].mode = (uint8_t)((k + w) % OPX_NMODE);
                P.op[k].wave = (uint8_t)((k * 5u + w) % OPX_NWAVE);
                P.op[k].ring_depth = (uint8_t)(60 + 8 * k), P.op[k].ring_shape = (uint8_t)(15 * k);
                P.op[k].fold_gain = (uint8_t)(20 * k), P.op[k].fold_oscmix = 80, P.op[k].fold_bias = (int8_t)(10 * k - 25);
                P.op[k].flt_type = (uint8_t)((k * 7u + w) % 12u), P.op[k].flt_reso = (uint8_t)(18 * k);
                P.op[k].flt_cutoff = (int16_t)(120 * k - 300), P.op[k].flt_oscmix = 70;
                P.op[k].fx_type = (uint8_t)((k * 3u + w) % 10u), P.op[k].fx_oscmix = 60;
                P.op[k].fx_e = (uint16_t)(k * 40u + 10u), P.op[k].fx_f = (uint16_t)(k * 30u + 50u);
                P.op[k].ktrk_center = 60, P.op[k].ktrk_low = (int8_t)(-30 + 10 * k), P.op[k].ktrk_high = (int8_t)(40 - 10 * k);
                P.op[k].ktrk_lcrv = (uint8_t)(k & 1u), P.op[k].ktrk_hcrv = (uint8_t)(k >> 1 & 1u);
            }
        } else {
            w = a - OPX_NALG - 1u;
            P.algo = 4;                                /* (algorithm 5: three pairs, no loop) */
            for (k = 0; k < 6u; k++) {
                P.op[k].wave = (uint8_t)(w < OPX_NWAVE ? w : (k + w) % OPX_NWAVE);
                P.op[k].width = (uint8_t)(w >= OPX_NWAVE ? 20 + 10 * k : 0);
            }
        }
        opx_sample_order = 1;
        setup(&P, 0);
        engine_render(48 + a % 24u, 100, y0, FS / 4, FS / 8, 0);
        opx_sample_order = 0;
        setup(&P, 0);
        engine_render(48 + a % 24u, 100, y1, FS / 4, FS / 8, 0);
        if (memcmp(y0, y1, sizeof y0)) {
            printf("opx: orders differ: case %u\n", a);
            bad++;
        }
        n++;
    }
    {
        char what[120];
        snprintf(what, sizeof what, "the operator order = the sample order, to the bit (%u patches: 40 algorithms, USER, 23 waves, WIDTH, modes)", n);
        check(what, !bad);
    }
}

/* M2b: the operator modes (MODE p.43) and KTRK (p.53): operator 1 heard, operator 2 its modulator (algorithm 5's
 * first pair), the others silent; C4 */
static void mode_pair(uint32_t md, uint32_t ratio2, uint32_t lvl2, double *y, uint32_t n)
{
    uint32_t k;
    memset(&P, 0, sizeof P);
    P.algo = 4;
    for (k = 0; k < 6u; k++)
        P.op[k] = OP_FLAT, P.op[k].level = 0;
    P.op[0].level = 100;
    P.op[0].mode = (uint8_t)md;
    P.op[1].level = (uint8_t)lvl2;
    P.op[1].coarse = (uint8_t)(6 + ratio2);              /* (coarse 7 = 1) */
    P.op[0].ring_depth = 100;
    P.op[0].fold_oscmix = 100;
    (void)n;
}
static void test_modes(void)
{
    static double y[FS / 2];
    double f = 440.0 * pow(2.0, (60 - 69) / 12.0), h[6], l0, l1;
    uint32_t i;
    char what[160];
#define MODE_H(n) (harm_db(y, FS / 4, 8192, (n) * f))
    /* RING at DEPTH 100: sin(f) x sin(3f) = the sum and the difference, 2f and 4f; neither f nor 3f */
    mode_pair(OPX_M_RING, 3, 100, y, 0);
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    for (i = 1; i <= 4u; i++)
        h[i] = MODE_H(i);
    snprintf(what, sizeof what, "RING MOD 100 %%: 2f %.1f, 4f %.1f dB; f %.1f, 3f %.1f dB (the sum and difference only)",
             h[2] - h[2], h[4] - h[2], h[1] - h[2], h[3] - h[2]);
    check(what, fabs(h[4] - h[2]) < 1.0 && h[1] - h[2] < -40 && h[3] - h[2] < -40);
    /* RING at DEPTH 0: the oscillator alone, whatever its input */
    mode_pair(OPX_M_RING, 3, 100, y, 0);
    P.op[0].ring_depth = 0;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    snprintf(what, sizeof what, "RING MOD 0 %%: the oscillator alone (3f %.0f dB under f)", MODE_H(1) - MODE_H(3));
    check(what, MODE_H(3) - MODE_H(1) < -60);
    /* WAVE FOLDER: GAIN 0 a full-scale sine unfolded; GAIN 100 folded (odd harmonics), BIAS: even ones too */
    mode_pair(OPX_M_FOLD, 1, 0, y, 0);
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    l0 = MODE_H(3) - MODE_H(1);
    mode_pair(OPX_M_FOLD, 1, 0, y, 0);
    P.op[0].fold_gain = 100;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    l1 = MODE_H(3) - MODE_H(1);
    h[2] = MODE_H(2) - MODE_H(1);
    snprintf(what, sizeof what, "WAVE FOLDER: 3rd harmonic GAIN 0 %.0f dB, GAIN 100 %.1f dB (folded); 2nd %.0f dB (no BIAS)", l0, l1, h[2]);
    check(what, l0 < -50 && l1 > -20 && h[2] < -50);
    P.op[0].fold_bias = 40;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    h[2] = MODE_H(2) - MODE_H(1);
    snprintf(what, sizeof what, "WAVE FOLDER BIAS 40 %%: even harmonics (2nd %.1f dB)", h[2]);
    check(what, h[2] > -30);
    /* BYPASS: the input as it comes (the modulator at 3f, nothing at f); MUTE: silence */
    mode_pair(OPX_M_BYPASS, 3, 100, y, 0);
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    snprintf(what, sizeof what, "BYPASS: the modulator's 3f passes, f %.0f dB under it", MODE_H(1) - MODE_H(3));
    check(what, MODE_H(1) - MODE_H(3) < -60);
    mode_pair(OPX_M_MUTE, 3, 100, y, 0);
    setup(&P, 0);
    engine_render(60, 127, y, FS / 2, 0, 0);
    l0 = 0;
    for (i = 0; i < FS / 2; i++)
        l0 += fabs(y[i]);
    check("MUTE: silence", l0 == 0);
    /* KTRK: LOW -100 % LIN, CENTER C4: an octave down 12 dB lower; EXP: 3 dB; HIGH +50 % LIN an octave up: +6 dB
     * (operator 1 at LEVEL 50: room to rise) */
    {
        static const struct { int8_t lo, hi; uint8_t lc, hc, note; double want; const char *what; } KT[] = {
            {-100, 0, 0, 0, 48, -12.0, "LOW -100 % LIN, an octave down"},
            {-100, 0, 1, 0, 48, -3.0, "LOW -100 % EXP, an octave down"},
            {0, 50, 0, 0, 72, 6.0, "HIGH +50 % LIN, an octave up"},
            {0, -100, 0, 1, 84, -12.0, "HIGH -100 % EXP, two octaves up"},
        };
        uint32_t j;
        for (j = 0; j < NELEM(KT); j++) {
            double a, b;
            mode_pair(OPX_M_FM, 1, 0, y, 0);
            P.op[0].level = 80;
            setup(&P, 0);
            engine_render(KT[j].note, 127, y, FS / 4, 0, 0);
            a = env_at(y, FS / 8);
            P.op[0].ktrk_center = 60, P.op[0].ktrk_low = KT[j].lo, P.op[0].ktrk_high = KT[j].hi;
            P.op[0].ktrk_lcrv = KT[j].lc, P.op[0].ktrk_hcrv = KT[j].hc;
            setup(&P, 0);
            engine_render(KT[j].note, 127, y, FS / 4, 0, 0);
            b = env_at(y, FS / 8);
            snprintf(what, sizeof what, "KTRK %s: %+.2f dB (want %+.1f)", KT[j].what, 20 * log10(b / a), KT[j].want);
            check(what, fabs(20 * log10(b / a) - KT[j].want) < 0.3);
        }
    }
    /* FILTER (TYPE p.34), the operator's SAW (its harmonics at 1/n) through it, OSC MIX 100 %, no input: a harmonic
     * against another, the slopes and the cutoff placed as the types' responses give them (SVF -6 dB at the cutoff at
     * RESO 0, 12 dB / octave; the ladder -3 dB a stage). RESO: the harmonic at the cutoff lifted (against RESO 0) */
    {
        static const struct { uint8_t type; int16_t cut; uint8_t reso; uint32_t ha, hb; double lo, hi; const char *what; } FT[] = {
            {0, 0, 0, 4, 1, -36, -25, "LPF at the 1st: the 4th under the 1st"},
            {5, 0, 0, 4, 1, -62, -42, "MG LPF24 at the 1st: the 4th under the 1st"},
            {4, 0, 0, 4, 1, -40, -26, "MG LPF12 at the 1st: the 4th under the 1st"},
            {1, 360, 0, 1, 8, -18, -8, "HPF at the 8th: the 1st under the 8th"},
            {7, 360, 0, 1, 8, -75, -35, "MG HPF24 at the 8th: the 1st under the 8th"},
            {10, 0, 0, 4, 1, -36, -25, "MS-20 LPF at the 1st: the 4th under the 1st"},
            {11, 360, 0, 1, 8, -18, -8, "MS-20 HPF at the 8th: the 1st under the 8th"},
            {2, 240, 0, 4, 1, -8, -3, "BPF at the 4th: the 4th (-12) against the 1st (-6.5)"},
            {3, 240, 0, 4, 2, -100, -40, "BRF at the 4th: the 4th notched"},
            {8, 240, 0, 4, 1, -6, 6, "MG BPF6 at the 4th: the 4th against the 1st"},
            {9, 240, 0, 4, 1, -3, 5, "MG BPF12 at the 4th: the 4th (-12) against the 1st (-13)"},
            {0, 240, 90, 4, 0, 8, 20, "LPF at the 4th, RESO 90 %: the 4th lifted"},
            {5, 240, 90, 4, 0, 8, 25, "MG LPF24 at the 4th, RESO 90 %: the 4th lifted"},
            {10, 240, 90, 4, 0, 8, 25, "MS-20 LPF at the 4th, RESO 90 %: the 4th lifted"},
        };
        uint32_t j;
        for (j = 0; j < NELEM(FT); j++) {
            double d = 0;
            uint32_t pass;
            for (pass = 0; pass < (FT[j].hb ? 1u : 2u); pass++) {   /* (hb 0: the same harmonic, RESO against RESO 0) */
                mode_pair(OPX_M_FILTER, 1, 0, y, 0);
                P.op[0].wave = OPX_W_SAW;
                P.op[0].flt_type = FT[j].type, P.op[0].flt_cutoff = FT[j].cut, P.op[0].flt_reso = pass ? 0 : FT[j].reso;
                P.op[0].flt_oscmix = 100;
                setup(&P, 0);
                engine_render(60, 127, y, FS / 2, 0, 0);
                d = FT[j].hb ? MODE_H(FT[j].ha) - MODE_H(FT[j].hb) : pass ? d - MODE_H(FT[j].ha) : MODE_H(FT[j].ha);
            }
            snprintf(what, sizeof what, "FILTER %s: %.1f dB (%.0f .. %.0f)", FT[j].what, d, FT[j].lo, FT[j].hi);
            check(what, d >= FT[j].lo && d <= FT[j].hi);
        }
        /* FILTER FM: the input (a sine at 3f) moves the cutoff: the spectrum differs from no input's; no blow-up */
        {
            double a3, b3, pk = 0;
            mode_pair(OPX_M_FILTER_FM, 3, 0, y, 0);
            P.op[0].wave = OPX_W_SAW, P.op[0].flt_type = 5, P.op[0].flt_reso = 50;
            setup(&P, 0);
            engine_render(60, 127, y, FS / 2, 0, 0);
            a3 = MODE_H(2) - MODE_H(1);
            P.op[1].level = 100;
            setup(&P, 0);
            engine_render(60, 127, y, FS / 2, 0, 0);
            b3 = MODE_H(2) - MODE_H(1);
            for (i = 0; i < FS / 2; i++)
                pk = fmax(pk, fabs(y[i]));
            snprintf(what, sizeof what, "FILTER FM: the input moves the cutoff (2nd harmonic %.1f dB without, %.1f with; peak %.0f)", a3, b3, pk);
            check(what, fabs(a3 - b3) > 1.0 && pk < 65536.0);
        }
    }
    /* EFFECT (FX p.34): the operator's SAW (or SIN) through each, against the effect's neutral settings */
    {
        static const struct { uint8_t type, wave; uint16_t e, f, e0, f0; uint32_t h; double lo, hi; const char *what; } XT[] = {
            {0, OPX_W_SAW, 740, 360, 740, 180, 4, 15, 20, "PEAK EQ at the 4th, +18 dB: the 4th lifted"},
            {0, OPX_W_SAW, 740, 0, 740, 180, 4, -20, -12, "PEAK EQ at the 4th, -18 dB: the 4th cut"},
            {1, OPX_W_SAW, 300, 180, 180, 180, 1, 5, 12, "SHELV EQ LOW +12 dB: the 1st (262 Hz) lifted"},
            {1, OPX_W_SAW, 180, 300, 180, 180, 32, 8, 13, "SHELV EQ HIGH +12 dB: the 32nd (8.4 kHz) lifted"},
            {5, OPX_W_SIN, 100, 180, 0, 180, 3, 30, 200, "DIST 100 %: a 3rd harmonic from a sine"},
            {6, OPX_W_SIN, 100, 180, 0, 180, 3, 12, 200, "DRIVE 100 %: the 3rd harmonic up (DRIVE 0 already rounds a full-scale sine)"},
            {7, OPX_W_SIN, 200 - 200, 2200, 25400, 2200, 3, 20, 200, "DEC FREQ 2.00: held twice a period, harmonics"},
            {8, OPX_W_SIN, 40, 0, 0, 0, 3, 12, 200, "WAVESHAPER TYPE 40: the 3rd harmonic up (against TYPE 0)"},
        };
        uint32_t j, pass;
        for (j = 0; j < NELEM(XT); j++) {
            double d = 0;
            for (pass = 0; pass < 2u; pass++) {
                mode_pair(OPX_M_EFFECT, 1, 0, y, 0);
                P.op[0].wave = XT[j].wave, P.op[0].fx_type = XT[j].type, P.op[0].fx_oscmix = 100;
                P.op[0].fx_e = pass ? XT[j].e0 : XT[j].e, P.op[0].fx_f = pass ? XT[j].f0 : XT[j].f;
                setup(&P, 0);
                engine_render(60, 127, y, FS / 2, 0, 0);
                d = pass ? d - (MODE_H(XT[j].h) - MODE_H(1) * (XT[j].h != 1u)) : MODE_H(XT[j].h) - MODE_H(1) * (XT[j].h != 1u);
            }
            snprintf(what, sizeof what, "EFFECT %s: %+.1f dB (%.0f .. %.0f)", XT[j].what, d, XT[j].lo, XT[j].hi);
            check(what, d >= XT[j].lo && d <= XT[j].hi);
        }
        /* COMB at the pitch, FB +90 %: the harmonics reinforced (the level up); every type at its extremes: bounded */
        {
            double a1, b1, pk = 0;
            uint32_t ty, ex;
            mode_pair(OPX_M_EFFECT, 1, 0, y, 0);
            P.op[0].wave = OPX_W_SAW, P.op[0].fx_type = 4, P.op[0].fx_oscmix = 50, P.op[0].fx_e = 500, P.op[0].fx_f = 100;
            setup(&P, 0);
            engine_render(60, 127, y, FS / 2, 0, 0);
            a1 = MODE_H(1);
            P.op[0].fx_f = 190;
            setup(&P, 0);
            engine_render(60, 127, y, FS / 2, 0, 0);
            b1 = MODE_H(1);
            snprintf(what, sizeof what, "EFFECT COMB at the pitch, FB +90 %%: the 1st %+.1f dB (resonant)", b1 - a1);
            check(what, b1 - a1 > 6);
            for (ty = 0; ty < 10u; ty++)
                for (ex = 0; ex < 4u; ex++) {
                    mode_pair(OPX_M_EFFECT, 3, 100, y, 0);
                    P.op[0].wave = OPX_W_SQR, P.op[0].fx_type = (uint8_t)ty, P.op[0].fx_oscmix = 100;
                    P.op[0].fx_e = (uint16_t)(ex & 1u ? OPX_FXV[ty][1] - OPX_FXV[ty][0] : 0);
                    P.op[0].fx_f = (uint16_t)(ex & 2u ? OPX_FXV[ty][3] - OPX_FXV[ty][2] : 0);
                    setup(&P, 0);
                    engine_render(36 + 12 * ex, 127, y, FS / 4, 0, 0);
                    for (i = 0; i < FS / 4; i++)
                        pk = fmax(pk, fabs(y[i]));
                }
            snprintf(what, sizeof what, "EFFECT: every type at the ends of E and F, low to high notes: bounded (peak %.0f)", pk);
            check(what, pk < 131072.0);
        }
    }
#undef MODE_H
}

/* Felucca's macros MLVL FB MRAT DTUN (E3..E6) are off: they change nothing (OPX_FELUCCA_MACROS 0) */
static void test_macros_off(void)
{
    static double y0[FS / 4], y1[FS / 4];
    int16_t e0[8] = {0, 0, 0, 0, 0, 0, 0, 0}, e1[8] = {0, 0, 0, 40, 40, 8, 100, 0};
    memset(&P, 0, sizeof P);
    P = OPX_FACTORY[0];
    setup(&P, e0);
    engine_render(60, 100, y0, FS / 4, FS / 8, 0);
    setup(&P, e1);
    engine_render(60, 100, y1, FS / 4, FS / 8, 0);
    check("Felucca's macros MLVL FB MRAT DTUN (E3..E6) off: the sound unchanged", !memcmp(y0, y1, sizeof y0));
    setup(&P, e0);
    opx_t_cut = 40 << 8, opx_t_shape = 50 << 8;      /* (ENV / LFO -> FLT and SHP as far as they go) */
    engine_render(60, 100, y1, FS / 4, FS / 8, 0);
    opx_t_cut = opx_t_shape = 0;
    check("Felucca's FLT and SHP routes off on OPX: the sound unchanged", !memcmp(y0, y1, sizeof y0));
}

/* USER plays the matrix as it is: without a heard operator, silence (no fallback to the patch's algorithm) */
static void test_user_empty(void)
{
    static double y[FS / 4];
    uint32_t i;
    double e = 0;
    int16_t ev[8] = {41, 0, 0, 0, 0, 0, 0, 0};
    memset(&P, 0, sizeof P);
    for (i = 0; i < 6u; i++)
        P.op[i] = OP_FLAT;
    P.algo = 0;
    setup(&P, ev);
    engine_render(60, 127, y, FS / 4, 0, 0);
    for (i = 0; i < FS / 4; i++)
        e += fabs(y[i]);
    check("ALG USR on a patch without a heard operator in its matrix: silent", e == 0);
}

static void test_velocity(void)
{
    static double y[FS / 2];
    double a, b;
    memset(&P, 0, sizeof P);
    P.algo = 31;
    P.op[0] = OP_FLAT;
    P.op[0].level = 100;
    P.op[0].vel = 100;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 4, 0, 0);
    a = env_at(y, FS / 8);
    engine_render(60, 64, y, FS / 4, 0, 0);
    b = env_at(y, FS / 8);
    check("VEL 100 %: velocity 64 about 6 dB below 127 (+-1 dB)", fabs(20 * log10(b / a) + 6.0) < 1.0);
    P.op[0].vel = 0;
    setup(&P, 0);
    engine_render(60, 127, y, FS / 4, 0, 0);
    a = env_at(y, FS / 8);
    engine_render(60, 30, y, FS / 4, 0, 0);
    b = env_at(y, FS / 8);
    check("VEL 0: velocity changes nothing", fabs(20 * log10(b / a)) < 0.01);
}


/* ------------------------------------------------------------------ M4 --- */
/* the frequency of y over [from, from + n): rising zero crossings, between samples */
static double freq_at(const double *y, uint32_t from, uint32_t n)
{
    double first = -1, last = -1;
    uint32_t i, c = 0;
    for (i = from + 1u; i < from + n; i++)
        if (y[i - 1] < 0 && y[i] >= 0) {
            double x = (double)(i - 1u) + y[i - 1] / (y[i - 1] - y[i]);
            if (first < 0) first = x;
            last = x;
            c++;
        }
    return c > 1u ? (double)(c - 1u) * FS / (last - first) : 0;
}
static void m4_patch(void)                       /* one sine carrier, ratio 1, sustained, nothing modulating it */
{
    memset(&P, 0, sizeof P);
    P.algo = 31;
    P.op[0] = OP_FLAT;
    P.op[0].level = 100;
    P.eg[0].sus = 100;
}
static void test_m4(void)
{
    static double y[FS * 2];
    double f0, a, b, lo, hi;
    uint32_t i;
    int16_t bpm0 = song.g[G_BPM];
    m4_patch();
    setup(&P, 0);
    engine_render(69, 127, y, FS / 2, 0, 0);
    f0 = freq_at(y, FS / 4, FS / 4);

    /* EG1: ATK 0, DEC to SUS 0, P MOD EG1 +12.00: an octave up at the start, the note's pitch after the decay */
    m4_patch();
    P.eg[0].dec = 600, P.eg[0].sus = 0;                  /* (DEC about 0.8 s) */
    P.op[0].pm_eg1 = 1200;
    setup(&P, 0);
    engine_render(69, 127, y, FS * 2, 0, 0);
    a = freq_at(y, 64, 441);
    b = freq_at(y, FS * 3 / 2, FS / 4);
    check("M4 EG1 -> P MOD EG1 +12.00: an octave up at the start, back to the note after DEC (+-1 %)",
          fabs(a / f0 - 2.0) < 0.02 && fabs(b / f0 - 1.0) < 0.01);
    P.op[0].pm_eg1 = -1200;
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    check("  .. -12.00: an octave down", fabs(freq_at(y, 64, 882) / f0 - 0.5) < 0.01);
    m4_patch();
    P.eg[0].rel = 700;
    P.op[0].pm_eg1 = 1200;
    setup(&P, 0);
    engine_render(69, 127, y, FS, FS / 4, 0);
    a = freq_at(y, FS / 8, FS / 16);
    b = freq_at(y, FS * 3 / 4, FS / 8);
    check("  .. SUS 100 holds it an octave up; the key let go, REL brings it back", fabs(a / f0 - 2.0) < 0.01 && b / f0 < 1.2);

    /* LFO1: SIN, P MOD LFO1 +1.00: the pitch swings +-1 semitone; SPEED and TEMPO */
    m4_patch();
    P.lfo[0].wave = OPX_L_SIN, P.lfo[0].speed = 600;     /* (0.01 x 10000^(600/1023): 2.2 Hz) */
    P.op[0].pm_lfo1 = 100;
    setup(&P, 0);
    engine_render(69, 127, y, FS * 2, 0, 0);
    lo = 1e9, hi = 0;
    for (i = 0; i + 882u < FS * 2u; i += 441u) {
        a = freq_at(y, i, 882);
        lo = a < lo ? a : lo, hi = a > hi ? a : hi;
    }
    check("M4 LFO1 SIN -> P MOD LFO1 +1.00: the pitch swings +-1 semitone (the extremes +-0.2 st)",
          fabs(12 * log2(hi / f0) - 1.0) < 0.2 && fabs(12 * log2(lo / f0) + 1.0) < 0.2);
    {
        opx_rt_t *r = &opx_rt[0];
        int ok = 1;
        P.lfo[0].speed = 0; opx_build(&trk[0], r); ok &= fabs(r->lf[0].inc / 3116550.0 - 0.01) < 0.0001;
        P.lfo[0].speed = 1023; opx_build(&trk[0], r); ok &= fabs(r->lf[0].inc / 3116550.0 / 100.0 - 1.0) < 0.01;
        P.lfo[0].speed = 512; opx_build(&trk[0], r); ok &= fabs(r->lf[0].inc / 3116550.0 / (0.01 * pow(10000, 512.0 / 1023)) - 1.0) < 0.01;
        check("M4 LFO1 SPEED: 0.01 Hz at 0, 100 Hz at 1023, 0.01 x 10000^(c/1023) between (+-1 %)", ok);
        P.lfo[0].tempo = 1, P.lfo[0].sync = 11;          /* 1/4 */
        song.g[G_BPM] = 120;
        opx_build(&trk[0], r);
        ok = fabs(r->lf[0].inc / 3116550.0 - 2.0) < 0.001;
        P.lfo[0].sync = 23;                            /* 4/1: 16 beats */
        opx_build(&trk[0], r);
        ok &= fabs(r->lf[0].inc / 3116550.0 - 0.125) < 0.001;
        check("M4 LFO1 TEMPO: 1/4 at 120 BPM is 2 Hz, 4/1 1/8 Hz", ok);
        {   /* the opsix's 24 note values (read off one), in its knob's order: each length's triplet, dotted, straight */
            static const double LEN[24] = {1. / 48, 3. / 128, 1. / 32, 1. / 24, 3. / 64, 1. / 16, 1. / 12, 3. / 32, 1. / 8,
                1. / 6, 3. / 16, 1. / 4, 1. / 3, 3. / 8, 1. / 2, 2. / 3, 3. / 4, 1, 4. / 3, 3. / 2, 2, 8. / 3, 3, 4};
            ok = NELEM(OPX_SYNC) == 24u;
            for (i = 0; i < 24u; i++) {
                P.lfo[0].sync = (uint8_t)i;
                opx_build(&trk[0], r);
                ok &= fabs(r->lf[0].inc / 3116550.0 / (2.0 / (4 * LEN[i])) - 1.0) < 0.001 && (i == 0 || LEN[i] > LEN[i - 1]);
            }
            P.lfo[0].sync = 200;                       /* (out of range: 1/4) */
            opx_build(&trk[0], r);
            ok &= fabs(r->lf[0].inc / 3116550.0 - 2.0) < 0.001;
            check("M4 LFO1 TEMPO: the opsix's 24 note values 1/48 .. 4/1 at 120 BPM (+-0.1 %), rising; out of range 1/4", ok);
        }
    }

    /* LMOD LFO1: SQR at 100 %: the level between full and silent; at -100 % the other way round; 50 %: half */
    m4_patch();
    P.lfo[0].wave = OPX_L_SQR, P.lfo[0].speed = 512, P.lfo[0].ksync = 2;
    P.op[0].lm_lfo1 = 100;
    setup(&P, 0);
    engine_render(69, 127, y, FS, 0, 0);
    a = env_at(y, 441 * 4);                          /* (SQR's first half: +1) */
    b = env_at(y, (uint32_t)(FS / (2 * 0.01 * pow(10000, 512.0 / 1023))) + 441 * 4);
    check("M4 LMOD LFO1 +100 % on SQR: full level on +1, silent on -1", b < a * 0.01);
    P.op[0].lm_lfo1 = -100;
    setup(&P, 0);
    engine_render(69, 127, y, FS, 0, 0);
    check("  .. -100 %: silent on +1, full on -1", env_at(y, 441 * 4) < a * 0.01 &&
          fabs(env_at(y, (uint32_t)(FS / (2 * 0.01 * pow(10000, 512.0 / 1023))) + 441 * 4) / a - 1) < 0.05);
    P.op[0].lm_lfo1 = 50;
    setup(&P, 0);
    engine_render(69, 127, y, FS, 0, 0);
    check("  .. +50 %: half the level on -1", fabs(env_at(y, (uint32_t)(FS / (2 * 0.01 * pow(10000, 512.0 / 1023))) + 441 * 4) / a - 0.5) < 0.05);

    /* FADE: KSYNC VOICE, FADE 1 s: little swing at first, the whole after */
    m4_patch();
    P.lfo[0].wave = OPX_L_SQR, P.lfo[0].speed = 700, P.lfo[0].ksync = 2, P.lfo[0].fade = 1000;
    P.op[0].pm_lfo1 = 1200;
    setup(&P, 0);
    engine_render(69, 127, y, FS * 2, 0, 0);
    a = freq_at(y, 441, 441);
    b = freq_at(y, FS * 3 / 2, 300);
    check("M4 LFO1 FADE 1 s: at 10 ms under a semitone off, after 1.5 s an octave (SQR +-12.00)",
          fabs(12 * log2(a / f0)) < 1.0 && fabs(fabs(12 * log2(b / f0)) - 12.0) < 0.3);

    /* PROG PITCH TRANS, OCT: the note's pitch (ratio operators; a FIXED one stays) */
    m4_patch();
    P.prog_trans = 12;
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    a = freq_at(y, FS / 8, FS / 8);
    P.prog_trans = 0, P.prog_oct = -1;
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    b = freq_at(y, FS / 8, FS / 8);
    P.prog_oct = 2, P.op[0].fixed = 1, P.op[0].fixed_freq = 22000;
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    check("M4 PROG TRANS +12 an octave up, OCT -1 one down; a FIXED operator stays (220 Hz)",
          fabs(a / f0 - 2) < 0.01 && fabs(b / f0 - 0.5) < 0.01 && fabs(freq_at(y, FS / 8, FS / 8) - 220.0) < 1.0);

    /* P MOD VEL (read off an opsix): 100 % at velocity 127 ratio 1 -> 11 (FINE and COARSE), 64 -> 6.04 (FINE); 50 % at
     * 64: FINE 3.52, COARSE 4 (the nearest whole step); velocity 0 none */
    m4_patch();
    P.op[0].pm_vel = 100;
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    a = freq_at(y, FS / 8, FS / 8);
    engine_render(69, 0, y, FS / 4, 0, 0);
    b = freq_at(y, FS / 8, FS / 8);
    engine_render(69, 64, y, FS / 4, 0, 0);
    lo = freq_at(y, FS / 8, FS / 8);
    P.op[0].pm_vel = 50;
    setup(&P, 0);
    engine_render(69, 64, y, FS / 4, 0, 0);
    hi = freq_at(y, FS / 8, FS / 8);
    check("M4 P MOD VEL FINE: 100 % at 127 ratio 11, at 64 6.04, 50 % at 64 3.52 (+-0.5 %), velocity 0 the note",
          fabs(a / f0 / 11 - 1) < 0.005 && fabs(lo / f0 / 6.039 - 1) < 0.005 && fabs(hi / f0 / 3.520 - 1) < 0.005 &&
          fabs(b / f0 - 1) < 0.01);
    P.op[0].pm_dest = 1;
    setup(&P, 0);
    engine_render(69, 64, y, FS / 4, 0, 0);
    hi = freq_at(y, FS / 8, FS / 8);
    P.op[0].pm_vel = 100;
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    check("  .. COARSE: whole steps to the nearest: 100 % at 127 ratio 11, 50 % at 64 ratio 4",
          fabs(freq_at(y, FS / 8, FS / 8) / f0 / 11 - 1) < 0.005 && fabs(hi / f0 / 4 - 1) < 0.005);
    P.op[0].coarse = 8, P.op[0].pm_dest = 0;         /* (COARSE 2: the addition goes on the final ratio, 2 + 10) */
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    a = freq_at(y, FS / 8, FS / 8);
    P.op[0].coarse = 6, P.op[0].fine = 500, P.op[0].pm_vel = 50;   /* (1/2 x 0.50 = 0.25, + 50 % x 10 = 5.25) */
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    b = freq_at(y, FS / 8, FS / 8);
    P.op[0].coarse = 8, P.op[0].fine = 500, P.op[0].pm_vel = 55, P.op[0].pm_dest = 1;   /* (opsix: C5 from C3) */
    setup(&P, 0);
    engine_render(69, 127, y, FS / 4, 0, 0);
    lo = freq_at(y, FS / 8, FS / 8);
    check("  .. FINE on the final ratio: COARSE 2 at 127 ratio 12 (opsix: G6 from C3); 1/2 x 0.50 at 50 % 5.25; COARSE "
          "whole steps on COARSE: 2 x 0.50, 55 % at 127: (2 + 6) x 0.5 = 4 (opsix: C5)",
          fabs(a / f0 / 12 - 1) < 0.005 && fabs(b / f0 / 5.25 - 1) < 0.005 && fabs(lo / f0 / 4 - 1) < 0.005);

    /* the LFO shapes: bounds, the bipolar ones -1 .. +1, the "+" and GTR 0 .. +1; S/H held over a cycle */
    {
        uint32_t w, u, ok = 1;
        opx_lfo_t l;
        opx_rt_t r;
        for (w = 0; w < OPX_NLFOW; w++) {
            int32_t mn = 99999, mx = -99999;
            if (OPX_L_RANDOM(w))
                continue;
            for (u = 0; u < 65536u; u += 7u) {
                int32_t v = opx_lfo_shape(w, u);
                mn = v < mn ? v : mn, mx = v > mx ? v : mx;
            }
            if (OPX_L_UNI(w))
                ok &= mn >= 0 && mn < 400 && mx > 32300;
            else
                ok &= mn < -32300 && mx > 32300 && mn >= -32767 && mx <= 32767;
        }
        ok &= opx_lfo_shape(OPX_L_SIN, 0) == 0 && opx_lfo_shape(OPX_L_TRI, 0) == 0 && opx_lfo_shape(OPX_L_TRI, 16384) == 32767;
        bad += !ok;
        printf("opx: %-96s %s\n", "M4 LFO shapes: the bipolar ones -1 .. +1, GTR and the + ones 0 .. +1; SIN, TRI from 0 rising", ok ? "ok" : "FAIL");
        memset(&r, 0, sizeof r);
        memset(&l, 0, sizeof l);
        r.lf[0].w = OPX_L_SH, r.lf[0].inc = 1u << 28;   /* (16 blocks a cycle) */
        opx_lfo_reset(&l, &r.lf[0], 1);
        {
            int32_t v0 = opx_lfo_step(&l, &r.lf[0], r.lf[0].inc), changes = 0;
            for (u = 1; u < 160u; u++) {             /* (16 blocks a cycle: a new level every 16th) */
                int32_t v = opx_lfo_step(&l, &r.lf[0], r.lf[0].inc);
                changes += v != v0;
                v0 = v;
            }
            ok = changes >= 8 && changes <= 10;
        }
        r.lf[0].w = OPX_L_RL;
        for (u = 0; u < 64u; u++) {
            int32_t v = opx_lfo_step(&l, &r.lf[0], r.lf[0].inc);
            ok &= v >= -32768 && v <= 32767;
        }
        check("M4 LFO S/H: one level a cycle; RND LEVEL inside the range", ok);
    }
    /* CTRL: the source multiplies the INT. P MOD EG1 +12.00 under CTRL: OFF the octave, MOD WHEEL 0 none, full the
     * octave; VEL at velocity 64 half (+6 st); PITCH WHEEL down -1 (an octave down); LMOD LFO1 under MOD WHEEL 0 none */
    {
        static const struct { uint8_t c, vel, mw; int16_t bend; double st; } CT[] = {
            {OPX_C_OFF, 127, 0, 0, 12}, {OPX_C_MW, 127, 0, 0, 0}, {OPX_C_MW, 127, 127, 0, 12},
            {OPX_C_VEL, 64, 0, 0, 6.05}, {OPX_C_PW, 127, 0, -8192, -12}, {OPX_C_PWM, 127, 0, -8192, 12},
            {OPX_C_PWP, 127, 0, -8192, 0}, {OPX_C_NOTE, 127, 0, 0, 1.69}, {OPX_C_EG2, 127, 0, 0, 0}};
        int ok = 1;
        for (i = 0; i < NELEM(CT); i++) {
            double st;
            m4_patch();
            P.op[0].pm_eg1 = 1200, P.op[0].pm_eg1_ctrl = CT[i].c;
            setup(&P, 0);
            trk[0].mw = CT[i].mw;
            opx_ctl_bend[0] = CT[i].bend;
            engine_render(69, CT[i].vel, y, FS / 4, 0, 0);
            st = 12 * log2(freq_at(y, FS / 8, FS / 16) / f0);
            if (fabs(st - CT[i].st) > 0.1) {
                printf("opx: M4 CTRL %u: %.2f st, want %.2f\n", CT[i].c, st, CT[i].st);
                ok = 0;
            }
        }
        trk[0].mw = 0, opx_ctl_bend[0] = 0;
        check("M4 CTRL: OFF, MOD WHEEL, VEL, PITCH WHEEL (+ / -), NOTE (C4 +- 64), EG2 (here at 0) x P MOD EG1 INT", ok);
        m4_patch();
        P.lfo[0].wave = OPX_L_SQR, P.lfo[0].speed = 600;
        P.op[0].lm_lfo1 = 100, P.op[0].lm_lfo1_ctrl = OPX_C_MW;
        setup(&P, 0);
        engine_render(69, 127, y, FS, 0, 0);
        lo = 1e30, hi = 0;
        for (i = FS / 8; i + 4410u < FS; i += 2205u) {   /* (100 ms: 44 cycles of the note) */
            double e = 0;
            uint32_t j;
            for (j = 0; j < 4410u; j++)
                e += y[i + j] * y[i + j];
            lo = e < lo ? e : lo, hi = e > hi ? e : hi;
        }
        ok = lo > hi * 0.95;
        trk[0].mw = 127;
        engine_render(69, 127, y, FS, 0, 0);
        lo = 1e30, hi = 0;
        for (i = FS / 8; i + 4410u < FS; i += 2205u) {
            double e = 0;
            uint32_t j;
            for (j = 0; j < 4410u; j++)
                e += y[i + j] * y[i + j];
            lo = e < lo ? e : lo, hi = e > hi ? e : hi;
        }
        trk[0].mw = 0;
        check("M4 CTRL MOD WHEEL x LMOD LFO1 +100: at 0 the level steady, at 127 it swings", ok && lo < hi * 0.5);
    }
    memset(&P, 0, sizeof P);                         /* (the tests after: no M4 values left) */
    song.g[G_BPM] = bpm0;
}

/* M5a: EG2, EG3, LFO2, LFO3 as CTRL sources; V.PATCH: its sources, destinations, CTRL and INT */
static void m5_vp(uint32_t i, uint32_t src, int32_t amt, uint32_t ctrl, uint32_t dst)
{
    P.vp[i].src = (uint8_t)src, P.vp[i].amt = (int16_t)amt, P.vp[i].ctrl = (uint8_t)ctrl, P.vp[i].dst = (uint16_t)dst;
}
static double m5_st(uint32_t vel, double f0)        /* the note's pitch from 1/8 s on, semitones from f0 */
{
    static double y[FS / 2];
    engine_render(69, vel, y, FS / 4, 0, 0);
    return 12 * log2(freq_at(y, FS / 8, FS / 16) / f0);
}
static double m5_rms(uint32_t from, uint32_t n)
{
    static double y[FS];
    double e = 0;
    uint32_t i;
    engine_render(69, 127, y, from + n, 0, 0);
    for (i = from; i < from + n; i++)
        e += y[i] * y[i];
    return sqrt(e / n);
}
static void test_m5(void)
{
    static const struct { uint8_t c; uint8_t sus; double st; } EC[] = {
        {OPX_C_EG2, 50, 6}, {OPX_C_EG3, 25, 3}, {OPX_C_EG2, 0, 0}};
    double f0, st, a, b;
    uint32_t i;
    int ok = 1;
    m4_patch();
    setup(&P, 0);
    f0 = 440.0 * pow(2.0, (m5_st(127, 440.0)) / 12.0);   /* (the plain note's own pitch) */
    for (i = 0; i < NELEM(EC); i++) {                /* EG2 / EG3 held at SUS x P MOD EG1 +12.00: SUS x 12 semitones */
        m4_patch();
        P.op[0].pm_eg1 = 1200, P.op[0].pm_eg1_ctrl = EC[i].c;
        P.eg[EC[i].c - OPX_C_EG1].sus = EC[i].sus;
        setup(&P, 0);
        st = m5_st(127, f0);
        if (fabs(st - EC[i].st) > 0.1) {
            printf("opx: M5 CTRL EG %u SUS %u: %.2f st, want %.2f\n", EC[i].c, EC[i].sus, st, EC[i].st);
            ok = 0;
        }
    }
    check("M5 CTRL: EG2, EG3 held at SUS scale P MOD EG1's INT (SUS 50: half the octave)", ok);
    m4_patch();                                      /* LFO2 as CTRL: a SQR at 2 Hz makes the EG1 INT swing */
    P.op[0].pm_eg1 = 1200, P.op[0].pm_eg1_ctrl = OPX_C_LFO2;
    P.lfo[1].wave = OPX_L_SQRP, P.lfo[1].speed = 589, P.lfo[1].ksync = 2;
    setup(&P, 0);
    {
        static double y[FS];
        double lo = 1e9, hi = -1e9;
        engine_render(69, 127, y, FS, 0, 0);
        for (i = FS / 8; i + 2205u < FS; i += 2205u) {
            double s2 = 12 * log2(freq_at(y, i, 2205) / f0);
            lo = s2 < lo ? s2 : lo, hi = s2 > hi ? s2 : hi;
        }
        check("M5 CTRL: LFO2 (SQR+, 2 Hz) x P MOD EG1 +12.00: the pitch steps between 0 and an octave",
              fabs(lo) < 0.3 && fabs(hi - 12) < 0.3);
    }
    /* V.PATCH: MOD WHEEL -> OP1 PITCH, INT +12.00: wheel 0 none, full an octave; under CTRL VEL at velocity 64 half */
    m4_patch();
    m5_vp(0, OPX_C_MW, 1200, OPX_C_OFF, OPX_VD_OP + OPX_DO_PITCH);
    setup(&P, 0);
    trk[0].mw = 0;
    a = m5_st(127, f0);
    trk[0].mw = 127;
    b = m5_st(127, f0);
    P.vp[0].ctrl = OPX_C_VEL;
    setup(&P, 0);
    trk[0].mw = 127;                                 /* (setup clears the wheel) */
    st = m5_st(64, f0);
    trk[0].mw = 0;
    check("M5 V.PATCH MOD WHEEL -> OP1 PITCH +12.00: wheel 0 none, full +12 semitones; CTRL VEL at 64 half",
          fabs(a) < 0.05 && fabs(b - 12) < 0.1 && fabs(st - 6.05) < 0.15);
    /* CC+ 74 -> OP1 LEVEL -100.00: no CC yet none; CC 127 silent; CC+/- 20 -> PROG PITCH +12.00 at 64 none, at 127 up */
    m4_patch();
    m5_vp(0, OPX_VS_CCU + 73, -10000, OPX_C_OFF, OPX_VD_OP + OPX_DO_LEVEL);
    m5_vp(1, OPX_VS_CCB + 19, 1200, OPX_C_OFF, OPX_VD_PROG + OPX_DP_PITCH);
    setup(&P, 0);
    memset(opx_cc, 0, sizeof opx_cc);
    a = m5_rms(FS / 8, FS / 8);
    opx_cc[0][74] = 128;
    b = m5_rms(FS / 8, FS / 8);
    opx_cc[0][74] = 0, opx_cc[0][20] = 65;
    st = m5_st(127, f0);
    opx_cc[0][20] = 128;
    ok = fabs(st) < 0.05 && fabs(m5_st(127, f0) - 12) < 0.15;
    memset(opx_cc, 0, sizeof opx_cc);
    check("M5 V.PATCH CC+ 74 -> OP1 LEVEL -100: none until a CC, silent at 127; CC+/- 20 -> PROG PITCH: 0 at 64, +12 at 127",
          a > 1000 && b < a * 0.01 && ok);
    /* EG3 -> OP1 SUS: EG3 at SUS 100 holds, OP1 SUS 60 + 40 = 100 (louder than SUS 60 alone); LFO3 -> PROG PITCH */
    m4_patch();
    P.op[0].sus = 60, P.op[0].dec = 300;
    setup(&P, 0);
    a = m5_rms(FS / 2, FS / 8);
    P.eg[2].sus = 100;
    m5_vp(0, OPX_VS_EG1 + 2, 4000, OPX_C_OFF, OPX_VD_OP + OPX_DO_SUS);
    setup(&P, 0);
    b = m5_rms(FS / 2, FS / 8);
    check("M5 V.PATCH EG3 (held at 100) -> OP1 SUS +40.00: SUS 60 heard as 100", b > a * 1.4 && b < a * 1.9);
    /* a destination OPX does not play yet (FILTER CUTOFF): nothing moves */
    m4_patch();
    m5_vp(0, OPX_C_MW, 10000, OPX_C_OFF, OPX_VD_FLT);
    setup(&P, 0);
    check("M5 V.PATCH: a destination not played yet (FILTER) is skipped", opx_rt[0].nvp == 0u);
    memset(&P, 0, sizeof P);
}

/* M5b: the main filter */
static int32_t m5b_code(double hz) { return (int32_t)lround(1023.0 * log2(hz / 6.489) / log2(23680.0 / 6.489)); }
static void m5b_saw(uint32_t type, double hz, uint32_t reso)   /* a SAW at A2 (110 Hz) through the filter */
{
    m4_patch();
    P.op[0].wave = OPX_W_SAW;
    P.flt.on = 1, P.flt.type = (uint8_t)type, P.flt.cutoff = (uint16_t)m5b_code(hz), P.flt.reso = (uint16_t)reso;
    P.flt.kc = 60;
}
static void test_m5b(void)
{
    static double y[FS], z[FS];
    double lo, hi, d1, d2;
    uint32_t i;
    int ok = 1;
    m4_patch();
    P.op[0].wave = OPX_W_SAW;
    setup(&P, 0);
    engine_render(45, 127, z, FS / 2, 0, 0);         /* (the SAW, no filter) */
    m5b_saw(OPX_MF_LPF12, 23680, 0);
    P.flt.cutoff = 1023;
    setup(&P, 0);
    check("M5b FILTER: an LPF wide open, RESO 0, nothing moving it (the opsix's INIT) is skipped", opx_rt[0].fon == 0u);
    m5b_saw(OPX_MF_LPF12, 1000, 0);                  /* LPF12 at 1 kHz: 110 Hz as it was, 4.4 kHz down ~24 dB */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d1 = harm_db(y, FS / 4, 8192, 110) - harm_db(z, FS / 4, 8192, 110);
    d2 = harm_db(y, FS / 4, 8192, 4400) - harm_db(z, FS / 4, 8192, 4400);
    if (fabs(d1) > 1.5 || d2 > -18 || d2 < -32) printf("opx: LPF12: 110 Hz %+.1f dB, 4.4 kHz %+.1f dB\n", d1, d2), ok = 0;
    m5b_saw(OPX_MF_LPF24, 1000, 0);                  /* LPF24: twice the slope */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d2 = harm_db(y, FS / 4, 8192, 4400) - harm_db(z, FS / 4, 8192, 4400);
    if (d2 > -38) printf("opx: LPF24: 4.4 kHz %+.1f dB\n", d2), ok = 0;
    m5b_saw(OPX_MF_HPF12, 1000, 0);                  /* HPF12: the low end down, the top as it was */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d1 = harm_db(y, FS / 4, 8192, 110) - harm_db(z, FS / 4, 8192, 110);
    d2 = harm_db(y, FS / 4, 8192, 3300) - harm_db(z, FS / 4, 8192, 3300);
    if (d1 > -30 || fabs(d2) > 2) printf("opx: HPF12: 110 Hz %+.1f dB, 3.3 kHz %+.1f dB\n", d1, d2), ok = 0;
    check("M5b FILTER: LPF12 / LPF24 / HPF12 at 1 kHz: 12 and 24 dB an octave (4.4 kHz -24, -48 dB), the pass band as it was", ok);
    ok = 1;                                          /* RESO: the harmonic at the cutoff lifted */
    m5b_saw(OPX_MF_LPF12, 1100, 0);
    setup(&P, 0);
    engine_render(45, 127, z, FS / 2, 0, 0);
    m5b_saw(OPX_MF_LPF12, 1100, 9000);
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d1 = harm_db(y, FS / 4, 8192, 1100) - harm_db(z, FS / 4, 8192, 1100);
    check("M5b FILTER: RESO 90 % lifts the harmonic at the cutoff (+10 dB or more)", d1 > 10);
    for (i = 0; i < OPX_NMF; i++) {                  /* every type: finite, inside full scale, RESO 100 % */
        m5b_saw(i, 500, 10000);
        setup(&P, 0);
        engine_render(45, 127, y, FS / 2, 0, 0);
        lo = 1e9, hi = -1e9;
        for (uint32_t j = 0; j < FS / 2; j++)
            lo = y[j] < lo ? y[j] : lo, hi = y[j] > hi ? y[j] : hi;
        if (!(hi < 131071 && lo > -131071 && hi - lo > 1000)) printf("opx: FILTER type %u: %.3f .. %.3f\n", i, lo, hi), ok = 0;
    }
    check("M5b FILTER: all 11 types at RESO 100 %: sound, inside full scale", ok);
    ok = 1;                                          /* (each type wide open, RESO 70 %, KTRK 1.00, at C7: no runaway) */
    for (i = 0; i < OPX_NMF; i++) {
        uint32_t j;
        m5b_saw(i, 23680, 7000);
        P.flt.kh = 64, P.flt.eg2 = 1800, P.eg[1].sus = 100;
        setup(&P, 0);
        engine_render(96, 127, y, FS / 2, 0, 0);
        lo = 1e9, hi = -1e9;
        for (j = 0; j < FS / 2; j++)
            lo = y[j] < lo ? y[j] : lo, hi = y[j] > hi ? y[j] : hi;
        if (!(hi < 131070 && lo > -131070)) printf("opx: FILTER type %u at the top: %.0f .. %.0f\n", i, lo, hi), ok = 0;
    }
    check("M5b FILTER: every type at the top of its range (C7, KTRK 1.00, EG2 +18), RESO 70 %: no runaway", ok);
    m5b_saw(OPX_MF_LPF12, 300, 0);                   /* EG2 +48.00 held at SUS 100: the cutoff 4 octaves up */
    setup(&P, 0);
    engine_render(45, 127, z, FS / 2, 0, 0);
    P.flt.eg2 = 4800, P.eg[1].sus = 100;
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d1 = harm_db(y, FS / 4, 8192, 2200) - harm_db(z, FS / 4, 8192, 2200);
    m5b_saw(OPX_MF_LPF12, 300, 0);                   /* KTRK HIGH +64 (1.00): an octave up, the cutoff an octave up */
    P.flt.kh = 64, P.flt.kc = 45;
    setup(&P, 0);
    engine_render(57, 127, y, FS / 2, 0, 0);
    m5b_saw(OPX_MF_LPF12, 600, 0);
    P.flt.kc = 45;
    setup(&P, 0);
    engine_render(57, 127, z, FS / 2, 0, 0);
    d2 = harm_db(y, FS / 4, 8192, 2640) - harm_db(z, FS / 4, 8192, 2640);
    check("M5b FILTER: EG2 INT +48 opens it (2.2 kHz +20 dB or more); KTRK HIGH 1.00 moves it with the key", d1 > 20 && fabs(d2) < 1);
    m5b_saw(OPX_MF_LPF12, 300, 0);                   /* V.PATCH MOD WHEEL -> FILTER CUTOFF +33.80 (4 octaves at full) */
    P.vp[0].src = OPX_C_MW, P.vp[0].amt = 3380, P.vp[0].dst = OPX_VD_FLT;
    setup(&P, 0);
    engine_render(45, 127, z, FS / 2, 0, 0);
    trk[0].mw = 127;
    engine_render(45, 127, y, FS / 2, 0, 0);
    trk[0].mw = 0;
    d1 = harm_db(y, FS / 4, 8192, 2200) - harm_db(z, FS / 4, 8192, 2200);
    check("M5b V.PATCH MOD WHEEL -> FILTER CUTOFF: the wheel opens it", d1 > 20);
    memset(&P, 0, sizeof P);
}

/* M5c: a program effect on the SAW at A2: type t, knobs v (values) in slot k */
static void m5c_fx(uint32_t k, uint32_t t, int b, int c, int e, int f)
{
    P.fx[k].type = (uint8_t)t, P.fx[k].on = 1;
    P.fx[k].v[0] = (int16_t)b, P.fx[k].v[1] = (int16_t)c, P.fx[k].v[2] = (int16_t)e, P.fx[k].v[3] = (int16_t)f;
}
static double m5c_rms(const double *y, uint32_t from, uint32_t n)
{
    double a = 0;
    for (uint32_t i = from; i < from + n; i++)
        a += y[i] * y[i];
    return 10 * log10(a / n + 1e-9);
}
static void test_m5c(void)
{
    static double y[FS], z[FS], r[FS];
    uint8_t pk[OPX_PK_BYTES];
    opx_patch_t d;
    double d1, d2, d3, lo, hi;
    uint32_t i, t;
    int ok = 1;
    opx_defaults(pk);                                /* the INIT: FX1 CHO, FX2 3EQ, FX3 REVERB, all ON (as an opsix's) */
    memset(&d, 0, sizeof d);
    opx_decode(pk, &d);
    check("M5c FX: INIT has FX1 CHORUS, FX2 3-BAND EQ (flat), FX3 REVERB HALL, all ON; the 13 cheap types play, CHO and the reverbs only in the OPX-only build",
          d.fx[0].type == 1u && d.fx[1].type == 14u && d.fx[2].type == 28u && d.fx[0].on && d.fx[1].on && d.fx[2].on &&
          d.fx[1].v[0] == 0 && d.fx[1].v[3] == 1000 && opx_fx_played(1) == OPX_FX_ALL && opx_fx_played(14) &&
          opx_fx_played(28) == OPX_FX_ALL);
    m5b_saw(OPX_MF_LPF12, 23680, 0);                 /* the SAW alone */
    P.flt.on = 0;
    setup(&P, 0);
    engine_render(45, 127, z, FS / 2, 0, 0);
    m5c_fx(1, 14, 0, 0, 0, 1000);                    /* 3EQ flat: as it was */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d1 = 0;
    for (i = FS / 4; i < FS / 2; i++)
        d1 = fabs(y[i] - z[i]) > d1 ? fabs(y[i] - z[i]) : d1;
    check("M5c 3EQ flat (the INIT's FX2): skipped, the sound exactly as it was", d1 == 0 && !opx_fx_live(&P.fx[1]));
    m5c_fx(1, 14, 120, -120, 120, 1000);              /* LOW +12, HIGH -12, MID +12 at 1 kHz */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d1 = harm_db(y, FS / 4, 8192, 110) - harm_db(z, FS / 4, 8192, 110);
    d2 = harm_db(y, FS / 4, 8192, 990) - harm_db(z, FS / 4, 8192, 990);
    d3 = harm_db(y, FS / 4, 8192, 7040) - harm_db(z, FS / 4, 8192, 7040);
    printf("opx: 3EQ LOW +12 HIGH -12 MID +12 @1k: 110 Hz %+.1f, 990 Hz %+.1f, 7.0 kHz %+.1f dB\n", d1, d2, d3);
    check("M5c 3EQ: LOW +12 lifts 110 Hz (+10 dB or more), MID +12 at 1 kHz lifts 990 Hz (+9), HIGH -12 cuts 7 kHz (-3: first-order shelves)",
          d1 > 10 && d2 > 9 && d3 < -3);
    memset(P.fx, 0, sizeof P.fx);
    m5c_fx(0, 13, 10, 0, 0, 1000);                   /* FLTR CUTOFF 10 % (~40 Hz), no LFO */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    d1 = harm_db(y, FS / 4, 8192, 2200) - harm_db(z, FS / 4, 8192, 2200);
    check("M5c FLTR: CUTOFF low takes the top off (2.2 kHz -30 dB or more)", d1 < -30);
    m5c_fx(0, 5, 100, 0, 1000, 500);                 /* PAN DEPTH 100 TRI 1 Hz PHASE 50 %: two channels, opposite */
    opx_t_right = r, opx_t_wide = 0;
    setup(&P, 0);
    engine_render(45, 127, y, FS, 0, 0);
    opx_t_right = 0;
    d1 = m5c_rms(y, FS / 8, FS / 8) - m5c_rms(r, FS / 8, FS / 8);   /* (a quarter cycle apart) */
    d2 = m5c_rms(y, 5 * FS / 8, FS / 8) - m5c_rms(r, 5 * FS / 8, FS / 8);
    printf("opx: PAN: left - right %+.1f dB, then %+.1f dB\n", d1, d2);
    check("M5c PAN: two channels (the mix at full width), the louder side moving across", opx_t_wide && fabs(d1) > 3 &&
          fabs(d2) > 3 && d1 * d2 < 0);
    m5c_fx(0, 19, -200, -60, 1000, 0);               /* LIMIT THRE -20 CEIL -6: never past -6 dBFS */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 2, 0, 0);
    hi = 0;
    for (i = 0; i < FS / 2; i++)
        hi = fabs(y[i]) > hi ? fabs(y[i]) : hi;
    printf("opx: LIMIT -20 / -6: peak %.0f (-6 dBFS: %.0f)\n", hi, OPX_FXFS * 0.501);
    check("M5c LIMIT: the peak at CEIL (-6 dBFS) at most, and near it", hi <= OPX_FXFS * 0.502 && hi > OPX_FXFS * 0.4);
    m5c_fx(0, 15, 0, -850, 1000, 0);                 /* DIST LEVEL -Inf: silent */
    setup(&P, 0);
    engine_render(45, 127, y, FS / 4, 0, 0);
    hi = 0;
    for (i = 0; i < FS / 4; i++)
        hi = fabs(y[i]) > hi ? fabs(y[i]) : hi;
    check("M5c DIST LEVEL -Inf: silent", hi == 0);
    m4_patch();                                      /* a sine: DIST DRIVE 100 makes harmonics */
    setup(&P, 0);
    engine_render(57, 127, z, FS / 2, 0, 0);
    m5c_fx(0, 15, 100, 0, 1000, 0);
    setup(&P, 0);
    engine_render(57, 127, y, FS / 2, 0, 0);
    d1 = harm_db(y, FS / 4, 8192, 660) - harm_db(y, FS / 4, 8192, 220);
    d2 = harm_db(z, FS / 4, 8192, 660) - harm_db(z, FS / 4, 8192, 220);
    check("M5c DIST DRIVE 100: the third harmonic of a sine up from nothing (-30 dB of the first or more)", d1 > -30 && d2 < -60);
    m5c_fx(0, 17, 100, 1200, 40, 0);                 /* DEC 12 kHz, 4 bits: few levels */
    setup(&P, 0);
    engine_render(57, 127, y, FS / 2, 0, 0);
    {
        double lv[64];
        uint32_t nl = 0, j;
        for (i = FS / 4; i < FS / 2 && nl < 64u; i++) {
            for (j = 0; j < nl && lv[j] != y[i]; j++)
                ;
            if (j == nl)
                lv[nl++] = y[i];
        }
        check("M5c DEC BIT 4.0: 16 levels or fewer", nl <= 17u);
    }
    for (t = 1; t < OPX_NFXT; t++) {                 /* every type, its knobs at both ends: finite, bounded */
        uint32_t end;
        for (end = 0; end < 2u; end++) {
            const opx_fxt_t *x = &OPX_FXT[t];
            m5b_saw(OPX_MF_LPF12, 23680, 0);
            P.flt.on = 0;
            P.fx[0].type = (uint8_t)t, P.fx[0].on = 1;
            for (i = 0; i < x->nk; i++)
                P.fx[0].v[i] = end ? x->k[i].hi : x->k[i].lo;
            m5c_fx(1, 5, 100, 0, 5000, 300);         /* (then PAN: two channels into FX3) */
            P.fx[2] = P.fx[0];
            setup(&P, 0);
            engine_render(45, 127, y, FS / 4, 0, 0);
            lo = 1e12, hi = -1e12;
            for (i = 0; i < FS / 4; i++)
                lo = y[i] < lo ? y[i] : lo, hi = y[i] > hi ? y[i] : hi;
            if (!(hi < (1 << 23) && lo > -(1 << 23)))
                printf("opx: FX %s at its %s: %.0f .. %.0f\n", x->name, end ? "highs" : "lows", lo, hi), ok = 0;
        }
    }
    check("M5c FX: every type, its knobs at both ends, mono and after PAN: inside the states' bounds", ok);
    memset(&P, 0, sizeof P);
}

static void test_factory(void)
{
    static double y[FS], z[FS];
    uint32_t pi, note, i, ok = 1, same;
    double peak = 0, worst_mean = 0;
    setup(0, 0);
    trk[0].p[P_E7] = 0;
    engine_render(60, 100, y, FS / 2, 0, 0);
    memset(opx_fxs, 0, sizeof opx_fxs);              /* (the part's effects from rest again: their LFOs run on) */
    engine_render(60, 100, z, FS / 2, 0, 0);
    same = !memcmp(y, z, sizeof(double) * (FS / 2));
    check("a note from silence renders the same samples twice", same);
    for (pi = 0; pi < OPX_NFACTORY; pi++)
        for (note = 24; note <= 96; note += 12) {
            double s = 0;
            setup(0, 0);
            trk[0].p[P_E7] = (int16_t)pi;
            engine_render(note, 127, y, FS, FS / 2, 0);
            for (i = 0; i < FS; i++) {
                s += y[i];
                peak = fabs(y[i]) > peak ? fabs(y[i]) : peak;
            }
            s = fabs(s / FS);
            worst_mean = s > worst_mean ? s : worst_mean;
            ok &= peak <= 2.0 * VOICE_FS;
        }
    {
        char what[160];
        snprintf(what, sizeof what, "factory patches C1..C7: peak %.0f (<= 2 x VOICE_FS), |mean| %.0f (< 1 %% of VOICE_FS)",
                 peak, worst_mean);
        check(what, ok && worst_mean < 0.01 * VOICE_FS);
    }
}

/* ------------------------------------------------------------------ cost --- */
static double cpu_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint8_t cost_wave;        /* the heavy patch's waveform (M2) */
static double engine_time(uint32_t e, uint32_t notes)   /* CPU seconds for 2 s of track 0, `notes` held */
{
    static int32_t o[2 * CTL];
    uint32_t i, nb = FS * 2u / CTL, rep;
    double best = 1e9;
    for (rep = 0; rep < 3u; rep++) {
        double t0;
        memset(trk, 0, sizeof trk);
        memset(eng_mem, 0, sizeof eng_mem);
        host_tracks_init();
        host_preset(&trk[0], e, 0);
        if (e == ENGI_OPX) {
            static opx_patch_t heavy;
            uint32_t k;
            memset(&heavy, 0, sizeof heavy);
            heavy.algo = 0;                              /* algorithm 1: all six sounding, three inputs */
            for (k = 0; k < 6u; k++)
                heavy.op[k] = OP_FLAT, heavy.op[k].fb = k == 5u ? 40 : 0, heavy.op[k].wave = cost_wave;
            opx_own[0] = &heavy;
        }
        trk[0].p[P_VOICE] = V_POLY;
        for (i = 0; i < notes; i++)
            trk_note_on(&trk[0], 48 + 3 * i, 100);
        for (i = 0; i < 64u; i++)
            mix_block(o, CTL);
        t0 = cpu_now();
        for (i = 0; i < nb; i++)
            mix_block(o, CTL);
        t0 = cpu_now() - t0;
        best = t0 < best ? t0 : best;
    }
    opx_own[0] = 0;
    return best;
}

static void test_cost(void)
{
    double fm6 = engine_time(ENGI_FM6, 6) - engine_time(ENGI_FM6, 0);
    double opx = engine_time(ENGI_OPX, 6) - engine_time(ENGI_OPX, 0);
    printf("opx: cost: 6 voices, 2 s: OPX %.1f ms, FM6 %.1f ms of host CPU: OPX = %.2f x FM6\n", opx * 1e3, fm6 * 1e3,
           opx / fm6);
    check("cost: measured against FM6 (printed; the device decides the voice count)", (fm6 > 0 || FELUCCA_OPX_ONLY) && opx > 0);
    {   /* M2: the heaviest waveforms on all six operators */
        static const uint8_t W[] = {OPX_W_SAW, OPX_W_SAWHD, OPX_W_SQRHD, OPX_W_A12345, OPX_W_PINK};
        uint32_t i;
        for (i = 0; i < NELEM(W); i++) {
            double c;
            cost_wave = W[i];
            c = engine_time(ENGI_OPX, 6) - engine_time(ENGI_OPX, 0);
            printf("opx: cost: all six operators %-11s: OPX = %.2f x FM6\n", N_OPX_WAVE[W[i]], c / fm6);
        }
        cost_wave = 0;
    }
}

/* ----------------------------------------------------------------- demos --- */
static void demo(const char *dir, uint32_t pi)
{
    static int32_t o[2 * CTL];
    const preset_t *pr = &ENG_OPX.presets[pi];
    uint32_t pat = pr->pat ? pr->pat - 1u : 4u, step = FS / 8u, s, i, held = 0, frames = 0;
    char path[512], nm[32];
    FILE *w;
    for (i = 0; pr->name[i] && i < 31u; i++)
        nm[i] = pr->name[i] == ' ' || pr->name[i] == '.' ? '_' : pr->name[i];
    nm[i] = 0;
    snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, pi, nm);
    if (!(w = fopen(path, "wb")))
        return;
    wav_hdr(w, 0);
    memset(trk, 0, sizeof trk);
    memset(eng_mem, 0, sizeof eng_mem);
    host_tracks_init();
    host_preset(&trk[0], ENGI_OPX, pi);
    opx_own[0] = 0;
    for (s = 0; s < 48u; s++) {                       /* three times through the pattern's 16 steps, 120 BPM 1/16 */
        uint32_t k = s % 16u, note = PATTERNS[pat].note[k], fl = PATTERNS[pat].flags[k];
        if (!(fl & 4u) && held) {
            trk_note_off(&trk[0], held);
            held = 0;
        }
        if (note && !(fl & 4u)) {
            trk_note_on(&trk[0], note, fl & 1u ? 120 : 90);
            held = note;
        }
        for (i = 0; i < step; i += CTL) {
            uint32_t j;
            mix_block(o, CTL);
            for (j = 0; j < CTL; j++)
                wav_put(w, o[2 * j], o[2 * j + 1]);
            frames += CTL;
        }
    }
    if (held)
        trk_note_off(&trk[0], held);
    for (i = 0; i < FS * 2u; i += CTL) {
        uint32_t j;
        mix_block(o, CTL);
        for (j = 0; j < CTL; j++)
            wav_put(w, o[2 * j], o[2 * j + 1]);
        frames += CTL;
    }
    rewind(w);
    wav_hdr(w, frames);
    fclose(w);
}

/* --dump: the factory patches and a record of random values, packed: a hex line, then name=value, then -- (checked
 * by tools/opx_patch_spec.py --check-c: the firmware's packer and the spec agree bit for bit) */
static void dump(void)
{
    static uint8_t pk[OPX_PK_BYTES];
    uint32_t i, f, k, seed = 12345;
    for (i = 0; i <= OPX_NFACTORY + 2u; i++) {
        if (i < OPX_NFACTORY)
            opx_encode(&OPX_FACTORY[i], pk);
        else if (i == OPX_NFACTORY)
            opx_defaults(pk);
        else
            for (f = 0; f < OPXF_COUNT; f++) {            /* every field anywhere in its range */
                seed = seed * 1103515245u + 12345u;
                opx_put(pk, f, OPX_FIELDS[f].lo + (int32_t)((seed >> 8) % (uint32_t)(OPX_FIELDS[f].hi - OPX_FIELDS[f].lo + 1)));
            }
        for (k = 0; k < OPX_PK_BYTES; k++)
            printf("%02x", pk[k]);
        printf("\n");
        for (f = 0; f < OPXF_COUNT; f++)
            printf("%s=%d\n", OPX_FIELD_NAME[f], (int)opx_get(pk, f));
        printf("--\n");
    }
}

int main(int argc, char **argv)
{
    uint32_t pi;
    opx_wt_build();                                    /* (the waveform tables: the firmware builds them in opx_poll) */
    if (argc > 1 && !strcmp(argv[1], "--dump")) {
        dump();
        return 0;
    }
    test_reference();
    test_orders();
    test_macros_off();
    test_modes();
    test_envelopes();
    test_feedback();
    test_velocity();
    test_m4();
    test_m5();
    test_m5b();
    test_m5c();
    test_user_empty();
    test_waves();
    test_factory();
    test_cost();
    if (argc > 1) {
        for (pi = 0; pi < ENG_OPX.npresets; pi++)
            demo(argv[1], pi);
        printf("opx: demos in %s\n", argv[1]);
    }
    printf(bad ? "opx: FAILED\n" : "opx: all ok\n");
    return bad != 0;
}
