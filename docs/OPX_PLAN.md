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
| M2a | The 23 waveforms and WIDTH | done (device cost to be measured) |
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
- **Waveforms (M2a)** replace the operator's sine (`eng_opx.c` `opx_osc`); a plain sine with WIDTH OFF takes
  M1's path unchanged, sample for sample. SAW and SQR use a polyBLEP, the HD ones a cubic B-spline BLEP over four
  samples (about 9 dB less aliasing at 2.2 kHz, 20–60 dB less lower in the band, `opx_test`); the ADTV ones are sums
  of sines scaled to a full-scale peak; the noises keep a state per operator per voice. Assumptions: WIDTH squeezes
  the cycle into its first WIDTH % and is silent after (from the manual's pictures); HD means a cleaner high end
  (the manual says "richer"); BLUE is pink noise's difference (+9 dB over 4 octaves, short of +12 near Nyquist).
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

On the host, six OPX voices cost about 1.8 times six FM6 voices (`opx_test` prints the ratio). With waveforms on
all six operators (M2a) the host measures: SAW 2.0, SAW HD 2.5, SQR HD 3.2, ADTV 12345 4.1, NOISE PINK 2.7 times FM6.
Whether the heavy ones fit six voices on the device is not yet measured: MENU > SYSTEM > INFO shows the CPU load. FM6 runs six voices on
the device, so OPX has the same six-voice cap (`OPX_POLY`); on the device, six held OPX notes play without dropouts.
The compiled render loop's cost is tracked by `tests/target_budget.py` (`opx_render` in `tests/target_budget.txt`).
M2–M5 add work per sample, so each of them needs the device checked again with six voices held.
