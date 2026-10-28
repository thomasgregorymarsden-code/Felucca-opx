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
| M2b | Operator modes RING, FILTER, FILTER FM, FOLD, EFFECT, BYPASS, MUTE; level key tracking | done (device cost: see [Cost](#cost); SHORT DELAY limited to 16.6 ms, see below; sound not yet compared with an opsix) |
| | **Second release, `0.1.0`: M2a and M2b; the VOICE pages in OPERATORS; SELECT turns pages with BPM LOCK ON** | |
| M3 | OPX patches in user presets and the editor backup; the web editor's OPX patch page | done: user presets (16), the backup, the storage repacked ([OPX_STORAGE.md](OPX_STORAGE.md)), the web editor's OPX tab |
| M4 | EG1 (pitch), LFO1 and their default routes: each operator's P MOD (EG1, LFO1, VEL), LMOD LFO1, PROG PITCH (LFO1, TRANS, OCT); the CTRL sources | done (pages EG1 and LFO1, the rows in OPERATORS, the web editor; not yet compared with an opsix) |
| M5a | EG2, EG3, LFO2, LFO3; the virtual patch (V.PATCH 1–12) to the destinations OPX has; every CTRL source live | done (pages EG1-3, LFO1-3, OPERATORS' V.PATCH section, the web editor; INT's scale a guess, see [Assumptions](#assumptions-to-check-by-ear)) |
| M5b | Main filter (EG2, LFO2, key tracking), as far as the processor time allows | done: one a voice (OPERATORS' FILTER section, the web editor; V.PATCH FILTER CUTOFF and RESO played; on the device +9.4 to +11.5 points on six notes, see [Cost](#cost)) |
| M5c | Effects 1–3 | part 1 done: the chain, the 13 types with no buffer (OPERATORS' FX1-3 section, the web editor); part 2 done (OPX Solo): the 6 short-buffer types; next: the delays and reverbs in Solo (Felucca's buffers; see [How M5c works](#how-m5c-works)) |

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
    TIME stops at 16.6 ms (the manual's 300 ms do not fit the FM-1's RAM), COMB reaches down to 58 Hz, and only one
    operator per voice gets the line (the first to run in SHORT DELAY or COMB; another plays dry).
- **KTRK (M2b)**: the level rises (+) or falls (−) with the distance from CENTER, LOW below it and HIGH above;
  LIN is 1 dB a semitone at 100 %, EXP the square of the distance over 48 semitones; −96 to +24 dB, and never past
  the operator's full scale. Worked out at note-on.
- **Self-feedback** uses the mean of the operator's last two outputs (as the DX7 does), and is linear in FB: 100 % gives 2 rad. At 75 % the harmonics come out within 3 dB of a sawtooth's, which matches the manual's description.

## How M4 works

- **EG1** is an envelope like the operators' (ATK DEC SUS REL CURVE), one a note, from 0 to 1. **P MOD EG1 INT** moves
  the operator's pitch by INT semitones at EG1's peak (±144.00).
- **LFO1** runs per part (KSYNC OFF and COMMON: one for the part, COMMON reset by the first note from no keys held) or
  per note (KSYNC VOICE), stepped once a 32-sample block. SPEED is 0.01–100 Hz (0.01 × 10000^(c/1023); both ends read on an opsix), or with
  TEMPO on a note value at the song's tempo (MIDI clock's when it follows one). PHASE is where a reset starts it (RND:
  anywhere); FADE (0–9.900 s, both ends read on an opsix) fades it in after the note-on (only with KSYNC on, as the manual says).
  The opsix's SPEED knob is not a log scale (6.00 Hz at its centre, where OPX's code puts 1.00 Hz); OPX keeps its log
  code, since a stored speed plays the same either way and only the knob travel differs. The 23 waves are the manual's
  list (p.56): the bipolar ones −1 to +1, GTR and the "+" ones 0 to +1.
- **P MOD LFO1 INT** and **PROG PITCH LFO1 INT** move the pitch by INT semitones at the LFO's +1; **LMOD LFO1 INT**
  dips the operator's level, never above its own: at +100 % a bipolar LFO goes from full (its −1) to silent (its +1),
  a unipolar one from full (0) to silent (+1); −100 % turns the LFO over.
- **P MOD VEL** (RATIO only), measured on an opsix: INT % × velocity/127 × 10. DEST FINE adds it to the final ratio,
  COARSE × FINE (100 % at velocity 127: ratio 1 → 11, COARSE 2 → 12; at 64: 1 → 6.04; 50 % at 64 → 3.52). DEST COARSE
  adds it, to the nearest whole number, to COARSE before FINE multiplies (COARSE 2, FINE 0.50, 55 % at 127:
  (2 + 6) × 0.5 = 4); the steps fall at 5, 15 … 95 % at velocity 127.
- **PROG PITCH TRANS** and **OCT** move the note (not the FIXED operators).
- **CTRL** (each INT's): the source's value multiplies the INT (p.51). OFF keeps the INT as it is. MOD WHEEL,
  AFTER TOUCH and the PITCH WHEEL are the part's MIDI ones, PEDAL its channel's damper; NOTE, VEL, EXP VEL the note's;
  EG1..3 and LFO1..3 their values now (M5).
- Not yet: KSYNC OFF should also reset the LFO when a program is chosen or the sequencer starts (p.57).
- Each EG and LFO runs only while something takes it (an INT that is on, a CTRL or a V.PATCH source), and the per-operator
  modulation arithmetic only for an operator with a route on: a patch without modulation costs what it did before M4.

## How M5a works

- **EG2, EG3, LFO2, LFO3** are EG1's and LFO1's kind (the same rows; a note's or the part's as KSYNC says). Until M5b's
  filter they reach the sound through a CTRL (EG2 as the CTRL of an EG1 INT, say) or a virtual patch.
- **V.PATCH 1–12** (p.71): SRC × CTRL × INT moves DEST, per voice, a block at a time. The sources (p.88): the CTRL list's
  wheels, pedal, NOTE, VEL, EXP VEL, REL VEL (64); EG1..3 (0 to +1) and LFO1..3; CC+ (0 to +1 over 0..127) and CC+/−
  (−1 to +1 about 64) of CC 1..119 as last received on the part's channel (none yet: 0); AFTER TOUCH the part's; POLY
  AFTER 0 (Felucca keeps no poly aftertouch). The sources are read as the last block left them (one block, 0.7 ms, late),
  so the EGs' and LFOs' own values can move their own times.
- **Destinations played:** PROG PITCH, OCT, TRANS, LEVEL, ATK, DECAY, REL, SUS; each operator's PITCH, COARSE, OCT,
  TRANS, LEVEL, ATK, DECAY, REL, SUS, FM FB; EG1..3's ATK, DECAY, REL, SUS; LFO1..3's SPEED (a KSYNC VOICE LFO's only:
  the part's LFO is one for all the notes), PHASE (not the random waves) and FADE. The rest of the list (the modes'
  values, the effects, ARP, SEQ, PROG PAN TEMPO ALGO ALGO FB GLIDE UNISON, the LFOs' WAVE) is kept in the
  patch, shown dimmed on the device and marked * in the web editor, and does nothing yet.
- **INT's scale** is a guess (the manual: "depends on destination"): INT 100.00 with the source at full moves the
  destination over its whole range: PITCH 100 semitones (so INT reads in semitones, as P MOD's), OCT 4 octaves, TRANS
  24 semitones, COARSE 38 steps, LEVEL SUS FB 100, a stage's time the 1023 codes of 0 ms – 90 s, SPEED 0.01 – 100 Hz,
  PHASE 360°, FADE 9.9 s, PROG LEVEL ×0 – ×2. To be read off an opsix (INT's display for a few destinations).
- **Memory:** the three EGs and LFOs took 72 bytes a note, so a voice's delay line (SHORT DELAY, COMB) is 736 samples
  (16.6 ms; was 768).

## How M5b works

- **One filter a voice** (the opsix's per-voice filter, p.58), after the operators' mix and before the effects: TYPE
  (the 11 of p.38), ENABLE, CUTOFF, RESO, EG2 INT and its CTRL, KTRK LOW / CENTER / HIGH, LFO2 INT and its CTRL. An
  LPF wide open (CUTOFF code 1000 and up, ~19 kHz and above) with RESO 0 and nothing moving it is skipped, so the INIT (LPF 12 ON at
  23.68 kHz) costs nothing.
- **Two structures:** the state-variable filter (LPF 12, HPF 12, BPF 12, BRF 12; LPF / HPF MS-20 the same with the
  resonance limited inside the loop, a soft-saturating stand-in for the MS-20's) and the four-pole ladder (LPF 24,
  LPF POLY6, HPF 24, BPF 6, BRF 6), each type an output mix of the structure's points. The voice goes in at half scale
  and comes out at full, so several carriers at full level stay inside its range.
- **The coefficients** are computed once a block (32 samples, 0.7 ms): CUTOFF + EG2 × INT + LFO2 × INT + key
  tracking + V.PATCH, in octaves, then one exp2 for the frequency. EG2 and LFO2 INT are in semitones (±142: the whole
  range); CUTOFF runs 6.49 Hz – 23.68 kHz over its 1023 codes (the opsix's display ends).
- **KTRK:** LOW below CENTER, HIGH above it, each a slope c / (128 − |c|) semitones a semitone: 64 is 1.00 (the cutoff
  follows the key), ±127 ±INF (the end of the range at once). The opsix shows these as numbers without a unit; the
  mapping is a guess.
- **V.PATCH** FILTER CUTOFF (INT 100.00: the whole range) and FILTER RESO (INT 100.00: 100 %) are now played.
- **Memory:** the filter's state (five words a note) took the room, so a voice's delay line (SHORT DELAY, COMB) is 728 samples
  (16.5 ms; was 736).
- **Cost:** the loop is the same kind as an operator's FILTER mode (coefficients once a block, not a sample at a time
  as FILTER FM's), so on the device expect roughly what one operator in FILTER mode adds, +16 to +17 points on six
  notes. Not measured yet; if it is too much, a cheaper filter (32-bit arithmetic, or one shared by the part) is the
  fallback. The benchmark has three rows for it (MAIN LPF12, LPF24, MS-20; SIN, EG2 moving it).

## How M5c works

- **Where:** after a part's voices (the engine's `post`, called by voice.c track_render), FX1 → FX2 → FX3 on the
  part's block, before Felucca's mix (level, pan, sends). One chain a part (the opsix's is the program's).
- **Stereo:** a part is mono (or, with SPREAD, two sides). The chain runs on one channel until an effect makes two
  (PAN), then on both; a part made stereo goes to the mix at full width through SPREAD's path (fx.c mix_spread).
- **The types** (tools/opx_patch_spec.py FX_TYPES): the manual's 30 with their four knobs, ranges and units. Each
  knob's field holds the value less the knob's lowest. Names fit the device's card: some are shortened (the BPM
  types without the space, CHAMBR, W.ROOM, TW 1X12 …). The defaults when a type is picked are chosen, not read.
- **Played now (kind 0, no buffer):** PHA, PHABPM, PAN, PANBPM, A.WAH, EXC, FLTR, 3EQ, DIST, G.AMP, DEC, LIMIT, COMP;
  the designs and their assumptions are listed at the top of firmware/src/opx_fx.c (first-order shelves at 250 Hz
  and 4 kHz for 3EQ, as the operators' SHELV EQ; G.AMP's 16 types as gains and cabinet corners, not models; ...).
- **Played in OPX Solo only (kind 1, a short buffer):** CHO, U.ENS, FLNG, FLNBPM, RTRY, ENH. Each FX slot has a
  1024-sample (21 ms) int16 line per channel in the pool (3 slots × 2 × 2 KB = 12 KB), read with linear
  interpolation; cleared when the slot's type changes. Designs (not the opsix's, which are not published):
  - CHO: an 8 ms line swept ±DEPTH·4 ms by a sine at RATE; the right side's sweep PHASE apart. Makes two channels.
  - U.ENS: VOICE taps (1–4) spread 6–14 ms, each swept ±2 ms at RATE (0.2–6 Hz) with its own phase, alternate
    taps left and right; the taps' sum scaled by 1/√n.
  - FLNG, FLNBPM: 0.2 ms + DEPTH·5 ms, a triangle sweep, FEEDBACK ±90 %; per channel, the channel count kept.
  - RTRY: an 800 Hz crossover; the horn a Doppler line (1.5 ± 0.4 ms) with amplitude modulation, the drum
    amplitude modulation alone; SLOW 0.83 / 0.67 Hz, FAST 6.75 / 5.8 Hz (horn / drum), the horn reaching the new
    speed in ~0.7 s, the drum in ~3 s; SPKR as tone and mic-spread variants. Makes two channels.
  - ENH: the part above 300 Hz delayed by DELAY·20 ms, added to the left and taken from the right (×DEPTH·SPREAD).
  Tests: tests/opx_test.c test_m5c2 (two channels apart where expected, levels within 6 dB of dry, RTRY FAST's
  level swing). Not yet measured on the device: their CPU cost.
- **Not yet (kept, the sound passed; NOT AVAILABLE in both firmwares):** kind 2 (experimental), long buffers: DLY,
  DLYBPM, PDLY, PDLBPM, TDLY, TDLBPM, REF, REVERB, SHVERB, SPRING, GRAIN. In OPX, kind 1 is NOT AVAILABLE too.
- **Memory** (the M5b build): the pool had 10.6 KB free with 8 KB kept spare by build.py, RAM 8.5 KB with 4 KB kept,
  flash 21.6 KB. The chain's state takes 1.3 KB of the pool (12 slots). Kind 1 needs its buffers from a part's
  engine memory (the voices' SHORT DELAY lines would shrink); kind 2 more than the FM-1 has free: the opsix's
  delays reach 2.73 s stereo (~480 KB). The way open: borrow Felucca's own delay (128 KB, 1.49 s mono) and reverb
  buffers, one part at a time, silencing the bus it borrows.
- **Decided (2026-10-26): two builds.** OPX (this firmware: four parts, every engine) keeps the 13; the
  other 17 read NOT AVAILABLE and keep their knobs. An OPX-only firmware (one part, Felucca's other engines, effects
  and sequencer left out) plays all 30 (OPX_FX_ALL); a patch moves between the two unchanged. Its base: this tree,
  as a build configuration (FELUCCA_OPX_ONLY=1, "OPX Solo", opx-solo); a move to the fm1-base platform (on felucca-bsp) later,
  once its interface settles.

## The OPX-only firmware (FELUCCA_OPX_ONLY=1)

- **Built by** `FELUCCA_OPX_ONLY=1 ./build.sh`: `build/opx-solo.fwsc`, version `v…-opx-solo`. CI builds both
  firmwares; the OPX-only one's installer page is the site's `/dev/opx-solo/` (a release's `/opx-solo/`).
- **One part:** track 1 plays OPX; tracks 2–4 and every other engine are a silent reserved row (opx_map.h,
  engines.c eng_part). The other engines' code is then referenced from nowhere that runs, so the compiler drops
  most of it; the stores and the protocol keep four tracks and the engine numbers, so projects and patches load in
  either firmware (another engine, or OPX on tracks 2–4: silent here). PRESETS and the editor offer OPX alone;
  track 2–4 cannot be selected. The parts' engine memory: track 1's region alone (three of 12.9 KB freed).
- **Not yet stripped:** the sequencer, Felucca's send effects (chorus, delay, reverb buses), the other engines' UI
  pages and their data (the FM6 bank, SAMPLE's sets, SLICE). Next as the remaining effects need the room: Felucca's
  delay and reverb buffers (~144 KB) go to OPX's delays and reverbs.

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
- **M4, the LFO shapes:** the manual draws them without a formula: X. waves on CURVE 10's exponential, TRI4/TRI6/SAW4/
  SAW6 the shape held over 4 or 6 steps, the random ones a new level (or, TIME, a flip) a cycle of random length (×0.5–2).
- **M4, P MOD VEL, DEST FINE:** measured with FINE 1.00 only: on the final ratio, or on COARSE before FINE as DEST COARSE,
  is not yet told apart (COARSE 2, FINE 0.50, 55 % at 127: 6.5 or 3.75). Assumed the final ratio. DEST COARSE's whole
  steps below COARSE 1 are assumed whole numbers added (1/2 + 1 = 1.5).
- **M4, CTRL:** (OFF first, as on an opsix, though the manual's list has none) NOTE −1 to +1
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

| | 2026-10-16: one loop, every wave inline | 2026-10-19: an operator at a time, tables | 2026-10-25: M4 (c31ae67) | One note, M4 |
|---|---|---|---|---|
| Idle (the mix, effects, no voice) | 12.3 % | 12.4 % | 12.3 % | |
| FM6, F1 | 26.8 % | 26.6 % | 26.7 % | 4.4 % |
| OPX, SIN | 56.7 % | 38.4 % | 43.4 % | 7.2 % |
| OPX, SIN 12BIT / 8BIT | 100.5 % | 40.6 % | 46.6 % | 7.7 % |
| OPX, TRI / SAW / SAW HD | 87.2 / 101.0 / 135.2 % | 41.6 / 43.5 / 43.6 % | 47.6 / 49.4 / 49.2 % | 7.9 / 8.2 % |
| OPX, SQR / SQR HD (two table reads) | 116.5 / 165.5 % | 53.3 / 53.4 % | 58.4 / 58.5 % | 9.7 % |
| OPX, ADTV (all eleven) | 104.5–125.7 % | 41.8 % | 47.8 % | 7.9 % |
| OPX, NOISE S/H / WHITE | 99.1 / 94.7 % | 39.5 / 37.0 % | 44.8 / 42.3 % | 7.4 / 7.0 % |
| OPX, NOISE PINK / BLUE | 128.7 % | 57.6 / 57.2 % | 63.0 / 62.4 % | 10.5 % |

So six OPX notes with every operator on any wave take at most ~75 % in all with the idle mix (NOISE PINK: 12.3 +
63.0 %), under the 85 % where the audio drops a voice. M4 (EG1, LFO1, the P MOD / LMOD / CTRL work each block) added
about 3.3 points to every row over M2b (SIN 40.1 to 43.4 %) with no modulation in use: work done whether or not a
route is on. Since then the render skips it when no route needs it: EG1 runs only while an EG1 INT (or a CTRL set to
EG1 on an INT that is on) takes it, and an operator with no P MOD / LMOD INT on (and PROG PITCH LFO1 at 0) skips its
modulation arithmetic (on the host: 1.3 % fewer instructions in the render; on the device not yet measured). EG1
switched on during a held note starts from the note's start, not where it would have been. A patch with a loop back to a lower operator (below) still costs about the old SIN figure,
~57 %. M2b–M5 spend the rest.

**2026-10-28, M5c, OPX (four parts):** idle 12.4 %, FM6 F1 26.8 % (one note 4.4 %). OPX: SIN 47.9 % (one
note 7.9 %), SIN 12BIT / 8BIT 50.8 %, TRI 51.8 %, SAW / SAW HD 53.7 / 53.4 %, SQR / SQR HD 63.5 % (10.5 %), ADTV
52.0 %, NOISE S/H / WHITE 49.2 / 47.8 %, PINK / BLUE 68.3 / 67.4 %. Every row ~4.5 points over M4 (SIN 43.4 to
47.9 %): EG2/3, LFO2/3, V.PATCH and the main filter's checks, with nothing of them in use. Modes (all six operators):
RING 66.5 %, WAVE FOLDER 68.3 %, FILTER LPF 143.9 %, FILTER MG24 154.9 %, FILTER FM 195.0 %, EFFECT PEAK EQ 105.7 %,
PHASER 147.5 %, COMB 87.5 %: one operator in a filter mode ~+16 points (MG ~+18, FILTER FM ~+25), PHASER ~+17, PEAK EQ
~+10, COMB ~+7, RING / FOLD ~+3. **The main filter** (SIN): LPF12 57.3 %, LPF24 59.4 %, MS-20 57.4 % (one note
9.5–9.9 %): +9.4 to +11.5 points on six notes, so one a voice fits: 12.4 + 59.4 = 72 % with every voice filtered.
Room for a patch on six notes: ~72 points (85 less the idle mix). From these figures the factory bank's heaviest
poly patches were ~80 (AIR PAD: two operators in FILTER) and ~74 (CHOIR); both now use RING / FM operators instead
(~64, ~58). VOX LEAD (four FILTER operators, ~114 on six notes) is MONO: one voice, ~19.

**2026-10-28, M5c, OPX Solo:** idle 14.9 % (track 1 on OPX E.PIANO: its PAN and 3EQ run on the silent part
every block; OPX's idle row has FM6 there), OPX SIN 45.5 % (one note 7.5 %), TRI 49.7 %, SAW 51.6 %, SQR /
SQR HD 60.5 / 60.6 %, ADTV 49.9 %, NOISE S/H / WHITE / PINK / BLUE 46.7 / 44.3 / 65.0 / 64.5 %; RING 62.5 %, FOLD
66.1 %, FILTER LPF 141.2 %, MG24 150.5 %, FILTER FM 192.8 %, PEAK EQ 102.9 %, PHASER 146.3 %, COMB 85.5 %; MAIN LPF12 /
LPF24 / MS-20 55.0 / 57.0 / 55.0 % (one note 9.1–9.5 %). The voices ~2.4 points cheaper than in OPX, the idle
2.5 points dearer: ~70 points for six notes of a patch. The hats moved their high-pass from an operator to the main
filter (~6 points less); every other poly factory patch estimates under ~65.

**The modes** (the mode on all six operators of six notes, SIN, the values typical; "one operator" the cost of one
operator in that mode over a sine operator, on six notes):

| Mode | Six notes, 2026-10-19 | One operator | Six notes, 2026-10-25 (M4) | One operator |
|---|---|---|---|---|
| RING MOD | 56.5 % | +2.7 % | 60.5 % | +2.9 % |
| WAVE FOLDER | 58.3 % | +3.0 % | 64.0 % | +3.4 % |
| FX COMB | 81.6 % | +6.9 % | 83.3 % | +6.7 % |
| FX PEAK EQ | 99.5 % | +9.9 % | 101.1 % | +9.6 % |
| FILTER LPF (state-variable) | 132.9 % | +15.5 % | 139.1 % | +16.0 % |
| FX PHASER | 136.4 % | +16.1 % | 144.7 % | +16.9 % |
| FILTER MG24 (ladder) | 141.8 % | +17.0 % | 146.6 % | +17.2 % |
| FILTER FM | 185.8 % | +24.3 % | 191.2 % | +24.6 % |

With M4 a patch on six notes has room for one operator in a filter mode (FILTER FM: 12.3 + 43.4 + 24.6 = 80.3 %);
two (LPF ~88 %, FILTER FM ~105 %) go over the 85 %. Before M4 two LPF operators fitted (~84 %). FILTER FM recomputes the filter's coefficients every sample (an exp2, a tangent table and a divide); the
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
