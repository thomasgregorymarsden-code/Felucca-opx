/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX fork: an on-device benchmark, for the CPU budget of the OPX engine (docs/OPX_PLAN.md). MENU > SYSTEM > INFO,
 * OCT+ runs it; the results are a scrolling page (ui_menu.c, ui.menu 4).
 *
 * What it measures, on the FM-1's own processor and compiled code:
 *   - the mix with no note (every track idle): the base the others are measured against
 *   - six held notes of FM6 (its first factory patch) and of OPX: algorithm 1 with all six operators sounding, the
 *     sixth with feedback (as tests/opx_test.c's cost), with each of the 23 waveforms on all six operators
 *   - the arithmetic itself: a dependent chain of multiply-adds in 32-bit and 64-bit integers, and in float when built
 *     for the r3 core (FELUCCA_CPU=r3: the FPU; a generic build makes `float` a software routine it does not link)
 * Each engine row is the share of real time the six notes take on top of the idle mix (the audio sheds a voice above
 * 85 % for the whole mix: audio.c SHED_TICKS), and the same per note. OPX's rows are named by the waveform.
 *
 * How it stays out of the music's way: the transport must be stopped. Every note is cut; track 1 (its sound, its
 * engine's state, its FM6 and OPX patches) is saved and put back after; the audio interrupt keeps running but outputs
 * silence and touches no track (bench_mute, audio.c), so USB and the keys go on. The timing runs in chunks of 8 blocks
 * with interrupts off (a few ms each: nothing else counted), the watchdog fed between them. Track 1's engine state is
 * cleared after, as an engine switch clears it (engines.c eng_mem_claim). Keys or MIDI notes played during the run may
 * sound on the selected track once it ends. */
#ifndef FELUCCA_BENCH
#define FELUCCA_BENCH FELUCCA_OPX
#endif
#if FELUCCA_BENCH
static volatile uint8_t bench_mute;                    /* audio.c: the ISR's halves are silence */
#define BENCH_ROWS 48u
enum { BR_HEAD, BR_PCT, BR_NS, BR_SUB };               /* a row: a heading (ALL ONE), a share of real time (permille),
                                                        * ns x 10, a sub-heading */
static struct { char name[16]; uint8_t kind; uint16_t all, one; } bench_row[BENCH_ROWS];   /* (ui_menu.c draws them) */
static uint8_t bench_n;
static int32_t bench_out[2u * CTL];
static opx_patch_t bench_px __attribute__((section(".pool")));
static track_t bench_trk __attribute__((section(".pool")));
#define BENCH_CHUNK 8u                                 /* blocks timed with interrupts off at a time */
#define BENCH_BLOCKS 128u                              /* blocks timed per case (~93 ms of audio) */
#define BENCH_TICKS_PER_SAMPLE 544u                    /* TIMER4 ticks (24 MHz) per sample at 44.1 kHz (audio.c DAC_TICKS) */

static uint32_t bench_mix(uint32_t blocks)            /* TIMER4 ticks of `blocks` mix blocks */
{
    uint32_t done = 0, ticks = 0;
    while (done < blocks) {
        uint32_t k, t0;
        fm1_irq_off();
        t0 = fm1_ticks();
        for (k = 0; k < BENCH_CHUNK && done < blocks; k++, done++)
            mix_block(bench_out, CTL);
        ticks += fm1_ticks() - t0;
        fm1_irq_on();
        fm1_wdt_feed();
    }
    return ticks;
}
static void bench_line(uint32_t kind, const char *name, uint32_t all, uint32_t one)
{
    if (bench_n >= BENCH_ROWS)
        return;
    str_cpy(bench_row[bench_n].name, name, sizeof bench_row[0].name);
    bench_row[bench_n].kind = (uint8_t)kind;
    bench_row[bench_n].all = (uint16_t)(all > 65535u ? 65535u : all);
    bench_row[bench_n].one = (uint16_t)(one > 65535u ? 65535u : one);
    bench_n++;
}
static void bench_kill(track_t *t)                     /* every voice of t gone at once (no fade) */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = v->env_out = 0;
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
    t->xf_on = 0;
}
/* track 1 as engine e, its values from preset pi's, its engine state fresh */
static void bench_track(uint32_t e, uint32_t pi)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    track_t *t = &trk[0];
    const preset_t *p = &ENGINES[e]->presets[pi % ENGINES[e]->npresets];
    uint32_t i;
    bench_kill(t);
    for (i = 0; i < P_E0; i++)                         /* every value its default (no INSERT, SLICER, matrix ...) */
        t->p[i] = TP[i].def;
    t->eng_req = t->engine = (uint8_t)e;
    eng_mem_owner[0] = 0;                              /* its state from zeros now, before the notes start (a claim at the
                                                        * next block would wipe the notes' state: they would end at once) */
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = p->e[i];
    t->p[P_ATK] = p->env[0];
    t->p[P_DEC] = p->env[1];
    t->p[P_SUS] = p->env[2];
    t->p[P_REL] = p->env[3];
    t->p[P_VOICE] = V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(p->fx[i] ? p->fx[i] - 1 : FX_DEF[i]);
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;      /* (the sends: the engine alone) */
    eng_mem_claim(t);
}
/* six notes on track 1, their attack past, then the share of real time above the idle mix (permille) */
static uint32_t bench_voices;                         /* voices still sounding at the end of the last case */
static uint32_t bench_notes(uint32_t idle)
{
    uint32_t i, ticks, real = BENCH_BLOCKS * CTL * BENCH_TICKS_PER_SAMPLE;
    for (i = 0; i < 6u; i++)
        trk_note_on(&trk[0], 48u + 3u * i, 100);
    bench_mix(16);                                     /* (the engine switch, the attacks) */
    ticks = bench_mix(BENCH_BLOCKS);
    for (i = 0, bench_voices = 0; i < NVOICE; i++)    /* (still sounding at the end: the row is real) */
        bench_voices += trk[0].v[i].active;
    bench_kill(&trk[0]);
    return ticks > idle ? (uint32_t)(((uint64_t)(ticks - idle) * 1000u) / real) : 0u;
}
/* a note's share; fewer than six still sounding at the end: BENCH_FEW | how many (ui_menu.c: "n OF 6") */
#define BENCH_FEW 0xFFF0u
static uint32_t bench_one(uint32_t pm) { return bench_voices >= 6u ? pm / 6u : BENCH_FEW | bench_voices; }
/* the arithmetic: n dependent multiply-adds; ns per operation x 10 */
static volatile int32_t bench_i32 = 3;
static volatile int64_t bench_i64 = 3;
#if FELUCCA_HWFLOAT
static volatile float bench_f32 = 1.0001f;           /* (built for r3: the FPU) */
#endif
static uint32_t bench_ns10(uint32_t ticks, uint32_t n) { return (uint32_t)((uint64_t)ticks * 10000u / 24u / n); }
/* the cost of a branch and of a call: the same loop (a random number a step, a sum) bare, with an if on a bit of
 * the number (RANDOM: either way at random; ALWAYS: one way every time), and calling a function. The rows: ns a
 * step; the difference from LOOP is the branch's (or the call's) cost. (OPX's waveform loop: a few branches per
 * operator and sample; this tells whether they are what costs) */
