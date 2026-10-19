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
| M2b | Operator modes RING, FILTER, FILTER FM, FOLD, EFFECT, BYPASS, MUTE; level key tracking | |
| M3 | OPX patches in user presets and the editor backup; the web editor's OPX patch page | |
| M4 | EG1 (pitch), EG2 (filter), EG3, LFO1–3, pitch modulation | |
| M5 | Main filter, effects 1–3, virtual patch, as far as the processor time allows | |

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
- **Self-feedback** uses the mean of the operator's last two outputs (as the DX7 does), and is linear in FB: 100 % gives 2 rad. At 75 % the harmonics come out within 3 dB of a sawtooth's, which matches the manual's description.

## The device pages (M1.5): where the code is

| Page | Code | Tests |
|---|---|---|
| EDIT 1 / EDIT 2: the algorithm chart (1–40: FM6's layouts for 1–32, `OPX_CELL` for 33–40), the USER matrix, the small scope | `ui_graph.c` `graph_opx_algo`, `graph_opx_chart`, `graph_opx_matrix`, `graph_scope_mini`; the matrix's knobs `ui_fm6op.c` `xmat_*` | `tests/ui_test.c` `test_opx_ops`; `tests/ui_render.c` `opx_*` screens and every chart |
| OPERATORS: one 24-row rolling list (PITCH, EG, OSC, USER, ALGO) | `ui_fm6op.c` `xop_*` (the rows: `XL[]`), `ui_graph.c` `graph_opxlist` | the same |
| Editing a field, OWN, undo | `opx_patch.c` `opx_edit`, `opx_poll`; `ui.c` (the undo snapshot keeps the patch and SLOT) | the same |

Notes for changing them:

- **The list's rows** are kinds (`XR_*`) in `XL[]`; `xop_rowf` maps a row to its field for the selected operator. The
  USER rows start at `XL_TO0`; `TO OPn` is `opxf_ua(op, n - 1)`, the operator's own row its self feedback.
- **OUT** on the matrix and in the list is one control, `xop_out` / `xop_out_turn`: the operator's LEVEL while its
  OUT bit is set, OFF (the bit clear, LEVEL kept) below 0.
- **The scope's time scale** is two periods of the selected track's sounding pitch (its loudest voice, else the last
  note), one period below about 98 Hz, the buffer's 20 ms below about 49 Hz; triggered on a rising zero crossing.
- **USER always plays the matrix**; one with no heard operator is silent, as on the opsix.
- **The macros** (EDIT 1 / 2: ALG ATK DEC MLVL, FB MRAT DTUN SLOT) apply on top of the patch at every block
  (`eng_opx.c` `opx_build`). They are the OPX controls that automation, step locks and MIDI CC reach (CC74 MLVL,
  CC71 FB: `midi_control.c` `MIDI_CC_ENG`); the patch's own fields are not.

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
