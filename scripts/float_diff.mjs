// Differential test: Farmos runtime float formatting vs ECMAScript Number::toString (Node/V8 String(x)).
// Usage: node scripts/float_diff.mjs [--count N] [--seed S] [--cc clang] [--runtime DIR] [--tag NAME]
// --runtime: directory holding farm_rt.c/farm_rt.h/ryu (default <repo>/runtime); used by float_mutation.mjs.
// Farmos rule (M1-core section 6.1 rule 3): -0 prints "-0" (ES prints "0"); all other values must equal String(x).
import { spawnSync } from 'node:child_process';
import { writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const COUNT = Number(opt('--count', '1000000'));
const SEED = BigInt(opt('--seed', '20260925'));
const CC = opt('--cc', 'clang');
const RUNTIME = opt('--runtime', join(root, 'runtime'));
const TAG = opt('--tag', 'float_harness');

const outDir = join(root, 'build', 'float_diff');
mkdirSync(outDir, { recursive: true });
const exe = join(outDir, process.platform === 'win32' ? TAG + '.exe' : TAG);
const cc = spawnSync(CC, ['-O2', '-std=c11', '-I', RUNTIME, join(root, 'tests', 'float', 'float_harness.c'), '-o', exe, '-lm'], { encoding: 'utf8' });
if (cc.status !== 0) { console.error('harness build failed', cc.stdout, cc.stderr); process.exit(2); }

const M64 = (1n << 64n) - 1n;
let s = SEED;
function splitmix64() {
  s = (s + 0x9E3779B97F4A7C15n) & M64;
  let z = s;
  z = ((z ^ (z >> 30n)) * 0xBF58476D1CE4E5B9n) & M64;
  z = ((z ^ (z >> 27n)) * 0x94D049BB133111EBn) & M64;
  return z ^ (z >> 31n);
}
const dv = new DataView(new ArrayBuffer(8));
const bitsOf = (x) => { dv.setFloat64(0, x); return dv.getBigUint64(0); };
const valOf = (b) => { dv.setBigUint64(0, b); return dv.getFloat64(0); };
const hex = (b) => b.toString(16).padStart(16, '0');

const edge = [];
const addB = (b) => edge.push(b & M64);
const addV = (x) => addB(bitsOf(x));
const addNear = (x, k) => { const b = bitsOf(x); for (let i = -k; i <= k; i++) { const c = b + BigInt(i); if (c >= 0n && c <= M64) addB(c); } };
// signed zeros, subnormal/normal/max limits (both signs)
for (const sign of [0n, 1n << 63n]) {
  for (const b of [0n, 1n, 2n, 0x000FFFFFFFFFFFFFn, 0x0010000000000000n, 0x0010000000000001n, 0x7FEFFFFFFFFFFFFFn, 0x7FEFFFFFFFFFFFFEn, 0x3FF0000000000000n]) addB(sign | b);
}
// NaN (several payloads) and infinities
for (const b of [0x7FF8000000000000n, 0x7FF0000000000001n, 0xFFF8000000000000n, 0x7FFFFFFFFFFFFFFFn, 0x7FF0000000000000n, 0xFFF0000000000000n]) addB(b);
// powers of ten 1e-324 .. 1e308 and their nextafter neighbours
for (let e = -324; e <= 308; e++) { const x = Number('1e' + e); addNear(x, 1); addNear(-x, 1); }
// thresholds of Number::toString: 1e21 and 1e-7 (+-3 ulps), and nearby decimal boundaries
for (const x of [1e21, 1e-7, 1e-6, 1e20, 999999999999999900000, 9.999999999999999e20, 1.0000000000000001e21, 9.999999999999999e-8, 1.0000000000000001e-7]) { addNear(x, 3); addNear(-x, 3); }
// integers near 2^53
for (let i = -8; i <= 8; i++) { addV(2 ** 53 + i); addV(-(2 ** 53) + i); addV(2 ** 54 + 2 * i); }
addNear(2 ** 53, 4);
// familiar values
for (const x of [0.1, 0.2, 0.1 + 0.2, 0.3, 1.5, 123.456, 100, 5e-324, 1.7976931348623157e308, 123456789.125, 2.2250738585072014e-308, 4.9406564584124654e-324, 1 / 3, 2 / 3, Math.PI, Math.E]) { addV(x); addV(-x); }

const rand = [];
for (let i = 0; i < COUNT; i++) rand.push(splitmix64());
// secondary category: random "short decimals" (stress closest-of-shortest selection)
const shortDec = [];
const SHORT = Math.min(100000, Math.floor(COUNT / 10));
for (let i = 0; i < SHORT; i++) {
  const r = splitmix64();
  const digits = Number(r % 100000000000000000n);          // up to 17 digits
  const exp = Number((r >> 57n) % 60n) - 30;
  shortDec.push(bitsOf(Number(`${digits}e${exp}`)));
}

const all = edge.concat(shortDec, rand);
const input = all.map(hex).join('\n') + '\n';
const r = spawnSync(exe, [], { input, maxBuffer: 1 << 30, encoding: 'utf8' });
if (r.status !== 0) { console.error('harness failed', r.status, r.stderr); process.exit(2); }
const got = r.stdout.split('\n');
let mism = 0; const samples = [];
for (let i = 0; i < all.length; i++) {
  const x = valOf(all[i]);
  const want = Object.is(x, -0) ? '-0' : String(x);
  if (got[i] !== want) { mism++; if (samples.length < 20) samples.push(`${hex(all[i])}: got=${got[i]} want=${want}`); }
}
console.log(`float_diff: reference=node ${process.version} String(x) (V8 Number::toString), -0 -> "-0" per M1 6.1 rule 3`);
console.log(`float_diff: seed=${SEED} random_bit_patterns=${rand.length} short_decimals=${shortDec.length} edge_cases=${edge.length} total=${all.length}`);
console.log(`float_diff: mismatches=${mism}`);
for (const l of samples) console.log('  ' + l);
process.exit(mism === 0 ? 0 : 1);
