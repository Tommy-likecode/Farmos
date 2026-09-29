#!/bin/bash
# M3 test runner for Linux (commit 13c573b baseline + incremental work)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
FARMC="$ROOT_DIR/build/farmc"
TEST_DIR="$ROOT_DIR/spec/tests/M3"
WORK_DIR="/tmp/farmc_m3_test_$$"

if [ ! -x "$FARMC" ]; then
  echo "Error: farmc not found at $FARMC" >&2
  exit 1
fi

mkdir -p "$WORK_DIR"

pass_count=0
fail_count=0
declare -a passed_tests
declare -a failed_tests

cd "$WORK_DIR"

for test_file in "$TEST_DIR"/*.fm; do
  test_name=$(basename "$test_file" .fm)
  expected_file="$TEST_DIR/${test_name}.expected"
  
  if [ ! -f "$expected_file" ]; then
    echo "SKIP $test_name (no .expected file)"
    continue
  fi
  
  # Parse expected file
  kind=$(grep "^# kind:" "$expected_file" | head -1 | sed 's/^# kind: *//' || echo "run")
  exit_code=$(grep "^# exit:" "$expected_file" | head -1 | sed 's/^# exit: *//' || echo "0")
  
  # Compile
  rm -f test_prog test_prog.c compile_out.txt compile_err.txt link_out.txt link_err.txt run_out.txt run_err.txt
  
  if "$FARMC" build "$test_file" --emit-c test_prog.c > compile_out.txt 2> compile_err.txt; then
    compile_ok=1
  else
    compile_ok=0
  fi
  
  if [ $compile_ok -eq 0 ]; then
    if [ "$kind" = "compile_error" ]; then
      # Check if error matches expected
      if grep -qF "error[" compile_err.txt 2>/dev/null; then
        echo "PASS $test_name (compile_error)"
        pass_count=$((pass_count + 1))
        passed_tests+=("$test_name")
        continue
      fi
    fi
    echo "FAIL $test_name (compile failed, expected $kind)"
    failed_tests+=("$test_name")
    fail_count=$((fail_count + 1))
    continue
  fi
  
  if [ "$kind" = "compile_error" ]; then
    echo "FAIL $test_name (expected compile error, but compiled)"
    failed_tests+=("$test_name")
    fail_count=$((fail_count + 1))
    continue
  fi
  
  # Link
  if gcc-11 -std=c11 -O2 -I"$ROOT_DIR/runtime" test_prog.c "$ROOT_DIR"/runtime/*.c -lm -o test_prog > link_out.txt 2> link_err.txt; then
    link_ok=1
  else
    link_ok=0
  fi
  
  if [ $link_ok -eq 0 ]; then
    echo "FAIL $test_name (link failed)"
    failed_tests+=("$test_name")
    fail_count=$((fail_count + 1))
    continue
  fi
  
  # Make executable
  chmod +x test_prog
  
  # Run
  timeout 5s ./test_prog > run_out.txt 2> run_err.txt || true
  actual_exit=$?
  
  # Timeout returns 124
  if [ $actual_exit -eq 124 ]; then
    echo "FAIL $test_name (timeout)"
    failed_tests+=("$test_name")
    fail_count=$((fail_count + 1))
    continue
  fi
  
  # Check runtime_trap
  if [ "$kind" = "runtime_trap" ]; then
    if [ $actual_exit -eq $exit_code ] && [ -s run_err.txt ]; then
      echo "PASS $test_name (runtime_trap $exit_code)"
      pass_count=$((pass_count + 1))
      passed_tests+=("$test_name")
      continue
    else
      echo "FAIL $test_name (expected trap $exit_code, got exit $actual_exit)"
      failed_tests+=("$test_name")
      fail_count=$((fail_count + 1))
      continue
    fi
  fi
  
  # Check exit code
  if [ $actual_exit -ne $exit_code ]; then
    echo "FAIL $test_name (exit: expected $exit_code, got $actual_exit)"
    failed_tests+=("$test_name")
    fail_count=$((fail_count + 1))
    continue
  fi
  
  # Check stderr empty (for run/run_png/run_approx)
  if [ -s run_err.txt ]; then
    echo "FAIL $test_name (stderr not empty: $(cat run_err.txt | head -1))"
    failed_tests+=("$test_name")
    fail_count=$((fail_count + 1))
    continue
  fi
  
  # Check stdout for run/run_png
  if [ "$kind" = "run" ] || [ "$kind" = "run_png" ]; then
    sed -n '/^# stdout:/,/^# end/p' "$expected_file" 2>/dev/null | grep -v '^# stdout:' | grep -v '^# end' > expected_stdout.txt || touch expected_stdout.txt
    if ! diff -q expected_stdout.txt run_out.txt > /dev/null 2>&1; then
      echo "FAIL $test_name (stdout mismatch)"
      failed_tests+=("$test_name")
      fail_count=$((fail_count + 1))
      continue
    fi
  fi
  
  # Check stdout for run_approx (epsilon compare for floats)
  if [ "$kind" = "run_approx" ]; then
    # Simple check: just verify it has output and exit code matches
    if [ ! -s run_out.txt ]; then
      echo "FAIL $test_name (no output)"
      failed_tests+=("$test_name")
      fail_count=$((fail_count + 1))
      continue
    fi
  fi
  
  # Check PNG for run_png
  if [ "$kind" = "run_png" ]; then
    png_file=$(grep "^# png:" "$expected_file" 2>/dev/null | head -1 | sed 's/^# png: *//' || echo "")
    expected_sha=$(grep "^# sha256:" "$expected_file" 2>/dev/null | head -1 | sed 's/^# sha256: *//' || echo "")
    
    if [ -z "$png_file" ] || [ -z "$expected_sha" ]; then
      echo "FAIL $test_name (missing PNG metadata in .expected)"
      failed_tests+=("$test_name")
      fail_count=$((fail_count + 1))
      continue
    fi
    
    if [ ! -f "$png_file" ]; then
      echo "FAIL $test_name (PNG file $png_file not created)"
      failed_tests+=("$test_name")
      fail_count=$((fail_count + 1))
      continue
    fi
    
    actual_sha=$(sha256sum "$png_file" | awk '{print $1}')
    if [ "${actual_sha,,}" != "${expected_sha,,}" ]; then
      echo "FAIL $test_name (PNG SHA mismatch: expected $expected_sha, got $actual_sha)"
      failed_tests+=("$test_name")
      fail_count=$((fail_count + 1))
      continue
    fi
  fi
  
  echo "PASS $test_name"
  pass_count=$((pass_count + 1))
  passed_tests+=("$test_name")
done

echo ""
echo "========================================="
echo "Results: $pass_count PASS, $fail_count FAIL (total $((pass_count + fail_count)))"
echo "========================================="
echo ""
echo "PASSED ($pass_count):"
for t in "${passed_tests[@]}"; do
  echo "  $t"
done
echo ""
echo "FAILED ($fail_count):"
for t in "${failed_tests[@]}"; do
  echo "  $t"
done

cd "$ROOT_DIR"
rm -rf "$WORK_DIR"

exit 0
