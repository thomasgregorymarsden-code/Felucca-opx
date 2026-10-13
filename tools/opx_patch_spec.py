#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""OPX patch format: the bit-level layout of one OPX (Opsix-style altered FM) program.

This file is the single source of truth for the layout. It
  - lists every stored field with its bit width and value range (FIELDS),
  - packs a patch (a dict of field -> value) into bytes and unpacks it again,
  - prints a size report by section (--report),
  - runs a randomized round-trip test (--test).
The firmware's C layout is to be generated from FIELDS, never written by hand.

Ranges and resolutions follow the opsix Owner's Manual parameter guide (pp. 34-41, 83-91, the manual for the
firmware with 23 waveforms and the EFFECT / BYPASS / MUTE operator modes). Where the manual gives no resolution
(times, LFO speeds, cutoff), the field stores a code on a documented curve, fine enough to be inaudible:
  TIME10  0 = 0 ms; 1..1023 = 1 ms * 90000^((c-1)/1022), steps of 1.1 %       (EG times 0 ms .. 90 s)
  HZ10    0.01 Hz * 10000^(c/1023), steps of 0.9 %                          (LFO speed 0.01 .. 100 Hz)
  CUT10   6.489 Hz * (23680/6.489)^(c/1023), steps of 0.14 semitone          (main filter cutoff)
Every other field stores the manual's displayed value exactly, as an integer offset from its minimum
(value - lo), in the fewest bits that hold hi - lo.

