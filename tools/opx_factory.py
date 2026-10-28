#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""OPX fork: the OPX factory bank, written in the units one thinks in (ms, Hz, dB, ratios), compiled to the
firmware's tables.

    python3 tools/opx_factory.py          # writes firmware/src/opx_factory.h and opx_factory_cat.h
    python3 tools/opx_factory.py --check  # fails if those files are not what the bank makes (tests/run_tests.sh)

The patches are in tools/opx_factory_bank.py (one call of P() each). The first NORMAL of them are in both firmwares;
the rest only in OPX Solo (FELUCCA_OPX_ONLY), whose flash has the room. SLOT stores the patch's number in a
project byte (project.c: -64..127) with OWN and INIT after the factory patches, so there are at most 126.

The conversions follow the firmware (eng_opx.c):
  TIME10  envelope times: 0 = 0 ms, c = 1 .. 1023: 1 ms x 90000^((c - 1) / 1022)
  CUT10   the main filter's cutoff: 6.489 Hz x (23680 / 6.489)^(c / 1023)
  HZ10    an LFO's speed: 0.01 Hz x 10000^(c / 1023)
  LEVEL   (L / 100)^3 in amplitude; a modulator at 100 and a full envelope: 4 pi rad
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import opx_patch_spec as S  # noqa: E402

MAXN = 126                     # (SLOT: F1..F126, OWN 126, INIT 127: a project byte holds 127 at most)
NSTRUCT = 8                    # (F1..F8 as opx_patch_t: a track plays them before it has its own copy; the rest records)
NORMAL = 8                     # (the first NORMAL in both firmwares: OPX's flash has room for that many, ~1 KB to spare)


class C(int):
    """a raw code (TIME10, CUT10, HZ10, coarse index) where a unit value would otherwise be converted"""


def t10(ms):
    if isinstance(ms, C):
        return int(ms)
    if ms <= 0:
        return 0
    return max(1, min(1023, round(1 + 1022 * math.log(max(ms, 1.0)) / math.log(90000))))


def cut10(hz):
    if isinstance(hz, C):
        return int(hz)
    return max(0, min(1023, round(1023 * math.log(hz / 6.489) / math.log(23680 / 6.489))))


def hz10(hz):
    if isinstance(hz, C):
        return int(hz)
    return max(0, min(1023, round(1023 * math.log10(hz / 0.01) / 4)))


COARSE = [1 / 128, 1 / 64, 1 / 32, 1 / 16, 1 / 8, 1 / 4, 1 / 2] + list(range(1, 33))


def ratio(r):
    """a ratio -> (coarse index, fine x0.001): the coarse step at or below it, FINE the rest (0.5 .. 2.0)"""
    if isinstance(r, tuple):
        return r
    best = None
    for i, c in enumerate(COARSE):
        f = r / c
        if 0.5 <= f <= 2.0 and (best is None or abs(f - 1) < abs(best[1] / 1000 - 1)):
            best = (i, round(f * 1000))
    assert best, r
    return best


WAVES = ["SIN", "SIN12", "SIN8", "TRI", "SAW", "SAWHD", "SQR", "SQRHD", "ASAW3", "ASQR3", "ATRI3", "A12345", "A12",
         "A13", "A14", "A15", "A16", "A17", "A18", "NOISE_SH", "WHITE", "PINK", "BLUE"]
MODES = ["FM", "RING", "FILTER", "FILTER_FM", "FOLD", "EFFECT", "BYPASS", "MUTE"]
OPFLT = ["LPF", "HPF", "BPF", "BRF", "MG_LPF12", "MG_LPF24", "MG_HPF12", "MG_HPF24", "MG_BPF6", "MG_BPF12", "MS20_LPF",
         "MS20_HPF"]
