// SPDX-License-Identifier: GPL-3.0-only
// OPX fork: an OPX patch as the web editor handles it: the packed record of tools/opx_patch_spec.py (opx_fields.js,
// 434 bytes, every opsix program field; the firmware's opx_patch.c), its fields read and written by name, checked as
// the firmware checks one (opx_pk_ok), and the values the device shows (ui_fm6op.c xop_rowtext) as text.
import { OPX_FIELDS, OPX_PK_BYTES, OPX_FORMAT_VERSION, OPX_FX_TYPES } from "./opx_fields.js";

export { OPX_PK_BYTES, OPX_FORMAT_VERSION, OPX_FX_TYPES };
const BY = new Map(OPX_FIELDS.map((f) => [f[0], f]));
export const has = (name) => BY.has(name);
export const range = (name) => { const f = BY.get(name); return [f[3], f[4]]; };

export function get(pk, name) {
  const [, off, bits, lo] = BY.get(name);
  let v = 0;
  for (let k = 0; k < bits; k++) v += ((pk[(off + k) >> 3] >> ((off + k) & 7)) & 1) * 2 ** k;
  return v + lo;
}
export function put(pk, name, value) {           // (clamped into the field's range, as opx_put)
  const [, off, bits, lo, hi] = BY.get(name);
  const u = Math.max(lo, Math.min(hi, Math.round(+value || 0))) - lo;
  for (let k = 0; k < bits; k++) {
    const i = (off + k) >> 3, m = 1 << ((off + k) & 7);
    pk[i] = Math.floor(u / 2 ** k) % 2 ? pk[i] | m : pk[i] & ~m;
  }
}
export function defaults() {
  const pk = new Uint8Array(OPX_PK_BYTES);
  for (const [name, , , , , d] of OPX_FIELDS) put(pk, name, d);
  return pk;
}
// a record the firmware takes: its size and version, every field inside its range (opx_patch.c opx_pk_ok)
export function ok(pk) {
  if (!pk || pk.length !== OPX_PK_BYTES || get(pk, "version") !== OPX_FORMAT_VERSION) return false;
  return OPX_FIELDS.every(([name, , , , hi]) => get(pk, name) <= hi);
}
export function name(pk) {
  let s = "";
  for (let i = 0; i < 16; i++) s += String.fromCharCode(get(pk, `prog.name${i}`));
  return s.trimEnd();
}
export function setName(pk, s) {
  const t = String(s).toUpperCase().replace(/[^\x20-\x7E]/g, " ").slice(0, 16).padEnd(16, " ");
  for (let i = 0; i < 16; i++) put(pk, `prog.name${i}`, t.charCodeAt(i));
}

// value lists (the device's names, ui_fm6op.c and eng_opx.c)
export const MODES = ["FM", "RING MOD", "FILTER", "FILTER FM", "WAVE FOLDER", "EFFECT", "BYPASS", "MUTE"];
export const WAVES = ["SIN", "SIN 12BIT", "SIN 8BIT", "TRI", "SAW", "SAW HD", "SQR", "SQR HD", "ADTV SAW3", "ADTV SQR3",
  "ADTV TRI3", "ADTV 12345", "ADTV 1+2", "ADTV 1+3", "ADTV 1+4", "ADTV 1+5", "ADTV 1+6", "ADTV 1+7", "ADTV 1+8",
  "NOISE S/H", "NOISE WHITE", "NOISE PINK", "NOISE BLUE"];
export const NOISE0 = 19;                        // (the noises: no phase, so FB and WIDTH do nothing)
export const FILTERS = ["LPF", "HPF", "BPF", "BRF", "MG LPF12", "MG LPF24", "MG HPF12", "MG HPF24", "MG BPF6", "MG BPF12",
  "MS-20 LPF", "MS-20 HPF"];