Bits are written least significant first into a little-endian bit stream; the record is padded to a whole byte.
"""
import argparse
import random
import sys

FORMAT_VERSION = 1

# --- value lists (sizes from the manual) ---
OP_MODES = ["FM", "RING", "FILTER", "FILTER_FM", "FOLD", "EFFECT", "BYPASS", "MUTE"]   # p.34
NWAVES = 23            # Waveform List p.84 (SIN .. NOISE BLUE)
NOPFILT = 12           # operator filter types p.34 (LPF .. MS-20 HPF)
NOPFX = 10             # operator effect types p.34 (PEAK EQ .. PUNCH)
NCOARSE = 39           # coarse ratio 1/128, 1/64 .. 1/2, 1 .. 32
NCTRL = 17             # Control Source List p.85, + OFF
NLFOWAVES = 23         # LFO Wave List p.55
NSYNC = 20             # LFO tempo-sync values 1/48 .. 4/1 (count not listed in the manual: room for 32)
NMAINFILT = 11         # main filter types p.38
NFX = 31               # 30 effect types + OFF (Effect Parameter List p.91..)
NALGO = 41             # 40 preset algorithms + USER
NVP_SRC = 256          # V.Patch sources p.88: OFF 1, CTRL 5, KBD 4, EG/LFO 6, CC+ 119, CC+/- 119, MIDI 2
NVP_DST = 347          # V.Patch destinations p.89-90: OFF, PROG 16, OP1..6 x 48, EG x 3 x 4, LFO x 3 x 4,
                       # FILTER 2, FX x 3 x 4, ARP 3, SEQ 1 (the count is a reading of the list: room for 512)

FIELDS = []            # (section, name, bits, lo, hi, note)


def bits_for(lo, hi):
    return max(1, (hi - lo).bit_length())


def f(section, name, lo, hi, note="", bits=None):
    b = bits_for(lo, hi) if bits is None else bits
    assert (hi - lo) < (1 << b), name
    FIELDS.append((section, name, b, lo, hi, note))


def build_fields(all_mode_values=True):
    FIELDS.clear()
    f("header", "version", 0, 255, "FORMAT_VERSION")
    f("header", "flags", 0, 255, "bit 0: the program extras block holds values (else Felucca's track settings apply)")
    for o in range(1, 7):
        s = f"op{o}"
        f(s, f"{s}.mode", 0, len(OP_MODES) - 1, "/".join(OP_MODES))
        f(s, f"{s}.wave", 0, NWAVES - 1)
        modes = all_mode_values
        # MODE pages p.43-49. With all_mode_values every mode keeps its own values (as a program does when its
        # mode is switched back); otherwise only the active mode's (smaller, but a mode switch resets them).
        if modes:
            f(s, f"{s}.fm_fb", 0, 100, "%")
            f(s, f"{s}.fm_width", 0, 99, "0 = OFF, 1..99 %")
            f(s, f"{s}.ring_depth", 0, 100, "%")
            f(s, f"{s}.ring_shape", 0, 100, "%")
            f(s, f"{s}.flt_type", 0, NOPFILT - 1, "FILTER and FILTER FM")
            f(s, f"{s}.flt_cutoff", -500, 500, "x0.1 semitone, FILTER and FILTER FM")
            f(s, f"{s}.flt_reso", 0, 100, "%")
            f(s, f"{s}.flt_oscmix", 0, 100, "%, FILTER")
            f(s, f"{s}.fold_gain", 0, 100, "%")
            f(s, f"{s}.fold_oscmix", 0, 100, "%")
            f(s, f"{s}.fold_bias", -100, 100, "%")
            f(s, f"{s}.fx_type", 0, NOPFX - 1)
            f(s, f"{s}.fx_oscmix", 0, 100, "%")
            f(s, f"{s}.fx_e", 0, 32767, "the effect type's E value, in its own units (DEC FREQ needs 15 bits)")
            f(s, f"{s}.fx_f", 0, 32767, "the effect type's F value")
        else:
            f(s, f"{s}.mode_type", 0, 15, "filter type or effect type, by mode")
            f(s, f"{s}.mode_a", 0, 32767, "mode values A..D, meaning by mode")
            f(s, f"{s}.mode_b", 0, 32767)
            f(s, f"{s}.mode_c", 0, 1023)
            f(s, f"{s}.mode_d", 0, 127)
        # PITCH p.50-51: both the ratio and the fixed settings are kept (FREQ switches between them)
        f(s, f"{s}.freq_fixed", 0, 1, "0 RATIO, 1 FIXED")
        f(s, f"{s}.trans", -12, 12, "semitones")
        f(s, f"{s}.coarse", 0, NCOARSE - 1, "1/128 .. 1/2, 1 .. 32")
        f(s, f"{s}.fine", 500, 2000, "x0.001 (0.500 .. 2.000)")
        f(s, f"{s}.detune", -50, 50, "cents (one-cent steps, p.51)")
        f(s, f"{s}.fixed_freq", 1, 999999, "x0.01 Hz (0.01 .. 9999.99)")
        # P MOD p.51
        f(s, f"{s}.pmod_eg1", -14400, 14400, "x0.01 semitone")
        f(s, f"{s}.pmod_lfo1", -14400, 14400, "x0.01 semitone")
        f(s, f"{s}.pmod_vel", 0, 100, "%")
        f(s, f"{s}.pmod_eg1_ctrl", 0, NCTRL - 1)
        f(s, f"{s}.pmod_lfo1_ctrl", 0, NCTRL - 1)
        f(s, f"{s}.pmod_vel_dest", 0, 1, "FINE, COARSE")
        # EG p.52 (the operator's level envelope)
        f(s, f"{s}.eg_atk", 0, 1023, "TIME10")
        f(s, f"{s}.eg_dec", 0, 1023, "TIME10")
        f(s, f"{s}.eg_rel", 0, 1023, "TIME10")
        f(s, f"{s}.level", 0, 100, "%")
        f(s, f"{s}.eg_sus", 0, 100)
        f(s, f"{s}.eg_curve", 0, 10, "0 LIN .. 10 EXP")
        # KTRK p.53
        f(s, f"{s}.ktrk_low", -100, 100, "%")
        f(s, f"{s}.ktrk_center", 21, 108, "MIDI note, A0 .. C9")
        f(s, f"{s}.ktrk_high", -100, 100, "%")
        f(s, f"{s}.ktrk_lcrv", 0, 1, "LIN, EXP")
        f(s, f"{s}.ktrk_hcrv", 0, 1, "LIN, EXP")
        # LMOD p.54
        f(s, f"{s}.lmod_lfo1", -100, 100, "%")
        f(s, f"{s}.lmod_vel", 0, 100, "%")
        f(s, f"{s}.lmod_lfo1_ctrl", 0, NCTRL - 1)
    # algorithm, HOME p.42, USER ALG p.74
    f("algorithm", "algo", 0, NALGO - 1, "0..39 presets, 40 USER")
    f("algorithm", "home_atk", -1000, 1000, "x0.1 %, all operators' attack")
    f("algorithm", "home_decay", -1000, 1000, "x0.1 %, all operators' decay / release")
    for x in range(1, 7):
        for y in range(1, 7):
            if x != y:
                f("user_alg", f"ua.op{x}_to_op{y}", 0, 100, "%")
        f("user_alg", f"ua.op{x}_self", 0, 100, "% self feedback")
        f("user_alg", f"ua.op{x}_out", 0, 1, "direct out")
    # EG1 PITCH, EG2 FLTR, EG3 p.55
    for e in range(1, 4):
        s = f"eg{e}"
        f("egs", f"{s}.atk", 0, 1023, "TIME10")
        f("egs", f"{s}.dec", 0, 1023, "TIME10")
        f("egs", f"{s}.rel", 0, 1023, "TIME10")
        f("egs", f"{s}.sus", 0, 100)
        f("egs", f"{s}.curve", 0, 10)
    # LFO1 OP, LFO2 FLTR, LFO3 p.55-57
    for l in range(1, 4):
        s = f"lfo{l}"
        f("lfos", f"{s}.wave", 0, NLFOWAVES - 1)
        f("lfos", f"{s}.speed", 0, 1023, "HZ10 (TEMPO off)")
        f("lfos", f"{s}.sync", 0, 31, "note value 1/48 .. 4/1 (TEMPO on)")
        f("lfos", f"{s}.ksync", 0, 2, "OFF, COMMON, VOICE")
        f("lfos", f"{s}.phase", -181, 180, "degrees; -181 = RANDOM")
        f("lfos", f"{s}.tempo", 0, 1)
        f("lfos", f"{s}.fade", 0, 9900, "ms")
    # FILTER, FLTR MOD p.58-60
    f("filter", "flt.type", 0, NMAINFILT - 1)
    f("filter", "flt.enable", 0, 1)
    f("filter", "flt.cutoff", 0, 1023, "CUT10")
    f("filter", "flt.reso", 0, 10000, "x0.01 %")
    f("filter", "flt.eg2", -14200, 14200, "x0.01")
    f("filter", "flt.eg2_ctrl", 0, NCTRL - 1)
    f("filter", "flt.ktrk_low", -127, 127, "slope; +-127 = +-Inf (the manual gives no steps)")
    f("filter", "flt.ktrk_center", 0, 127, "MIDI note, C-1 .. G9")
    f("filter", "flt.ktrk_high", -127, 127, "slope; +-127 = +-Inf")
    f("filter", "flt.lfo2", -14200, 14200, "x0.01")
    f("filter", "flt.lfo2_ctrl", 0, NCTRL - 1)
    # EFFECT FX1..3 p.61, 91..: the active type's four values (each type's own units and range)
    for x in range(1, 4):
        s = f"fx{x}"
        f("effects", f"{s}.type", 0, NFX - 1, "0 = OFF")
        f("effects", f"{s}.enable", 0, 1)
        for k in "bcef":
            f("effects", f"{s}.{k}", 0, 32767, "knob value in the type's units (SPEED 0.020..20.000 Hz needs 15 bits)")
    # V.PATCH 1..12 p.71
    for v in range(1, 13):
        s = f"vp{v}"
        f("vpatch", f"{s}.src", 0, NVP_SRC - 1, "scope and source as one index")
        f("vpatch", f"{s}.int", -10000, 10000, "x0.01, meaning by destination")
        f("vpatch", f"{s}.ctrl", 0, NCTRL - 1)
        f("vpatch", f"{s}.dst", 0, 511, "scope and destination as one index")
    # PROG PITCH p.72, PROG MISC p.73
    f("program", "prog.trans", -12, 12)
    f("program", "prog.bend_up", -60, 60)
    f("program", "prog.bend_down", -60, 60)
    f("program", "prog.lfo1", -14400, 14400, "x0.01 semitone")
    f("program", "prog.lfo1_ctrl", 0, NCTRL - 1)
    f("program", "prog.oct", -2, 2)
    f("program", "prog.level", -1000, 60, "x0.1 dB; -1000 = -Inf")
    f("program", "prog.alg_fb", 0, 100, "%")
    f("program", "prog.phase", 0, 2, "SYNC, FREE, RANDOM")
    f("program", "prog.lofi", 0, 1)
    for i in range(16):
        f("program", f"prog.name{i}", 32, 126, "ASCII")
    # program extras: the opsix's own voice, arpeggiator and sequencer settings (p.62-70). A Felucca build uses its
    # tracks' settings for these and leaves the block at its defaults (flags bit 0 clear); a stand-alone build uses it.
    f("extras", "x.assign", 0, 2, "POLY, MONO, MONO LEGATO")
    f("extras", "x.glide", 0, 2, "OFF, ON, LEGATO")
    f("extras", "x.glide_time", 0, 50000, "ms")
    f("extras", "x.unison", 0, 7, "0 = OFF, 1..7 = 2..8 voices")
    f("extras", "x.uni_detune", 0, 200, "cents")
    f("extras", "x.uni_spread", 0, 100, "%")
    f("extras", "x.arp_run", 0, 2, "OFF, ON, LATCH")
    f("extras", "x.arp_ptrn", 0, 6)
    f("extras", "x.arp_reso", 0, 7, "1/32 triplet .. 1/4")
    f("extras", "x.arp_gate", 0, 100, "%")
    f("extras", "x.arp_oct", 1, 4)
    f("extras", "x.seq_tempo", 4000, 30000, "x0.01 BPM")
    f("extras", "x.seq_reso", 0, 15, "1/64 .. 4/1")
    f("extras", "x.seq_swing", -100, 100, "%")
    f("extras", "x.seq_mode", 0, 5)
    f("extras", "x.seq_len", 1, 16)
    f("extras", "x.seq_active", 0, 2, "BOTH, NOTE, MTN")
    return FIELDS


# defaults (an INIT program): a field not listed is 0 when its range holds 0, else its lowest value
DEFAULTS = {"version": FORMAT_VERSION, "algorithm": None}
for _o in range(1, 7):
    DEFAULTS.update({f"op{_o}.coarse": 7, f"op{_o}.fine": 1000, f"op{_o}.fixed_freq": 44000, f"op{_o}.level": 100,
                     f"op{_o}.eg_sus": 100, f"op{_o}.ktrk_center": 60, f"op{_o}.fm_width": 0})
for _e in range(1, 4):
    DEFAULTS[f"eg{_e}.sus"] = 100
for _l in range(1, 4):
    DEFAULTS.update({f"lfo{_l}.speed": 512, f"lfo{_l}.sync": 8})
DEFAULTS.update({"flt.cutoff": 1023, "flt.ktrk_center": 60, "prog.bend_up": 2, "prog.bend_down": -2,
                 "x.arp_gate": 75, "x.arp_oct": 1, "x.seq_tempo": 12000, "x.seq_len": 16})
for _i in range(16):
    DEFAULTS[f"prog.name{_i}"] = ord("INIT"[_i]) if _i < 4 else 32
del DEFAULTS["algorithm"]


def default_of(name, lo, hi):
    if name in DEFAULTS:
        return DEFAULTS[name]
    return 0 if lo <= 0 <= hi else lo


def defaults():
    return {name: default_of(name, lo, hi) for _, name, _, lo, hi, _ in FIELDS}


def c_name(name):
    return "OPXF_" + name.upper().replace(".", "_")


def emit_c(path):
    """the firmware's layout: field ids, bit offsets, widths, ranges, defaults (firmware/src/opx_patch.c reads it)"""
    off = 0
    rows, ids = [], []
    for _, name, b, lo, hi, _ in FIELDS:
        d = default_of(name, lo, hi)
        assert lo <= d <= hi, name
        rows.append(f"    {{{off}u, {b}u, {lo}, {hi}, {d}}},   /* {name} */")
        ids.append(f"    {c_name(name)},")
        off += b
    out = ["/* generated by tools/opx_patch_spec.py --emit-c: do not edit */",
           "#ifndef OPX_FIELDS_H", "#define OPX_FIELDS_H",
           f"#define OPX_FORMAT_VERSION {FORMAT_VERSION}",
           f"#define OPX_PK_BITS {off}u", f"#define OPX_PK_BYTES {(off + 7) // 8}u",
           "enum {", *ids, "    OPXF_COUNT", "};",
           "typedef struct { uint16_t off; uint8_t bits; int32_t lo, hi, def; } opx_field_t;",
           "static const opx_field_t OPX_FIELDS[OPXF_COUNT] = {", *rows, "};",
           "#ifdef OPX_FIELD_NAMES                 /* (tests: tests/opx_test.c --dump, checked by --check-c) */",
           "static const char *const OPX_FIELD_NAME[OPXF_COUNT] = {",
           *[f'    "{name}",' for _, name, _, _, _, _ in FIELDS], "};", "#endif", "#endif", ""]
    with open(path, "w") as f:
        f.write("\n".join(out))
    print(f"opx fields: {len(FIELDS)} fields, {(off + 7) // 8} bytes -> {path}")