OPFX = ["PEAK_EQ", "SHELV_EQ", "PHASER", "SHORT_DLY", "COMB", "DIST", "DRIVE", "DEC", "WAVESHAPER", "PUNCH"]
OPFXV = [(-500, 500, -180, 180), (-180, 180, -180, 180), (-500, 500, -100, 100), (1, 300, 0, 100),
         (-500, 500, -100, 100), (0, 100, -180, 180), (0, 100, -180, 180), (200, 25600, 200, 2400), (0, 59, 0, 100),
         (0, 100, -180, 180)]          # (eng_opx.c OPX_FXV)
MFLT = ["LPF12", "LPF24", "LPFMS", "LPFP6", "HPF12", "HPF24", "HPFMS", "BPF6", "BPF12", "BRF6", "BRF12"]
LFOW = ["TRI", "SAWD", "SAWU", "SQR", "SIN", "SH", "GTR", "XTRI", "XSAWD", "XSAWU", "TRI4", "TRI6", "SAW4D", "SAW6D",
        "SRT", "SRLT", "RL", "RT", "RLT", "TRIP", "SAWDP", "SAWUP", "SQRP"]
CTRL = ["OFF", "MW", "PW", "PWP", "PWM", "PEDAL", "NOTE", "VEL", "XVEL", "RVEL", "AT", "EG1", "EG2", "EG3", "LFO1", "LFO2",
        "LFO3"]
VSRC = {**{n: i for i, n in enumerate(CTRL[:10])}, "EG1": 10, "EG2": 11, "EG3": 12, "LFO1": 13, "LFO2": 14, "LFO3": 15,
        "AT": 16 + 2 * 119}
DO = ["PITCH", "COARSE", "OCT", "TRANS", "LEVEL", "ATK", "DEC", "REL", "SUS", "FB"]
DP = ["PITCH", "OCT", "TRANS", "LEVEL", "PAN", "ATK", "DEC", "REL", "SUS"]
VD_PROG, VD_OP = 1, 17
VD_EG = VD_OP + 6 * 39
VD_LFO = VD_EG + 12
VD_FLT = VD_LFO + 12
CATS = "BLPUKDFO"              # (category.c: B BASS, L LEAD, P PAD, U PLUCK, K KEYS, D DRUM, F FX, O OTHER)
PATS = {"ACID": 1, "OFFBEAT": 2, "MELODY": 3, "LEAD": 4, "PAD": 5, "KEYS": 6, "BELL": 7, "SUB": 8, "BEAT": 12,
        "ARP": 13}


def _idx(lst, v, what):
    if isinstance(v, int):
        return v
    assert v in lst, f"{what}: {v!r} not in {lst}"
    return lst.index(v)


def dest(d):
    """a V.PATCH destination: 'OP3.LEVEL', 'PROG.PITCH', 'FLT.CUTOFF', 'FLT.RESO', 'EG2.ATK', 'LFO1.SPEED'"""
    if isinstance(d, int):
        return d
    a, b = d.split(".")
    if a.startswith("OP"):
        return VD_OP + (int(a[2:]) - 1) * 39 + DO.index(b)
    if a == "PROG":
        return VD_PROG + DP.index(b)
    if a.startswith("EG"):
        return VD_EG + (int(a[2:]) - 1) * 4 + ["ATK", "DEC", "REL", "SUS"].index(b)
    if a.startswith("LFO"):
        return VD_LFO + (int(a[3:]) - 1) * 4 + ["WAVE", "SPEED", "PHASE", "FADE"].index(b)
    if a == "FLT":
        return VD_FLT + ["CUTOFF", "RESO"].index(b)
    raise ValueError(d)


