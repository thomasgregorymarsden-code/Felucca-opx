# Changelog

Felucca-OPX's releases. Felucca's own release notes, up to the 1.5.1 this fork is based on, are in
[docs/FELUCCA_HISTORY.md](docs/FELUCCA_HISTORY.md).

## Unreleased

- **OPX operator modes:** RING MOD, FILTER (12 types: LPF HPF BPF BRF, MG LPF12/24 HPF12/24 BPF6/12, MS-20 LPF/HPF),
  FILTER FM, WAVE FOLDER, EFFECT (PEAK EQ, SHELV EQ, PHASER, SHORT DELAY, COMB, DIST, DRIVE, DEC, WAVESHAPER, PUNCH),
  BYPASS and MUTE beside FM. SHORT DELAY stops at 17 ms (memory), one operator per voice.
- **OPX KTRK:** each operator's level follows the key: LOW and HIGH slopes about CENTER, LIN or EXP.
- **OPX pages:** ALGO replaces EDIT 1 / 2 on an OPX track: the opsix's HOME (ALG, ATK, DEC) and SLOT. Felucca's macros
  MLVL, FB, MRAT and DTUN are off on OPX (not part of the opsix), and so are Felucca's FLT and SHP modulation routes
  (ENV DEST, LFO DEST, the MOD matrix's CUT and SHP; their cards dimmed on OPX). PIT works as before. The algorithm chart has square boxes and sits in the
  left half; OPERATORS shows it beside the list (right half), the selected operator highlighted. The list follows the
  opsix's pages: MODE (the mode's own rows), PITCH, EG, KTRK, USER, ALGO.
- **OPX VOICE section:** on an OPX track, VOICE, VOICE 2 and VOICE 3 are the last section of OPERATORS (VOICE, GLIDE,
  GLIDE MODE, PRIORITY, ALLOC, DETUNE, PAN, MUTE, SPREAD); EDIT goes ALGO, OPERATORS. Other engines keep the pages.
- **SELECT turns pages with BPM LOCK ON:** SELECT alone turns the page either way, round the family shown (HOME:
  HOME, MIXER, CLOCK); with BPM LOCK OFF it is the tempo as before. No new setting.
- **OPX waveforms:** each operator has a WAVE, the opsix's 23 (sines at 16, 12 and 8 bits, triangle, saw and square
  with HD versions, eleven additive waves, four noises), and WIDTH. On EDIT > OPERATORS > OSC: WAVE, FB, WIDTH.
  Existing patches keep their sine and sound as before.
- Each periodic waveform is a table read, as the sine is. SAW and SQR are band-limited to 10 kHz, HD to 20 kHz.
- OPX renders an operator at a time over each 32-sample block (patches with a loop back to a lower operator keep the
  sample-by-sample order; both give the same output). On the device, six notes take 38 % instead of 57 % with sines,
  and 37–58 % with any waveform (was 87–166 %).
- The benchmark (MENU > SYSTEM > INFO, OCT+) times FM6, OPX with each waveform, multiply-adds, branches, calls and
  loads.
- CI no longer publishes the `-mcpu=r3` build: it measured no faster (docs/OPX_PLAN.md).

## 0.0.1 (2026-10-16)

The first release, based on Felucca 1.5.1: Felucca with the OPX engine. Everything Felucca 1.5.1 does works as before.

**New: the OPX engine** (engine 15), an altered-FM engine after the Korg opsix, in FM mode so far.

- Six sine operators per voice, six voices. All 40 of the opsix's algorithms (1–32 the DX7's, 33–40 its own) and
  USER, a 6 × 6 modulation matrix with self feedback and a heard / not heard output per operator.
- Per operator: RATIO or FIXED frequency, FINE, DTUN, TRANS, LEVEL, an ATK DEC SUS REL envelope with CURVE (LIN to
  EXP), velocity to level, feedback. Per patch: the algorithm and ALG FB (the loop of algorithms 4, 6 and 34).
- Seven factory patches (SLOT F1–F7): E.PIANO, BELL, FM BASS, BRASS, SAW LEAD, LOOP PAD, X-MOD.
- EDIT 1 (ALGO) draws the algorithm as a chart, or USER as the opsix's matrix, with a small oscilloscope; on USER the
  knobs pick a cell and set its amount, and OUT sets the operator's output level.
- EDIT > OPERATORS: every operator parameter in one scrolling list (PITCH, EG, OSC, USER, ALGO). KNOB 1 the
  operator, 2 the section, 3 the row, 4 the value.
- Macros on EDIT 1 / 2 (ALG, ATK, DEC, MLVL, FB, MRAT, DTUN, SLOT) for quick changes, automation, step locks and
  MIDI CC (CC74 MLVL, CC71 FB).
- Editing makes the patch the track's own (SLOT OWN); SAVE held undoes. Each track's patch is saved with the project
  and the autosave.

**Compatibility**

- Projects and user presets without an OPX track move between Felucca 1.5.1 and Felucca-OPX both ways.
- A project with an OPX track does not open on Felucca or the official firmware (its slot shows EMPTY there; the
  data stays in flash until something is saved into that slot). Neither does a user preset of an OPX sound.
- User sample slot 3 holds 40 KB instead of 80 KB; the other half keeps the OPX patches. A longer sample already in
  slot 3 must be uploaded again.
- Not yet saved: an OPX track's own patch in a user preset or in the editor's backup archive. These come back with
  their SLOT's factory patch.
- MIDI LEARN assignments, settings and everything else are kept when installing.
