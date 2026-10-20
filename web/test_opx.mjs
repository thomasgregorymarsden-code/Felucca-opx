// SPDX-License-Identifier: GPL-3.0-only
// OPX fork: web/opx_patch.js against tools/opx_patch_spec.py: records it packed (node web/test_opx.mjs VECTORS.json,
// from `opx_patch_spec.py --vectors N`) read field by field and written back byte for byte; defaults, the checks
// the firmware makes (opx_pk_ok), the name, the device's texts.
import { readFileSync } from "node:fs";
import * as OPX from "./opx_patch.js";
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
console.log(fails ? `OPX WEB TESTS FAILED (${fails})` : "opx web tests passed");
process.exit(fails ? 1 : 0);