# ------------------------------------------------------------------------------------------------- the patch parts --
def O(level, r=1, det=0, a=0, d=0, s=100, rl=0, c=0, fb=0, vel=0, wave="SIN", width=0, mode="FM", trans=0,
      hz=None, ktrk=None, pe=0, pe_c="OFF", pl=0, pl_c="OFF", pv=0, pv_dest=0, ll=0, ll_c="OFF", **m):
    """an operator. level 0..100; r a ratio (or hz= a fixed frequency); det cents; a d rl ms (C(code) raw), s %;
    c CURVE 0 LIN .. 10 EXP; fb FM FB %; vel LMOD VEL %; ktrk (low %, high %, center note[, low EXP, high EXP]): dB
    per semitone x100 below / above center; pe pl P MOD EG1 / LFO1 INT (semitones), ll LMOD LFO1 INT %.
    Modes' values (m): ring=(depth, shape); fold=(gain, oscmix, bias); flt=(type, cutoff st, reso, oscmix);
    fx=(type, oscmix, E, F) E F in the effect's units"""
    co, fi = (7, 1000) if hz else ratio(r)
    o = {"level": level, "coarse": co, "fine": fi, "detune": det, "trans": trans, "atk": t10(a), "dec": t10(d),
         "rel": t10(rl), "sus": s, "curve": c, "fb": fb, "vel": vel, "wave": _idx(WAVES, wave, "wave"),
         "width": width, "mode": _idx(MODES, mode, "mode")}
    if hz:
        o["fixed"], o["fixed_freq"] = 1, round(hz * 100)
    if ktrk:
        k = tuple(ktrk) + (0, 0)
        o.update(ktrk_low=k[0], ktrk_high=k[1], ktrk_center=k[2], ktrk_lcrv=k[3], ktrk_hcrv=k[4])
    if pe:
        o["pm_eg1"], o["pm_eg1_ctrl"] = round(pe * 100), _idx(CTRL, pe_c, "ctrl")
    if pl:
        o["pm_lfo1"], o["pm_lfo1_ctrl"] = round(pl * 100), _idx(CTRL, pl_c, "ctrl")
    if pv:
        o["pm_vel"], o["pm_dest"] = pv, pv_dest
    if ll:
        o["lm_lfo1"], o["lm_lfo1_ctrl"] = ll, _idx(CTRL, ll_c, "ctrl")
    if "ring" in m:
        o["ring_depth"], o["ring_shape"] = m.pop("ring")
    if "fold" in m:
        o["fold_gain"], o["fold_oscmix"], o["fold_bias"] = m.pop("fold")
    if "flt" in m:
        ty, st, re_, mix = m.pop("flt")
        o.update(flt_type=_idx(OPFLT, ty, "op filter"), flt_cutoff=round(st * 10), flt_reso=re_, flt_oscmix=mix)
    if "fx" in m:
        ty, mix, e, f = m.pop("fx")
        t = _idx(OPFX, ty, "op fx")
        lo = OPFXV[t]
        assert lo[0] <= e <= lo[1] and lo[2] <= f <= lo[3], (ty, e, f)
        o.update(fx_type=t, fx_oscmix=mix, fx_e=e - lo[0], fx_f=f - lo[2])
    assert not m, m
    assert 0 <= level <= 100 and 0 <= s <= 100 and 0 <= c <= 10 and 0 <= fb <= 100 and 0 <= vel <= 100, o
    assert -50 <= det <= 50 and 500 <= fi <= 2000, o
    return o


OFF = O(0)


def EG(a=0, d=0, s=0, rl=0, c=0):
    return {"atk": t10(a), "dec": t10(d), "rel": t10(rl), "sus": s, "curve": c}


def LFO(wave="TRI", hz=1.0, ks=0, fade=0, phase=0, sync=None):
    """KSYNC 0 OFF (free, the part's), 1 COMMON, 2 VOICE; fade ms; sync a note value ('1/16'): TEMPO on"""
    return {"wave": _idx(LFOW, wave, "lfo wave"), "speed": hz10(hz),
            "sync": S.SYNC_NAMES.index(sync) if sync else 0, "tempo": 1 if sync else 0, "ksync": ks,
            "phase": phase, "fade": fade}


def VP(src, d, amt, ctrl="OFF"):
    """src: 'MW', 'VEL', 'NOTE', 'EG1'.., 'LFO1'.., 'AT'; d a destination (dest()); amt % of the destination's whole
    range (OP LEVEL: points; FLT CUTOFF: 100 % = 142 semitones; PITCH: 100 % = 100 semitones)"""
    return {"src": VSRC[src], "ctrl": _idx(CTRL, ctrl, "ctrl"), "amt": round(amt * 100), "dst": dest(d)}