export const EFFECTS = ["PEAK EQ", "SHELV EQ", "PHASER", "SHORT DLY", "COMB", "DIST", "DRIVE", "DEC", "WAVESHAPER", "PUNCH"];
export const COARSE = Array.from({ length: 39 }, (_, c) => (c < 7 ? `1/${128 >> c}` : String(c - 6)));
export const NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const noteName = (n) => NOTES[n % 12] + (Math.floor(n / 12) - 1);
export const ALGOS = 40;                         // 1..40, then USER (algo 40)
export const USER = 40;
// the modes' own rows (ui_fm6op.c xop_list): [field suffix, label]
export const MODE_ROWS = [
  [["fm_fb", "FB"], ["fm_width", "WIDTH"]],
  [["ring_depth", "DEPTH"], ["ring_shape", "SHAPE"]],
  [["flt_type", "TYPE"], ["flt_oscmix", "OSC MIX"], ["flt_cutoff", "CUTOFF"], ["flt_reso", "RESO"]],
  [["flt_type", "TYPE"], ["flt_cutoff", "CUTOFF"], ["flt_reso", "RESO"]],
  [["fold_gain", "GAIN"], ["fold_oscmix", "OSC MIX"], ["fold_bias", "BIAS"]],
  [["fx_type", "FX"], ["fx_oscmix", "OSC MIX"], ["fx_e", "E"], ["fx_f", "F"]],
  [], []];
// an effect's E and F (eng_opx.c OPX_FXV: each type's range; ui_fm6op.c: names and units). Stored as value - lo
export const FXV = [[-500, 500, -180, 180], [-180, 180, -180, 180], [-500, 500, -100, 100], [1, 300, 0, 100],
  [-500, 500, -100, 100], [0, 100, -180, 180], [0, 100, -180, 180], [200, 25600, 200, 2400], [0, 59, 0, 100],
  [0, 100, -180, 180]];
export const FX_NAMES = [["FREQ", "GAIN"], ["LOW", "HIGH"], ["FREQ", "FB"], ["TIME", "FB"], ["FREQ", "FB"], ["DIST", "LOW"],
  ["DRIVE", "LOW"], ["FREQ", "BIT"], ["TYPE", "DAMP"], ["PUNCH", "HIGH"]];
// units: 0 % (unsigned), 1 % signed, 2 semitones x0.1, 3 dB x0.1, 4 ms, 5 x0.01, 6 a number
export const FX_UNITS = [[2, 3], [3, 3], [2, 1], [4, 0], [2, 1], [0, 3], [0, 3], [5, 5], [6, 0], [0, 3]];
// a new effect type's neutral E and F (ui_fm6op.c xop_knob), as stored
export const FX_NEUTRAL = [[500, 180], [180, 180], [500, 100], [9, 30], [500, 150], [50, 180], [50, 180], [1400, 1000],
  [0, 0], [50, 180]];

const signed = (v, d = 0) => (v > 0 ? "+" : "") + (d ? (v / 10 ** d).toFixed(d) : String(v));
// TIME10 code c (EG times): 0 = 0 ms; 1 ms x 90000^((c - 1) / 1022)
export function timeText(c) {
  if (c <= 0) return "0 MS";
  const ms = 90000 ** ((c - 1) / 1022);
  return ms < 10 ? `${ms.toFixed(1)} MS` : ms < 1000 ? `${Math.round(ms)} MS` : ms < 10000 ? `${(ms / 1000).toFixed(2)} S`
    : `${(ms / 1000).toFixed(1)} S`;
}
export const hzText = (f) => (f < 10000 ? (f / 100).toFixed(2) : f < 100000 ? (f / 100).toFixed(1) : f < 1000000
  ? String(Math.floor(f / 100)) : `${(f / 100000).toFixed(1)}K`) + " HZ";
