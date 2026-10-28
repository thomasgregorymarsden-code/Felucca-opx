# Felucca's release history (up to 1.5.1)

OPX is based on Felucca 1.5.1. These are Felucca's own notes for the releases leading to it, kept as they
were in Felucca's README; issue and discussion numbers refer to [Felucca's repository](https://github.com/hugelton/Felucca).
OPX's own releases are in [CHANGELOG.md](../CHANGELOG.md).

## New in 1.5.1

- **SEQ > DETAIL** (Discussion #199): SEQ now goes STEP, **DETAIL**, AUTOMATION. DETAIL is the STEP page (the same
  cursor, the same piano roll or drum grid) with KNOB 1–4 as the cursor step's **CHANCE**, **RATCH**, **NUDGE** and
  **VEL**; PRESETS walks the steps, and on a DRUM track a white key picks its step. The four are drawn as lanes over
  the steps; EDIT puts the step's four back. On STEP and the drum grid a step with a CHANCE, RATCH or NUDGE gets a
  small mark. These are the values AUTOMATION lists (EDIT there still deletes a row): nothing new is stored.
- **MIDI Program Change** (PR #179 by [renebohne](https://github.com/renebohne)): a Program Change on a track's
  channel (routed as notes by MENU > MIDI > MIDI IN) loads that track's sound, numbered from 0 as PRESETS lists
  the engine's sounds. DRUM: its kits (0–5: STD, 80, 10, 66, 55, 77). FM6: 0–7 its presets (F1–F8), 8–39 the voice
  bank's B1–B32 that hold a voice. A number past the last is ignored; the engine never changes. SAVE held undoes it.
- **Dotted delay times:** the DLY page's TIME has **1/4D, 1/8D and 1/16D**, and the knob steps them by length
  (… 1/2, 1/4D, 1/4, 1/8D, 1/8, 1/16D, 8T …). The delay holds 1.49 s, so 1/4D is held to that at 60 BPM and below.
- **REV THROW and ECHO THROW** in the FX layer, on **D5** and **E5** by default: while held, every track's reverb
  send (REV THROW) or delay send (ECHO THROW) is full, gliding in and out without a click; ECHO THROW also lifts the
  delay's FDBK to 70 % if it is lower. Let go and the tails ring out. Nothing is saved or recorded. If you had set
  D5 or E5 to NONE, they now hold the throws: set them to NONE again (hold the key in the FX layer, turn PRESETS).
- Fixed: turning KNOB 4 (LIST) or KNOB 3 (FAV) on PRESETS made all the LEDs flash; the change is now saved once you
  leave the page or after 10 s idle (#197). MOMENTARY (LFO held) now works on MIXER too: LEVEL, PAN, REV and MUTE go
  back when LFO is let go (#195).
- USB audio: the descriptors list the audio streaming interface before the MIDI one, for the USB audio driver of
  older macOS, where the audio input did not appear (#67; expected to fix macOS 15 and earlier, to be confirmed).
- 1.5.1 keeps 1.5's formats: projects, presets and settings move between 1.5 and 1.5.1 both ways (on 1.5 a
  dotted delay TIME plays as 4BAR).

## New in 1.5

- **An INSERT effect on every track, with a dry / wet MIX** (Discussions #78, #177): FX → INSERT. KNOB 1 TYPE: SOFT,
  HARD, FOLD, FUZZ (drives: DRIVE, TONE, LEVEL), CRUSH (BITS, RATE, LPF), PHASR, FLANG, CHOR (RATE, DEPTH, FDBK);
  INSERT 2: MIX. It comes after DIST and before the SLICER, is part of the sound (user presets and projects keep it)
  and works with automation and locks. TYPE OFF, the default, plays exactly as before.
- **MIDI LEARN** (Discussion #170): on any page hold GLO and press D4 (MIDI LEARN ON), turn the FM-1 knob of a
  parameter, then move a knob or fader on your controller: that CC now sets the parameter (the header says e.g.
  "CC74 -> T1 CUT"). Turn the next knob and move the next control; OCT− clears the picked parameter's CC, OCT+ (or
  GLO + D4 again) ends it. Up to 16 CCs, kept with the settings; **MENU > MIDI > LEARN CLEAR** (OCT+) clears them all.
  Nothing changes for anyone who never learns.
- **ANALOG's filter TYPE: LP, BP, HP** (Discussion #104): EDIT → FILTER, a page an ANALOG track has after EDIT 2:
  KNOB 1 TYPE (LP the low-pass as before, BP band-pass, HP high-pass), KNOB 2–4 CUT, RES, KTR.
- **ENV SYNC** (Discussion #175): ENV DEST's KNOB 4 (ESYNC) makes ENV's ATK, DEC and REL note values of the tempo
  (1/64T to 4 bars, dotted and triplets) instead of milliseconds; they follow the tempo and an external clock.
- **A note's length on the piano roll** (Discussions #178, #173): on SEQ → STEP, KNOB 3 on a note step is its **LEN**:
  right ties the steps after it on, left takes them back. It stops before the next note ("NOTE AHEAD").
- **Per-step velocity** (Discussion #173): SEQ → AUTOMATION, **+ ADD VEL** (OCT+: SHOW) lists every note step's
  velocity, KNOB 4 sets it (1–127). Steps entered by hand play 96, steps recorded live keep how hard you played.
- **HELP** (Discussion #156; MENU > SYSTEM > HELP, OFF by default): ON shows a short hint, key caps and a few words, in
  the footer for about 2 s when a page opens, and under a quick layer's map while it shows.
- **SAVE reaches PRESETS on its second tap** (#173): SAVE's pages go USER, PRESETS, PHRASES, PROJECT, TOOLS. The quick
  save (SAVE, OCT+, OCT+) is unchanged.
- **The FX layer's map alternates light and dark columns** (#173), so neighbouring effects are told apart at a glance.
- **HOME LEVELS** (#134): MENU > DISPLAY > HOME has a third value, LEVELS: HOME shows the four tracks' rows and
  KNOB 1–4 set the LEVEL of T1–T4.
- ⚠ **SAMPLE's PIANO is the full piano again** (five notes, as in 1.0 to 1.1.5): sounds and projects made on 1.4 or
  1.4.1 that use PIANO play the full piano. PIANO HD in a user slot is no longer needed.
- Fixed: the arpeggiator follows TRN, ROOT, SCALE and QNT on held and latched notes (#191); MENU > DISPLAY > ANIM no
  longer changes HOME.
- ⚠ The INSERT, the filter TYPE and ENV SYNC are new sound parameters (OFF, LP, OFF by default): everything saved
  before plays as before. But **a project saved by 1.5 does not open on 1.2 to 1.4.1**: its slot shows EMPTY there
  (the data stays in flash until you save something into that slot there), and a backup that holds one is refused.
  User presets saved by 1.5 load there without the new values. The full backup still does not include the FM6
  voice bank.

## New in 1.4.1

- **Engine quick actions on the EDIT layer's black keys.** Hold EDIT: the black keys do what the selected track's
  engine offers, and the map shows it. On a DRUM track, black keys 1–8 (F#3 … A#4) mute its lanes KICK, SNARE, CLAP,
  HATCL, HATOP, TOM, RIM and BELL, one press to mute, the next to unmute. The LEDs show which lanes sound, the drum
  grid dims a muted lane. A muted lane keeps playing unheard, so unmuting brings it back in time. The mutes are for
  live play: never saved, not recorded, not undone; a sound or project load clears them, OCT− in the layer puts them
  back as they were when it opened. Other engines have no quick actions yet.
- **FM6 voice bank** (Discussion #168). The web editor's 6-OP FM tab sends a whole 32-voice SysEx bank (the standard
  4096-byte file) to the FM-1 in one go, with its name. On an FM6 track SLOT (EDIT 2, KNOB 4) now goes F1–F8, OWN,
  then **B1–B32**, the bank's voices; the algorithm chart shows the voice's name. Picking a voice copies it into the
  track, as F1–F8 do, so a project or user preset saved with it keeps that sound after you send another bank (it
  loads with SLOT OWN). With no bank sent, SLOT stops at OWN as before. The bank has its own flash sector and is only
  replaced once a new one arrived whole; projects, presets, samples and settings are not touched. **The full backup
  does not include the bank: keep the .syx file.**
- 1.4.1 opens and saves everything 1.4 does, in the same formats.

## ⚠ Coming from 1.1.5

1.4 is the first release since 1.1.5.1 and holds a lot (all of this holds for 1.4.1 and 1.5 too). Everything you saved loads and sounds as before, except
where marked here:

- **Projects are saved in a new format (FUN10).** 1.4 opens every older project, but **1.1.5.x and older cannot
  open a project saved by 1.4 or later**: they show its slot as empty (the data stays in flash until you save something into
  that slot there), and refuse a backup that holds one. User presets saved by 1.4 load on 1.1.5.x without the five
  new values (LFO 2's SYNC, TRIG and POL, QUANTIZE, SPREAD).
- **ARP ORD now plays the held notes in the order you pressed them** (up to 1.1.5 it played them as UP): a sound
  or project using ORD sounds different when its notes were pressed in another order than low to high.
- **SAMPLE's PIANO** was a lighter lo-fi piano in 1.4 and 1.4.1; since 1.5 it is the full piano of 1.1.5 again.
- **The pages moved** (Discussion #153): see Controls below. In short: SEQ is STEP and AUTOMATION; **SEQ held is SEQ
  TOOLS on every page** (it no longer opens SONG); **GLO opens SONG**; PHRASES is on SAVE; HOME goes round HOME,
  MIXER and CLOCK (BPM, swing, clock source); TUNE is MENU > AUDIO, MIDI IN (ROUT) is MENU > MIDI, and the USB state
  and CPU load are MENU > SYSTEM > INFO. REC held is the REC layer as before.
- **SONG has a lane per track**: older songs load with all four tracks on each row's slot and play as before; its
  knobs are KNOB 1 SECTION, 2 TRACK, 3 PAT, 4 REPS, and on SONG the white keys A3 B3 C4 D4 set the slot.
- **A fresh start uses the HALL reverb** (up to 1.1.5: ROOM). Saved projects keep their reverb TYPE.
- **BASS+ (MENU > SPEAKER EQ) is softer:** its added harmonics are at half the level, so a full-scale kick no longer
  distorts (#180).
- **The web editor** needs its update for the new features; use the one on the site.