def FLT(ty="LPF24", hz=2000.0, reso=0, eg2=0, eg2_c="OFF", kh=0, kl=0, kc=60, lfo2=0, lfo2_c="OFF"):
    """the main filter: reso %, eg2 lfo2 semitones; KTRK kh kl: 64 = 1.00 (the key's own), 32 = 0.5"""
    return {"type": _idx(MFLT, ty, "filter"), "on": 1, "eg2_ctrl": _idx(CTRL, eg2_c, "ctrl"),
            "lfo2_ctrl": _idx(CTRL, lfo2_c, "ctrl"), "kc": kc, "kl": kl, "kh": kh, "cutoff": cut10(hz),
            "reso": round(reso * 100), "eg2": round(eg2 * 100), "lfo2": round(lfo2 * 100)}


FXN = [t[0] for t in S.FX_TYPES]


def FX(ty, *vals):
    """a program effect in its knobs' units (opx_patch_spec FX_TYPES): FX('PAN', 35, 'SINE', 3.5, 50).
    Only the types both firmwares play (kind 0)"""
    t = FXN.index(ty)
    _, _, kind, knobs = S.FX_TYPES[t]
    assert kind == 0, f"{ty}: not played by OPX (kind {kind})"
    assert len(vals) == len(knobs), (ty, vals)
    v = []
    for x, (lab, lo, hi, dp, unit, dflt, names) in zip(vals, knobs):
        if names and isinstance(x, str):
            x = names.index(x)
        elif dp == -1:
            x = round(x / 10)
        else:
            x = round(x * 10 ** dp)
        assert lo <= x <= hi, (ty, lab, x)
        v.append(x)
    return {"type": t, "on": 1, "v": v}


BANK = []


def _alg_out():
    """the heard operators of algorithms 1..40 (eng_opx.c OPX_ALG's out column)"""
    src = open(os.path.join(ROOT, "firmware/src/eng_opx.c")).read()
    tab = src[src.index("static const opx_alg_t OPX_ALG[OPX_NALG] = {"):]
    tab = tab[:tab.index("};")]
    outs = []
    for m in re.finditer(r"\{\{(.*?)\}, ([^,]+), \d+, [^}]+\},\s*/\* (\d+)", tab):
        e = m.group(2).strip()
        outs.append(0x3F if e == "0x3Fu" else sum(1 << (int(b) - 1) for b in re.findall(r"B\((\d)\)", e)))
    assert len(outs) == 40, len(outs)
    return outs


ALG_OUT = _alg_out()


def P(name, cat, algo, ops, *, fb=0, ua=None, out=None, eg=(), lfo=(), vp=(), flt=None, fx=(), plfo=0, plfo_c="OFF",
      trans=0, oct=0, sends=(0, 0, 0, 0), mono=0, pat="KEYS", gain=0.0, note=""):
    """a factory patch: algo 1..40 or 'USER' (ua: [[x][y]] op x+1 -> op y+1 %, out: heard operators as '1 2'),
    ops OP1..OP6; eg / lfo: EG1.. / LFO1.. in order (None: INIT's); sends: Felucca's DIST CHORUS DELAY REVERB
    (0..127, the track's FX knobs); pat: the demo pattern; gain dB on the heard operators (the bank's levels, matched
    by ear and by opx_test --audit)"""
    assert len(name) <= 10 and re.fullmatch(r"[A-Z0-9 .+\-/']+", name), name
    assert cat in CATS, cat
    assert len(ops) == 6, name
    heard = ALG_OUT[algo - 1] if algo != "USER" else sum(1 << (int(k) - 1) for k in out.split())
    if gain and os.environ.get("OPX_NOGAIN"):   # (tools: the levels as written)
        gain = 0
    if gain:                   # (gain dB: the heard operators' LEVELs, (L / 100)^3 in amplitude, at most 100)
        ops = [dict(o, level=min(100, round(o["level"] * 10 ** (gain / 60)))) if heard >> k & 1 and o["level"] else o
               for k, o in enumerate(ops)]
    assert all(p["name"] != name for p in BANK), f"duplicate {name}"
    BANK.append({"name": name, "cat": cat, "algo": 40 if algo == "USER" else algo - 1, "alg_fb": fb,
                 "ua": ua or [[0] * 6 for _ in range(6)],
                 "ua_out": sum(1 << (int(k) - 1) for k in out.split()) if out else 0, "ops": ops,
                 "eg": list(eg), "lfo": list(lfo), "vp": list(vp), "flt": flt, "fx": list(fx),
                 "prog_lfo1": round(plfo * 100), "prog_lfo1_ctrl": _idx(CTRL, plfo_c, "ctrl"), "trans": trans,
                 "oct": oct, "sends": sends, "mono": mono, "pat": PATS[pat] if isinstance(pat, str) else pat,
                 "note": note})


