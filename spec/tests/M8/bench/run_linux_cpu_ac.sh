#!/usr/bin/env bash
# Sequential Linux CPU AC-M8-14: compat baseline then RR+MIS speed.
# Usage: bash spec/tests/M8/bench/run_linux_cpu_ac.sh [farmc]
set -euo pipefail
ROOT="$(cd "$(dirname "$BASH_SOURCE[0]")/../../../.." && pwd)"
FARMC="${1:-$ROOT/build/farmc}"
BENCH="$ROOT/spec/tests/M8/bench"
LOG="$BENCH/linux_cpu_full.log"
export FARM_RUNTIME="${FARM_RUNTIME:-$ROOT/runtime}"
export FARMOS_VULKAN_FORCE_FAIL=1
export FARMOS_THREADS="${FARMOS_THREADS:-$(nproc)}"
{
  echo "===== host $(uname -n) $(uname -srm) nproc=$(nproc) threads=$FARMOS_THREADS $(date -Is) ====="
  echo "farmc=$FARMC"
  echo "===== BASELINE $(date -Is) RR=0 sampling=0 ====="
  unset FARMOS_PATH_ROULETTE || true
  unset FARMOS_PATH_SAMPLING || true
  bash "$BENCH/run_artwork_bench.sh" "$FARMC" "$BENCH/out-linux-cpu-baseline"
  echo "===== SPEED $(date -Is) RR=1 sampling=1 ====="
  FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1 \
    bash "$BENCH/run_artwork_bench.sh" "$FARMC" "$BENCH/out-linux-cpu-speed"
  echo "===== DONE $(date -Is) ====="
  echo "baseline_log=$BENCH/out-linux-cpu-baseline/log.txt"
  echo "speed_log=$BENCH/out-linux-cpu-speed/log.txt"
} 2>&1 | tee "$LOG"