static volatile uint32_t bench_va, bench_vb, bench_one_way = 1;
static uint32_t __attribute__((noinline)) bench_callee(uint32_t x) { return x ^ (x >> 7); }
static void bench_branch(void)
{
    enum { N = 20000 };
    uint32_t i, t0, t, k, x, way = bench_one_way;
    static const char *const NAME[4] = {"LOOP", "+ BRANCH ALWAYS", "+ BRANCH RANDOM", "+ CALL"};
    for (k = 0; k < 4u; k++) {
        x = 12345u;
        fm1_irq_off();
        t0 = fm1_ticks();
        switch (k) {                                     /* (a store to a volatile a step: the work can't vanish; */
        case 0:                                          /* the arms store to different ones: a real branch) */
            for (i = 0; i < N; i++) {
                x = x * 1664525u + 1013904223u;
                bench_va = x >> 9;
            }
            break;
        case 1:
            for (i = 0; i < N; i++) {
                x = x * 1664525u + 1013904223u;
                if (way)
                    bench_va = x >> 9;
                else
                    bench_vb = x >> 11;
            }
            break;
        case 2:
            for (i = 0; i < N; i++) {
                x = x * 1664525u + 1013904223u;
                if ((x >> 16) & 1u)
                    bench_va = x >> 9;
                else
                    bench_vb = x >> 11;
            }
            break;
        default:
            for (i = 0; i < N; i++) {
                x = x * 1664525u + 1013904223u;
                bench_va = bench_callee(x) >> 9;
            }
        }
        t = fm1_ticks() - t0;
        fm1_irq_on();
        bench_line(BR_NS, NAME[k], bench_ns10(t, N), 0);
    }}