# ------------------------------------------------------------------------------------ the packed record (sparse) --
def encode(p):
    """the record firmware/src/opx_patch.c opx_encode makes of the patch, as {field name: value} (the spec's names)"""
    S.build_fields() if not S.FIELDS else None
    r = S.defaults()
    r["algo"], r["prog.alg_fb"] = p["algo"], p["alg_fb"]
    for k, o in enumerate(p["ops"]):
        n = f"op{k + 1}."
        g = lambda key, d=0: o.get(key, d)  # noqa: E731
        r.update({n + "level": g("level"), n + "coarse": g("coarse"), n + "fine": g("fine"), n + "detune": g("detune"),
                  n + "trans": g("trans"), n + "freq_fixed": g("fixed"),
                  n + "fixed_freq": g("fixed_freq") if g("fixed") else 44000, n + "eg_atk": g("atk"),
                  n + "eg_dec": g("dec"), n + "eg_rel": g("rel"), n + "eg_sus": g("sus"), n + "eg_curve": g("curve"),
                  n + "fm_fb": g("fb"), n + "lmod_vel": g("vel"), n + "wave": g("wave"), n + "fm_width": g("width"),
                  n + "mode": g("mode")})
        md = MODES[g("mode")]
        if md == "RING":
            r.update({n + "ring_depth": g("ring_depth"), n + "ring_shape": g("ring_shape")})
        elif md in ("FILTER", "FILTER_FM"):
            r.update({n + "flt_type": g("flt_type"), n + "flt_reso": g("flt_reso"), n + "flt_oscmix": g("flt_oscmix"),
                      n + "flt_cutoff": g("flt_cutoff")})
        elif md == "EFFECT":
            r.update({n + "fx_type": g("fx_type"), n + "fx_oscmix": g("fx_oscmix"), n + "fx_e": g("fx_e"),
                      n + "fx_f": g("fx_f")})
        elif md == "FOLD":
            r.update({n + "fold_gain": g("fold_gain"), n + "fold_oscmix": g("fold_oscmix"), n + "fold_bias": g("fold_bias")})
        if g("ktrk_center"):
            r.update({n + "ktrk_low": g("ktrk_low"), n + "ktrk_high": g("ktrk_high"), n + "ktrk_center": g("ktrk_center"),
                      n + "ktrk_lcrv": g("ktrk_lcrv"), n + "ktrk_hcrv": g("ktrk_hcrv")})
        r.update({n + "pmod_eg1": g("pm_eg1"), n + "pmod_eg1_ctrl": g("pm_eg1_ctrl"), n + "pmod_lfo1": g("pm_lfo1"),
                  n + "pmod_lfo1_ctrl": g("pm_lfo1_ctrl"), n + "pmod_vel": g("pm_vel"), n + "pmod_vel_dest": g("pm_dest"),
                  n + "lmod_lfo1": g("lm_lfo1"), n + "lmod_lfo1_ctrl": g("lm_lfo1_ctrl")})
        for j in range(6):
            r[f"ua.op{k + 1}_self" if j == k else f"ua.op{k + 1}_to_op{j + 1}"] = p["ua"][k][j]
        r[f"ua.op{k + 1}_out"] = p["ua_out"] >> k & 1
    for k in range(3):
        e = p["eg"][k] if k < len(p["eg"]) else None
        if e and any(e.values()):
            r.update({f"eg{k + 1}.{x}": e[x] for x in ("atk", "dec", "rel", "sus", "curve")})
        l = p["lfo"][k] if k < len(p["lfo"]) else None
        if l and (l["speed"] or l["tempo"]):
            r.update({f"lfo{k + 1}.{x}": l[x] for x in ("wave", "speed", "sync", "ksync", "phase", "tempo", "fade")})
    for k, v in enumerate(p["vp"]):
        if v["src"]:
            r.update({f"vp{k + 1}.src": v["src"], f"vp{k + 1}.int": v["amt"], f"vp{k + 1}.ctrl": v["ctrl"],
                      f"vp{k + 1}.dst": v["dst"]})
    f = p["flt"]
    if f:
        r.update({"flt.type": f["type"], "flt.enable": 1, "flt.cutoff": f["cutoff"], "flt.reso": f["reso"],
                  "flt.eg2": f["eg2"], "flt.eg2_ctrl": f["eg2_ctrl"], "flt.ktrk_low": f["kl"], "flt.ktrk_high": f["kh"],
                  "flt.ktrk_center": f["kc"] or 60, "flt.lfo2": f["lfo2"], "flt.lfo2_ctrl": f["lfo2_ctrl"]})
    r["prog.lfo1"], r["prog.lfo1_ctrl"], r["prog.trans"], r["prog.oct"] = (p["prog_lfo1"], p["prog_lfo1_ctrl"],
                                                                         p["trans"], p["oct"])
    for k in range(3):
        x = p["fx"][k] if k < len(p["fx"]) else {"type": 0, "on": 0, "v": []}
        knobs = S.FX_TYPES[x["type"]][3]
        r[f"fx{k + 1}.type"], r[f"fx{k + 1}.enable"] = x["type"], x["on"]
        for j, kn in enumerate(knobs):
            r[f"fx{k + 1}.{'bcef'[j]}"] = x["v"][j] - kn[1]
    for i in range(16):
        r[f"prog.name{i}"] = ord(p["name"][i]) if i < len(p["name"]) else 32
    return r


