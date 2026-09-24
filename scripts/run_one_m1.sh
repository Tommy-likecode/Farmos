#!/usr/bin/env bash
set -euo pipefail
FARMC="$1"; TESTS="$2"; NAME="$3"
export FARM_RUNTIME="$(dirname "$FARMC")/runtime"
[[ -f "$FARM_RUNTIME/farm_rt.c" ]] || FARM_RUNTIME="$(dirname "$(dirname "$FARMC")")/runtime"
# minimal shell runner — Windows is primary
echo "use powershell harness on Windows" >&2
exit 1
