# Changelog

Felucca-OPX's releases. Felucca's own release notes, up to the 1.5.1 this fork is based on, are in
[docs/FELUCCA_HISTORY.md](docs/FELUCCA_HISTORY.md).

## Unreleased

- **OPX M4: EG1, LFO1 and the pitch modulation.** Each operator's P MOD (EG1 INT, LFO1 INT, ±144.00 semitones; VEL
  with DEST FINE or COARSE) and LMOD LFO1 INT (±100 %), PROG PITCH (LFO1 INT, TRANS, OCT), each INT with its CTRL
  source (MOD WHEEL, PITCH WHEEL ±, PEDAL, NOTE, VEL, EXP VEL, AFTER TOUCH, EG1, LFO1; REL VEL fixed; EG2/3 and
  LFO2/3 come with M5). LFO1: the opsix's 23 waves, SPEED 0.01–100 Hz or a note value with TEMPO, KSYNC OFF /
  COMMON / VOICE, PHASE (or RND), FADE.
- **OPX pages EG1 and LFO1** (EDIT, after OP LEVELS): EG1 drawn beside its ATK DEC SUS REL CURVE; LFO1's wave drawn
  beside its rows and PROG PITCH's. OPERATORS gains the rows (PITCH: P MOD; EG: LMOD LFO1; sections EG1, LFO1,
  PROG). P MOD INT moves 0.05 a click up to 1, 0.50 up to 12, then 1 and 12 semitones.
- **The web editor's OPX tab** edits the same (Modulation, EG1, LFO1, Program pitch).
- Fix: HOME > OP MIX: a fast turn of PRESETS or ALGORITHM could still load a sound or change the track now and then
  (the knob was read twice a pass; a click landing between the reads went to the usual handler).
- Fixes: an OPX phaser product could overflow 32 bits at high resonance (found by the host sanitizer); a host test
  read past its buffer.

- **OPX user presets keep their patch** (up to 16 OPX user presets; a 17th is refused with "OPX PRESETS FULL", nothing
  written). A rename keeps it; erasing the preset or saving another sound over it frees its place. Presets saved by
  0.1.0 load their SLOT's factory patch, as before.
- **The full backup includes OPX:** the projects' and user presets' OPX patches and the tracks' current ones. Archives
  of 0.1.0 and Felucca still restore; a new archive restored onto 0.1.0 skips the OPX parts.
- **OPX pages ENVELOPE and OP LEVELS** (trial, beside OPERATORS): the selected operator's EG drawn with ATK DEC SUS
  REL CURVE beside it; the six operators' LEVEL beside the algorithm chart.
- **HOME > OP MIX** (OPX tracks; HOME's first page there): the six operator levels on six knobs, PRESETS OP 1,
  ALGORITHM OP 2, KNOB 1–4 OP 3–6.
- **OPX knobs:** FINE moves 0.01 a click (was 0.001, below what the screen shows). ATK, DEC and REL move through a
  ladder of times (1 ms steps at the bottom up to 5 s steps at the top): 10 ms to 1 s takes 36 clicks (was about 420).
- **The web editor's OPX tab:** read a track's OPX patch or a factory one, edit every operator value the engine plays
  (MODE and its values, PITCH, EG, KTRK, the USER matrix), send it to a track (also while editing), save it as a file
  (`.opx`, the 434-byte record) or load one. The library keeps an OPX sound's patch, and moves it to and from the
  device's user presets. New editor commands OPX_GET / OPX_PUT (121, 122).
- **Storage:** the projects' OPX patches are kept two projects per storage object (was one each), which frees room
  for the user presets' without taking more of sample slot 3. The first start after updating moves 0.1.0's layout
  over once; a power cut during the move loses nothing (tested at every write). Going back to 0.1.0 afterwards loses
  the projects' OPX patches, not the projects.

## 0.1.0 (2026-10-19)

Based on Felucca 1.5.1, as 0.0.1. Projects and patches saved with 0.0.1 open unchanged (the patch format is the same).

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
  with HD versions, eleven additive waves, four noises), and WIDTH. On EDIT > OPERATORS > MODE: WAVE, FB, WIDTH.
  Existing patches keep their sine and sound as before.
- Each periodic waveform is a table read, as the sine is. SAW and SQR are band-limited to 10 kHz, HD to 20 kHz.
- OPX renders an operator at a time over each 32-sample block (patches with a loop back to a lower operator keep the
  sample-by-sample order; both give the same output). On the device, six notes take 40 % instead of 57 % with sines,
  and 39–60 % with any waveform (was 87–166 %), above the idle mix.
- The benchmark (MENU > SYSTEM > INFO, OCT+) times FM6, OPX with each waveform and in each kind of mode,
  multiply-adds, branches, calls and loads.
- CI no longer publishes the `-mcpu=r3` build: it measured no faster (docs/OPX_PLAN.md).

**Known limitations**

- The operator filters and effects are standard designs with the opsix's names and ranges, not measured copies;
  the modes have not yet been compared with an opsix by ear.
- Processor time: about two operators in a filter mode per patch (two in FILTER FM go over; the audio then drops a
  voice). See docs/OPX_PLAN.md, Cost.
- SHORT DELAY stops at 17 ms (the opsix: 300 ms), one operator a voice.
- Not yet: an OPX track's own patch in user presets and the editor's backup, the web editor's OPX page (M3); the
  opsix's EG1-3, LFOs and virtual patch (M4); its main filter and effects (M5).

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
