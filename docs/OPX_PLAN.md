# OPX: design, status and plan

OPX is an altered-FM engine modelled on the Korg opsix, built into Felucca as engine 15. This document is for
developers: how the engine works, what is stored where, what is assumed, and what comes next. How to play and edit
it is in the [README](../README.md#the-opx-engine); the flash layout and the engine number are in
[OPX_STORAGE.md](OPX_STORAGE.md); the bit-packed patch format is `tools/opx_patch_spec.py`.

## Milestones

| | Scope | Status |
|---|---|---|
| M0 | Engine slot 15 (14 reserved), presets, the tables every engine appears in, relocatable storage region | done |
| M1 | Six operators in FM mode (sine), all 40 algorithms plus USER, ratio and fixed pitch, per-operator FB, ALG FB loops, per-operator ATK/DEC/SUS/REL/CURVE envelopes, level velocity; device macros; cost against FM6 | done |
| M1.5 | Operator editing on the device (algorithm charts, USER matrix, OPERATORS list, scope); each track's patch kept in the stored format; saved with projects and the autosave | done |
| | **First release, `0.0.1`: M0, M1 and M1.5** | |
| M2a | The 23 waveforms and WIDTH; the renderer an operator at a time | done (device cost: see [Cost](#cost)) |
| M2b | Operator modes RING, FILTER, FILTER FM, FOLD, EFFECT, BYPASS, MUTE; level key tracking | done (device cost: see [Cost](#cost); SHORT DELAY limited to 17 ms, see below; sound not yet compared with an opsix) |
| | **Second release, `0.1.0`: M2a and M2b; the VOICE pages in OPERATORS; SELECT turns pages with BPM LOCK ON** | |
| M3 | OPX patches in user presets and the editor backup; the web editor's OPX patch page | done: user presets (16), the backup, the storage repacked ([OPX_STORAGE.md](OPX_STORAGE.md)), the web editor's OPX tab |
| M4 | EG1 (pitch), LFO1 and their default routes: each operator's P MOD (EG1, LFO1, VEL), LMOD LFO1, PROG PITCH (LFO1, TRANS, OCT); the CTRL sources | done (pages EG1 and LFO1, the rows in OPERATORS, the web editor; not yet compared with an opsix) |
| M5 | EG2 (filter), EG3, LFO2–3, main filter, effects 1–3, virtual patch, as far as the processor time allows | |

Confirmed on a device so far: it installs and runs, and six OPX voices at once do not overload the processor. Not
yet compared with a real opsix by ear (see [Assumptions](#assumptions-to-check-by-ear)).
## How M1 works

- **One renderer for every algorithm.** Each algorithm becomes a modulation matrix: for every pair of operators, how much one modulates the other, plus each operator's self-feedback and which operators are heard.
  - The 40 presets are the manual's charts (p.83) at 100 % per connection: 1–32 are the DX7's algorithms, 33–40 the opsix's own.
  - USER is the patch's own matrix.
  - `tests/opx_test.c` checks every chart against a table written separately from the engine, and renders every algorithm against a floating-point reference implementation. They agree to better than −45 dB.
- **Order and timing.** Each sample, the operators run from 6 down to 1. An input from a higher-numbered operator is this sample's output. An input from a lower-numbered one, as in the loops of algorithms 4, 6 and 34 or an upward link in USER, is the previous sample's.
- **Waveforms (M2a)** replace the operator's sine; a plain sine with WIDTH OFF takes M1's path unchanged, sample for
  sample. Every periodic wave is read from a table the way the sine is (`eng_opx.c` `opx_wt_build`, `opx_table`), so
  it should cost about what the sine costs. The tables (16 KB of RAM: the pool had about 32 KB free) are built once
  in the main loop, after boot; until then every wave plays as a sine.
  - SIN 12BIT / 8BIT: the sine with its low 4 / 8 bits cleared. TRI: a table of four straight lines, exact.
  - The eleven ADTV waves: a 256-point table each, sums of sines scaled to a full-scale peak.
  - SAW: nine tables, with 1, 2, 4 … 256 harmonics at 1/n. Each block, the operator's pitch picks the table whose
    harmonics stay under 10 kHz; SAW HD goes to 20 kHz. SQR is the SAW table minus itself half a cycle later, which
    cancels the even harmonics, so it costs two reads. Each table has 32 points per cycle of its top harmonic (128 to
    1024 points). `opx_test` at 2.2 kHz: aliases and table images 79–88 dB below the fundamental.
  - The noises are made a block (32 samples) at a time per operator per voice. S/H follows the operator's own pitch,
    without FM.
  - Assumptions: WIDTH squeezes the cycle into its first WIDTH % and is silent after (from the manual's pictures);
    HD means more high harmonics (the manual says "richer high-end overtones"); BLUE is pink noise's difference (+9 dB
    over 4 octaves, short of +12 near Nyquist).
- **Operator modes (M2b)** (`eng_opx.c` `opx_mode_y`): a mode other than FM takes its oscillator unmodulated (FB and
  WIDTH are FM's only, MODE p.43), made a block at a time, and its input, the sum of the operators feeding it, sample by
  sample. RING MOD multiplies them (DEPTH the balance, SHAPE rectifies the input); WAVE FOLDER sums the input, the
  oscillator at OSC MIX and BIAS, multiplies by 1–8 (GAIN) and folds back into full scale; BYPASS passes the input
  as it is (without the operator's level or envelope); MUTE is silent. Both render orders call the same code, and
  `opx_test` checks them equal to the bit with every mode on every operator.
  - **FILTER / FILTER FM** (`opx_filt`): the cutoff follows the operator's pitch, CUTOFF semitones from it, at most
    0.45 fs. LPF HPF BPF BRF are a trapezoidal state-variable filter (12 dB/octave); MG LPF12/24, HPF12/24, BPF6/12 a
    ladder of four trapezoidal one-poles with feedback from the last, its outputs mixed for each response; MS-20 the
    state-variable filter with its resonance limited in the loop (a rough likeness). RESO goes up to
    self-oscillation. FILTER FM moves the cutoff ±4 octaves at a full-scale input. The coefficients come from tables
    made by the build (no floating point on the device).
  - **EFFECT** (`opx_fx`): PEAK EQ (a biquad, Q 1), SHELV EQ (first-order shelves at 250 Hz and 4 kHz), PHASER (four
    all-passes with feedback), SHORT DELAY and COMB (the voice's own delay line), DIST (hard clip, ×1–50), DRIVE
    (cubic soft clip, ×1–10), DEC (sample-and-hold at FREQ × the pitch, whole bits), WAVESHAPER (60 sine shapers of
    rising strength: the 01/W's tables are not published), PUNCH (a fast against a slow envelope lifts the attack).
    Memory: each voice has one delay line of 768 samples (784 before M4) after its part's six notes (`OPX_DLY`), so SHORT DELAY's
    TIME stops at 17 ms (the manual's 300 ms do not fit the FM-1's RAM), COMB reaches down to 58 Hz, and only one
    operator per voice gets the line (the first to run in SHORT DELAY or COMB; another plays dry).
- **KTRK (M2b)**: the level rises (+) or falls (−) with the distance from CENTER, LOW below it and HIGH above;
  LIN is 1 dB a semitone at 100 %, EXP the square of the distance over 48 semitones; −96 to +24 dB, and never past
  the operator's full scale. Worked out at note-on.
- **Self-feedback** uses the mean of the operator's last two outputs (as the DX7 does), and is linear in FB: 100 % gives 2 rad. At 75 % the harmonics come out within 3 dB of a sawtooth's, which matches the manual's description.

## How M4 works

- **EG1** is an envelope like the operators' (ATK DEC SUS REL CURVE), one a note, from 0 to 1. **P MOD EG1 INT** moves
  the operator's pitch by INT semitones at EG1's peak (±144.00).
- **LFO1** runs per part (KSYNC OFF and COMMON: one for the part, COMMON reset by the first note from no keys held) or
  per note (KSYNC VOICE), stepped once a 32-sample block. SPEED is 0.01–100 Hz (0.01 × 10000^(c/1023)), or with
  TEMPO on a note value at the song's tempo (MIDI clock's when it follows one). PHASE is where a reset starts it (RND:
  anywhere); FADE fades it in after the note-on (only with KSYNC on, as the manual says). The 23 waves are the manual's
  list (p.56): the bipolar ones −1 to +1, GTR and the "+" ones 0 to +1.
- **P MOD LFO1 INT** and **PROG PITCH LFO1 INT** move the pitch by INT semitones at the LFO's +1; **LMOD LFO1 INT**
  dips the operator's level, never above its own: at +100 % a bipolar LFO goes from full (its −1) to silent (its +1),
  a unipolar one from full (0) to silent (+1); −100 % turns the LFO over.
- **P MOD VEL** (RATIO only) adds to FINE (DEST FINE) or COARSE steps (DEST COARSE) by velocity.
- **PROG PITCH TRANS** and **OCT** move the note (not the FIXED operators).
- **CTRL** (each INT's): the source's value multiplies the INT (p.51). OFF keeps the INT as it is. MOD WHEEL,
  AFTER TOUCH and the PITCH WHEEL are the part's MIDI ones, PEDAL its channel's damper; NOTE, VEL, EXP VEL the note's;
  EG1 and LFO1 their values now; EG2, EG3, LFO2, LFO3 give 0 until M5 adds them.
- Not yet: KSYNC OFF should also reset the LFO when a program is chosen or the sequencer starts (p.57).

## The device pages (M1.5): where the code is

| Page | Code | Tests |
|---|---|---|
| ALGO (in place of EDIT 1 / 2 on OPX): the algorithm chart (1–40: FM6's layouts for 1–32, `OPX_CELL` for 33–40; square boxes, the left half), the USER matrix, the small scope | `ui_graph.c` `graph_opx_algo`, `graph_opx_chart`, `graph_opx_matrix`, `graph_scope_mini`; the matrix's knobs `ui_fm6op.c` `xmat_*` | `tests/ui_test.c` `test_opx_ops`; `tests/ui_render.c` `opx_*` screens and every chart |
| OPERATORS: the operator's rolling list (MODE, PITCH, EG, KTRK, USER, ALGO; MODE's rows by its mode), the chart or matrix in the left half | `ui_fm6op.c` `xop_*` (the rows: `xop_list`), `ui_graph.c` `graph_opxlist`, `graph_opx_left` | the same |
| Editing a field, OWN, undo | `opx_patch.c` `opx_edit`, `opx_poll`; `ui.c` (the undo snapshot keeps the patch and SLOT) | the same |

Notes for changing them:

- **The list's rows** are kinds (`XR_*`) that `xop_list` builds for the selected operator (its MODE section by its
  mode); `xop_rowf` maps a row to its field, `xop_find` a kind to its row. `TO OPn` is `opxf_ua(op, n - 1)`, the
  operator's own row its self feedback. E and F of EFFECT take the effect type's names, units and range (`OPX_FXV`);
  picking another FX sets them to its neutral values.
- **OUT** on the matrix and in the list is one control, `xop_out` / `xop_out_turn`: the operator's LEVEL while its
  OUT bit is set, OFF (the bit clear, LEVEL kept) below 0.
- **The scope's time scale** is two periods of the selected track's sounding pitch (its loudest voice, else the last
  note), one period below about 98 Hz, the buffer's 20 ms below about 49 Hz; triggered on a rising zero crossing.
- **USER always plays the matrix**; one with no heard operator is silent, as on the opsix.
- **ALG ATK DEC** (the opsix's HOME, on ALGO) apply on top of the patch at every block (`eng_opx.c` `opx_build`);
  automation and step locks reach them, the patch's own fields not. Felucca's FM6-style macros MLVL FB MRAT DTUN
  (E3..E6) are off on OPX (`OPX_FELUCCA_MACROS`): not read, not shown (`ui.c` `page_visible`), no MIDI CC. So are
  Felucca's FLT and SHP routes (ENV DEST, LFO DEST, the MOD matrix's CUT and SHP): `opx_render` ignores the voice's
  cutoff and shape, and their cards are DIM on an OPX track (`ui_draw.c` `opx_route_off`). PIT stays.

## What is stored

Each track keeps its patch in the bit-packed format of `tools/opx_patch_spec.py`: 434 bytes, every opsix field. The C layout is generated from that spec at build time (`build/gen/opx_fields.h`), and fields the engine doesn't play yet are kept through every edit and save. `tests/opx_test.c --dump` with `opx_patch_spec.py --check-c` checks that the firmware's packer and the spec agree bit for bit.

## Where it is stored

The four tracks' patches are saved beside the project, in its extension object (see [OPX_STORAGE.md](OPX_STORAGE.md)). The autosave gets its own extension. Each extension carries the CRC of the project record it belongs to:

- A load takes the patches only when the CRC matches the record it loaded.
- A missing, damaged or stale extension is ignored, and the track plays its SLOT's factory patch. An OWN track without its patch plays F1 and stays OWN.
- A rename re-links the extension.
- A project without OPX tracks writes no extension.

**Not yet covered:** user presets and the editor's backup archive. An OPX sound saved as a user preset, or restored from a backup, comes back with its SLOT's factory patch.

## Assumptions to check by ear

The manual gives no curve for these, so each is a reasonable guess:

- **LEVEL taper:** amplitude = (LEVEL / 100)³, i.e. −7.5 dB at 75 and −18 dB at 50.
- **Maximum modulation:** an operator at LEVEL 100 with a full envelope modulates by 4π rad (2 cycles), like the DX7 at output level 99.
- **CURVE:** 10 is an exponential approach that reaches 2⁻¹⁰ by the end of the stage; 1–9 blend linearly between that and LIN.
- **Velocity (LMOD VEL):** at 100 %, velocity 1 is silent and 127 is full, linear in between.
- **FB parameter:** every operator has its own FB, and the self-loops drawn in the charts are informational only.
- **The modes' depths:** WAVE FOLDER's GAIN ×1–8; FILTER FM ±4 octaves at a full-scale input; RING SHAPE as a blend
  towards the rectified input; BYPASS without the operator's level.
- **KTRK scale:** 100 % LIN = 1 dB a semitone; EXP the square of the distance over 48 semitones.
- **M4, LFO wave order:** the manual's list read down its columns; the opsix's knob order is not given.
- **M4, the LFO shapes:** the manual draws them without a formula: X. waves on CURVE 10's exponential, TRI4/TRI6/SAW4/
  SAW6 the shape held over 4 or 6 steps, the random ones a new level (or, TIME, a flip) a cycle of random length (×0.5–2).
- **M4, P MOD VEL:** at 100 % and velocity 127, FINE +1.000 (an octave on a ratio of 1) or 8 COARSE steps.
- **M4, CTRL:** an OFF entry (the manual's list has none; without it every INT would need a source); NOTE −1 to +1
  over C4 ± 64 notes (the manual: "the center note is 60"); EXP VEL velocity squared; REL VEL fixed at 64 (Felucca
  keeps no release velocity); PITCH WHL+ / WHL− 0 to +1 as the wheel goes up / down.
- **Filters and effects:** how the opsix builds them is not published: the types are standard designs with the
  manual's names and ranges (state-variable, ladder, biquad, shelves, all-passes); the WAVESHAPER's 60 tables are
  sine shapers, not the 01/W's; SHELV EQ's corners (250 Hz, 4 kHz) and PEAK EQ's Q (1) are chosen.

## Cost

On the host, six OPX voices cost about 1.8 times six FM6 voices (`opx_test` prints the ratio and the cost of the
heavier waves).

**On the device:** MENU > SYSTEM > INFO, OCT+ runs a benchmark (`firmware/src/bench.c`): with the transport stopped it
times the mix on the FM-1 itself (track 1 borrowed and put back, the audio silent for a few seconds) and lists the share
of real time six held notes take for FM6 and for OPX with each waveform, plus 32- and 64-bit multiply-adds. The audio
drops a voice above 85 % (`audio.c`).

The percentages are above the idle mix (six notes, all six operators on the wave; "one" is one note). Measured on a
device, 0.0.2-dev:

| | 2026-10-16: one loop, every wave inline | 2026-10-19: an operator at a time, tables | One note, now |
|---|---|---|---|
| Idle (the mix, effects, no voice) | 12.3 % | 12.4 % | |
| FM6, F1 | 26.8 % | 26.6 % | 4.4 % |
| OPX, SIN | 56.7 % | 38.4 % | 6.4 % |
| OPX, SIN 12BIT / 8BIT | 100.5 % | 40.6 % | 6.7 % |
| OPX, TRI / SAW / SAW HD | 87.2 / 101.0 / 135.2 % | 41.6 / 43.5 / 43.6 % | 6.9 / 7.2 % |
| OPX, SQR / SQR HD (two table reads) | 116.5 / 165.5 % | 53.3 / 53.4 % | 8.8 % |
| OPX, ADTV (all eleven) | 104.5–125.7 % | 41.8 % | 6.9 % |
| OPX, NOISE S/H / WHITE | 99.1 / 94.7 % | 39.5 / 37.0 % | 6.5 / 6.1 % |
| OPX, NOISE PINK / BLUE | 128.7 % | 57.6 / 57.2 % | 9.6 % |

So six OPX notes with every operator on any wave take at most ~70 % in all with the idle mix, under the 85 % where
the audio drops a voice. A patch with a loop back to a lower operator (below) still costs about the old SIN figure,
~57 %. M2b–M5 spend the rest.

**The modes** (2026-10-19, the mode on all six operators of six notes, SIN, the values typical; the third column the
cost of one operator in that mode over a sine operator, on six notes):

| Mode | Six notes | One operator in it |
|---|---|---|
| RING MOD | 56.5 % | +2.7 % |
| WAVE FOLDER | 58.3 % | +3.0 % |
| FX COMB | 81.6 % | +6.9 % |
| FX PEAK EQ | 99.5 % | +9.9 % |
| FILTER LPF (state-variable) | 132.9 % | +15.5 % |
| FX PHASER | 136.4 % | +16.1 % |
| FILTER MG24 (ladder) | 141.8 % | +17.0 % |
| FILTER FM | 185.8 % | +24.3 % |

A patch has room for about two operators in a filter mode; two in FILTER FM (~89 % with the idle mix) go over the
85 %. FILTER FM recomputes the filter's coefficients every sample (an exp2, a tangent table and a divide); the
state-variable filter uses 64-bit products (52.7 ns against 33.3 ns a multiply-add). Both are to be made cheaper
once the features are complete: coefficients every 2-4 samples for FILTER FM, 32-bit arithmetic for the filters.
The waves' rows rose ~2 points with M2b (SIN 38.4 to 40.1 %), probably the per-operator mode checks (not confirmed).

**What decided the cost**, found by elimination on the device (the benchmark's LOOP, BRANCH, CALL and LOAD rows):

- Not the code cache: the waveform loop's code shrank 98 % (tables instead of every wave inline) and the cost stayed.
- Not branches: one that goes the same way each time costs nothing; a random one ~26 ns.
- Not where the code runs: the same loops from RAM (`.ram_text`) cost the same as from flash.
- Not which memory: a 16-bit load costs the same from RAM, the pool, the stack or flash (~30 ns a step of a sum).
- What changed it: the order. The first renderer ran every operator each sample, keeping each operator's state in
  memory and reloading it; the compiled loop had ~60 instructions and ~30 loads and stores per operator and sample.
  Running one operator over the whole block (as FM6's `fm6_op_run` does) keeps its state in registers: the sine loop
  is ~25 instructions and 4 loads and stores a sample. Six notes of SIN: 57 % to 39 %; the waves: ~100 % to ~42 %.
- A patch where an operator takes a lower one's output (algorithms 4, 6, 34's loops with ALG FB; USER's upward links)
  needs the previous sample of an operator not yet run, so it keeps the sample-by-sample loop and its cost. `opx_test`
  checks that both orders give the same output to the bit.

**Floating point:** Felucca is compiled for the compiler's generic pi32v2 core, where `float` becomes a call to a
software routine that the firmware does not link. JieLi's SDK compiles for `-mcpu=r3`, the AC79's core, with an FPU.
`FELUCCA_CPU=r3` builds that way and adds a float row to the benchmark. Measured once (2026-10-16): the r3 build ran
everything within about 1 % of the generic build (FM6 27.0 %, OPX SIN 57.1 %, SQR HD 163.7 %); a 32-bit multiply-add
took 30.5 ns, a float one 27.7 ns. So r3 gains nothing for the integer code, and float is no faster than integer;
the DSP stays integer and CI no longer publishes an r3 build. Float in the audio code would also need the audio
interrupt (`hal/fm1_isr.S`) to save the FPU registers, which it does not.

FM6 runs six voices on the device, so OPX has the same six-voice cap (`OPX_POLY`); on the device, six held OPX sine
notes play without dropouts. The compiled render loops' cost is tracked by `tests/target_budget.py` (`opx_op_*`, `opx_loop_*`,
`opx_render` in `tests/target_budget.txt`).
M2–M5 add work per sample, so each of them needs the device checked again with six voices held.
