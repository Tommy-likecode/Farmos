// Mutation check for the float differential test: proves scripts/float_diff.mjs detects a wrong
// ECMAScript layout threshold. Each mutant is a copy of runtime/ under build/float_mut/<id>/ with ONE
// exact textual replacement in farm_rt.c (asserted to match exactly once); the source tree is never
// modified, so there is nothing to revert. Runs the FULL diff (default 1,000,000 random patterns,
// seed 20260925) unless --count is given.
// Usage: node scripts/float_mutation.mjs [--count N] [--seed S] [--cc clang]
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, cpSync, rmSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const pass = [];
for (const k of ['--count', '--seed', '--cc']) { const v = opt(k, null); if (v !== null) pass.push(k, v); }

const src = readFileSync(join(root, 'runtime', 'farm_rt.c'), 'utf8');
const lineOf = (needle) => src.slice(0, src.indexOf(needle)).split('\n').length;
const A = 'if (k <= n && n <= 21) {';
const B = '} else if (0 < n && n <= 21) {';
const A2 = 'if (k <= n && n <= 20) {';
const B2 = '} else if (0 < n && n <= 20) {';
const mutants = [
  { id: 'M1_int_form_21_to_20', repl: [[A, A2]] },
  // Equivalent mutant (expected 0): binary64 shortest digits have k <= 17 < 21, so n == 21 always satisfies
  // k <= n and takes the integer branch first; the decimal branch never sees n == 21.
  { id: 'M2_dec_form_21_to_20', repl: [[B, B2]], equivalent: true },
  { id: 'M3_both_21_to_20', repl: [[A, A2], [B, B2]] },
];
let bad = 0;
for (const m of mutants) {
  let mutated = src;
  for (const [from] of m.repl) {
    const count = src.split(from).length - 1;
    if (count !== 1) { console.error(`${m.id}: expected exactly 1 match of [${from}], found ${count}`); process.exit(2); }
  }
  for (const [from, to] of m.repl) mutated = mutated.replace(from, to);
  const dir = join(root, 'build', 'float_mut', m.id);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  cpSync(join(root, 'runtime'), dir, { recursive: true });
  writeFileSync(join(dir, 'farm_rt.c'), mutated);
  const r = spawnSync(process.execPath, [join(root, 'scripts', 'float_diff.mjs'), '--runtime', dir, '--tag', m.id, ...pass],
                      { encoding: 'utf8', maxBuffer: 1 << 28 });
  const mm = /mismatches=(\d+)/.exec(r.stdout || '');
  const n = mm ? Number(mm[1]) : NaN;
  const hdr = (/seed=.*$/m.exec(r.stdout || '') || [''])[0];
  const desc = m.repl.map(([f, t]) => `line ${lineOf(f)}: [${f}] -> [${t}]`).join(' + ');
  console.log(`${m.id}: runtime/farm_rt.c ${desc}; ${hdr}; mismatches=${n}${m.equivalent ? ' (equivalent mutant, expected 0)' : ''}`);
  if (m.equivalent ? n !== 0 : !(n > 0)) bad++;
}
console.log(bad === 0 ? 'float_mutation: every non-equivalent mutant detected' : `float_mutation: ${bad} mutant(s) NOT detected`);
process.exit(bad === 0 ? 0 : 1);
