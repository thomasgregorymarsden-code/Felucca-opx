// SPDX-License-Identifier: GPL-3.0-only
// OPX fork: the opsix comparison test patches (the test sheet "opsix <-> OPX test patches", the MIDI file
// OPX_opsix_compare.mid), each at the first setting its row lists, as OPX patch records. Each starts from the INIT
// patch (the opsix's INITIALIZE: OP1 alone at 100) and the sheet's base: OP1 REL 50 ms. Test 12 (tempo sync: test 10
// with TEMPO ON) and test 18 (load: test 13) reuse others, so 16 patches fit the FM-1's 16 OPX user presets.
// make(OX) -> [{ test, name (12 characters, the preset's), marker, pk (the record) }]

// TIME10 code nearest t ms (1 ms x 90000^((c - 1) / 1022)); HZ10 code nearest f Hz (0.01 Hz x 10000^(c / 1023))
export const timeCode = (ms) => (ms <= 0 ? 0 : Math.round(1 + 1022 * Math.log(ms) / Math.log(90000)));
export const hzCode = (hz) => Math.round(1023 * Math.log10(hz / 0.01) / 4);
const RATIO = { "1/2": 6, 1: 7, 2: 8, 3: 9 };                 // COARSE codes (ui_fm6op.c xop_coarse)
const LFO = { TRI: 0, SQR: 3, SIN: 4 };                         // the LFO Wave List order (eng_opx.c OPX_L_*)
const MODE = { FM: 0, RING: 1, FILTER: 2, FILTER_FM: 3, FOLD: 4, EFFECT: 5 };
const WAVE = { SIN: 0, SAW: 4 };
const CTRL = { OFF: 0, MOD_WHEEL: 1 };

export const TESTS = [
  [1, "T01 LVL TAPR", "01 TUNING", { "op2.level": 25, "op2.eg_rel": "50ms" }],
  [2, "T02 RATIO", "01 TUNING", { "op2.level": 50, "op2.eg_rel": "50ms", "op2.coarse": RATIO[2] }],
  [3, "T03 FEEDBACK", "01 TUNING", { "op1.fm_fb": 25 }],
  [4, "T04 WAVES", "03 KEY RANGE", { "op1.wave": WAVE.SAW }],
  [5, "T05 EG CURVE", "04 LONG HOLD", { "op1.eg_atk": "500ms", "op1.eg_dec": "2000ms", "op1.eg_sus": 50,
    "op1.eg_rel": "2000ms", "op1.eg_curve": 0 }],
  [6, "T06 VELOCITY", "02 VELOCITY", { "op1.lmod_vel": 100 }],
  [7, "T07 KEY TRK", "03 KEY RANGE", { "op1.ktrk_center": 60, "op1.ktrk_low": -100, "op1.ktrk_high": -100 }],
  [8, "T08 PITCH EG", "05 SHORT NOTES", { "eg1.atk": 0, "eg1.dec": "300ms", "eg1.sus": 0, "eg1.rel": 0, "eg1.curve": 0,
    "op1.pmod_eg1": 1200 }],
  [9, "T09 VIBRATO", "06 MOD WHEEL", { "lfo1.wave": LFO.SIN, "lfo1.speed": "5hz", "lfo1.ksync": 0, "lfo1.phase": 0,
    "lfo1.tempo": 0, "lfo1.fade": 0, "op1.pmod_lfo1": 100, "op1.pmod_lfo1_ctrl": CTRL.MOD_WHEEL }],
  [10, "T10 TREMOLO", "04 LONG HOLD", { "lfo1.wave": LFO.SQR, "lfo1.speed": "2hz", "lfo1.ksync": 0,
    "op1.lmod_lfo1": 100, "op1.lmod_lfo1_ctrl": CTRL.OFF }],
  [11, "T11 KSYNC", "10 STAGGERED", { "lfo1.wave": LFO.SIN, "lfo1.speed": "2hz", "lfo1.ksync": 0,
    "op1.pmod_lfo1": 200, "op1.pmod_lfo1_ctrl": CTRL.OFF }],
  [13, "T13 FILTER", "03 KEY RANGE", { "op2.wave": WAVE.SAW, "op2.level": 100, "op1.mode": MODE.FILTER,
    "op1.flt_type": 0, "op1.flt_cutoff": 0, "op1.flt_reso": 50, "op1.flt_oscmix": 0 }],
  [14, "T14 FILT FM", "04 LONG HOLD", { "op2.wave": WAVE.SIN, "op2.level": 50, "op1.mode": MODE.FILTER_FM,
    "op1.wave": WAVE.SAW, "op1.flt_type": 0, "op1.flt_cutoff": 0, "op1.flt_reso": 30 }],
  [15, "T15 RING", "01 TUNING", { "op2.level": 100, "op2.coarse": RATIO[3], "op1.mode": MODE.RING,
    "op1.ring_depth": 100, "op1.ring_shape": 0 }],
  [16, "T16 FOLDER", "01 TUNING", { "op1.mode": MODE.FOLD, "op1.fold_gain": 50, "op1.fold_oscmix": 100,
    "op1.fold_bias": 0 }],
  [17, "T17 EFFECT", "01 TUNING", { "op1.mode": MODE.EFFECT, "op1.fx_oscmix": 100, "op1.fx_type": 5,
    "op1.fx_e": 50, "op1.fx_f": 180 }],                    // (DIST: E DIST 50 %, F LOW 0.0 dB, stored as value - lo)
];

export function make(OX) {
  return TESTS.map(([test, name, marker, set]) => {
    const pk = OX.defaults();
    OX.put(pk, "op1.eg_rel", timeCode(50));             // (the sheet's base)
    for (const [f, v] of Object.entries(set)) {
      const x = typeof v !== "string" ? v : v.endsWith("ms") ? timeCode(+v.slice(0, -2)) : hzCode(+v.slice(0, -2));
      if (!OX.has(f)) throw new Error(`opx_tests: no field ${f}`);
      OX.put(pk, f, x);
    }
    OX.setName(pk, name);
    return { test, name, marker, pk };
  });
}
