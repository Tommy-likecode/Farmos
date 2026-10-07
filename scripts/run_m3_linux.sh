#!/usr/bin/env bash
# Linux M3 suite: delegates to run_one_linux.sh (farmc build, cwd=fixtures).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
FARMC="${1:-$ROOT_DIR/build/farmc}"
exec bash "$SCRIPT_DIR/run_suite_linux.sh" "$FARMC" "$ROOT_DIR/spec/tests/M3"