def sparse(p):
    """the record as (field index, value) for the fields off their defaults: field deltas and zigzag values as
    varints (opx_patch.c opx_factory_pk reads them)"""
    rec, d, out, last = encode(p), S.defaults(), bytearray(), 0
    for i, (_, name, _, lo, hi, _) in enumerate(S.FIELDS):
        v = rec[name]
        assert lo <= v <= hi, (p["name"], name, v)
        if v == d[name]:
            continue
        for x in (i - last, (-v << 1) - 1 if v < 0 else v << 1):
            while True:
                out.append((x & 0x7F) | (0x80 if x > 0x7F else 0))
                x >>= 7
                if not x:
                    break
        last = i
    return bytes(out)


# ------------------------------------------------------------------------------------------------------ the output --
OPF = ["level", "coarse", "fine", "detune", "trans", "fixed", "fixed_freq", "atk", "dec", "rel", "sus", "curve", "fb",
       "vel", "wave", "width", "mode", "ring_depth", "ring_shape", "fold_gain", "fold_oscmix", "fold_bias", "flt_type",
       "flt_reso", "flt_oscmix", "flt_cutoff", "fx_type", "fx_oscmix", "fx_e", "fx_f", "ktrk_low", "ktrk_high",
       "ktrk_center", "ktrk_lcrv", "ktrk_hcrv", "pm_eg1", "pm_lfo1", "pm_vel", "pm_dest", "lm_lfo1", "pm_eg1_ctrl",
       "pm_lfo1_ctrl", "lm_lfo1_ctrl"]


def c_op(o):
    return "{" + ", ".join(f".{k} = {o[k]}" for k in OPF if o.get(k)) + "}"


def c_struct(d, keys):
    return "{" + ", ".join(f".{k} = {d[k]}" for k in keys if d.get(k)) + "}"


