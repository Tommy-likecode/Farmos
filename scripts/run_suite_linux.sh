#!/usr/bin/env bash
# Run every *.expected fixture in a tests dir via run_one_linux.sh
# (same contract as scripts/run_one_m1.ps1: farmc build cwd=fixtures dir).
set -u
FARMC="${1:?farmc path}"
TESTS_DIR="${2:?tests dir}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ONE="$SCRIPT_DIR/run_one_linux.sh"

pass=0
fail=0
skip=0
failed=()
while IFS= read -r exp; do
  name="$(basename "$exp" .expected)"
  if [ "$name" = "039_mixed_arith_error" ] && [[ "$TESTS_DIR" == */M1 ]]; then
    echo "SKIP $name (superseded by M2 int-lit coercion)"
    skip=$((skip + 1))
    continue
  fi
  if bash "$ONE" "$FARMC" "$TESTS_DIR" "$name"; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    failed+=("$name")
  fi
done < <(ls -1 "$TESTS_DIR"/*.expected | sort)

echo "---"
echo "PASS $pass  FAIL $fail  SKIP $skip"
if [ "$fail" -ne 0 ]; then
  echo "Failed:"
  printf '  %s\n' "${failed[@]}"
  exit 1
fi
exit 0
