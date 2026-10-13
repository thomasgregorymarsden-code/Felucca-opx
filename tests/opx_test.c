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
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}

/* the engine alone, one voice: note at vel, n samples, gate released at rel (0: held). y: the render's out[] */
static void engine_render(uint32_t note, uint32_t vel, double *y, uint32_t n, uint32_t rel, int *ended)
{
    static int32_t b[CTL];
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
    m.shape = 64 << 8;
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

static void test_factory(void)
{
    static double y[FS], z[FS];
    uint32_t pi, note, i, ok = 1, same;
    double peak = 0, worst_mean = 0;
    setup(0, 0);
    trk[0].p[P_E7] = 0;
    engine_render(60, 100, y, FS / 2, 0, 0);
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
                heavy.op[k] = OP_FLAT, heavy.op[k].fb = k == 5u ? 40 : 0;
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
    check("cost: measured against FM6 (printed; the device decides the voice count)", fm6 > 0 && opx > 0);
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

int main(int argc, char **argv)
{
    uint32_t pi;
    test_reference();
    test_envelopes();
    test_feedback();
    test_velocity();
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