def c_patch(p):
    L = [f'    {{.name = "{p["name"]}", .algo = {p["algo"]}, .alg_fb = {p["alg_fb"]},   /* {p["note"]} */']
    if any(any(r) for r in p["ua"]):
        L.append("     .ua = {" + ", ".join("{" + ", ".join(map(str, r)) + "}" for r in p["ua"]) + "}, "
                 f".ua_out = {p['ua_out']},")
    L.append("     .op = {" + ",\n            ".join(c_op(o) for o in p["ops"]) + "},")
    if p["eg"]:
        L.append("     .eg = {" + ", ".join(c_struct(e, ["atk", "dec", "rel", "sus", "curve"]) if e else "{0}"
                                          for e in p["eg"]) + "},")
    if p["lfo"]:
        L.append("     .lfo = {" + ", ".join(c_struct(e, ["wave", "speed", "sync", "tempo", "ksync", "phase", "fade"])
                                           if e else "{0}" for e in p["lfo"]) + "},")
    for k in ("prog_lfo1", "prog_lfo1_ctrl"):
        if p[k]:
            L.append(f"     .{k} = {p[k]},")
    if p["trans"]:
        L.append(f"     .prog_trans = {p['trans']},")
    if p["oct"]:
        L.append(f"     .prog_oct = {p['oct']},")
    if p["vp"]:
        L.append("     .vp = {" + ", ".join(c_struct(v, ["src", "ctrl", "amt", "dst"]) for v in p["vp"]) + "},")
    if p["flt"]:
        L.append("     .flt = " + c_struct(p["flt"], ["type", "on", "eg2_ctrl", "lfo2_ctrl", "kc", "kl", "kh", "cutoff",
                                                       "reso", "eg2", "lfo2"]) + ",")
    if p["fx"]:
        L.append("     .fx = {" + ", ".join(f"{{.type = {f['type']}, .on = 1, .v = {{{', '.join(map(str, f['v']))}}}}}"
                                          for f in p["fx"]) + "},")
    L[-1] = L[-1].rstrip(",") + "},"
    return "\n".join(L)


def c_preset(i, p):
    d, c, dl, r = p["sends"]
    return (f'    {{"{p["name"]}", {{0, 0, 0, 0, 0, 0, 0, {i}}}, {{0, 0, 127, 0}}, 0, {p["mono"]}, '
            f'FX({d}, {c}, {dl}, {r}), PAT({p["pat"]})}},')


HEAD = ("/* SPDX-License-Identifier: GPL-3.0-only */\n"
        "/* GENERATED by tools/opx_factory.py from tools/opx_factory_bank.py: edit the bank there, then run it. */\n")


def c_bytes(b, ind="    "):
    return "".join(ind + ", ".join(str(x) for x in b[i:i + 24]) + ",\n" for i in range(0, len(b), 24))


