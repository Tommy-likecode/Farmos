#!/usr/bin/env bash
# M6 §10: build and run BM-a..d; CI asserts exit 0. Timings are informative.
set -u
FARMC="${1:?farmc path}"
ROOT="${2:?repo root}"
BM="$ROOT/benchmarks"
td="$(mktemp -d /tmp/farmc_bench_XXXXXX)"
cleanup() { rm -rf "$td"; }
trap cleanup EXIT
fail=0
run_one() {
  local src="$1" name="$2"
  local out="$td/$name"
  echo "BM $name: building"
  if ! "$FARMC" build "$src" -o "$out"; then
    echo "FAIL benchmarks: build $src"
    fail=1
    return
  fi
  local t0 t1
  t0=$(date +%s.%N)
  if ! (cd "$td" && "$out" >/dev/null); then
    echo "FAIL benchmarks: run $src"
    fail=1
    return
  fi
  t1=$(date +%s.%N)
  echo "PASS $name  (informative wall_s=$(python3 -c "print('{:.3f}'.format(float('$t1')-float('$t0')))" 2>/dev/null || echo n/a))"
}
run_one "$BM/a_empty_loop.fm" BM-a
run_one "$BM/b_vector_ops.fm" BM-b
run_one "$BM/c_stack_600.fm" BM-c
run_one "$BM/d_tiny_path.fm" BM-d
if [ "$fail" -ne 0 ]; then exit 1; fi
echo "PASS benchmarks"
exit 0
