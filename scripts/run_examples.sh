#!/usr/bin/env bash
# Smoke-build and run examples/01..05 (M6 §9).
set -u
FARMC="${1:?farmc path}"
ROOT="${2:?repo root}"
EX="$ROOT/examples"
td="$(mktemp -d /tmp/farmc_examples_XXXXXX)"
cleanup() { rm -rf "$td"; }
trap cleanup EXIT
fail=0
run_one() {
  local src="$1" name="$2"
  local out="$td/$name"
  if ! "$FARMC" build "$src" -o "$out"; then
    echo "FAIL examples: build $src"
    fail=1
    return
  fi
  if ! (cd "$td" && "$out"); then
    echo "FAIL examples: run $src"
    fail=1
    return
  fi
  echo "PASS $name"
}
run_one "$EX/01_hello.fm" 01_hello
run_one "$EX/02_rotating_cube.fm" 02_rotating_cube
run_one "$EX/03_cornell_path.fm" 03_cornell_path
run_one "$EX/04_glass_mirror.fm" 04_glass_mirror
run_one "$EX/05_stacking_bounce.fm" 05_stacking_bounce
if [ "$fail" -ne 0 ]; then exit 1; fi
echo "PASS examples"
exit 0