// the effect's E or F (w 0 / 1) of type ty, stored value v, as text
export function fxText(ty, w, v) {
  const x = v + FXV[ty][w * 2];
  switch (FX_UNITS[ty][w]) {
    case 1: return signed(x) + " %";
    case 2: return signed(x, 1) + " ST";
    case 3: return signed(x, 1) + " DB";
    case 4: return `${x} MS`;
    case 5: return (x / 100).toFixed(2);
    case 6: return String(x).padStart(2, "0");
    default: return `${x} %`;
  }
}
// operator k's (1..6) field `f` as the device shows it
export function text(pk, k, f) {
  const n = `op${k}.${f}`, v = get(pk, n);
  switch (f) {
    case "mode": return MODES[v];
    case "wave": return WAVES[v];
    case "flt_type": return FILTERS[v];
    case "fx_type": return EFFECTS[v];
    case "fm_width": return v ? `${v} %` : "OFF";
    case "flt_cutoff": return signed(v, 1) + " ST";
    case "fold_bias": case "ktrk_low": case "ktrk_high": return signed(v) + " %";
    case "fx_e": case "fx_f": return fxText(get(pk, `op${k}.fx_type`), f === "fx_f" ? 1 : 0, v);
    case "freq_fixed": return v ? "FIXED" : "RATIO";
    case "coarse": return COARSE[v];
    case "fine": return (v / 1000).toFixed(3);
    case "fixed_freq": return hzText(v);
    case "detune": return signed(v) + " CT";
    case "trans": return signed(v) + " ST";
    case "eg_atk": case "eg_dec": case "eg_rel": return timeText(v);
    case "eg_curve": return v === 0 ? "LIN" : v === 10 ? "EXP" : String(v);
    case "ktrk_center": return noteName(v);
    case "ktrk_lcrv": case "ktrk_hcrv": return v ? "EXP" : "LIN";
    default: return `${v} %`;
  }
}
// M4: the LFO Wave List (p.56, eng_opx.c OPX_L_*), the Control Source List after OFF (p.85, OPX_C_*), LFO1's tempo
// note values (eng_opx.c OPX_SYNC: read off an opsix, its SPEED knob's 24 with TEMPO on)
export const LFO_WAVES = ["TRI", "SAW DOWN", "SAW UP", "SQR", "SIN", "S/H", "GTR", "X.TRI", "X.SAW DOWN", "X.SAW UP", "TRI4",
  "TRI6", "SAW4 DOWN", "SAW6 DOWN", "S.RND TIME", "S.RND LVTIME", "RND LEVEL", "RND TIME", "RND LVTIME", "TRI+",
  "SAW DOWN+", "SAW UP+", "SQR+"];
export const CTRLS = ["OFF", "MOD WHEEL", "PITCH WHEEL", "PITCH WHL+", "PITCH WHL-", "PEDAL", "NOTE", "VEL", "EXP VEL",
  "REL VEL", "AFTER TOUCH", "EG1", "EG2", "EG3", "LFO1", "LFO2", "LFO3"];
export const SYNC = ["1/48", "3/128", "1/32", "1/24", "3/64", "1/16", "1/12", "3/32", "1/8", "1/6", "3/16", "1/4", "1/3",
  "3/8", "1/2", "2/3", "3/4", "1/1", "4/3", "3/2", "2/1", "8/3", "3/1", "4/1"];
export const KSYNC = ["OFF", "COMMON", "VOICE"];
// M5b: the main filter's types (the manual p.38's order) and its CUTOFF code c (CUT10): 6.489 Hz x (23680 / 6.489)^(c / 1023)
export const MAIN_FILTERS = ["LPF 12", "LPF 24", "LPF MS-20", "LPF POLY6", "HPF 12", "HPF 24", "HPF MS-20", "BPF 6",
  "BPF 12", "BRF 6", "BRF 12"];
export const cutText = (c) => { const h = 6.489 * (23680 / 6.489) ** (c / 1023);
  return h < 100 ? `${h.toFixed(2)} HZ` : h < 1000 ? `${Math.round(h)} HZ` : `${(h / 1000).toFixed(2)} KHZ`; };
// KTRK LOW / HIGH c: the slope c / (128 - |c|) (64: 1.00; +-127: +-INF)
export const ktrkText = (c) => Math.abs(c) >= 127 ? (c > 0 ? "+INF" : "-INF") : signed(Math.round(c * 100 / (128 - Math.abs(c))), 2);
// HZ10 code c (LFO SPEED, TEMPO off): 0.01 Hz x 10000^(c / 1023)
export const speedText = (c) => `${(0.01 * 10000 ** (c / 1023)).toFixed(2)} HZ`;
// x0.01 semitone (P MOD, PROG LFO1 INT)
export const stText = (v) => signed(v, 2) + " ST";

// the matrix (USER): what operator x (1..6) sends to y (its own: its self feedback); its direct out
export const uaField = (x, y) => (x === y ? `ua.op${x}_self` : `ua.op${x}_to_op${y}`);
export const outField = (x) => `ua.op${x}_out`;