/* a load's cost from each memory: a sum of 1024 16-bit values, ns a value: RAM (.bss), the pool (the OPX waveform
 * tables' memory), the stack, flash (the sine table, through the cache) */
static int16_t bench_ram[1024];
static int32_t __attribute__((noinline)) bench_sum(const int16_t *p, uint32_t passes)
{
    uint32_t i, k;
    int32_t acc = 0;
    for (k = 0; k < passes; k++)
        for (i = 0; i < 1024u; i++)
            acc += p[i];
    return acc;
}
static void bench_loads(void)
{
    enum { PASSES = 20 };
    int16_t stk[1024];
    const int16_t *src[4];
    static const char *const NAME[4] = {"LOAD RAM", "LOAD POOL", "LOAD STACK", "LOAD FLASH"};
    uint32_t i, k, t0, t;
    for (i = 0; i < 1024u; i++)
        stk[i] = bench_ram[i] = (int16_t)i;
    src[0] = bench_ram, src[1] = opx_wt, src[2] = stk, src[3] = SINE;
    for (k = 0; k < 4u; k++) {
        fm1_irq_off();
        t0 = fm1_ticks();
        bench_va = (uint32_t)bench_sum(src[k], PASSES);
        t = fm1_ticks() - t0;
        fm1_irq_on();
        bench_line(BR_NS, NAME[k], bench_ns10(t, PASSES * 1024u), 0);
    }
}
static void bench_math(void)
{
    enum { N = 20000 };
    uint32_t i, t0, t;
    bench_branch();
    bench_loads();
    {
        int32_t a = bench_i32, x = 1;
        fm1_irq_off();
        t0 = fm1_ticks();
        for (i = 0; i < N; i++)
            x = x * a + (int32_t)i;
        t = fm1_ticks() - t0;
        fm1_irq_on();
        bench_i32 = x;
        bench_line(BR_NS, "INT32 MAC", bench_ns10(t, N), 0);
    }
    {
        int64_t a = bench_i64, x = 1;
        fm1_irq_off();
        t0 = fm1_ticks();
        for (i = 0; i < N; i++)
            x = x * a + (int64_t)i;
        t = fm1_ticks() - t0;
        fm1_irq_on();
        bench_i64 = x;
        bench_line(BR_NS, "INT64 MAC", bench_ns10(t, N), 0);
    }
#if FELUCCA_HWFLOAT
    {
        float a = bench_f32, x = 1.0f;
        fm1_irq_off();
        t0 = fm1_ticks();
        for (i = 0; i < N; i++)
            x = x * a + 0.5f;
        t = fm1_ticks() - t0;
        fm1_irq_on();
        bench_f32 = x;
        bench_line(BR_NS, "FLOAT MAC", bench_ns10(t, N), 0);
    }
#endif
    fm1_wdt_feed();
}

