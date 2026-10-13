# OPX: plan and status

OPX is an altered-FM engine modelled on the Korg opsix, built into Felucca as engine 15. The flash layout and engine number are covered in [OPX_STORAGE.md](OPX_STORAGE.md), and the bit-packed patch format in `tools/opx_patch_spec.py`.

## Milestones

| | Scope | Status |
|---|---|---|
| M0 | Engine slot 15 (14 reserved), presets, the tables every engine appears in, relocatable storage region | done |
| M1 | Six operators in FM mode (sine), all 40 algorithms plus USER, ratio and fixed pitch, per-operator FB, ALG FB loops, per-operator ATK/DEC/SUS/REL/CURVE envelopes, level velocity; device macros; cost against FM6 | **done (on-device listening and cost pending)** |
| M2 | Operator modes RING, FILTER, FILTER FM, FOLD, EFFECT, BYPASS, MUTE; the 23 waveforms and WIDTH; level key tracking | |
| M3 | The full patch stored per project (extension objects) and in user presets; editor backup; the web editor's OPX patch page | |
| M4 | EG1 (pitch), EG2 (filter), EG3, LFO1–3, pitch modulation | |
| M5 | Main filter, effects 1–3, virtual patch, as far as the processor time allows | |

## How M1 works

- **One renderer for every algorithm.** Each algorithm becomes a modulation matrix: for every pair of operators, how much one modulates the other, plus each operator's self-feedback and which operators are heard.
  - The 40 presets are the manual's charts (p.83) at 100 % per connection: 1–32 are the DX7's algorithms, 33–40 the opsix's own.
  - USER is the patch's own matrix.
  - `tests/opx_test.c` checks every chart against a table written separately from the engine, and renders every algorithm against a floating-point reference implementation. They agree to better than −45 dB.
- **Order and timing.** Each sample, the operators run from 6 down to 1. An input from a higher-numbered operator is this sample's output. An input from a lower-numbered one, as in the loops of algorithms 4, 6 and 34 or an upward link in USER, is the previous sample's.
- **Self-feedback** uses the mean of the operator's last two outputs (as the DX7 does), and is linear in FB: 100 % gives 2 rad. At 75 % the harmonics come out within 3 dB of a sawtooth's, which matches the manual's description.

## Assumptions to check by ear

The manual gives no curve for these, so each is a reasonable guess:

- **LEVEL taper:** amplitude = (LEVEL / 100)³, i.e. −7.5 dB at 75 and −18 dB at 50.
- **Maximum modulation:** an operator at LEVEL 100 with a full envelope modulates by 4π rad (2 cycles), like the DX7 at output level 99.
- **CURVE:** 10 is an exponential approach that reaches 2⁻¹⁰ by the end of the stage; 1–9 blend linearly between that and LIN.
- **Velocity (LMOD VEL):** at 100 %, velocity 1 is silent and 127 is full, linear in between.
- **FB parameter:** every operator has its own FB, and the self-loops drawn in the charts are informational only.

## Cost

On the host, six OPX voices cost about 1.5–1.7 times six FM6 voices (`opx_test`). FM6 runs six voices on the device, so OPX starts with the same six-voice cap (`OPX_POLY`). The device itself is the real measurement: if notes drop or the audio glitches with many keys held, the cap or the inner loop needs work. The cost of the compiled render loop is tracked by `tests/target_budget.py`.