def total_bits():
    return sum(b for _, _, b, _, _, _ in FIELDS)


def pack(patch):
    acc, nbits, out = 0, 0, bytearray()
    for _, name, b, lo, hi, _ in FIELDS:
        v = patch[name]
        if not lo <= v <= hi:
            raise ValueError(f"{name} = {v} outside {lo}..{hi}")
        acc |= (v - lo) << nbits
        nbits += b
        while nbits >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nbits -= 8
    if nbits:
        out.append(acc & 0xFF)
    return bytes(out)


def unpack(data):
    acc, nbits, pos, patch = 0, 0, 0, {}
    for _, name, b, lo, hi, _ in FIELDS:
        while nbits < b:
            if pos >= len(data):
                raise ValueError("record too short")
            acc |= data[pos] << nbits
            pos += 1
            nbits += 8
        v = (acc & ((1 << b) - 1)) + lo
        acc >>= b
        nbits -= b
        if v > hi:
            raise ValueError(f"{name} = {v} outside {lo}..{hi}")
        patch[name] = v
    return patch


def report():
    sections = {}
    for s, _, b, _, _, _ in FIELDS:
        sec = "operators (6)" if s.startswith("op") else s
        n, bits = sections.get(sec, (0, 0))
        sections[sec] = (n + 1, bits + b)
    print(f"{'section':<16}{'fields':>8}{'bits':>8}{'bytes':>8}")
    for sec, (n, bits) in sections.items():
        print(f"{sec:<16}{n:>8}{bits:>8}{bits / 8:>8.1f}")
    tb = total_bits()
    print(f"{'total':<16}{len(FIELDS):>8}{tb:>8}{(tb + 7) // 8:>8}")
    byte_each = sum(1 if b <= 8 else 2 if b <= 16 else 3 for _, _, b, _, _, _ in FIELDS)
    print(f"(a byte-aligned layout of the same fields: {byte_each} bytes)")


