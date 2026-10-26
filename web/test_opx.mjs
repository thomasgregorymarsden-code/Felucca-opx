// SPDX-License-Identifier: GPL-3.0-only
// OPX fork: web/opx_patch.js against tools/opx_patch_spec.py: records it packed (node web/test_opx.mjs VECTORS.json,
// from `opx_patch_spec.py --vectors N`) read field by field and written back byte for byte; defaults, the checks
// the firmware makes (opx_pk_ok), the name, the device's texts.
import { readFileSync } from "node:fs";
import * as OPX from "./opx_patch.js";
import * as TST from "./opx_tests.js";
let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const hex = (b) => Buffer.from(b).toString("hex");
const vec = JSON.parse(readFileSync(process.argv[2], "utf8"));
let read = true, written = true;
for (const v of vec) {
  const pk = Uint8Array.from(Buffer.from(v.hex, "hex")), mine = new Uint8Array(OPX.OPX_PK_BYTES);
  for (const [n, x] of Object.entries(v.values)) { if (OPX.get(pk, n) !== x) read = false; OPX.put(mine, n, x); }
  if (hex(mine) !== v.hex) written = false;
}
ok(vec.length >= 4 && read, `opx: ${vec.length} records the spec packed read field by field`);
ok(written, "opx: written field by field they are the spec's bytes");
ok(hex(OPX.defaults()) === vec[0].hex && OPX.ok(OPX.defaults()), "opx: defaults() is the spec's INIT, and the firmware takes it");
const pk = OPX.defaults();
OPX.put(pk, "op1.level", 1000);
ok(OPX.get(pk, "op1.level") === 100, "opx: put clamps into the field's range");
const bad = OPX.defaults(); OPX.put(bad, "version", 2);
const wide = OPX.defaults(); wide[Math.floor(16 / 8)] |= 7;   // (op1.mode's 3 bits: 7 is inside, still ok)
ok(!OPX.ok(bad) && OPX.ok(wide) && !OPX.ok(pk.slice(0, 433)), "opx: ok() refuses another version or size");
const over = OPX.defaults(); OPX.put(over, "op1.wave", 22); over[2] |= 0xF8;   // (op1.wave: 5 bits at 19 -> 31 > 22)
ok(!OPX.ok(over), "opx: ok() refuses a field above its range (as opx_pk_ok)");
OPX.setName(pk, "my patch");
ok(OPX.name(pk) === "MY PATCH" && OPX.get(pk, "prog.name15") === 32, "opx: the name, upper case, padded with spaces");
OPX.put(pk, "op2.eg_atk", 1); OPX.put(pk, "op2.coarse", 7); OPX.put(pk, "op2.fx_type", 3); OPX.put(pk, "op2.fx_e", 9);
OPX.put(pk, "op2.flt_cutoff", -125); OPX.put(pk, "op2.ktrk_center", 60);
ok(OPX.text(pk, 2, "eg_atk") === "1.0 MS" && OPX.text(pk, 2, "coarse") === "1" && OPX.text(pk, 2, "fx_e") === "10 MS" &&
   OPX.text(pk, 2, "flt_cutoff") === "-12.5 ST" && OPX.text(pk, 2, "ktrk_center") === "C4", "opx: the device's texts");
const D = OPX.defaults();
ok(OPX.get(D, "op1.level") === 100 && [2, 3, 4, 5, 6].every((k) => OPX.get(D, `op${k}.level`) === 0) && OPX.get(D, "algo") === 0,
   "opx: INIT is the opsix's: OP1 alone at 100, algorithm 1");
const tp = TST.make(OPX);
ok(tp.length === 16 && tp.every((x) => OPX.ok(x.pk) && x.name.length <= 12 && OPX.name(x.pk) === x.name),
   "opx: 16 test patches (one per OPX user preset), each a record the firmware takes, named");
ok(OPX.timeText(TST.timeCode(50)) === "50 MS" && OPX.timeText(TST.timeCode(2000)) === "2.00 S" &&
   Math.abs(parseFloat(OPX.speedText(TST.hzCode(5))) / 5 - 1) < 0.01 && OPX.get(tp[0].pk, "op2.level") === 25 && OPX.get(tp[15].pk, "op1.mode") === 5,
   "opx: the test patches' times, LFO speeds (the nearest code, +-1 %) and values as the sheet's");
{                                                  // M5: V.PATCH's lists: every index once, in order, as the firmware's
  const run = (g) => g.flatMap(([, names, at]) => names.map((_, k) => at + k));
  const src = run(OPX.VP_SRC), dst = run(OPX.VP_DST);
  ok(src.length === 256 && src.every((v, i) => v === i) && dst.length === 293 && dst.every((v, i) => v === i) &&
     OPX.range("vp1.dst")[1] >= 292 && OPX.vpPlayed(17 + 4) && OPX.vpPlayed(275) && OPX.vpPlayed(276) && !OPX.vpPlayed(277) && !OPX.vpPlayed(5),
     "opx: V.PATCH sources 0..255, destinations 0..292 in order; OP1 LEVEL and FILTER (M5b) played, FX1 and PAN not yet");
  const d = OPX.defaults();                         // M5b: the main filter: the opsix INIT, and its texts
  ok(OPX.get(d, "flt.enable") === 1 && OPX.get(d, "flt.type") === 0 && OPX.get(d, "flt.cutoff") === 1023 &&
     OPX.MAIN_FILTERS.length === 11 && OPX.cutText(1023) === "23.68 KHZ" && OPX.cutText(0) === "6.49 HZ" &&
     OPX.cutText(579) === "673 HZ" && OPX.ktrkText(64) === "+1.00" && OPX.ktrkText(-127) === "-INF" && OPX.ktrkText(0) === "0.00",
     "opx: FILTER: INIT LPF 12 ON, open; CUTOFF 6.49 HZ .. 23.68 KHZ as the device shows it; KTRK slopes");
  const e = OPX.defaults();                         // M5c: the INIT's effects; a type's knobs, their texts
  OPX.fxSetType(e, 2, 15);                          // (DIST: DRIVE 50, LEVEL -12.0 dB, EQFREQ 1000 Hz, EQGAIN 0)
  const dist = OPX.OPX_FX_TYPES[15][3];
  ok(OPX.get(d, "fx1.type") === 1 && OPX.get(d, "fx2.type") === 14 && OPX.get(d, "fx3.type") === 28 && OPX.get(d, "fx3.enable") === 1 &&
     OPX.OPX_FX_TYPES.length === 31 && !OPX.fxPlayed(1) && OPX.fxPlayed(14) && !OPX.fxPlayed(28) &&
     OPX.get(e, "fx2.type") === 15 && OPX.pfxText(dist[1], OPX.get(e, "fx2.c")) === "-12.0 DB" && OPX.pfxText(dist[1], 0) === "-INF DB" &&
     OPX.pfxText(dist[2], OPX.get(e, "fx2.e")) === "1000 HZ" && OPX.pfxText(OPX.OPX_FX_TYPES[1][3][2], 480) === "0.500 HZ",
     "opx: FX: INIT FX1 CHO, FX2 3EQ, FX3 REVERB; a type set with its knobs' defaults; the knobs' texts");
}
console.log(fails ? `OPX WEB TESTS FAILED (${fails})` : "opx web tests passed");
process.exit(fails ? 1 : 0);