// M5: V.PATCH's sources and destinations (eng_opx.c OPX_VS_*, OPX_VD_*; ui_fm6op.c xvp_*): [scope, [names], first]
const CC = Array.from({ length: 119 }, (_, k) => `CC ${k + 1}`);
export const VP_SRC = [["OFF", ["---"], 0], ["CTRL", ["MOD WHL", "PITCH", "PITCH+", "PITCH-", "PEDAL"], 1],
  ["KBD", ["NOTE", "VEL", "EXP VEL", "REL VEL"], 6], ["EG/LFO", ["EG1", "EG2", "EG3", "LFO1", "LFO2", "LFO3"], 10],
  ["CC+", CC, 16], ["CC+/-", CC, 135], ["MIDI", ["A.TOUCH", "POLY AT"], 254]];
const VP_PROG = ["PITCH", "OCT", "TRANS", "LEVEL", "PAN", "ATK", "DECAY", "REL", "SUS", "TEMPO", "ALGO", "ALGO FB", "GLIDE",
  "UNI VCS", "UNI DTN", "UNI SPR"];
const VP_OP = ["PITCH", "COARSE", "OCT", "TRANS", "LEVEL", "ATK", "DECAY", "REL", "SUS", "FM FB", "WIDTH", "R.DEPTH",
  "R.SHAPE", "F.GAIN", "F.BIAS", "F.MIX", "CUTOFF", "RESO", "FLT MIX", "FX MIX", "PEQ FRQ", "PEQ GN", "SEQ LOW", "SEQ HI",
  "PHS FRQ", "PHS FB", "SDL TIM", "SDL FB", "CMB FRQ", "CMB FB", "DST AMT", "DST LOW", "DRV AMT", "DRV LOW", "DEC FRQ",
  "DEC BIT", "WS DAMP", "PCH AMT", "PCH HI"];
export const VP_DST = [["OFF", ["---"], 0], ["PROG", VP_PROG, 1],
  ...[1, 2, 3, 4, 5, 6].map((k) => [`OP${k}`, VP_OP, 17 + 39 * (k - 1)]),
  ...[1, 2, 3].map((k) => [`EG${k}`, ["ATK", "DECAY", "REL", "SUS"], 251 + 4 * (k - 1)]),
  ...[1, 2, 3].map((k) => [`LFO${k}`, ["WAVE", "SPEED", "PHASE", "FADE"], 263 + 4 * (k - 1)]),
  ["FILTER", ["CUTOFF", "RESO"], 275], ...[1, 2, 3].map((k) => [`FX${k}`, ["PARAM 1", "PARAM 2", "PARAM 3", "PARAM 4"],
  277 + 4 * (k - 1)]), ["ARP", ["RESO", "GATE", "OCT"], 289], ["SEQ", ["SWING"], 292]];
// the destinations OPX plays now (eng_opx.c opx_vd_on): the rest are kept and do nothing yet
// M5c: the program effects. A type OPX plays (kind 0: the rest are kept and pass the sound for now, as the firmware's
// opx_fx_played); a knob's field (value - lo) as text in its type's units; FX n's type set with its knobs' defaults
export const fxPlayed = (t) => t > 0 && t < OPX_FX_TYPES.length && OPX_FX_TYPES[t][2] === 0;
export function pfxText(k, code) {
  const [, lo, , dp, unit, , names] = k, v = code + lo;
  if (names) return names[code] ?? "?";
  if (lo === -850 && v <= -850) return "-INF " + unit;
  const s = dp < 0 ? String(v * 10) : dp === 0 ? String(v) : (v / 10 ** dp).toFixed(dp);
  return unit ? `${s} ${unit}` : s;
}
export function fxSetType(pk, n, t) {
  const ty = OPX_FX_TYPES[t] ?? OPX_FX_TYPES[0];
  put(pk, `fx${n}.type`, t);
  "bcef".split("").forEach((c, j) => put(pk, `fx${n}.${c}`, j < ty[3].length ? ty[3][j][5] - ty[3][j][1] : 0));
}
export function vpPlayed(d) {
  if (d >= 1 && d < 17) return d - 1 <= 8 && d - 1 !== 4;
  if (d >= 17 && d < 251) return (d - 17) % 39 <= 9;
  if (d >= 251 && d < 263) return true;
  if (d >= 263 && d < 275) return (d - 263) % 4 !== 0;
  if (d >= 275 && d < 277) return true;               // (M5b: FILTER CUTOFF, RESO)
  return false;
}