def self_test(n=2000, seed=1):
    rnd = random.Random(seed)
    names = [(name, lo, hi) for _, name, _, lo, hi, _ in FIELDS]
    size = (total_bits() + 7) // 8
    for k in range(n):
        if k == 0:
            p = {name: lo for name, lo, hi in names}
        elif k == 1:
            p = {name: hi for name, lo, hi in names}
        else:
            p = {name: rnd.randint(lo, hi) for name, lo, hi in names}
        b = pack(p)
        assert len(b) == size, (len(b), size)
        assert unpack(b) == p, f"round trip {k}"
    d = defaults()
    assert unpack(pack(d)) == d, "defaults round trip"
    for name, lo, hi in names[:5]:                       # out-of-range values are refused, not wrapped
        bad = {nm: l for nm, l, h in names}
        bad[name] = hi + 1
        try:
            pack(bad)
            raise AssertionError(f"{name}: {hi + 1} was packed")
        except ValueError:
            pass
    print(f"opx_patch_spec: {n} round trips of {len(names)} fields, {size} bytes each: OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", action="store_true", help="size by section")
    ap.add_argument("--test", action="store_true", help="randomized pack / unpack round trips")
    ap.add_argument("--emit-c", metavar="HEADER", help="write the firmware's field table (build.py)")
    ap.add_argument("--decode", action="store_true",
                    help="read packed records as hex lines on stdin, print name=value lines (tests/run_tests.sh)")
    ap.add_argument("--check-c", metavar="DUMP",
                    help="check records the firmware packed (tests/opx_test.c --dump: a hex line, then its name=value "
                         "lines, then --) against this spec's unpack")
    ap.add_argument("--active-mode-only", action="store_true",
                    help="store only the active operator mode's values (smaller; a mode switch resets them)")
    a = ap.parse_args()
    build_fields(all_mode_values=not a.active_mode_only)
    if a.emit_c:
        emit_c(a.emit_c)
        return 0
    if a.check_c:
        n, bad = 0, 0
        blocks = open(a.check_c).read().split("--\n")
        for blk in blocks:
            lines = [l for l in blk.splitlines() if l.strip()]
            if not lines:
                continue
            got = unpack(bytes.fromhex(lines[0]))
            want = {k: int(v) for k, v in (l.split("=", 1) for l in lines[1:])}
            if got != want:
                diff = [k for k in got if got[k] != want.get(k)]
                print(f"opx_patch_spec: record {n}: {len(diff)} fields differ, e.g. {diff[:5]}")
                bad += 1
            n += 1
        print(f"opx_patch_spec: {n} records packed by the firmware unpack to its values: {'OK' if not bad and n else 'FAILED'}")
        return 1 if bad or not n else 0
    if a.decode:
        for line in sys.stdin:
            line = line.strip()
            if line:
                for k, v in unpack(bytes.fromhex(line)).items():
                    print(f"{k}={v}")
                print("--")
        return 0
    if a.test:
        self_test()
    if a.report or not a.test:
        report()
    return 0


if __name__ == "__main__":
    sys.exit(main())