/* the whole run (main loop, the transport stopped): 1 = done, 0 = not run (playing) */
static int bench_run(void)
{
    static const uint8_t W[] = {OPX_W_SIN12, OPX_W_SIN8, OPX_W_TRI, OPX_W_SAW, OPX_W_SAWHD, OPX_W_SQR, OPX_W_SQRHD,
                                OPX_W_ASAW3, OPX_W_ASQR3, OPX_W_ATRI3, OPX_W_A12345, OPX_W_A12, OPX_W_A18,
                                OPX_W_NOISE_SH, OPX_W_WHITE, OPX_W_PINK, OPX_W_BLUE};
    uint8_t fm6p[FP_SIZE + 1u], fm6own[FM6_PACKED], fm6slot = fm6_slot[0], fm6ok = fm6_own_ok;
    const opx_patch_t *own = opx_own[0];
    uint32_t i, k, idle, pm;
    if (song.playing || transport_req == 1u)
        return 0;
    bench_n = 0;
    memcpy(fm6p, fm6_patch[0], sizeof fm6p);
    memcpy(fm6own, fm6_own[0], sizeof fm6own);
    fm1_irq_off();
    bench_mute = 1;
    memcpy(&bench_trk, &trk[0], sizeof bench_trk);
    for (i = 0; i < NPART; i++)
        bench_kill(&trk[i]);
    fm1_irq_on();

    bench_track(ENGI_FM6, 0);                          /* (an engine with no note: the idle mix) */
    bench_mix(16);
    idle = bench_mix(BENCH_BLOCKS);
    bench_line(BR_PCT, "IDLE MIX", (uint32_t)(((uint64_t)idle * 1000u) / (BENCH_BLOCKS * CTL * BENCH_TICKS_PER_SAMPLE)),
               0xFFFFu);
    bench_line(BR_HEAD, "6 NOTES", 0, 0);

    bench_track(ENGI_FM6, 0);                          /* FM6: its first factory patch */
    fm6_load_slot(0, 0);
    pm = bench_notes(idle);
    bench_line(BR_PCT, "FM6 F1", pm, bench_one(pm));
    bench_line(BR_SUB, "OPX, THE WAVE ON ALL 6 OPS", 0, 0);

    memset(&bench_px, 0, sizeof bench_px);             /* OPX: algorithm 1, all six sounding, the sixth with feedback */
    bench_px.algo = 0;
    for (k = 0; k < OPX_NOP; k++) {
        opx_op_t *o = &bench_px.op[k];
        o->level = 80;
        o->coarse = 7;
        o->fine = 1000;
        o->sus = 100;
        o->fb = k == 5u ? 40 : 0;
    }
    for (i = 0; i <= NELEM(W); i++) {
        uint32_t w = i ? W[i - 1u] : OPX_W_SIN;
        char name[20];
        for (k = 0; k < OPX_NOP; k++)
            bench_px.op[k].wave = (uint8_t)w;
        bench_track(ENGI_OPX, 0);
        opx_own[0] = &bench_px;
        pm = bench_notes(idle);
        str_cpy(name, N_OPX_WAVE[w], sizeof name);
        bench_line(BR_PCT, name, pm, bench_one(pm));
    }
    bench_line(BR_SUB, "OPX, THE MODE ON ALL 6 OPS", 0, 0);   /* (M2b: SIN, the mode's values typical) */
    {
        static const struct { uint8_t md, type; char name[12]; } MD[] = {
            {OPX_M_RING, 0, "RING MOD"}, {OPX_M_FOLD, 0, "WAVE FOLDER"}, {OPX_M_FILTER, 0, "FILTER LPF"},
            {OPX_M_FILTER, 5, "FILTER MG24"}, {OPX_M_FILTER_FM, 0, "FILTER FM"}, {OPX_M_EFFECT, 0, "FX PEAK EQ"},
            {OPX_M_EFFECT, 2, "FX PHASER"}, {OPX_M_EFFECT, 4, "FX COMB"}};
        for (i = 0; i < NELEM(MD); i++) {
            for (k = 0; k < OPX_NOP; k++) {
                opx_op_t *o = &bench_px.op[k];
                o->wave = OPX_W_SIN, o->mode = MD[i].md;
                o->ring_depth = 100, o->ring_shape = 30, o->fold_gain = 50, o->fold_oscmix = 100;
                o->flt_type = MD[i].type, o->flt_reso = 40, o->flt_oscmix = 100, o->flt_cutoff = 120;
                o->fx_type = MD[i].type, o->fx_oscmix = 100, o->fx_e = 620, o->fx_f = 150;
            }
            bench_track(ENGI_OPX, 0);
            opx_own[0] = &bench_px;
            pm = bench_notes(idle);
            bench_line(BR_PCT, MD[i].name, pm, bench_one(pm));
        }
        for (k = 0; k < OPX_NOP; k++)
            bench_px.op[k].mode = OPX_M_FM;
    }
    bench_math();

    fm1_irq_off();                                     /* track 1 back as it was */
    bench_kill(&trk[0]);
    memcpy(&trk[0], &bench_trk, sizeof bench_trk);
    bench_kill(&trk[0]);
    eng_mem_owner[0] = 0;
    opx_own[0] = own;
    memcpy(fm6_patch[0], fm6p, sizeof fm6p);          /* (as it was, byte for byte: then the ISR's copy follows) */
    fm6_pgen[0]++;
    fm1_irq_on();
    fm6_slot[0] = fm6slot;
    memcpy(fm6_own[0], fm6own, sizeof fm6own);
    fm6_own_ok = fm6ok;
    bench_mute = 0;
    return 1;
}
#endif