def outputs():
    import opx_factory as M        # (the bank fills the module's BANK, also when this file runs as __main__)
    import opx_factory_bank  # noqa: F401
    BANK = M.BANK
    S.build_fields() if not S.FIELDS else None
    assert len(BANK) <= MAXN and NSTRUCT <= NORMAL <= len(BANK), len(BANK)
    n = len(BANK)
    pat = [c_patch(p) for p in BANK[:NSTRUCT]]
    pre = []
    rec, off, chk, coff = [], [0], [], [0]
    for i, p in enumerate(BANK):
        if i == NORMAL:
            pre.append("#if FELUCCA_OPX_ONLY          /* (the rest: OPX Solo's) */")
        pre.append(c_preset(i, p))
        sp = sparse(p)
        if i < NSTRUCT:
            chk.append(sp)
            coff.append(coff[-1] + len(sp))
        else:
            rec.append((i, sp))
            off.append(off[-1] + len(sp))
    if n > NORMAL:
        pre.append("#endif")
    k = NORMAL - NSTRUCT                 # (records in both firmwares; the rest Solo's)
    recs = "".join(f"    /* F{i + 1} {BANK[i]['name']} */\n" + c_bytes(b, "    ") for i, b in rec[:k])
    recs_solo = "".join(f"    /* F{i + 1} {BANK[i]['name']} */\n" + c_bytes(b, "    ") for i, b in rec[k:])
    def slots(k):
        v = [f'"F{i + 1}"' for i in range(k)] + ['"-"'] * (MAXN - k)
        return "".join("    " + ", ".join(v[i:i + 16]) + ",\n" for i in range(0, MAXN, 16))
    h = (HEAD + f"/* The OPX factory patches: F1..F{NSTRUCT} as opx_patch_t (eng_opx.c OPX_FACTORY: what a track plays "
         f"before it has its own copy), the rest as records of their fields off the defaults (OPX_FREC, OPX_FOFF: "
         f"opx_patch.c opx_factory_pk; {sum(len(b) for _, b in rec)} bytes for {len(rec)}, ~440 each as structs); the first "
         f"{NORMAL} in every OPX build, all {n} in OPX Solo. Their PRESETS entries (eng_opx.c OPX_PRESETS: SLOT "
         "= the patch) and the SLOT names. */\n"
         "#ifdef OPX_FACTORY_PATCHES\n" + "\n".join(pat) + "\n#endif\n"
         "#ifdef OPX_FACTORY_RECORDS        /* (each field: its index less the last one's, then the value zigzagged; "
         "varints) */\n"
         "static const uint8_t OPX_FREC[] = {\n" + recs + "#if FELUCCA_OPX_ONLY\n" + recs_solo + "#endif\n    0,   /* (an end: never empty) */\n};\n"
         "static const uint16_t OPX_FOFF[] = {\n" + c_bytes(off[:k + 1]) + "#if FELUCCA_OPX_ONLY\n" + c_bytes(off[k + 1:])
         + "#endif\n};\n#endif\n"
         "#if defined OPX_FACTORY_CHECK && defined OPX_FACTORY_RECORDS   /* (host tests: F1..F" + str(NSTRUCT) + "'s records) */\n"
         "static const uint8_t OPX_FREC_CHECK[] = {\n" + c_bytes(b"".join(chk)) + "};\n"
         "static const uint16_t OPX_FOFF_CHECK[] = {\n" + c_bytes(coff) + "};\n#endif\n"
         "#ifdef OPX_FACTORY_PRESETS\n" + "\n".join(pre) + "\n#endif\n"
         "#ifdef OPX_FACTORY_SLOTS          /* (a name per value 0..125; the ones a firmware lacks: -) */\n"
         + "#if FELUCCA_OPX_ONLY\n" + slots(n) + "#else\n" + slots(min(n, NORMAL)) + "#endif\n#endif\n")
    cats = "".join(p["cat"] for p in BANK)
    hc = (HEAD + "/* the OPX factory patches' categories (category.c PRESET_CAT[15]): a letter each, in their order */\n"
          "#ifndef FELUCCA_OPX_ONLY\n#define FELUCCA_OPX_ONLY 0\n#endif\n#if FELUCCA_OPX_ONLY\n"
          f'#define OPX_CAT "{cats}"\n#else\n#define OPX_CAT "{cats[:NORMAL]}"\n#endif\n')
    return {"firmware/src/opx_factory.h": h, "firmware/src/opx_factory_cat.h": hc}


def main():
    import opx_factory as M
    out = outputs()
    BANK = M.BANK
    if "--check" in sys.argv:
        bad = [f for f, t in out.items() if not os.path.exists(os.path.join(ROOT, f)) or
               open(os.path.join(ROOT, f)).read() != t]
        if bad:
            sys.exit("opx_factory: out of date (run tools/opx_factory.py): " + ", ".join(bad))
        print(f"opx_factory: {len(BANK)} patches, the tables up to date")
        return
    for f, t in out.items():
        with open(os.path.join(ROOT, f), "w") as fh:
            fh.write(t)
    print(f"opx_factory: {len(BANK)} patches ({NORMAL} in OPX) -> " + ", ".join(out))


if __name__ == "__main__":
    main()
