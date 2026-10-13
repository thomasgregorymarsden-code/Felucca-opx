# OPX: plan and status

OPX is an altered-FM engine modelled on the Korg opsix, built into Felucca as engine 15. The flash layout and engine number are covered in [OPX_STORAGE.md](OPX_STORAGE.md), and the bit-packed patch format in `tools/opx_patch_spec.py`.

## Milestones

| | Scope | Status |
|---|---|---|
| M0 | Engine slot 15 (14 reserved), presets, the tables every engine appears in, relocatable storage region | done |
| M1 | Six operators in FM mode (sine), all 40 algorithms plus USER, ratio and fixed pitch, per-operator FB, ALG FB loops, per-operator ATK/DEC/SUS/REL/CURVE envelopes, level velocity; device macros; cost against FM6 | **done (on-device listening and cost pending)** |
| M1.5 | Operator editing on the device; each track's patch kept in the stored format; saved with projects and the autosave (pulled forward from M3/M4) | **done** |
| M2 | Operator modes RING, FILTER, FILTER FM, FOLD, EFFECT, BYPASS, MUTE; the 23 waveforms and WIDTH; level key tracking | |
| M3 | OPX patches in user presets and the editor backup; the web editor's OPX patch page | |
| M4 | EG1 (pitch), EG2 (filter), EG3, LFO1–3, pitch modulation | |
| M5 | Main filter, effects 1–3, virtual patch, as far as the processor time allows | |

## How M1 works

- **One renderer for every algorithm.** Each algorithm becomes a modulation matrix: for every pair of operators, how much one modulates the other, plus each operator's self-feedback and which operators are heard.
  - The 40 presets are the manual's charts (p.83) at 100 % per connection: 1–32 are the DX7's algorithms, 33–40 the opsix's own.
  - USER is the patch's own matrix.
  - `tests/opx_test.c` checks every chart against a table written separately from the engine, and renders every algorithm against a floating-point reference implementation. They agree to better than −45 dB.
- **Order and timing.** Each sample, the operators run from 6 down to 1. An input from a higher-numbered operator is this sample's output. An input from a lower-numbered one, as in the loops of algorithms 4, 6 and 34 or an upward link in USER, is the previous sample's.
- **Self-feedback** uses the mean of the operator's last two outputs (as the DX7 does), and is linear in FB: 100 % gives 2 rad. At 75 % the harmonics come out within 3 dB of a sawtooth's, which matches the manual's description.

## Editing on the device (M1.5)

**EDIT 1 (ALGO) and EDIT 2** show the algorithm in the left two-thirds of the panel and a small oscilloscope in the right third.

- **A preset algorithm** is drawn as a chart in FM6's style. Algorithms 1–32 use FM6's layouts; the test suite checks that OPX's 1–32 route exactly as FM6's. 33–40 have layouts of their own. The ALG FB loop (algorithms 4, 6, 34) is drawn as a return path.
- **USER** (ALG USR, or PAT on a patch whose algorithm is USER) is drawn as the opsix's FM MATRIX:
  - a column per sending operator, a row per receiving operator, and a dot sized by the amount;
  - the diagonal is self feedback;
  - the bottom row is OUT.
- **On EDIT 1 with USER**, knob 1 is still ALG; knob 2 picks the row, knob 3 the column, and knob 4 sets the amount (0–100 %; OUT toggles). ATK, DEC and MLVL return when a preset algorithm is chosen.
- **EDIT 2** shows the same matrix with nothing selected; its knobs keep their usual jobs.
- **USER always plays the matrix.** One with no heard operator is silent, as on the opsix; it no longer falls back to the patch's preset algorithm.

On an OPX track, the page after EDIT 2 is **OPERATORS**: a scrolling list over the track's own patch, styled like AUTOMATION's (7 rows on screen, the selected row highlighted, value on the right).

| Knob | Selects |
|---|---|
| 1 | the section (PAGE): PITCH, EG, OSC, USER, ALGO |
| 2 | the operator (1–6; ALGO has none) |
| 3 | the row; each section remembers its own |
| 4 | the row's value |

| Section | Rows (for the selected operator) |
|---|---|
| PITCH | MODE (RATIO / FIXED), RATIO (1/128–32) or FREQ in FIXED mode (a semitone per detent), FINE (×0.50–×2.00; in FIXED mode a cent per detent, shown to 0.01 Hz), DTUN (cents), TRANS (semitones) |
| EG | LEVEL, ATK, DEC, SUS, REL, CURVE (LIN, 1–9, EXP), VEL |
| OSC | FB (M2 adds MODE, WAVE and the mode settings here) |
| USER | TO OP1–TO OP6 (a fixed list; the selected operator's own row reads "(SELF)" and is its self feedback), OUT (heard). These rows are dimmed unless the track plays ALG USR. |
| ALGO | ALG (the patch's algorithm, 1–40 or USER), ALG FB |

- **Editing makes the patch OWN.** SLOT then shows OWN, and SLOT F1–F7 loads a factory patch over it.
- **Undo:** holding SAVE undoes the last knob gesture, patch and SLOT together; holding it again redoes.
- **The macros on EDIT 1/2** still apply on top of the patch.

### What is stored

Each track keeps its patch in the bit-packed format of `tools/opx_patch_spec.py`: 434 bytes, every opsix field. The C layout is generated from that spec at build time (`build/gen/opx_fields.h`), and fields the engine doesn't play yet are kept through every edit and save. `tests/opx_test.c --dump` with `opx_patch_spec.py --check-c` checks that the firmware's packer and the spec agree bit for bit.

### Where it is stored

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

On the host, six OPX voices cost about 1.5–1.7 times six FM6 voices (`opx_test`). FM6 runs six voices on the device, so OPX starts with the same six-voice cap (`OPX_POLY`). The device itself is the real measurement: if notes drop or the audio glitches with many keys held, the cap or the inner loop needs work. The cost of the compiled render loop is tracked by `tests/target_budget.py`.
